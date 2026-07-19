#pragma once

#include "db/connection.h"
#include "db/session.h"
#include "thread_pool.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace NTPCC {

class ObConnectionPool {
public:
    ObConnectionPool(const std::string& connectionString,
                     size_t poolSize,
                     size_t ioThreads,
                     const std::string& path = {});

    ObConnectionPool(ObConnectionConfig config, size_t poolSize, size_t ioThreads);

    ~ObConnectionPool();

    ObConnectionPool(const ObConnectionPool&) = delete;
    ObConnectionPool& operator=(const ObConnectionPool&) = delete;

    ObSession AcquireSession();
    void ReleaseSession(ObSession session);

    class SessionGuard {
    public:
        SessionGuard(ObConnectionPool& pool, ObSession session)
            : pool_(&pool), session_(std::move(session)) {}

        ~SessionGuard() {
            if (pool_ && session_.HasConnection()) {
                pool_->ReleaseSession(std::move(session_));
            }
        }

        SessionGuard(SessionGuard&& o) noexcept
            : pool_(o.pool_), session_(std::move(o.session_)) {
            o.pool_ = nullptr;
        }

        SessionGuard(const SessionGuard&) = delete;
        SessionGuard& operator=(const SessionGuard&) = delete;
        SessionGuard& operator=(SessionGuard&&) = delete;

        ObSession& operator*() { return session_; }
        ObSession* operator->() { return &session_; }

    private:
        ObConnectionPool* pool_;
        ObSession session_;
    };

    SessionGuard AcquireGuard();
    void CancelAll();

    IExecutor* GetExecutor() { return ioPool_.get(); }
    size_t GetPoolSize() const { return poolSize_; }

private:
    ObConnectionConfig config_;
    size_t poolSize_ = 0;
    std::unique_ptr<TThreadPool> ioPool_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<std::unique_ptr<ObConnection>> free_;
    std::vector<ObConnection*> checkedOut_;
    std::shared_ptr<std::atomic<bool>> shutdownFlag_ =
        std::make_shared<std::atomic<bool>>(false);
    bool shutdown_ = false;
};

} // namespace NTPCC
