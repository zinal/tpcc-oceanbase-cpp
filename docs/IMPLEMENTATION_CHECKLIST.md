# Implementation checklist

Детали — в `PORTING_PLAN.md`.  
Скоуп: **только OceanBase** + **только OceanBase Connector/C** (без PostgreSQL backend, без libmysqlclient).

## Phase 1 — Adapter (Connector/C)

- [ ] Install LibOBClient (`libobclient` / `libobclnt`) in build env
- [x] `cmake/FindOBClient.cmake` — OB only, no mysqlclient fallback
- [ ] `src/db/connection.h/.cpp` (opaque `MYSQL*` wrapper)
- [ ] `src/db/params.h` (+ bind helpers for prepared statements)
- [ ] `src/db/query_result.h` (materialized rows from `MYSQL_RES`)
- [ ] `src/db/errors.h` (retryable classification)
- [ ] `src/db/session.h/.cpp` (`ObSession`)
- [ ] `src/db/connection_pool.h/.cpp`
- [ ] Wire `CMakeLists.txt` to Connector/C; drop pqxx from build
- [ ] Replace call sites: `PgSession` → `ObSession`, `pqxx::params` → `Params`
- [ ] Delete `src/pg_session.*`, `src/pg_connection_pool.*`, pqxx `src/query_result.h`
- [ ] Connection flags / DSN: host/port/user/password/database
- [ ] CLI/help: OceanBase wording only
- [ ] `run --simulate-select1` works against OceanBase (link: libobclient)

## Phase 2 — DDL

- [ ] `init.cpp` OB MySQL-tenant DDL
- [ ] `clean.cpp` drop order / database
- [ ] `path_checker.cpp` without `pg_indexes`
- [ ] `--path` = database name + `USE`

## Phase 3 — Import

- [ ] `ExecuteBulk` batched INSERT (or LOAD DATA via Connector/C)
- [ ] Remove `synchronous_commit`
- [ ] `ANALYZE TABLE`
- [ ] `import` + `check --after-import` for w=10

## Phase 4 — Transactions

- [ ] All `$n` → `?`
- [ ] Payment without `RETURNING`
- [ ] Simulation cast for Connector/C
- [ ] Retry via `DbError` (not pqxx)
- [ ] `tpcc_ob_tests` green

## Phase 5 — Checks

- [ ] Replace `BOOL_AND` / `BOOL_OR`
- [ ] Replace `FULL JOIN`
- [ ] Post-run `check` green

## Phase 6 — CI / docs

- [ ] `docker-compose.yml` smoke verified
- [ ] `tests/*.sh` use **`obclient`** only
- [ ] GitHub Actions: install LibOBClient + OB smoke (or documented manual)
- [ ] README: LibOBClient install + quick start
- [ ] No pqxx / libpq / libmysqlclient / PostgreSQL runtime references
