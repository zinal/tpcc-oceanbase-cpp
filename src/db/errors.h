#pragma once

#include <exception>
#include <string>
#include <string_view>

namespace NTPCC {

// MySQL / OceanBase error classification for transaction retry.
enum class DbErrorKind {
    Other,
    Deadlock,          // MySQL 1213 (ER_LOCK_DEADLOCK)
    LockWaitTimeout,   // MySQL 1205 (ER_LOCK_WAIT_TIMEOUT)
    SerializationFailure,
    ConnectionLost,
    Shutdown,
};

inline DbErrorKind ClassifyDbError(int nativeCode, std::string_view /*message*/ = {}) {
    switch (nativeCode) {
        case 1213:
            return DbErrorKind::Deadlock;
        case 1205:
            return DbErrorKind::LockWaitTimeout;
        case 1317: // ER_QUERY_INTERRUPTED (KILL QUERY during shutdown)
            return DbErrorKind::Shutdown;
        case 2006: // CR_SERVER_GONE_ERROR
        case 2013: // CR_SERVER_LOST
            return DbErrorKind::ConnectionLost;
        default:
            return DbErrorKind::Other;
    }
}

inline bool IsRetryableTxError(DbErrorKind kind) {
    return kind == DbErrorKind::Deadlock
        || kind == DbErrorKind::LockWaitTimeout
        || kind == DbErrorKind::SerializationFailure;
}

class DbError : public std::exception {
public:
    DbError(int code, std::string message)
        : code_(code)
        , kind_(ClassifyDbError(code, message))
        , message_(std::move(message))
    {}

    const char* what() const noexcept override { return message_.c_str(); }
    int Code() const { return code_; }
    DbErrorKind Kind() const { return kind_; }
    bool Retryable() const { return IsRetryableTxError(kind_); }

private:
    int code_;
    DbErrorKind kind_;
    std::string message_;
};

} // namespace NTPCC
