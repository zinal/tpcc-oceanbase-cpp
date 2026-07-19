# Upstream baseline

Imported from [ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

| Field | Value |
|-------|-------|
| Commit | `c32833bb9f9e28b0c98bf4e969d0ffffeb6e8f60` |
| Date | 2026-04-19 |
| Message | Cleanup README |

PostgreSQL-specific files are a **temporary** baseline and will be **deleted** in Phase 1
(no dual PostgreSQL backend in the final product):

- `src/pg_session.*`, `src/pg_connection_pool.*`, `src/query_result.h` (pqxx)
- SQL still to rewrite in `init.cpp`, `import.cpp`, `check.cpp`, `transaction_*.cpp`, …

Target client: **OceanBase Connector/C only** (`src/db/`, see `PORTING_PLAN.md`).
