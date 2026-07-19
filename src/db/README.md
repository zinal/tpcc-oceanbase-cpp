# `src/db` — OceanBase adapter (target)

This directory defines the **target** database API for the OceanBase port.

| File | Role |
|------|------|
| `params.h` | Bound parameters for `?` placeholders |
| `query_result.h` | Materialized rows (no `pqxx::result`) |
| `errors.h` | Deadlock / lock-wait classification |
| `session.h` | `ObSession` — query/modify/tx/bulk |
| `connection_pool.h` | Pool + DSN parse |

**Not implemented yet:** `session.cpp`, `connection_pool.cpp`, CMake link to `libobclient`/`libmysqlclient`.

Until Phase 1 lands, the runnable baseline still lives in:

- `src/pg_session.*`
- `src/pg_connection_pool.*`
- `src/query_result.h` (pqxx-backed)

See `docs/PORTING_PLAN.md`.
