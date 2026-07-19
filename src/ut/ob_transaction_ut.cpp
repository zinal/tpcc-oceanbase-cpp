#include "transactions.h"
#include "task_queue.h"
#include "db/connection.h"
#include "db/errors.h"
#include "db/session.h"
#include "thread_pool.h"
#include "init.h"
#include "import.h"
#include "clean.h"
#include "constants.h"
#include "util.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace NTPCC;

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

bool TryConnect(const std::string& connStr) {
    try {
        auto cfg = ParseConnectionString(connStr);
        auto conn = ObConnection::Connect(cfg, /*selectDatabase=*/false);
        auto result = conn->QuerySimple("SELECT 1 AS v");
        return result.TryNextRow() && result.GetInt32("v") == 1;
    } catch (...) {
        return false;
    }
}

ObConnectionConfig PathConfig(const std::string& connStr, const std::string& path) {
    auto cfg = ParseConnectionString(connStr);
    cfg.path = path;
    return cfg;
}

} // namespace

class TpccObTest : public ::testing::Test {
protected:
    static std::string ConnStr;
    static std::string PathDb;
    static bool Available;
    static bool SetupOK;

    static void SetUpTestSuite() {
        ConnStr = AdminConnection();
        PathDb = "tpcc_tx_ut";
        Available = TryConnect(ConnStr);
        if (!Available) {
            return;
        }

        try {
            try {
                CleanSync(ConnStr, PathDb);
            } catch (...) {
            }
            InitSync(ConnStr, PathDb);

            TImportConfig cfg;
            cfg.ConnectionString = ConnStr;
            cfg.Path = PathDb;
            cfg.WarehouseCount = 1;
            cfg.LoadThreadCount = 1;
            cfg.UseTui = false;
            ImportSync(cfg);
            SetupOK = true;
        } catch (const std::exception& e) {
            std::cerr << "TpccObTest setup failed: " << e.what() << "\n";
        }
    }

    static void TearDownTestSuite() {
        if (!Available) {
            return;
        }
        try {
            CleanSync(ConnStr, PathDb);
        } catch (...) {
        }
    }

    void SetUp() override {
        if (!Available) {
            GTEST_SKIP() << "DB not reachable (set TPCC_TEST_CONNECTION_ADMIN)";
        }
        if (!SetupOK) {
            GTEST_SKIP() << "DB setup failed";
        }

        GetGlobalErrorVariable().store(false);

        queue_ = CreateTaskQueue(1, 0, 10, 10);
        executor_ = std::make_unique<TThreadPool>(2);
        Reconnect();
        queue_->Run();
    }

    void TearDown() override {
        if (queue_) {
            queue_->WakeupAndNeverSleep();
            queue_->Join();
        }
        session_ = ObSession{};
        executor_.reset();
    }

    void Reconnect() {
        auto conn = ObConnection::Connect(PathConfig(ConnStr, PathDb));
        session_ = ObSession(std::move(conn), executor_.get());
    }

    bool RunTx(TFuture<bool> (*txFunc)(TTransactionContext&,
                                       std::chrono::microseconds&,
                                       ObSession&)) {
        TTransactionContext ctx{
            .TerminalID = 0,
            .WarehouseID = 1,
            .WarehouseCount = 1,
            .TaskQueue = *queue_,
        };

        std::chrono::microseconds latency{};
        auto future = txFunc(ctx, latency, session_);
        try {
            return future.Get();
        } catch (const TUserAbortedException&) {
            return false;
        }
    }

    int QueryInt(const std::string& sql) {
        auto conn = ObConnection::Connect(PathConfig(ConnStr, PathDb));
        auto result = conn->QuerySimple(sql);
        if (!result.TryNextRow()) {
            throw std::runtime_error("QueryInt returned no rows: " + sql);
        }
        return result.GetInt32("v");
    }

    double QueryDouble(const std::string& sql) {
        auto conn = ObConnection::Connect(PathConfig(ConnStr, PathDb));
        auto result = conn->QuerySimple(sql);
        if (!result.TryNextRow()) {
            throw std::runtime_error("QueryDouble returned no rows: " + sql);
        }
        return result.GetDouble("v");
    }

    std::unique_ptr<ITaskQueue> queue_;
    std::unique_ptr<TThreadPool> executor_;
    ObSession session_;
};

std::string TpccObTest::ConnStr;
std::string TpccObTest::PathDb;
bool TpccObTest::Available = false;
bool TpccObTest::SetupOK = false;

TEST(DbErrorClassifyTest, MapsRetryableMysqlCodes) {
    EXPECT_EQ(ClassifyDbError(1213), DbErrorKind::Deadlock);
    EXPECT_EQ(ClassifyDbError(1205), DbErrorKind::LockWaitTimeout);
    EXPECT_EQ(ClassifyDbError(6235), DbErrorKind::SerializationFailure);
    EXPECT_EQ(ClassifyDbError(1317), DbErrorKind::Shutdown);
    EXPECT_TRUE(DbError(1213, "deadlock").Retryable());
    EXPECT_TRUE(DbError(1205, "lock wait").Retryable());
    EXPECT_TRUE(DbError(6235, "can't serialize access").Retryable());
    EXPECT_FALSE(DbError(1317, "interrupted").Retryable());
    EXPECT_FALSE(DbError(1062, "duplicate").Retryable());
    EXPECT_FALSE(DbError(2006, "gone").Retryable());
}

