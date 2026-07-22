#include "db/prepared_statement.h"
#include "db/errors.h"

#include <mysql.h>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace NTPCC {
namespace {

[[noreturn]] void ThrowStmtError(MYSQL_STMT* stmt, const char* what) {
    const int code = stmt ? static_cast<int>(mysql_stmt_errno(stmt)) : 0;
    const char* msg = stmt ? mysql_stmt_error(stmt) : "null stmt handle";
    throw DbError(code, std::string(what) + ": [" + std::to_string(code) + "] " + msg);
}

constexpr size_t kResultBufferSize = 65536;

struct ParamBinding {
    enum class Kind { Null, Int32, Int64, Uint64, Double, String, Timestamp } kind = Kind::Null;

    int32_t i32 = 0;
    int64_t i64 = 0;
    uint64_t u64 = 0;
    double dbl = 0;
    std::string str;
    unsigned long strLength = 0;
    MYSQL_TIME ts{};
    MYSQL_BIND bind{};

    void ResetBind() {
        std::memset(&bind, 0, sizeof(bind));
    }
};

void SetupParamBind(ParamBinding& slot) {
    slot.ResetBind();
    switch (slot.kind) {
        case ParamBinding::Kind::Null:
            slot.bind.buffer_type = MYSQL_TYPE_NULL;
            break;
        case ParamBinding::Kind::Int32:
            slot.bind.buffer_type = MYSQL_TYPE_LONG;
            slot.bind.buffer = &slot.i32;
            slot.bind.is_unsigned = false;
            break;
        case ParamBinding::Kind::Int64:
            slot.bind.buffer_type = MYSQL_TYPE_LONGLONG;
            slot.bind.buffer = &slot.i64;
            slot.bind.is_unsigned = false;
            break;
        case ParamBinding::Kind::Uint64:
            slot.bind.buffer_type = MYSQL_TYPE_LONGLONG;
            slot.bind.buffer = &slot.u64;
            slot.bind.is_unsigned = true;
            break;
        case ParamBinding::Kind::Double:
            slot.bind.buffer_type = MYSQL_TYPE_DOUBLE;
            slot.bind.buffer = &slot.dbl;
            break;
        case ParamBinding::Kind::String:
            slot.bind.buffer_type = MYSQL_TYPE_STRING;
            slot.strLength = static_cast<unsigned long>(slot.str.size());
            slot.bind.buffer = slot.str.data();
            slot.bind.buffer_length = slot.strLength;
            slot.bind.length = &slot.strLength;
            break;
        case ParamBinding::Kind::Timestamp:
            slot.bind.buffer_type = MYSQL_TYPE_TIMESTAMP;
            slot.bind.buffer = &slot.ts;
            break;
    }
}

void FillParamSlot(ParamBinding& slot, const Params::Value& value) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Params::Null>) {
                slot.kind = ParamBinding::Kind::Null;
            } else if constexpr (std::is_same_v<T, int32_t>) {
                slot.kind = ParamBinding::Kind::Int32;
                slot.i32 = v;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                slot.kind = ParamBinding::Kind::Int64;
                slot.i64 = v;
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                slot.kind = ParamBinding::Kind::Uint64;
                slot.u64 = v;
            } else if constexpr (std::is_same_v<T, double>) {
                slot.kind = ParamBinding::Kind::Double;
                slot.dbl = v;
            } else if constexpr (std::is_same_v<T, std::string>) {
                slot.kind = ParamBinding::Kind::String;
                slot.str = v;
            } else if constexpr (std::is_same_v<T, Params::Timestamp>) {
                slot.kind = ParamBinding::Kind::Timestamp;
                slot.ts.year = v.year;
                slot.ts.month = static_cast<unsigned int>(v.month);
                slot.ts.day = static_cast<unsigned int>(v.day);
                slot.ts.hour = static_cast<unsigned int>(v.hour);
                slot.ts.minute = static_cast<unsigned int>(v.minute);
                slot.ts.second = static_cast<unsigned int>(v.second);
            }
        },
        value);
    SetupParamBind(slot);
}

