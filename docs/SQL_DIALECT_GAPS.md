# SQL dialect gaps: PostgreSQL → OceanBase (MySQL mode)

Инвентаризация по baseline `src/` из [tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

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

| PG | OB options |
|----|------------|
| `pqxx::stream_to` (COPY) | batched multi-row INSERT; `LOAD DATA LOCAL INFILE`; OB bulk API |
| `SET synchronous_commit = off` | убрать или OB-эквивалент |
| `ANALYZE warehouse` | `ANALYZE TABLE warehouse` |

## DML features

| Feature | File | OB replacement |
|---------|------|----------------|
| `UPDATE ... RETURNING cols` | `transaction_payment.cpp` | `SELECT ... FOR UPDATE` + `UPDATE` |
| `SELECT ... FOR UPDATE` | `transaction_neworder.cpp` | keep; validate locking |
| `SELECT $1::int` | `transaction_simulation.cpp` | `SELECT CAST(? AS SIGNED)` |
| `ON CONFLICT` | — | not used |
| `SKIP LOCKED` | — | not used |

## Isolation / transactions

| PG | OB |
|----|----|
| `pqxx::transaction<repeatable_read>` | `START TRANSACTION ISOLATION LEVEL REPEATABLE READ` |
| retry on `pqxx::transaction_rollback` | map MySQL/OB errors: deadlock `1213`, lock wait `1205`, etc. |

## Checks (`check.cpp`)

| PG | OB |
|----|----|
| `BOOL_AND(expr)` | `MIN(expr)` / `BIT_AND` pattern |
| `BOOL_OR(expr)` | `MAX(expr)` |
| `FULL JOIN` | emulate with LEFT/RIGHT UNION |

Portable enough as-is: `LIMIT`, `COALESCE`, `ABS`, `COUNT(DISTINCT)`, `UNION ALL`, `LEFT JOIN`, `CURRENT_TIMESTAMP`.

## Connection / tooling (tests)

| PG | OB |
|----|----|
| `host=... dbname=... user=postgres` | `host=...;port=2881;user=root@test;database=tpcc` |
| `createdb` / `dropdb` / `psql` | `mysql` / `obclient` |
| port `5432` | port `2881` |
