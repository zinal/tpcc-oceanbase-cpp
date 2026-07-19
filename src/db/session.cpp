#include "db/session.h"
#include "db/connection.h"

#include <stdexcept>

namespace NTPCC {

ObSession::ObSession(std::unique_ptr<ObConnection> conn, IExecutor* executor,
                     std::shared_ptr<std::atomic<bool>> shutdownFlag)
    : conn_(std::move(conn))
    , executor_(executor)
    , shutdownFlag_(std::move(shutdownFlag))
{}

ObSession::ObSession(ObSession&& other) noexcept
    : conn_(std::move(other.conn_))
    , inTxn_(other.inTxn_)
    , executor_(other.executor_)
    , shutdownFlag_(std::move(other.shutdownFlag_))
{
    other.inTxn_ = false;
    other.executor_ = nullptr;
}

ObSession& ObSession::operator=(ObSession&& other) noexcept {
    if (this != &other) {
        conn_ = std::move(other.conn_);
        inTxn_ = other.inTxn_;
        executor_ = other.executor_;
        shutdownFlag_ = std::move(other.shutdownFlag_);
        other.inTxn_ = false;
        other.executor_ = nullptr;
    }
    return *this;
}

ObSession::~ObSession() {
    if (conn_ && inTxn_) {
        try {
            conn_->Rollback();
        } catch (...) {
        }
        inTxn_ = false;
    }
}

void ObSession::CheckShutdown() const {
    if (shutdownFlag_ && shutdownFlag_->load(std::memory_order_relaxed)) {
        throw std::runtime_error("session shutdown");
    }
}

bool ObSession::HasConnection() const {
    return conn_ != nullptr && conn_->Ok();
}

std::unique_ptr<ObConnection> ObSession::ReleaseConnection() {
    if (conn_ && inTxn_) {
        try {
            conn_->Rollback();
        } catch (...) {
        }
        inTxn_ = false;
    }
    return std::move(conn_);
}

namespace {

void EnsureTxn(ObConnection& conn, bool& inTxn) {
    if (!inTxn) {
        conn.BeginRepeatableRead();
        inTxn = true;
    }
}

} // namespace

TFuture<QueryResult> ObSession::ExecuteQuery(std::string_view sql, const Params& params) {
    TPromise<QueryResult> promise;
    auto future = promise.GetFuture();
    std::string sqlCopy(sql);

    executor_->Submit([this, sqlCopy = std::move(sqlCopy), params,
                       p = std::move(promise)]() mutable {
        try {
            CheckShutdown();
            EnsureTxn(*conn_, inTxn_);
            p.SetValue(conn_->Query(sqlCopy, params));
        } catch (...) {
            p.SetException(std::current_exception());
        }
    });

    return future;
}

TFuture<uint64_t> ObSession::ExecuteModify(std::string_view sql, const Params& params) {
    TPromise<uint64_t> promise;
    auto future = promise.GetFuture();
    std::string sqlCopy(sql);

    executor_->Submit([this, sqlCopy = std::move(sqlCopy), params,
                       p = std::move(promise)]() mutable {
        try {
            CheckShutdown();
            EnsureTxn(*conn_, inTxn_);
            p.SetValue(conn_->Execute(sqlCopy, params));
        } catch (...) {
            p.SetException(std::current_exception());
        }
    });

    return future;
}

TFuture<void> ObSession::Commit() {
    TPromise<void> promise;
    auto future = promise.GetFuture();

    executor_->Submit([this, p = std::move(promise)]() mutable {
        try {
            CheckShutdown();
            if (inTxn_) {
                conn_->Commit();
                inTxn_ = false;
            }
            p.SetValue();
        } catch (...) {
            inTxn_ = false;
            p.SetException(std::current_exception());
        }
    });

    return future;
}

TFuture<void> ObSession::Rollback() {
    TPromise<void> promise;
    auto future = promise.GetFuture();

    executor_->Submit([this, p = std::move(promise)]() mutable {
        try {
            if (inTxn_) {
                conn_->Rollback();
                inTxn_ = false;
            }
            p.SetValue();
        } catch (...) {
            inTxn_ = false;
            p.SetException(std::current_exception());
        }
    });

    return future;
}

TFuture<QueryResult> ObSession::ExecuteNonTx(std::string_view sql) {
    TPromise<QueryResult> promise;
    auto future = promise.GetFuture();
    std::string sqlCopy(sql);

    executor_->Submit([this, sqlCopy = std::move(sqlCopy),
                       p = std::move(promise)]() mutable {
        try {
            if (inTxn_) {
                conn_->Rollback();
                inTxn_ = false;
            }
            p.SetValue(conn_->QuerySimple(sqlCopy));
        } catch (...) {
            p.SetException(std::current_exception());
        }
    });

    return future;
}

TFuture<void> ObSession::ExecuteBulk(
    const std::string& tableName,
    const std::vector<std::string>& columns,
    BulkWriter writer)
{
    TPromise<void> promise;
    auto future = promise.GetFuture();

    executor_->Submit([this, tableName, columns, writer = std::move(writer),
                       p = std::move(promise)]() mutable {
        try {
            CheckShutdown();
            EnsureTxn(*conn_, inTxn_);

            constexpr size_t kBatchRows = 200;
            std::vector<BulkRow> batch;
            batch.reserve(kBatchRows);

            auto flush = [&]() {
                if (batch.empty()) {
                    return;
                }
                std::string sql = "INSERT INTO `" + tableName + "` (";
                for (size_t i = 0; i < columns.size(); ++i) {
                    if (i) sql += ',';
                    sql += '`' + columns[i] + '`';
                }
                sql += ") VALUES ";

                Params params;
                for (size_t r = 0; r < batch.size(); ++r) {
                    if (r) sql += ',';
                    sql += '(';
                    const auto& row = batch[r];
                    if (row.size() != columns.size()) {
                        throw std::runtime_error("Bulk row column count mismatch");
                    }
                    for (size_t c = 0; c < row.size(); ++c) {
                        if (c) sql += ',';
                        sql += '?';
                        if (row[c]) {
                            params(*row[c]);
                        } else {
                            params(nullptr);
                        }
                    }
                    sql += ')';
                }
                conn_->Execute(sql, params);
                batch.clear();
            };

            writer([&](BulkRow row) {
                batch.push_back(std::move(row));
                if (batch.size() >= kBatchRows) {
                    flush();
                }
            });
            flush();
            p.SetValue();
        } catch (...) {
            p.SetException(std::current_exception());
        }
    });

    return future;
}

} // namespace NTPCC
