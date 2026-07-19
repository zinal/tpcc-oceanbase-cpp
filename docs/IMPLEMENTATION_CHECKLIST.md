# Implementation checklist

Детали — в `PORTING_PLAN.md`.  
Скоуп: **только OceanBase** + **только OceanBase Connector/C**.

## Phase 1 — Adapter (Connector/C)

- [x] Install LibOBClient (`libobclient` / `libobclnt`) in build env
- [x] `cmake/FindOBClient.cmake` — OB only, no mysqlclient fallback
- [x] `src/db/connection.h/.cpp` (opaque `MYSQL*` wrapper)
- [x] `src/db/params.h` (+ `MakeParams`)
- [x] `src/db/query_result.h` (materialized rows)
- [x] `src/db/errors.h` (retryable classification)
- [x] `src/db/session.h/.cpp` (`ObSession`)
- [x] `src/db/connection_pool.h/.cpp`
- [x] Wire `CMakeLists.txt` to Connector/C; drop pqxx from build
- [x] Replace call sites: `PgSession` → `ObSession`, `pqxx::params` → `MakeParams`
- [x] Delete `src/pg_session.*`, `src/pg_connection_pool.*`, pqxx result impl
- [x] Connection DSN: host/port/user/password/database
- [x] CLI/help: OceanBase wording
- [x] `run --simulate-select1` works (Connector/C linked)
- [x] `tpcc_ob_tests` adapter tests
- [x] Stub `init` / `import` / `clean` / `check` until later phases

## Phase 2 — DDL

- [x] `init.cpp` OB MySQL-tenant DDL
- [x] `clean.cpp` drop order / database
- [x] `path_checker.cpp` without `pg_indexes`
- [x] `--path` = database name + `USE`
- [x] `CreateIndexes` via information_schema
- [x] `ob_ddl_ut` + CI init/clean smoke

## Phase 3 — Import

- [x] `ExecuteBulk` wired into real import (batched multi-row INSERT)
- [x] Remove PG-only session knobs (`synchronous_commit` / `search_path`)
- [x] `ANALYZE TABLE`
- [x] `import -w 1` smoke + `ob_import_ut` / ExecuteBulk UT
- [x] `check --after-import` (Phase 5; CI w=1, manual w=10)

## Phase 4 — Transactions

- [x] `$n` → `?` (done with adapter cutover)
- [x] Payment without `RETURNING` (SELECT FOR UPDATE + UPDATE)
- [x] Simulation cast for Connector/C
- [x] Retry via `DbError`
- [x] Full `tpcc_ob_tests` transaction suite against loaded schema (`ob_transaction_ut`)
- [x] Short CI `run --no-delays` after import (`--duration-seconds`)

## Phase 5 — Checks

- [x] Replace `BOOL_AND` / `BOOL_OR` (`MIN`/`MAX` 0/1)
- [x] Replace `FULL JOIN` (LEFT JOIN + UNION ALL)
- [x] `check --after-import` after import
- [x] Post-run `check` green (CI smoke)

## Phase 6 — CI / docs

- [ ] `docker-compose.yml` smoke verified on OceanBase CE
- [ ] `tests/*.sh` end-to-end green
- [ ] GitHub Actions: install LibOBClient + OB smoke
- [x] README / `docs/LIBOBCLIENT.md`
- [x] No pqxx / libpq in runtime binary
