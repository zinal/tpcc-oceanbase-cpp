# `src/db` — OceanBase Connector/C adapter

Database access layer for the **OceanBase-only** build.

| File | Role |
|------|------|
| `params.h` | Bound parameters for `?` placeholders |
| `query_result.h` | Materialized rows (no `pqxx::result`) |
| `errors.h` | Deadlock / lock-wait classification |
| `session.h` | `ObSession` — query/modify/tx/bulk |
| `connection_pool.h` | Pool + DSN parse |

**Client:** OceanBase Connector/C (`libobclient` / `libobclnt`) only.  
Do not link `libmysqlclient`.

**Phase 1:** implement `connection.cpp`, `session.cpp`, `connection_pool.cpp`;  
then delete temporary PostgreSQL baseline (`src/pg_*.cpp`, pqxx `query_result.h`).

See `docs/PORTING_PLAN.md`.
