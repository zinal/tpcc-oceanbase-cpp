#include "clean.h"
#include "constants.h"
#include "db/connection.h"
#include "init.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace {

std::string AdminConnection() {
    if (const char* env = std::getenv("TPCC_TEST_CONNECTION_ADMIN")) {
        return env;
    }
    if (const char* env = std::getenv("TPCC_TEST_CONNECTION")) {
        return env;
    }
    return "host=127.0.0.1;port=3306;user=root;password=tpcc;database=tpcc";
}

bool CanConnect() {
    try {
        auto cfg = NTPCC::ParseConnectionString(AdminConnection());
        auto conn = NTPCC::ObConnection::Connect(cfg, /*selectDatabase=*/false);
        auto result = conn->QuerySimple("SELECT 1 AS v");
        return result.TryNextRow() && result.GetInt32("v") == 1;
    } catch (...) {
        return false;
    }
}

bool TableExists(NTPCC::ObConnection& conn, const std::string& db, const std::string& table) {
    auto result = conn.Query(
        "SELECT 1 AS ok FROM information_schema.tables "
        "WHERE table_schema = ? AND table_name = ? AND table_type = 'BASE TABLE' LIMIT 1",
        NTPCC::MakeParams(db, table));
    return result.TryNextRow();
}

bool ForeignKeysExist(NTPCC::ObConnection& conn, const std::string& db) {
    auto result = conn.Query(
        "SELECT 1 AS ok FROM information_schema.table_constraints "
        "WHERE table_schema = ? AND constraint_type = 'FOREIGN KEY' LIMIT 1",
        NTPCC::MakeParams(db));
    return result.TryNextRow();
}

} // namespace

TEST(ObDdlTest, InitAndCleanRoundTrip) {
    if (!CanConnect()) {
        GTEST_SKIP() << "DB not available (set TPCC_TEST_CONNECTION_ADMIN)";
    }

    const std::string connStr = AdminConnection();
    const std::string pathDb = "tpcc_ddl_ut";

    try {
        NTPCC::CleanSync(connStr, pathDb);
    } catch (...) {
    }

    NTPCC::InitSync(connStr, pathDb);

    {
        auto cfg = NTPCC::ParseConnectionString(connStr);
        cfg.path = pathDb;
        auto conn = NTPCC::ObConnection::Connect(cfg);
        EXPECT_TRUE(TableExists(*conn, pathDb, "warehouse"));
        EXPECT_TRUE(TableExists(*conn, pathDb, "customer"));
        EXPECT_TRUE(TableExists(*conn, pathDb, "order_line"));
    }

    NTPCC::CreateIndexes(connStr, pathDb);
    {
        auto cfg = NTPCC::ParseConnectionString(connStr);
        cfg.path = pathDb;
        auto conn = NTPCC::ObConnection::Connect(cfg);
        auto idx = conn->Query(
            "SELECT 1 AS ok FROM information_schema.statistics "
            "WHERE table_schema = ? AND index_name = ? LIMIT 1",
            NTPCC::MakeParams(pathDb, std::string(NTPCC::INDEX_CUSTOMER_NAME)));
        EXPECT_TRUE(idx.TryNextRow());
    }

    NTPCC::CleanSync(connStr, pathDb);

    {
        auto admin = NTPCC::ObConnection::Connect(
            NTPCC::ParseConnectionString(connStr), /*selectDatabase=*/false);
        auto exists = admin->Query(
            "SELECT 1 AS ok FROM information_schema.schemata WHERE schema_name = ? LIMIT 1",
            NTPCC::MakeParams(pathDb));
        EXPECT_FALSE(exists.TryNextRow());
    }
}

TEST(ObDdlTest, ForeignKeysConfigurable) {
    if (!CanConnect()) {
        GTEST_SKIP() << "DB not available (set TPCC_TEST_CONNECTION_ADMIN)";
    }

    const std::string connStr = AdminConnection();
    const std::string pathDb = "tpcc_ddl_fk_ut";

    try {
        NTPCC::CleanSync(connStr, pathDb);
    } catch (...) {
    }

    NTPCC::TInitOptions withFk;
    withFk.EnableForeignKeys = true;
    NTPCC::InitSync(connStr, pathDb, withFk);

    {
        auto cfg = NTPCC::ParseConnectionString(connStr);
        cfg.path = pathDb;
        auto conn = NTPCC::ObConnection::Connect(cfg);
        EXPECT_TRUE(ForeignKeysExist(*conn, pathDb));
    }

    NTPCC::TInitOptions withoutFk;
    withoutFk.EnableForeignKeys = false;
    NTPCC::InitSync(connStr, pathDb, withoutFk);

    {
        auto cfg = NTPCC::ParseConnectionString(connStr);
        cfg.path = pathDb;
        auto conn = NTPCC::ObConnection::Connect(cfg);
        EXPECT_FALSE(ForeignKeysExist(*conn, pathDb));
    }

    NTPCC::CleanSync(connStr, pathDb);
}