QueryResult MaterializeStmtResult(MYSQL_STMT* stmt, MYSQL_RES* meta) {
    if (!meta) {
        return QueryResult{};
    }

    const unsigned numFields = mysql_num_fields(meta);
    MYSQL_FIELD* fields = mysql_fetch_fields(meta);

    std::vector<std::string> columns;
    columns.reserve(numFields);
    for (unsigned i = 0; i < numFields; ++i) {
        columns.emplace_back(fields[i].name ? fields[i].name : "");
    }

    std::vector<std::vector<char>> buffers(numFields);
    std::vector<unsigned long> lengths(numFields);
    std::vector<my_bool> isNull(numFields);
    std::vector<MYSQL_BIND> resultBinds(numFields);

    for (unsigned i = 0; i < numFields; ++i) {
        buffers[i].assign(kResultBufferSize, '\0');
        std::memset(&resultBinds[i], 0, sizeof(MYSQL_BIND));
        resultBinds[i].buffer_type = MYSQL_TYPE_STRING;
        resultBinds[i].buffer = buffers[i].data();
        resultBinds[i].buffer_length = kResultBufferSize;
        resultBinds[i].length = &lengths[i];
        resultBinds[i].is_null = &isNull[i];
    }

    if (mysql_stmt_bind_result(stmt, resultBinds.data()) != 0) {
        mysql_free_result(meta);
        ThrowStmtError(stmt, "mysql_stmt_bind_result failed");
    }

    std::vector<std::vector<std::optional<std::string>>> rows;
    while (true) {
        const int rc = mysql_stmt_fetch(stmt);
        if (rc == MYSQL_NO_DATA) {
            break;
        }
        if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
            mysql_free_result(meta);
            ThrowStmtError(stmt, "mysql_stmt_fetch failed");
        }

        std::vector<std::optional<std::string>> row;
        row.reserve(numFields);
        for (unsigned i = 0; i < numFields; ++i) {
            if (isNull[i]) {
                row.emplace_back(std::nullopt);
            } else {
                row.emplace_back(std::string(buffers[i].data(), lengths[i]));
            }
        }
        rows.push_back(std::move(row));
    }

    mysql_free_result(meta);
    return QueryResult(std::move(columns), std::move(rows));
}

} // namespace

struct ObStatementCache::Impl {
    struct StmtEntry {
        MYSQL_STMT* stmt = nullptr;
        bool prepared = false;
        std::vector<ParamBinding> paramSlots;
        std::vector<MYSQL_BIND> paramBinds;
    };

    MYSQL* mysql = nullptr;
    StmtEntry entries[static_cast<size_t>(QueryId::Count)];

    explicit Impl(void* mysqlHandle) : mysql(static_cast<MYSQL*>(mysqlHandle)) {}

    StmtEntry& Get(QueryId id) {
        return entries[static_cast<size_t>(id)];
    }

    void Prepare(StmtEntry& entry, QueryId id) {
        if (entry.prepared) {
            return;
        }

        entry.stmt = mysql_stmt_init(mysql);
        if (!entry.stmt) {
            throw std::runtime_error("mysql_stmt_init failed");
        }

        const std::string_view sql = QuerySql(id);
        if (mysql_stmt_prepare(entry.stmt, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
            ThrowStmtError(entry.stmt, "mysql_stmt_prepare failed");
        }

        entry.prepared = true;
    }

    void Clear() {
        for (size_t i = 0; i < static_cast<size_t>(QueryId::Count); ++i) {
            if (entries[i].stmt) {
                mysql_stmt_close(entries[i].stmt);
                entries[i].stmt = nullptr;
            }
            entries[i].prepared = false;
            entries[i].paramSlots.clear();
            entries[i].paramBinds.clear();
        }
    }
};

ObStatementCache::ObStatementCache(void* mysql)
    : impl_(std::make_unique<Impl>(mysql))
{}

ObStatementCache::~ObStatementCache() {
    Clear();
}

void ObStatementCache::Clear() {
    if (impl_) {
        impl_->Clear();
    }
}

QueryResult ObStatementCache::Query(QueryId id, const Params& params) {
    Impl::StmtEntry& entry = impl_->Get(id);
    impl_->Prepare(entry, id);

    entry.paramSlots.resize(params.Size());
    entry.paramBinds.resize(params.Size());
    for (size_t i = 0; i < params.Size(); ++i) {
        FillParamSlot(entry.paramSlots[i], params.Values()[i]);
        entry.paramBinds[i] = entry.paramSlots[i].bind;
    }

    if (!params.Empty()) {
        if (mysql_stmt_bind_param(entry.stmt, entry.paramBinds.data()) != 0) {
            ThrowStmtError(entry.stmt, "mysql_stmt_bind_param failed");
        }
    }

    if (mysql_stmt_execute(entry.stmt) != 0) {
        ThrowStmtError(entry.stmt, "mysql_stmt_execute failed");
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(entry.stmt);
    return MaterializeStmtResult(entry.stmt, meta);
}

uint64_t ObStatementCache::Execute(QueryId id, const Params& params) {
    Impl::StmtEntry& entry = impl_->Get(id);
    impl_->Prepare(entry, id);

    entry.paramSlots.resize(params.Size());
    entry.paramBinds.resize(params.Size());
    for (size_t i = 0; i < params.Size(); ++i) {
        FillParamSlot(entry.paramSlots[i], params.Values()[i]);
        entry.paramBinds[i] = entry.paramSlots[i].bind;
    }

    if (!params.Empty()) {
        if (mysql_stmt_bind_param(entry.stmt, entry.paramBinds.data()) != 0) {
            ThrowStmtError(entry.stmt, "mysql_stmt_bind_param failed");
        }
    }

    if (mysql_stmt_execute(entry.stmt) != 0) {
        ThrowStmtError(entry.stmt, "mysql_stmt_execute failed");
    }

    return static_cast<uint64_t>(mysql_stmt_affected_rows(entry.stmt));
}

} // namespace NTPCC
