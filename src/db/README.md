# `src/db` — OceanBase Connector/C adapter

| File | Role |
|------|------|
| `connection.h/.cpp` | `ObConnection` over `MYSQL*` (`libobclnt`) |
| `params.h` | Bound parameters / `MakeParams` for `?` |
| `query_result.h` | Materialized rows |
| `errors.h` | Deadlock / lock-wait classification |
| `session.h/.cpp` | `ObSession` — async via IO thread pool |
| `connection_pool.h/.cpp` | Pool + DSN parse |

Client: **OceanBase Connector/C only** (`libobclnt`). See `docs/LIBOBCLIENT.md`.
