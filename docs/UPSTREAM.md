# Upstream baseline

Imported from [ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

| Field | Value |
|-------|-------|
| Commit | `c32833bb9f9e28b0c98bf4e969d0ffffeb6e8f60` |
| Date | 2026-04-19 |
| Message | Cleanup README |

PostgreSQL-specific files kept as reference during the port:

- `src/pg_session.*`, `src/pg_connection_pool.*`, `src/query_result.h`
- SQL in `init.cpp`, `import.cpp`, `check.cpp`, `transaction_*.cpp`, `common_queries.cpp`

Target replacement lives under `src/db/` (see `PORTING_PLAN.md`).
