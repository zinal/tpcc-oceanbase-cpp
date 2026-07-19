#include "db/connection.h"
#include "db/errors.h"

#include <mysql.h>

#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace NTPCC {
namespace {

[[noreturn]] void ThrowMysqlError(MYSQL* mysql, const char* what) {
    const int code = mysql ? static_cast<int>(mysql_errno(mysql)) : 0;
    const char* msg = mysql ? mysql_error(mysql) : "null mysql handle";
    throw DbError(code, std::string(what) + ": [" + std::to_string(code) + "] " + msg);
}

std::string Trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

void SetKv(ObConnectionConfig& cfg, const std::string& key, const std::string& value) {
    if (key == "host" || key == "hostname") {
        cfg.host = value;
    } else if (key == "port") {
        cfg.port = std::stoi(value);
    } else if (key == "user" || key == "uid") {
        cfg.user = value;
    } else if (key == "password" || key == "pwd" || key == "passwd") {
        cfg.password = value;
    } else if (key == "database" || key == "db" || key == "dbname") {
        cfg.database = value;
    } else if (key == "path") {
        cfg.path = value;
    }
}

QueryResult MaterializeResult(MYSQL_RES* res) {
    if (!res) {
        return QueryResult{};
    }

    const unsigned numFields = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);

    std::vector<std::string> columns;
    columns.reserve(numFields);
    for (unsigned i = 0; i < numFields; ++i) {
        columns.emplace_back(fields[i].name ? fields[i].name : "");
    }

    std::vector<std::vector<std::optional<std::string>>> rows;
    rows.reserve(mysql_num_rows(res));

    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        unsigned long* lengths = mysql_fetch_lengths(res);
        std::vector<std::optional<std::string>> values;
        values.reserve(numFields);
        for (unsigned i = 0; i < numFields; ++i) {
            if (row[i] == nullptr) {
                values.emplace_back(std::nullopt);
            } else {
                values.emplace_back(std::string(row[i], lengths[i]));
            }
        }
        rows.push_back(std::move(values));
    }

    mysql_free_result(res);
    return QueryResult(std::move(columns), std::move(rows));
}

std::string EscapeString(MYSQL* mysql, const std::string& value) {
    std::string out(value.size() * 2 + 1, '\0');
    const unsigned long n = mysql_real_escape_string(
        mysql, out.data(), value.data(), static_cast<unsigned long>(value.size()));
    out.resize(n);
    return out;
}

std::string FormatParam(MYSQL* mysql, const Params::Value& value) {
    return std::visit(
        [&](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Params::Null>) {
                return "NULL";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "'" + EscapeString(mysql, v) + "'";
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream oss;
                oss.precision(15);
                oss << v;
                return oss.str();
            } else {
                return std::to_string(v);
            }
        },
        value);
}

// Expand `?` placeholders left-to-right with escaped literals.
// Sufficient for TPC-C parameter shapes; avoids stmt bind complexity across Connector/C builds.
std::string BindSql(MYSQL* mysql, const std::string& sql, const Params& params) {
    if (params.Empty()) {
        return sql;
    }

    std::string out;
    out.reserve(sql.size() + 64);
    size_t paramIndex = 0;
    for (size_t i = 0; i < sql.size(); ++i) {
        if (sql[i] == '?') {
            if (paramIndex >= params.Size()) {
                throw std::runtime_error("Not enough bound parameters for SQL");
            }
            out += FormatParam(mysql, params.Values()[paramIndex++]);
        } else {
            out.push_back(sql[i]);
        }
    }
    if (paramIndex != params.Size()) {
        throw std::runtime_error("Too many bound parameters for SQL");
    }
    return out;
}

} // namespace

struct ObConnection::Impl {
    MYSQL* mysql = nullptr;
};

ObConnectionConfig ParseConnectionString(const std::string& connection) {
    ObConnectionConfig cfg;
    if (connection.empty()) {
        return cfg;
    }

    const char sep = (connection.find(';') != std::string::npos) ? ';' : ' ';
    std::string token;
    std::istringstream iss(connection);
    while (std::getline(iss, token, sep)) {
        token = Trim(token);
        if (token.empty()) {
            continue;
        }
        const auto eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        SetKv(cfg, Trim(token.substr(0, eq)), Trim(token.substr(eq + 1)));
    }
    return cfg;
}

