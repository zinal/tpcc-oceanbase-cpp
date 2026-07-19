# Implementation checklist

Отмечайте пункты по мере реализации. Детали — в `PORTING_PLAN.md`.

## Phase 1 — Adapter

- [ ] `src/db/params.h` (+ bind helpers)
- [ ] `src/db/query_result.h/.cpp` (materialized rows)
- [ ] `src/db/errors.h` (retryable classification)
- [ ] `src/db/session.h/.cpp` (`ObSession`)
- [ ] `src/db/connection_pool.h/.cpp`
- [ ] `cmake/FindOBClient.cmake` (or MySQL client)
- [ ] Wire `CMakeLists.txt` to new adapter; remove pqxx sources from link
- [ ] Replace call sites: `PgSession` → `ObSession`, `pqxx::params` → `Params`
- [ ] Connection flags: host/port/user/password/database
- [ ] `run --simulate-select1` works against OceanBase

## Phase 2 — DDL

- [ ] `init.cpp` MySQL DDL
- [ ] `clean.cpp` drop order / database
- [ ] `path_checker.cpp` without `pg_indexes`
- [ ] `--path` means database name + `USE`

## Phase 3 — Import

- [ ] `ExecuteBulk` batched INSERT (or LOAD DATA)
- [ ] Remove `synchronous_commit`
- [ ] `ANALYZE TABLE`
- [ ] `import` + `check --after-import` for w=10

## Phase 4 — Transactions

- [ ] All `$n` → `?`
- [ ] Payment without `RETURNING`
- [ ] Simulation cast
- [ ] Retry mapping for deadlock/lock wait
- [ ] `tpcc_ob_tests` green

## Phase 5 — Checks

- [ ] Replace `BOOL_AND` / `BOOL_OR`
- [ ] Replace `FULL JOIN`
- [ ] Post-run `check` green

## Phase 6 — CI / docs

- [ ] `docker-compose.yml` smoke verified
- [ ] `tests/*.sh` use mysql/obclient
- [ ] GitHub Actions OB smoke (or documented manual)
- [ ] README quick start end-to-end
- [ ] No remaining pqxx / PostgreSQL references in code
