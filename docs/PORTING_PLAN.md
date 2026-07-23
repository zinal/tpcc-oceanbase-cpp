# План портирования tpcc-postgres-cpp → OceanBase

Исходник: [ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp)

## Решения по скоупу (зафиксировано)

1. **Только OceanBase в итоге.** Dual-stack / запуск тех же тестов на PostgreSQL **не поддерживаем**.  
   Файлы `pg_session.*`, `pg_connection_pool.*`, pqxx-`query_result.h` и любой PG-only код **удаляются** в ходе порта, а не остаются как альтернативный backend.
2. **Клиент — штатный OceanBase Connector/C (LibOBClient / `libobclient`).**  
   Не использовать `libmysqlclient` / MariaDB Connector/C «как совместимый MySQL-драйвер». API у Connector/C совместим по форме с MySQL C API (`mysql.h`, `MYSQL*`), но линковка и поставка — только OB (`-lobclient` / `-lobclnt`, пакеты `libobclient`).

Серверный tenant для TPC-C: **MySQL-compatible tenant** OceanBase (порт 2881) — это режим SQL на сервере, не выбор клиентской библиотеки.

| | Было (upstream) | Цель |
|--|-----------------|------|
| СУБД | PostgreSQL | OceanBase CE / Enterprise (MySQL tenant) |
| Клиент | libpq + libpqxx | **OceanBase Connector/C** only |
| SQL | PostgreSQL dialect | MySQL-compatible SQL на OB |
| Тесты | PG docker / `psql` | OB docker / `obclient` |

## Цель

Сохранить архитектуру бенчмарка (корутины, TUI, CLI `init/import/check/run/clean`, TPC-C mix/delays/warmup) и **полностью** заменить PostgreSQL/libpqxx слой на адаптер над OceanBase Connector/C.

Под *портированием* понимаем:

1. Заменить PG-типы/API на OB Connector/C.
2. Перевести SQL с PostgreSQL-диалекта на диалект MySQL-tenant OceanBase.
3. Заменить `libpqxx` на тонкий адаптер сессии/пула над Connector/C.
4. Оставить ядро benchmark/TUI/корутин без изменения семантики TPC-C.
5. Удалить PostgreSQL baseline из дерева после переключения call site’ов.

## Что уже есть в репозитории

| Компонент | Статус |
|-----------|--------|
| Baseline исходников upstream | Импортирован; PG-слой удалён |
| План и инвентаризация SQL (`docs/`) | Этот документ + `SQL_DIALECT_GAPS.md` + checklist |
| Адаптер `src/db/` (Connector/C) | **Phase 1 done** — `ObSession` / pool / `libobclnt` |
| DDL / clean / `--path` | **Phase 2 done** |
| Bulk import (`ExecuteBulk`) | **Phase 3 done** |
| TPC-C транзакции + retry | **Phase 4 done** |
| Consistency checks | **Phase 5 done** |
| docker-compose / CI / README / smoke | **Phase 6 done** |

## Архитектура (целевая)

```
main.cpp
  ├─ init / import / clean / check / run
  ├─ Terminal (корутина) → transactions_*
  ├─ Runner + RunnerTUI / ImportTUI (ftxui)
  ├─ TaskQueue + TimerQueue + ThreadPool(IO)
  └─ ObSession / ObConnectionPool / QueryResult  ← OceanBase Connector/C
```

**libpq async не используется** в upstream: DB-вызовы синхронны на IO thread pool, корутина ждёт `TFuture`. Синхронный Connector/C (`mysql_real_query` / prepared statements) вписывается в ту же модель.

## Стратегия адаптера

```
src/db/
  params.h              — биндинги для ? (не $1)
  query_result.h        — материализованный результат
  session.h/.cpp        — ObSession над MYSQL* из libobclient
  connection_pool.h/.cpp
  errors.h              — deadlock / lock wait / disconnect (коды OB/MySQL-tenant)
  connection.h/.cpp     — opaque ObConnection, connect/close/kill
```

CMake: `FindOBClient.cmake` ищет **только** `libobclient` / `libobclnt` и заголовки Connector/C. Fallback на `libmysqlclient` **запрещён**.

### Целевой API сессии

```cpp
TFuture<QueryResult> ExecuteQuery(std::string_view sql, const Params& params = {});
TFuture<uint64_t>    ExecuteModify(std::string_view sql, const Params& params = {});
TFuture<void>        Commit();
TFuture<void>        Rollback();
TFuture<QueryResult> ExecuteNonTx(std::string_view sql);
TFuture<void>        ExecuteBulk(const std::string& table,
                                 const std::vector<std::string>& columns,
                                 BulkWriter writer);
```

Транзакции: `START TRANSACTION ISOLATION LEVEL REPEATABLE READ` при первом query/modify.

## Фазы реализации

### Фаза 0 — подготовка репозитория

- [x] Импорт baseline upstream
- [x] Документация плана и SQL gaps
- [x] Каркас `src/db/`
- [x] CMake / docker-compose / CI / README под OceanBase
- [x] Submodules fmt/spdlog/gflags/ftxui/googletest
- [x] Уточнение скоупа: OB-only, Connector/C only (этот апдейт)

### Фаза 1 — DB-адаптер на OceanBase Connector/C ✅

1. [x] LibOBClient + `find_package(OBClient REQUIRED)` (без mysqlclient fallback).
2. [x] `ObConnection` + `Params` + `QueryResult` (prepared statements / `mysql_stmt_bind_param`).
3. [x] `ObSession` + `ObConnectionPool` (`USE` для `--path`, `KILL QUERY` в `CancelAll`).
4. [x] runner/terminal/transactions → `ObSession` / `MakeParams`.
5. [x] Удалены `pg_session.*`, `pg_connection_pool.*`; `query_result.h` → `db/query_result.h`.
6. [x] `tpcc_ob_tests`; CLI/help под OceanBase.
7. [x] `init`/`import`/`clean`/`check` — заглушки до фаз 2–5.

