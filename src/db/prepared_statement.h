#pragma once

#include "db/params.h"
#include "db/queries.h"
#include "db/query_result.h"

#include <memory>

namespace NTPCC {

// Per-connection prepared statement cache (one handle per QueryId).
class ObStatementCache {
public:
    // Opaque Connector/C connection handle (MYSQL*).
    explicit ObStatementCache(void* mysql);
    ~ObStatementCache();

    ObStatementCache(const ObStatementCache&) = delete;
    ObStatementCache& operator=(const ObStatementCache&) = delete;

    QueryResult Query(QueryId id, const Params& params);
    uint64_t Execute(QueryId id, const Params& params);

    void Clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace NTPCC
