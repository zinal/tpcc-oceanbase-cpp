#include "db/connection_pool.h"
#include "log.h"

#include <stdexcept>

namespace NTPCC {

ObConnectionPool::ObConnectionPool(const std::string& connectionString,
                                   size_t poolSize,
                                   size_t ioThreads,
                                   const std::string& path)
    : ObConnectionPool(
          [&] {
              auto cfg = ParseConnectionString(connectionString);
              if (!path.empty()) {
                  cfg.path = path;
              }
              return cfg;
          }(),
          poolSize,
          ioThreads)
{}

ObConnectionPool::ObConnectionPool(ObConnectionConfig config, size_t poolSize, size_t ioThreads)
    : config_(std::move(config))
    , poolSize_(poolSize)
    , ioPool_(std::make_unique<TThreadPool>(ioThreads))
{
    LOG_I("Creating OceanBase connection pool: {} connections, {} IO threads ({}:{}/{})",
          poolSize_, ioThreads, config_.host, config_.port,
          config_.path.empty() ? config_.database : config_.path);

    for (size_t i = 0; i < poolSize_; ++i) {
        free_.push(ObConnection::Connect(config_));
    }

    LOG_I("Connection pool ready");
}

ObConnectionPool::~ObConnectionPool() {
    {
        std::lock_guard lock(mu_);
        shutdown_ = true;
    }
    cv_.notify_all();

    ioPool_->Join();

    std::lock_guard lock(mu_);
    while (!free_.empty()) {
        free_.pop();
    }
}

ObSession ObConnectionPool::AcquireSession() {
    std::unique_lock lock(mu_);
    cv_.wait(lock, [this] { return !free_.empty() || shutdown_; });

    if (shutdown_ && free_.empty()) {
        throw std::runtime_error("Connection pool is shutting down");
    }

    auto conn = std::move(free_.front());
    free_.pop();
    checkedOut_.push_back(conn.get());
    return ObSession(std::move(conn), ioPool_.get(), shutdownFlag_);
}

void ObConnectionPool::ReleaseSession(ObSession session) {
    auto conn = session.ReleaseConnection();
    if (!conn) {
        return;
    }

    {
        std::lock_guard lock(mu_);
        std::erase(checkedOut_, conn.get());
        free_.push(std::move(conn));
    }
    cv_.notify_one();
}

void ObConnectionPool::CancelAll() {
    shutdownFlag_->store(true, std::memory_order_release);

    std::vector<ObConnection*> victims;
    {
        std::lock_guard lock(mu_);
        victims = checkedOut_;
    }
    for (auto* conn : victims) {
        try {
            conn->KillQuery(config_);
        } catch (...) {
        }
    }
}

ObConnectionPool::SessionGuard ObConnectionPool::AcquireGuard() {
    return SessionGuard(*this, AcquireSession());
}

} // namespace NTPCC
