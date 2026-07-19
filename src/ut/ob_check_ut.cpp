#include "check.h"
#include "clean.h"
#include "db/connection.h"
#include "import.h"
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

} // namespace

TEST(ObCheckTest, AfterImportConsistency) {
    if (!CanConnect()) {
        GTEST_SKIP() << "DB not available (set TPCC_TEST_CONNECTION_ADMIN)";
    }
    if (const char* skip = std::getenv("TPCC_SKIP_CHECK_UT"); skip && *skip && skip[0] != '0') {
        GTEST_SKIP() << "TPCC_SKIP_CHECK_UT set";
    }

    const std::string connStr = AdminConnection();
    const std::string pathDb = "tpcc_check_ut";

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

    ASSERT_NO_THROW(NTPCC::CheckSync(connStr, 1, /*afterImport=*/true, pathDb));

    NTPCC::CleanSync(connStr, pathDb);
}
