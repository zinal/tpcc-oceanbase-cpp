#include "clean.h"

#include "constants.h"
#include "db/connection.h"
#include "log.h"
#include "util.h"

#include <fmt/format.h>

#include <stdexcept>
#include <string>

namespace NTPCC {

namespace {

ObConnectionConfig ConfigWithPath(const std::string& connectionString, const std::string& path) {
    auto cfg = ParseConnectionString(connectionString);
    if (!path.empty()) {
        cfg.path = path;
    }
    return cfg;
}

void DropTable(ObConnection& conn, const char* tableName) {
    const std::string sql = std::string("DROP TABLE IF EXISTS ") + QuoteIdent(tableName);
    LOG_T("Dropping table {}", tableName);
    try {
        conn.ExecuteSimple(sql);
        LOG_I("Table {} dropped successfully", tableName);
    } catch (const std::exception& ex) {
        LOG_W("Failed to drop table {}: {}", tableName, ex.what());
    }
}

} // anonymous

void CleanSync(const std::string& connectionString, const std::string& path) {
    auto cfg = ConfigWithPath(connectionString, path);
    const std::string db = EffectiveDatabase(cfg);
    if (db.empty()) {
        throw std::runtime_error(
            "No database specified: set --connection database=... or --path");
    }

    // Connect without selecting DB: target DB may be empty/missing after partial inits.
    auto conn = ObConnection::Connect(cfg, /*selectDatabase=*/false);

    // If database does not exist, nothing to clean.
    auto exists = conn->Query(
        "SELECT 1 AS ok FROM information_schema.schemata WHERE schema_name = ? LIMIT 1",
        MakeParams(db));
    if (!exists.TryNextRow()) {
        LOG_I("Database '{}' does not exist; nothing to clean", db);
        return;
    }

    conn->UseDatabase(db);
    LOG_I("Starting to drop TPC-C tables in database '{}'", db);

    // Children first (no CASCADE on DROP TABLE in MySQL mode).
    DropTable(*conn, TABLE_ORDER_LINE);
    DropTable(*conn, TABLE_NEW_ORDER);
    DropTable(*conn, TABLE_OORDER);
    DropTable(*conn, TABLE_HISTORY);
    DropTable(*conn, TABLE_CUSTOMER);
    DropTable(*conn, TABLE_DISTRICT);
    DropTable(*conn, TABLE_STOCK);
    DropTable(*conn, TABLE_ITEM);
    DropTable(*conn, TABLE_WAREHOUSE);

    try {
        conn->ExecuteSimple(
            fmt::format("DROP TABLEGROUP IF EXISTS {}", TABLEGROUP_TPCC));
        LOG_I("Table group '{}' dropped", TABLEGROUP_TPCC);
    } catch (const std::exception& ex) {
        LOG_W("Failed to drop table group '{}': {}", TABLEGROUP_TPCC, ex.what());
    }

    // --path means a dedicated database for the benchmark: drop it after tables.
    if (!path.empty()) {
        try {
            // Cannot drop the database we are currently using.
            conn->ExecuteSimple("USE information_schema");
            conn->ExecuteSimple("DROP DATABASE IF EXISTS " + QuoteIdent(path));
            LOG_I("Database '{}' dropped", path);
        } catch (const std::exception& ex) {
            LOG_W("Failed to drop database '{}': {}", path, ex.what());
        }
    }

    LOG_I("All TPC-C tables dropped successfully");
}

} // namespace NTPCC
