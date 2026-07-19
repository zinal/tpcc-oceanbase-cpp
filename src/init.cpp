#include "init.h"

#include "constants.h"
#include "db/connection.h"
#include "db/errors.h"
#include "log.h"
#include "util.h"

#include <stdexcept>
#include <string>

namespace NTPCC {

namespace {

// Statements executed one-by-one (OceanBase/MySQL tenant dialect).
const char* const kDropTables[] = {
    "DROP TABLE IF EXISTS history",
    "DROP TABLE IF EXISTS new_order",
    "DROP TABLE IF EXISTS order_line",
    "DROP TABLE IF EXISTS oorder",
    "DROP TABLE IF EXISTS customer",
    "DROP TABLE IF EXISTS district",
    "DROP TABLE IF EXISTS stock",
    "DROP TABLE IF EXISTS item",
    "DROP TABLE IF EXISTS warehouse",
};

const char* const kCreateTables[] = {
    R"(CREATE TABLE warehouse (
    w_id       int            NOT NULL,
    w_ytd      decimal(12, 2) NOT NULL,
    w_tax      decimal(4, 4)  NOT NULL,
    w_name     varchar(10)    NOT NULL,
    w_street_1 varchar(20)    NOT NULL,
    w_street_2 varchar(20)    NOT NULL,
    w_city     varchar(20)    NOT NULL,
    w_state    char(2)        NOT NULL,
    w_zip      char(9)        NOT NULL,
    PRIMARY KEY (w_id)
))",
    R"(CREATE TABLE item (
    i_id    int           NOT NULL,
    i_name  varchar(24)   NOT NULL,
    i_price decimal(5, 2) NOT NULL,
    i_data  varchar(50)   NOT NULL,
    i_im_id int           NOT NULL,
    PRIMARY KEY (i_id)
))",
    R"(CREATE TABLE stock (
    s_w_id       int           NOT NULL,
    s_i_id       int           NOT NULL,
    s_quantity   int           NOT NULL,
    s_ytd        decimal(8, 2) NOT NULL,
    s_order_cnt  int           NOT NULL,
    s_remote_cnt int           NOT NULL,
    s_data       varchar(50)   NOT NULL,
    s_dist_01    char(24)      NOT NULL,
    s_dist_02    char(24)      NOT NULL,
    s_dist_03    char(24)      NOT NULL,
    s_dist_04    char(24)      NOT NULL,
    s_dist_05    char(24)      NOT NULL,
    s_dist_06    char(24)      NOT NULL,
    s_dist_07    char(24)      NOT NULL,
    s_dist_08    char(24)      NOT NULL,
    s_dist_09    char(24)      NOT NULL,
    s_dist_10    char(24)      NOT NULL,
    FOREIGN KEY (s_w_id) REFERENCES warehouse (w_id) ON DELETE CASCADE,
    FOREIGN KEY (s_i_id) REFERENCES item (i_id) ON DELETE CASCADE,
    PRIMARY KEY (s_w_id, s_i_id)
))",
    R"(CREATE TABLE district (
    d_w_id      int            NOT NULL,
    d_id        int            NOT NULL,
    d_ytd       decimal(12, 2) NOT NULL,
    d_tax       decimal(4, 4)  NOT NULL,
    d_next_o_id int            NOT NULL,
    d_name      varchar(10)    NOT NULL,
    d_street_1  varchar(20)    NOT NULL,
    d_street_2  varchar(20)    NOT NULL,
    d_city      varchar(20)    NOT NULL,
    d_state     char(2)        NOT NULL,
    d_zip       char(9)        NOT NULL,
    FOREIGN KEY (d_w_id) REFERENCES warehouse (w_id) ON DELETE CASCADE,
    PRIMARY KEY (d_w_id, d_id)
))",
    R"(CREATE TABLE customer (
    c_w_id         int            NOT NULL,
    c_d_id         int            NOT NULL,
    c_id           int            NOT NULL,
    c_discount     decimal(4, 4)  NOT NULL,
    c_credit       char(2)        NOT NULL,
    c_last         varchar(16)    NOT NULL,
    c_first        varchar(16)    NOT NULL,
    c_credit_lim   decimal(12, 2) NOT NULL,
    c_balance      decimal(12, 2) NOT NULL,
    c_ytd_payment  float          NOT NULL,
    c_payment_cnt  int            NOT NULL,
    c_delivery_cnt int            NOT NULL,
    c_street_1     varchar(20)    NOT NULL,
    c_street_2     varchar(20)    NOT NULL,
    c_city         varchar(20)    NOT NULL,
    c_state        char(2)        NOT NULL,
    c_zip          char(9)        NOT NULL,
    c_phone        char(16)       NOT NULL,
    c_since        timestamp      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    c_middle       char(2)        NOT NULL,
    c_data         varchar(500)   NOT NULL,
    FOREIGN KEY (c_w_id, c_d_id) REFERENCES district (d_w_id, d_id) ON DELETE CASCADE,
    PRIMARY KEY (c_w_id, c_d_id, c_id)
))",
    R"(CREATE TABLE history (
    h_c_id   int           NOT NULL,
    h_c_d_id int           NOT NULL,
    h_c_w_id int           NOT NULL,
    h_d_id   int           NOT NULL,
    h_w_id   int           NOT NULL,
    h_date   timestamp     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    h_amount decimal(6, 2) NOT NULL,
    h_data   varchar(24)   NOT NULL,
    FOREIGN KEY (h_c_w_id, h_c_d_id, h_c_id) REFERENCES customer (c_w_id, c_d_id, c_id) ON DELETE CASCADE,
    FOREIGN KEY (h_w_id, h_d_id) REFERENCES district (d_w_id, d_id) ON DELETE CASCADE
))",
    R"(CREATE TABLE oorder (
    o_w_id       int       NOT NULL,
    o_d_id       int       NOT NULL,
    o_id         int       NOT NULL,
    o_c_id       int       NOT NULL,
    o_carrier_id int                DEFAULT NULL,
    o_ol_cnt     int       NOT NULL,
    o_all_local  int       NOT NULL,
    o_entry_d    timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (o_w_id, o_d_id, o_id),
    FOREIGN KEY (o_w_id, o_d_id, o_c_id) REFERENCES customer (c_w_id, c_d_id, c_id) ON DELETE CASCADE,
    UNIQUE (o_w_id, o_d_id, o_c_id, o_id)
))",
    R"(CREATE TABLE new_order (
    no_w_id int NOT NULL,
    no_d_id int NOT NULL,
    no_o_id int NOT NULL,
    FOREIGN KEY (no_w_id, no_d_id, no_o_id) REFERENCES oorder (o_w_id, o_d_id, o_id) ON DELETE CASCADE,
    PRIMARY KEY (no_w_id, no_d_id, no_o_id)
))",
    R"(CREATE TABLE order_line (
    ol_w_id        int           NOT NULL,
    ol_d_id        int           NOT NULL,
    ol_o_id        int           NOT NULL,
    ol_number      int           NOT NULL,
    ol_i_id        int           NOT NULL,
    ol_delivery_d  timestamp     NULL DEFAULT NULL,
    ol_amount      decimal(6, 2) NOT NULL,
    ol_supply_w_id int           NOT NULL,
    ol_quantity    decimal(6, 2) NOT NULL,
    ol_dist_info   char(24)      NOT NULL,
    FOREIGN KEY (ol_w_id, ol_d_id, ol_o_id) REFERENCES oorder (o_w_id, o_d_id, o_id) ON DELETE CASCADE,
    FOREIGN KEY (ol_supply_w_id, ol_i_id) REFERENCES stock (s_w_id, s_i_id) ON DELETE CASCADE,
    PRIMARY KEY (ol_w_id, ol_d_id, ol_o_id, ol_number)
))",
};

