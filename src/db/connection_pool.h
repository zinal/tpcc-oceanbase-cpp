#pragma once

// Target connection pool for OceanBase.
// Baseline: src/pg_connection_pool.h

#include "db/session.h"
#include "thread_pool.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace NTPCC {

struct ObConnectionConfig {
    std::string host = "127.0.0.1";
    int port = 2881;
    std::string user = "root@test";
    std::string password;
    std::string database = "tpcc";
    // Optional: maps CLI --path to database name (USE db) instead of PG schema.
    std::string path;
};

class ObConnectionPool {
public:
    ObConnectionPool(ObConnectionConfig config, size_t poolSize, size_t ioThreads);
    ~ObConnectionPool();

    ObConnectionPool(const ObConnectionPool&) = delete;
    ObConnectionPool& operator=(const ObConnectionPool&) = delete;

    ObSession AcquireSession();
    void ReleaseSession(ObSession session);

    class SessionGuard {
    public:
        SessionGuard(ObConnectionPool* pool, ObSession session)
            : pool_(pool), session_(std::move(session)) {}
        ~SessionGuard() {
            if (pool_ && session_.HasConnection()) {
                pool_->ReleaseSession(std::move(session_));
            }
        }
        ObSession& Get() { return session_; }
        ObSession* operator->() { return &session_; }
    private:
        ObConnectionPool* pool_;
        ObSession session_;
    };

    SessionGuard AcquireGuard();
    void CancelAll();
    IExecutor* Executor();

private:
    ObConnectionConfig config_;
    std::unique_ptr<TThreadPool> ioPool_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<std::unique_ptr<ObConnection>> free_;
    std::vector<ObConnection*> checkedOut_;
    std::shared_ptr<std::atomic<bool>> shutdownFlag_;
};

// Parse "host=...;port=...;user=...;password=...;database=..." (OceanBase DSN).
ObConnectionConfig ParseConnectionString(const std::string& connection);

} // namespace NTPCC
