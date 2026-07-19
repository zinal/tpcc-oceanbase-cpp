#include "path_checker.h"

#include "constants.h"
#include "db/connection.h"
#include "log.h"
#include "util.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace NTPCC {

namespace {

ObConnectionConfig ConfigWithPath(const std::string& connectionString, const std::string& path) {
    auto cfg = ParseConnectionString(connectionString);
    if (!path.empty()) {
        cfg.path = path;
    }
    return cfg;
}

std::unique_ptr<ObConnection> ConnectChecked(const ObConnectionConfig& cfg) {
    const std::string db = EffectiveDatabase(cfg);
    if (db.empty()) {
        throw std::runtime_error(
            "No database specified: set --connection database=... or --path");
    }

    auto conn = ObConnection::Connect(cfg, /*selectDatabase=*/false);
    auto exists = conn->Query(
        "SELECT 1 AS ok FROM information_schema.schemata WHERE schema_name = ? LIMIT 1",
        MakeParams(db));
    if (!exists.TryNextRow()) {
        // Create is not our job here; callers decide. Return connection without USE
        // so ListTables can still query information_schema for a missing DB (empty set).
        return conn;
    }
    conn->UseDatabase(db);
    return conn;
}

std::unordered_set<std::string> ListTables(ObConnection& conn, const std::string& database) {
    auto result = conn.Query(
        "SELECT table_name AS table_name FROM information_schema.tables "
        "WHERE table_schema = ? AND table_type = 'BASE TABLE'",
        MakeParams(database));

    std::unordered_set<std::string> tables;
    while (result.TryNextRow()) {
        tables.insert(result.GetString("table_name"));
    }
    return tables;
}

std::unordered_set<std::string> ListIndexes(ObConnection& conn, const std::string& database,
                                            const std::string& tableName) {
    auto result = conn.Query(
        "SELECT index_name AS index_name FROM information_schema.statistics "
        "WHERE table_schema = ? AND table_name = ?",
        MakeParams(database, tableName));

    std::unordered_set<std::string> indexes;
    while (result.TryNextRow()) {
        indexes.insert(result.GetString("index_name"));
    }
    return indexes;
}

void CheckTablesExist(ObConnection& conn, const std::string& database, const char* what) {
    auto tables = ListTables(conn, database);
    for (const char* table : TPCC_TABLES) {
        if (!tables.contains(table)) {
            std::cerr << "TPC-C table '" << table << "' is missing. " << what << std::endl;
            std::exit(1);
        }
    }
}

void CheckNoTablesExist(ObConnection& conn, const std::string& database, const char* what) {
    auto tables = ListTables(conn, database);
    for (const char* table : TPCC_TABLES) {
        if (tables.contains(table)) {
            std::cerr << "TPC-C table '" << table << "' already exists. " << what << std::endl;
            std::exit(1);
        }
    }
}

void CheckIndexExists(ObConnection& conn, const std::string& database,
                      const std::string& tableName, const std::string& expectedIndex) {
    auto indexes = ListIndexes(conn, database, tableName);
    if (!indexes.contains(expectedIndex)) {
        std::cerr << "Table '" << tableName
                  << "' is missing expected index '" << expectedIndex
                  << "'. Did you forget to run 'tpcc import' (creates indexes)?" << std::endl;
        std::exit(1);
    }
}

int GetWarehouseCount(ObConnection& conn) {
    auto result = conn.QuerySimple("SELECT COUNT(*) AS cnt FROM warehouse");
    if (!result.TryNextRow()) {
        return 0;
    }
    return result.GetInt32("cnt");
}

} // anonymous

void CheckDbForInit(const std::string& connectionString, const std::string& path) noexcept {
    try {
        auto cfg = ConfigWithPath(connectionString, path);
        const std::string db = EffectiveDatabase(cfg);
        auto conn = ConnectChecked(cfg);
        // Missing database => no tables => OK for init.
        CheckNoTablesExist(*conn, db, "Already inited or forgot to clean?");
    } catch (const std::exception& e) {
        std::cerr << "Pre-flight check for init failed: " << e.what() << std::endl;
        std::exit(1);
    }
}

void CheckDbForImport(const std::string& connectionString, const std::string& path) noexcept {
    try {
        auto cfg = ConfigWithPath(connectionString, path);
        const std::string db = EffectiveDatabase(cfg);
        auto conn = ConnectChecked(cfg);

        CheckTablesExist(*conn, db, "Run 'tpcc init' first.");

        int whCount = GetWarehouseCount(*conn);
        if (whCount != 0) {
            std::cerr << "Database already has " << whCount
                      << " warehouses. Are you importing to already imported data?" << std::endl;
            std::exit(1);
        }
    } catch (const std::exception& e) {
        std::cerr << "Pre-flight check for import failed: " << e.what() << std::endl;
        std::exit(1);
    }
}

void CheckDbForRun(const std::string& connectionString, int expectedWhCount,
                   const std::string& path) noexcept {
    try {
        auto cfg = ConfigWithPath(connectionString, path);
        const std::string db = EffectiveDatabase(cfg);
        auto conn = ConnectChecked(cfg);

        CheckTablesExist(*conn, db, "Run 'tpcc init' and 'tpcc import' first.");

        CheckIndexExists(*conn, db, TABLE_CUSTOMER, INDEX_CUSTOMER_NAME);
        CheckIndexExists(*conn, db, TABLE_OORDER, INDEX_ORDER);

        int whCount = GetWarehouseCount(*conn);
        if (whCount == 0) {
            std::cerr << "Empty warehouse table (and maybe missing other TPC-C data), "
                      << "run 'tpcc import' first" << std::endl;
            std::exit(1);
        }
        if (whCount < expectedWhCount) {
            std::cerr << "Expected data for " << expectedWhCount
                      << " warehouses, but found only " << whCount << std::endl;
            std::exit(1);
        }
    } catch (const std::exception& e) {
        std::cerr << "Pre-flight check for run failed: " << e.what() << std::endl;
        std::exit(1);
    }
}

} // namespace NTPCC