std::unique_ptr<ObConnection> ObConnection::Connect(const ObConnectionConfig& config) {
    auto conn = std::unique_ptr<ObConnection>(new ObConnection());
    conn->impl_ = std::make_unique<Impl>();
    conn->impl_->mysql = mysql_init(nullptr);
    if (!conn->impl_->mysql) {
        throw std::runtime_error("mysql_init failed");
    }

    // OceanBase CE local/dev typically does not use TLS on 2881.
    const int sslEnforce = 0;
    mysql_options(conn->impl_->mysql, MYSQL_OPT_SSL_ENFORCE, &sslEnforce);

    if (!mysql_real_connect(
            conn->impl_->mysql,
            config.host.c_str(),
            config.user.c_str(),
            config.password.c_str(),
            config.database.empty() ? nullptr : config.database.c_str(),
            static_cast<unsigned int>(config.port),
            nullptr,
            CLIENT_MULTI_STATEMENTS | CLIENT_FOUND_ROWS))
    {
        ThrowMysqlError(conn->impl_->mysql, "mysql_real_connect failed");
    }

    if (!config.path.empty()) {
        conn->UseDatabase(config.path);
    }

    return conn;
}

ObConnection::~ObConnection() {
    if (impl_ && impl_->mysql) {
        mysql_close(impl_->mysql);
        impl_->mysql = nullptr;
    }
}

void ObConnection::UseDatabase(const std::string& database) {
    if (mysql_select_db(impl_->mysql, database.c_str()) != 0) {
        ThrowMysqlError(impl_->mysql, "USE database failed");
    }
}

void ObConnection::BeginRepeatableRead() {
    // Two statements: portable across OceanBase MySQL tenants and MySQL/MariaDB.
    if (mysql_query(impl_->mysql, "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ") != 0) {
        ThrowMysqlError(impl_->mysql, "SET TRANSACTION ISOLATION LEVEL failed");
    }
    if (mysql_query(impl_->mysql, "START TRANSACTION") != 0) {
        ThrowMysqlError(impl_->mysql, "START TRANSACTION failed");
    }
}

void ObConnection::Commit() {
    if (mysql_query(impl_->mysql, "COMMIT") != 0) {
        ThrowMysqlError(impl_->mysql, "COMMIT failed");
    }
}

void ObConnection::Rollback() {
    if (!impl_ || !impl_->mysql) {
        return;
    }
    if (mysql_query(impl_->mysql, "ROLLBACK") != 0) {
        // Best-effort during shutdown / already-closed txn.
    }
}

QueryResult ObConnection::Query(const std::string& sql, const Params& params) {
    const std::string bound = BindSql(impl_->mysql, sql, params);
    if (mysql_real_query(impl_->mysql, bound.data(), static_cast<unsigned long>(bound.size())) != 0) {
        ThrowMysqlError(impl_->mysql, "Query failed");
    }
    MYSQL_RES* res = mysql_store_result(impl_->mysql);
    if (!res && mysql_field_count(impl_->mysql) != 0) {
        ThrowMysqlError(impl_->mysql, "mysql_store_result failed");
    }
    return MaterializeResult(res);
}

uint64_t ObConnection::Execute(const std::string& sql, const Params& params) {
    const std::string bound = BindSql(impl_->mysql, sql, params);
    if (mysql_real_query(impl_->mysql, bound.data(), static_cast<unsigned long>(bound.size())) != 0) {
        ThrowMysqlError(impl_->mysql, "Execute failed");
    }
    // Drain any result sets from multi-statement just in case.
    do {
        if (MYSQL_RES* res = mysql_store_result(impl_->mysql)) {
            mysql_free_result(res);
        }
    } while (mysql_next_result(impl_->mysql) == 0);

    return static_cast<uint64_t>(mysql_affected_rows(impl_->mysql));
}

QueryResult ObConnection::QuerySimple(const std::string& sql) {
    return Query(sql, Params{});
}

uint64_t ObConnection::ExecuteSimple(const std::string& sql) {
    return Execute(sql, Params{});
}

void ObConnection::KillQuery(const ObConnectionConfig& adminConfig) {
    if (!impl_ || !impl_->mysql) {
        return;
    }
    const unsigned long tid = mysql_thread_id(impl_->mysql);
    try {
        auto admin = Connect(adminConfig);
        admin->ExecuteSimple("KILL QUERY " + std::to_string(tid));
    } catch (...) {
        // Ignore cancel failures during shutdown.
    }
}

unsigned long ObConnection::ThreadId() const {
    return impl_ && impl_->mysql ? mysql_thread_id(impl_->mysql) : 0;
}

bool ObConnection::Ok() const {
    return impl_ && impl_->mysql;
}

} // namespace NTPCC