TEST_F(TpccObTest, NewOrder) {
    int ordersBefore = QueryInt("SELECT COUNT(*) AS v FROM oorder");
    int newOrdersBefore = QueryInt("SELECT COUNT(*) AS v FROM new_order");
    int olBefore = QueryInt("SELECT COUNT(*) AS v FROM order_line");
    int nextOidSumBefore = QueryInt("SELECT COALESCE(SUM(d_next_o_id),0) AS v FROM district");

    bool committed = false;
    for (int attempt = 0; attempt < 200 && !committed; ++attempt) {
        if (attempt > 0) {
            Reconnect();
        }
        committed = RunTx(GetNewOrderTask);
    }
    ASSERT_TRUE(committed) << "NewOrder never committed in 200 attempts (expected ~1% abort rate)";

    int ordersAfter = QueryInt("SELECT COUNT(*) AS v FROM oorder");
    int newOrdersAfter = QueryInt("SELECT COUNT(*) AS v FROM new_order");
    int olAfter = QueryInt("SELECT COUNT(*) AS v FROM order_line");
    int nextOidSumAfter = QueryInt("SELECT COALESCE(SUM(d_next_o_id),0) AS v FROM district");

    EXPECT_GE(ordersAfter, ordersBefore + 1);
    EXPECT_GE(newOrdersAfter, newOrdersBefore + 1);
    EXPECT_GE(olAfter, olBefore + MIN_ITEMS);
    EXPECT_GE(nextOidSumAfter, nextOidSumBefore + 1);
}

TEST_F(TpccObTest, Payment) {
    double wYtdBefore = QueryDouble("SELECT COALESCE(SUM(w_ytd),0) AS v FROM warehouse");
    double dYtdBefore = QueryDouble("SELECT COALESCE(SUM(d_ytd),0) AS v FROM district");
    int histBefore = QueryInt("SELECT COUNT(*) AS v FROM history");

    bool ok = RunTx(GetPaymentTask);
    ASSERT_TRUE(ok);

    double wYtdAfter = QueryDouble("SELECT COALESCE(SUM(w_ytd),0) AS v FROM warehouse");
    double dYtdAfter = QueryDouble("SELECT COALESCE(SUM(d_ytd),0) AS v FROM district");
    int histAfter = QueryInt("SELECT COUNT(*) AS v FROM history");

    double wDelta = wYtdAfter - wYtdBefore;
    double dDelta = dYtdAfter - dYtdBefore;

    EXPECT_GT(wDelta, 0.0);
    EXPECT_NEAR(wDelta, dDelta, 0.01)
        << "warehouse and district YTD should increase by the same amount";
    EXPECT_EQ(histAfter, histBefore + 1);
}

TEST_F(TpccObTest, Delivery) {
    int newOrdersBefore = QueryInt("SELECT COUNT(*) AS v FROM new_order");

    bool ok = RunTx(GetDeliveryTask);
    ASSERT_TRUE(ok);

    int newOrdersAfter = QueryInt("SELECT COUNT(*) AS v FROM new_order");
    int removed = newOrdersBefore - newOrdersAfter;
    EXPECT_GE(removed, 0);
    EXPECT_LE(removed, DISTRICT_COUNT);

    if (removed > 0) {
        int withCarrier = QueryInt(
            "SELECT COUNT(*) AS v FROM oorder WHERE o_carrier_id IS NOT NULL AND o_w_id = 1");
        EXPECT_GT(withCarrier, 0);
    }
}

TEST_F(TpccObTest, OrderStatus) {
    EXPECT_TRUE(RunTx(GetOrderStatusTask));
}

TEST_F(TpccObTest, StockLevel) {
    EXPECT_TRUE(RunTx(GetStockLevelTask));
}

TEST_F(TpccObTest, MixedWorkloadNoFatalErrors) {
    // Full CheckSync is Phase 5; here we only verify transactions keep committing.
    constexpr int kRounds = 20;
    int commits = 0;

    for (int i = 0; i < kRounds; ++i) {
        Reconnect();
        try {
            if (RunTx(GetNewOrderTask)) {
                ++commits;
            }
        } catch (...) {
        }

        Reconnect();
        if (RunTx(GetPaymentTask)) {
            ++commits;
        }

        if (i % 5 == 0) {
            Reconnect();
            if (RunTx(GetDeliveryTask)) {
                ++commits;
            }
        }
    }

    Reconnect();
    EXPECT_TRUE(RunTx(GetOrderStatusTask));
    Reconnect();
    EXPECT_TRUE(RunTx(GetStockLevelTask));

    EXPECT_GT(commits, kRounds) << "expected most NewOrder/Payment/Delivery attempts to commit";
    EXPECT_FALSE(GetGlobalErrorVariable().load());
}