struct TIndexDef {
    const char* name;
    const char* sql;
};

const TIndexDef kIndexes[] = {
    {INDEX_CUSTOMER_NAME,
     "CREATE INDEX idx_customer_name ON customer (c_w_id, c_d_id, c_last, c_first)"},
    {INDEX_ORDER,
     "CREATE INDEX idx_order ON oorder (o_w_id, o_d_id, o_c_id, o_id)"},
};

ObConnectionConfig ConfigWithPath(const std::string& connectionString, const std::string& path) {
    auto cfg = ParseConnectionString(connectionString);
    if (!path.empty()) {
        cfg.path = path;
    }
    return cfg;
}

std::unique_ptr<ObConnection> ConnectToTargetDatabase(const ObConnectionConfig& cfg) {
    const std::string db = EffectiveDatabase(cfg);
    if (db.empty()) {
        throw std::runtime_error(
            "No database specified: set --connection database=... or --path");
    }

    // Connect without selecting DB so we can CREATE DATABASE first.
    auto conn = ObConnection::Connect(cfg, /*selectDatabase=*/false);

    auto exists = conn->Query(
        "SELECT 1 AS ok FROM information_schema.schemata WHERE schema_name = ? LIMIT 1",
        MakeParams(db));
    if (!exists.TryNextRow()) {
        try {
            conn->CreateDatabaseIfNotExists(db);
        } catch (const std::exception& ex) {
            throw std::runtime_error(
                "Database '" + db + "' does not exist and CREATE DATABASE failed: " + ex.what());
        }
    }
    conn->UseDatabase(db);
    return conn;
}

