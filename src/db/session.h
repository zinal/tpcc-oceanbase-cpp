#pragma once

// Target session API for OceanBase (MySQL C API).
// Implementation (session.cpp) is Phase 1 of docs/PORTING_PLAN.md.
// Baseline: src/pg_session.h (libpqxx).

#include "db/params.h"
#include "db/query_result.h"
#include "future.h"
#include "thread_pool.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NTPCC {

// Forward-declare opaque client handle to avoid forcing mysql.h on all TUs yet.
struct ObConnection;

using BulkRow = std::vector<std::optional<std::string>>;
using BulkWriter = std::function<void(std::function<void(BulkRow)>)>;

class ObSession {
public:
    ObSession() = default;
    ObSession(std::unique_ptr<ObConnection> conn, IExecutor* executor,
              std::shared_ptr<std::atomic<bool>> shutdownFlag = {});

    ObSession(ObSession&& other) noexcept;
    ObSession& operator=(ObSession&& other) noexcept;

    ObSession(const ObSession&) = delete;
    ObSession& operator=(const ObSession&) = delete;

    ~ObSession();

    // Lazily begins REPEATABLE READ transaction on first call.
    TFuture<QueryResult> ExecuteQuery(std::string_view sql, const Params& params = {});
    TFuture<uint64_t> ExecuteModify(std::string_view sql, const Params& params = {});
    TFuture<void> Commit();
    TFuture<void> Rollback();

    // DDL / session-level statements outside a TPC-C transaction.
    TFuture<QueryResult> ExecuteNonTx(std::string_view sql);

    // Replaces PostgreSQL COPY (pqxx::stream_to). Prefer batched multi-row INSERT.
    TFuture<void> ExecuteBulk(
        const std::string& tableName,
        const std::vector<std::string>& columns,
        BulkWriter writer);

    bool HasConnection() const;
    std::unique_ptr<ObConnection> ReleaseConnection();

    void SetShutdownFlag(std::shared_ptr<std::atomic<bool>> flag) {
        shutdownFlag_ = std::move(flag);
    }

private:
    void CheckShutdown() const;

    std::unique_ptr<ObConnection> conn_;
    bool inTxn_ = false;
    IExecutor* executor_ = nullptr;
    std::shared_ptr<std::atomic<bool>> shutdownFlag_;
};

} // namespace NTPCC
