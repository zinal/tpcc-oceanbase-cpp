# SQL dialect gaps: PostgreSQL → OceanBase (MySQL tenant)

Инвентаризация по baseline `src/` из [tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

Итоговый продукт — **только OceanBase**. Клиентский драйвер — **OceanBase Connector/C**, не libmysqlclient. Колонка «OB» ниже — SQL MySQL-compatible tenant на сервере.

## Placeholders

| Location | PG | OB |
|----------|----|----|
| `common_queries.cpp`, all `transaction_*.cpp` | `$1`, `$2`, … | `?` (prepared / binary protocol) |

## DDL / schema (`init.cpp`, `clean.cpp`, `path_checker.cpp`, pool)

| Feature | Example (PG) | Action |
|---------|--------------|--------|
| Schema | `CREATE SCHEMA IF NOT EXISTS x` | `CREATE DATABASE IF NOT EXISTS x` |
| Search path | `SET search_path TO x` | `USE x` on each connection |
| Drop schema | `DROP SCHEMA IF EXISTS x CASCADE` | `DROP DATABASE IF EXISTS x` (осторожно) или drop tables |
| Drop table | `DROP TABLE IF EXISTS t CASCADE` | `DROP TABLE IF EXISTS t` + порядок по FK |
| Index if not exists | `CREATE INDEX IF NOT EXISTS ...` | create-or-ignore / check catalog |
| Catalog indexes | `pg_indexes` | `information_schema.statistics` |

## Types (`init.cpp`)

Используются `int`, `decimal(p,s)`, `varchar`, `char`, `float`, `timestamp` — в целом переносимы.
Проверить: padding `CHAR`, precision `DECIMAL`, `CURRENT_TIMESTAMP` defaults, NULL timestamps в `ol_delivery_d`.

Нет `SERIAL` / sequences — OK.

## Bulk load (`import.cpp`)

| PG | OB (Phase 3) |
|----|--------------|
| `pqxx::stream_to` (COPY) | `ObSession::ExecuteBulk` — batched multi-row `INSERT` |
| `SET synchronous_commit = off` | removed (no PG session knob) |
| `SET search_path` | `USE` / `--path` database via `EffectiveDatabase` |
| `ANALYZE warehouse` | `ANALYZE TABLE \`warehouse\`` |

## DML features

| Feature | File | OB replacement |
|---------|------|----------------|
| `UPDATE ... RETURNING cols` | `transaction_payment.cpp` | `SELECT ... FOR UPDATE` + `UPDATE` |
| `SELECT ... FOR UPDATE` | `transaction_neworder.cpp` | keep; validate locking |
| `SELECT $1::int` | `transaction_simulation.cpp` | `SELECT CAST(? AS SIGNED)` |
| `ON CONFLICT` | — | not used |
| `SKIP LOCKED` | — | not used |

## Isolation / transactions

| PG | OB (Phase 4) |
|----|--------------|
| `pqxx::transaction<repeatable_read>` | `SET TRANSACTION ISOLATION LEVEL REPEATABLE READ` + `START TRANSACTION` |
| retry on `pqxx::transaction_rollback` | `DbError` / `Retryable()` for `1213`, `1205` |
| `pg_transaction_ut` | `ob_transaction_ut` against imported w=1 schema |

## Checks (`check.cpp`) — Phase 5

| PG | OB |
|----|----|
| `BOOL_AND(expr)` | `MIN(CASE WHEN expr THEN 1 ELSE 0 END)` |
| `BOOL_OR(expr)` | `MAX(CASE WHEN expr THEN 1 ELSE 0 END)` |
| `FULL JOIN` | `LEFT JOIN` + `UNION ALL` right-only rows |
| `SET search_path` | `--path` / `EffectiveDatabase` + `USE` |

Portable enough as-is: `LIMIT`, `COALESCE`, `ABS`, `COUNT(DISTINCT)`, `UNION ALL`, `LEFT JOIN`, `CURRENT_TIMESTAMP`.

## Connection / tooling (tests)

| PG | OB |
|----|----|
| `host=... dbname=... user=postgres` | `host=...;port=2881;user=root@test;database=tpcc` |
| `createdb` / `dropdb` / `psql` | `obclient` only |
| port `5432` | port `2881` |
