#include "clean.h"
#include "constants.h"
#include "db/connection.h"
#include "db/session.h"
#include "import.h"
#include "init.h"
#include "thread_pool.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <functional>
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

class InlineExecutor : public IExecutor {
public:
    void Submit(std::function<void()> task) override {
        task();
    }
};

int CountRows(NTPCC::ObConnection& conn, const char* table) {
    auto result = conn.QuerySimple(std::string("SELECT COUNT(*) AS cnt FROM `") + table + "`");
    if (!result.TryNextRow()) {
        return -1;
    }
    return result.GetInt32("cnt");
}

} // namespace

TEST(ObSessionBulkTest, ExecuteBulkInsertsRows) {
    if (!CanConnect()) {
        GTEST_SKIP() << "DB not available (set TPCC_TEST_CONNECTION_ADMIN)";
    }

    const std::string connStr = AdminConnection();
    const std::string pathDb = "tpcc_bulk_ut";

    try {
        NTPCC::CleanSync(connStr, pathDb);
    } catch (...) {
    }

    auto cfg = NTPCC::ParseConnectionString(connStr);
    cfg.path = pathDb;
    {
        auto admin = NTPCC::ObConnection::Connect(cfg, /*selectDatabase=*/false);
        admin->CreateDatabaseIfNotExists(pathDb);
        admin->UseDatabase(pathDb);
        admin->ExecuteSimple("DROP TABLE IF EXISTS bulk_demo");
        admin->ExecuteSimple(
            "CREATE TABLE bulk_demo ("
            "id INT NOT NULL, "
            "name VARCHAR(32) NOT NULL, "
            "note VARCHAR(32) NULL, "
            "PRIMARY KEY (id))");
    }

    InlineExecutor executor;
    auto conn = NTPCC::ObConnection::Connect(cfg);
    NTPCC::ObSession session(std::move(conn), &executor);

    session.ExecuteBulk(
        "bulk_demo",
        {"id", "name", "note"},
        [](auto emit) {
            for (int i = 1; i <= 5; ++i) {
                emit(NTPCC::BulkRow{
                    std::to_string(i),
                    "row-" + std::to_string(i),
                    i % 2 == 0 ? std::nullopt : std::optional<std::string>("x"),
                });
            }
        }).Get();
    session.Commit().Get();

    {
        auto check = NTPCC::ObConnection::Connect(cfg);
        EXPECT_EQ(CountRows(*check, "bulk_demo"), 5);
        auto nulls = check->QuerySimple(
            "SELECT COUNT(*) AS cnt FROM bulk_demo WHERE note IS NULL");
        ASSERT_TRUE(nulls.TryNextRow());
        EXPECT_EQ(nulls.GetInt32("cnt"), 2);
    }

    NTPCC::CleanSync(connStr, pathDb);
}

TEST(ObImportTest, ImportOneWarehouseRoundTrip) {
    if (!CanConnect()) {
        GTEST_SKIP() << "DB not available (set TPCC_TEST_CONNECTION_ADMIN)";
    }
    if (const char* skip = std::getenv("TPCC_SKIP_IMPORT_UT"); skip && *skip && skip[0] != '0') {
        GTEST_SKIP() << "TPCC_SKIP_IMPORT_UT set";
    }

    const std::string connStr = AdminConnection();
    const std::string pathDb = "tpcc_import_ut";

    try {
        NTPCC::CleanSync(connStr, pathDb);
    } catch (...) {
    }

    NTPCC::InitSync(connStr, pathDb);

    NTPCC::TImportConfig config;
    config.ConnectionString = connStr;
    config.Path = pathDb;
    config.WarehouseCount = 1;
    config.LoadThreadCount = 1;
    config.UseTui = false;
    NTPCC::ImportSync(config);

    {
        auto cfg = NTPCC::ParseConnectionString(connStr);
        cfg.path = pathDb;
        auto conn = NTPCC::ObConnection::Connect(cfg);
        EXPECT_EQ(CountRows(*conn, "warehouse"), 1);
        EXPECT_EQ(CountRows(*conn, "district"), NTPCC::DISTRICT_COUNT);
        EXPECT_EQ(CountRows(*conn, "item"), NTPCC::ITEM_COUNT);
        EXPECT_EQ(CountRows(*conn, "stock"), NTPCC::ITEM_COUNT);
        EXPECT_EQ(CountRows(*conn, "customer"),
                  NTPCC::DISTRICT_COUNT * NTPCC::CUSTOMERS_PER_DISTRICT);

        auto idx = conn->Query(
            "SELECT 1 AS ok FROM information_schema.statistics "
            "WHERE table_schema = ? AND index_name = ? LIMIT 1",
            NTPCC::MakeParams(pathDb, std::string(NTPCC::INDEX_CUSTOMER_NAME)));
        EXPECT_TRUE(idx.TryNextRow());
    }

    NTPCC::CleanSync(connStr, pathDb);
}
