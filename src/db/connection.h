#pragma once

#include "db/params.h"
#include "db/queries.h"
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
// Also accepts space-separated key=value.
ObConnectionConfig ParseConnectionString(const std::string& connection);

// Effective TPC-C database: --path if set, otherwise connection database.
std::string EffectiveDatabase(const ObConnectionConfig& config);

// Validate / backtick-quote a MySQL identifier (database/table/index).
std::string QuoteIdent(const std::string& ident);

struct ObConnection {
    ObConnection() = default;
    ~ObConnection();

    ObConnection(const ObConnection&) = delete;
    ObConnection& operator=(const ObConnection&) = delete;

    // If selectDatabase is false, skips initial USE / default schema selection
    // (needed to CREATE DATABASE before first USE).
    static std::unique_ptr<ObConnection> Connect(const ObConnectionConfig& config,
                                                 bool selectDatabase = true);

    void UseDatabase(const std::string& database);
    void CreateDatabaseIfNotExists(const std::string& database);
    void BeginRepeatableRead();
    void Commit();
    void Rollback();

    // Prepared statement path for TPC-C transactional queries.
    QueryResult Query(QueryId queryId, const Params& params = {});
    uint64_t Execute(QueryId queryId, const Params& params = {});

    // Dynamic SQL: with params uses a one-shot prepared statement bind;
    // without params uses mysql_real_query (DDL / multi-statement).
    QueryResult Query(const std::string& sql, const Params& params = {});
    uint64_t Execute(const std::string& sql, const Params& params = {});

    // Reconnect and rebuild prepared statement cache.
    void Reconnect(const ObConnectionConfig& config, bool selectDatabase = true);

    QueryResult QuerySimple(const std::string& sql);
    uint64_t ExecuteSimple(const std::string& sql);

    // Best-effort cancel via a short-lived admin connection + KILL QUERY.
    void KillQuery(const ObConnectionConfig& adminConfig);

    unsigned long ThreadId() const;
    bool Ok() const;

private:
    void EstablishConnection(const ObConnectionConfig& config, bool selectDatabase);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace NTPCC