bool IndexExists(ObConnection& conn, const std::string& database, const std::string& indexName) {
    auto result = conn.Query(
        "SELECT 1 AS ok FROM information_schema.statistics "
        "WHERE table_schema = ? AND index_name = ? LIMIT 1",
        MakeParams(database, indexName));
    return result.TryNextRow();
}

void ExecAll(ObConnection& conn, const char* const* statements, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        conn.ExecuteSimple(statements[i]);
    }
}

} // anonymous

void InitSync(const std::string& connectionString, const std::string& path) {
    LOG_I("Initializing TPC-C schema...");

    try {
        auto cfg = ConfigWithPath(connectionString, path);
        const std::string db = EffectiveDatabase(cfg);
        auto conn = ConnectToTargetDatabase(cfg);

        LOG_I("Using database '{}'", db);
        ExecAll(*conn, kDropTables, sizeof(kDropTables) / sizeof(kDropTables[0]));
        ExecAll(*conn, kCreateTables, sizeof(kCreateTables) / sizeof(kCreateTables[0]));
        LOG_I("All TPC-C tables created successfully");
    } catch (const std::exception& e) {
        LOG_E("Failed to create TPC-C tables: {}", e.what());
        LOG_E("After fixing the reason, you might need to run `tpcc clean`.");
        throw;
    }
}

void CreateIndexes(const std::string& connectionString, const std::string& path) {
    LOG_I("Creating secondary indexes...");

    try {
        auto cfg = ConfigWithPath(connectionString, path);
        const std::string db = EffectiveDatabase(cfg);
        auto conn = ConnectToTargetDatabase(cfg);

        for (const auto& index : kIndexes) {
            if (IndexExists(*conn, db, index.name)) {
                LOG_I("Index '{}' already exists, skipping", index.name);
                continue;
            }
            try {
                conn->ExecuteSimple(index.sql);
                LOG_I("Created index '{}'", index.name);
            } catch (const DbError& err) {
                // 1061 = ER_DUP_KEYNAME
                if (err.Code() == 1061) {
                    LOG_I("Index '{}' already exists, skipping", index.name);
                    continue;
                }
                throw;
            }
        }
        LOG_I("Secondary indexes ready");
    } catch (const std::exception& e) {
        LOG_E("Failed to create indexes: {}", e.what());
        throw;
    }
}

} // namespace NTPCC