**Критерий:** `./build/tpcc run --simulate-select1=5` (линк `libobclnt`) — выполнен.

### Фаза 2 — DDL / clean / path ✅

| PostgreSQL | OceanBase (MySQL tenant) |
|------------|--------------------------|
| `CREATE SCHEMA` + `SET search_path` | `CREATE DATABASE` + `USE` |
| `DROP TABLE ... CASCADE` | `DROP TABLE IF EXISTS` (порядок по FK) |
| `CREATE INDEX IF NOT EXISTS` | catalog check / ignore 1061 |
| `pg_indexes` | `information_schema.statistics` |

**Критерий:** `tpcc init` + `tpcc clean` (+ `--path`) — выполнен.

### Фаза 3 — Bulk import ✅

Вместо COPY / `pqxx::stream_to`:

1. [x] Батчевый multi-row `INSERT` через `ObSession::ExecuteBulk` (default).
2. `LOAD DATA LOCAL INFILE` — если понадобится скорость.
3. OB-specific bulk — только если понадобится скорость.

Убраны `SET synchronous_commit` / `SET search_path`; `ANALYZE` → `ANALYZE TABLE`.

**Критерий:** `import` (CLI/UT w=1 smoke) — выполнен.  
`check --after-import` / полный прогон w=10 — Phase 5.

### Фаза 4 — SQL транзакций TPC-C ✅

1. [x] `$1..$n` → `?`.
2. [x] `UPDATE ... RETURNING` → `SELECT ... FOR UPDATE` + `UPDATE`.
3. [x] Simulation: `SELECT CAST(? AS SIGNED)` / `SELECT ?`.
4. [x] Retry в `terminal.cpp` по `DbError` (deadlock / lock wait), не `pqxx::transaction_rollback`.
5. [x] `ob_transaction_ut` на загруженной схеме (w=1); CI smoke `run --no-delays --duration-seconds`.

**Критерий:** `tpcc_ob_tests` зелёные; короткий `run --no-delays` — выполнен.

### Фаза 5 — Consistency checks ✅

| PostgreSQL | Замена |
|------------|--------|
| `BOOL_AND` / `BOOL_OR` | `MIN` / `MAX` over 0/1 predicates |
| `FULL JOIN` | `LEFT JOIN` + `UNION ALL` anti-join |

**Критерий:** `check --after-import` после import и `check` после run — выполнен.

### Фаза 6 — Интеграция, CI, polish ✅

1. [x] `docker compose` → `oceanbase/oceanbase-ce` (+ `scripts/wait_for_oceanbase.sh`).
2. [x] `tests/*.sh` через `tpcc` + `--path`; ad-hoc SQL — `obclient` (fallback `mysql` только для MariaDB stand-in).
3. [x] CI: обязательный MariaDB stand-in smoke; optional OceanBase CE job (`continue-on-error`).
4. [x] README: LibOBClient, DSN, tenant `user@tenant`, smoke scripts.
5. [x] Runtime: нет pqxx / libpq / libmysqlclient (линк только `libobclnt`).
6. [x] Документированы defaults batch import (200) и pool/inflight.

## Риски

| Риск | Уровень | Митигация |
|------|---------|-----------|
| Доставка LibOBClient в CI/dev (не всегда в apt) | Высокий | Документировать RPM/YUM/build from [oceanbase/obclient](https://github.com/oceanbase/obclient) / obconnector-c; кэш артефакта |
| RR / locking ≠ PostgreSQL | Высокий | Тесты транзакций; карта error codes |
| Import без COPY | Высокий | Батчи; опционально LOAD DATA |
| `RETURNING` | Средний | SELECT + UPDATE |
| Cancel/shutdown | Средний | KILL QUERY через admin conn |
| `--path` | Средний | database name + `USE` |

## Параметры подключения

```
--host=127.0.0.1 --port=2881 --user=root@test --password=... --database=tpcc
```

или

```
--connection="host=127.0.0.1;port=2881;user=root@test;password=...;database=tpcc"
```

Docker-compose: tenant `test`, user `root@test`, SQL port `2881`.

## Зависимости сборки

| | Требование |
|--|------------|
| Компилятор | Clang 16+, C++23 |
| Клиент БД | **OceanBase Connector/C (`libobclient`)** — обязательно |
| Не использовать | libpq, libpqxx, **libmysqlclient** |
| Submodules | fmt, spdlog, gflags, ftxui, googletest |
| Опционально | tcmalloc |
| Runtime для тестов | `oceanbase/oceanbase-ce` + CLI `obclient` |

## Порядок работ (Phase 1)

1. Установить LibOBClient в окружении агента/CI.
2. Ужесточить `FindOBClient.cmake` (только obclient/obclnt).
3. Реализовать `src/db/*.cpp`, переключить call site’ы, удалить `pg_*`.
4. Собрать `tpcc`, прогнать `--simulate-select1=5` на OceanBase.
5. Далее фазы 2→5 по `IMPLEMENTATION_CHECKLIST.md`.

## Вне скоупа

- Поддержка PostgreSQL / dual backend.
- Линковка с `libmysqlclient` «для совместимости».
- Oracle mode tenant (отдельный диалект SQL).
- Нативный async nonblocking на сокетах (IO pool достаточен).
- Паритет скорости import с YDB bulk.
- Изменение TPC-C mix / бизнес-логики.
