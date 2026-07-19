# Upstream baseline

Imported from [ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

| Field | Value |
|-------|-------|
| Commit | `c32833bb9f9e28b0c98bf4e969d0ffffeb6e8f60` |
| Date | 2026-04-19 |
| Message | Cleanup README |

PostgreSQL-specific runtime files (`pg_session.*`, `pg_connection_pool.*`, pqxx
result wiring) were removed in Phase 1. This repository targets **OceanBase only**
with **OceanBase Connector/C** (`libobclnt` / `src/db/`).

See `docs/PORTING_PLAN.md` and `docs/LIBOBCLIENT.md`.
