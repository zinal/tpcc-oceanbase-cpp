#pragma once

#include "db/params.h"
#include "db/queries.h"
#include "db/query_result.h"

struct MYSQL;
struct MYSQL_STMT;

namespace NTPCC {

// Per-connection prepared statement cache (one handle per QueryId).
class ObStatementCache {
public:
    explicit ObStatementCache(MYSQL* mysql);
    ~ObStatementCache();

    ObStatementCache(const ObStatementCache&) = delete;
    ObStatementCache& operator=(const ObStatementCache&) = delete;

    QueryResult Query(QueryId id, const Params& params);
    uint64_t Execute(QueryId id, const Params& params);

    void Clear();

private:
    struct StmtEntry;

    StmtEntry& Get(QueryId id);
    void Prepare(StmtEntry& entry, QueryId id);

    MYSQL* mysql_ = nullptr;
    StmtEntry* entries_ = nullptr;
};

} // namespace NTPCC
