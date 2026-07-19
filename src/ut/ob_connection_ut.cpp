#include "db/connection.h"
#include "db/connection_pool.h"
#include "db/params.h"
#include "db/session.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace {

std::string TestConnectionString() {
    if (const char* env = std::getenv("TPCC_TEST_CONNECTION")) {
        return env;
    }
    return "host=127.0.0.1;port=2881;user=root@test;password=tpcc;database=test";
}

bool CanConnect() {
    try {
        auto cfg = NTPCC::ParseConnectionString(TestConnectionString());
        auto conn = NTPCC::ObConnection::Connect(cfg);
        auto result = conn->QuerySimple("SELECT 1 AS v");
        return result.TryNextRow() && result.GetInt32("v") == 1;
    } catch (...) {
        return false;
    }
}

} // namespace

TEST(ObConnectionTest, ParseConnectionString) {
    auto cfg = NTPCC::ParseConnectionString(
        "host=10.0.0.1;port=2881;user=root@test;password=secret;database=tpcc");
    EXPECT_EQ(cfg.host, "10.0.0.1");
    EXPECT_EQ(cfg.port, 2881);
    EXPECT_EQ(cfg.user, "root@test");
    EXPECT_EQ(cfg.password, "secret");
    EXPECT_EQ(cfg.database, "tpcc");

    auto cfg2 = NTPCC::ParseConnectionString(
        "host=localhost port=2881 dbname=tpcc user=root@test");
    EXPECT_EQ(cfg2.database, "tpcc");
    EXPECT_EQ(cfg2.host, "localhost");
}

TEST(ObConnectionTest, SelectOneAndRepeatableRead) {
    if (!CanConnect()) {
        GTEST_SKIP() << "OceanBase not available (set TPCC_TEST_CONNECTION)";
    }

    auto cfg = NTPCC::ParseConnectionString(TestConnectionString());
    auto conn = NTPCC::ObConnection::Connect(cfg);

    conn->BeginRepeatableRead();
    auto result = conn->Query("SELECT CAST(? AS SIGNED) AS v", NTPCC::MakeParams(42));
    ASSERT_TRUE(result.TryNextRow());
    EXPECT_EQ(result.GetInt32("v"), 42);

    auto result2 = conn->Query("SELECT ? AS s", NTPCC::MakeParams(std::string("hello")));
    ASSERT_TRUE(result2.TryNextRow());
    EXPECT_EQ(result2.GetString("s"), "hello");

    conn->Commit();
}

TEST(ObSessionTest, ExecuteQueryViaPool) {
    if (!CanConnect()) {
        GTEST_SKIP() << "OceanBase not available (set TPCC_TEST_CONNECTION)";
    }

    NTPCC::ObConnectionPool pool(TestConnectionString(), 2, 2);
    auto guard = pool.AcquireGuard();

    auto qr = guard->ExecuteQuery("SELECT 1 AS v").Get();
    ASSERT_TRUE(qr.TryNextRow());
    EXPECT_EQ(qr.GetInt32("v"), 1);

    guard->Commit().Get();
}
