#pragma once

#include "db/params.h"
#include "db/query_result.h"

#include <memory>
#include <string>

namespace NTPCC {

struct ObConnectionConfig {
    std::string host = "127.0.0.1";
    int port = 2881;
    std::string user = "root@test";
    std::string password;
    std::string database = "tpcc";
    // Optional: maps CLI --path to database name (USE db).
    std::string path;
};

// Parse "host=...;port=...;user=...;password=...;database=..."
// Also accepts space-separated key=value (libpq-like) for smoother migration.
ObConnectionConfig ParseConnectionString(const std::string& connection);

struct ObConnection {
    ObConnection() = default;
    ~ObConnection();

    ObConnection(const ObConnection&) = delete;
    ObConnection& operator=(const ObConnection&) = delete;

    static std::unique_ptr<ObConnection> Connect(const ObConnectionConfig& config);

    void UseDatabase(const std::string& database);
    void BeginRepeatableRead();
    void Commit();
    void Rollback();

    QueryResult Query(const std::string& sql, const Params& params = {});
    uint64_t Execute(const std::string& sql, const Params& params = {});
    QueryResult QuerySimple(const std::string& sql);
    uint64_t ExecuteSimple(const std::string& sql);

    // Best-effort cancel via a short-lived admin connection + KILL QUERY.
    void KillQuery(const ObConnectionConfig& adminConfig);

    unsigned long ThreadId() const;
    bool Ok() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace NTPCC
