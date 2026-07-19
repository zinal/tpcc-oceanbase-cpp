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
| Baseline исходников upstream (включая временно `pg_*`) | Импортирован как материал для порта |
| План и инвентаризация SQL (`docs/`) | Этот документ + `SQL_DIALECT_GAPS.md` |
| Каркас `src/db/` | Headers API |
| docker-compose / CI / README под OceanBase | Подготовлены |
| Реализация на Connector/C + удаление `pg_*` | **Phase 1 done** |
| DDL / import / check | Phase 2–5 |

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
2. [x] `ObConnection` + `Params` + `QueryResult` (литеральная подстановка `?` через escape).
3. [x] `ObSession` + `ObConnectionPool` (`USE` для `--path`, `KILL QUERY` в `CancelAll`).
4. [x] runner/terminal/transactions → `ObSession` / `MakeParams`.
5. [x] Удалены `pg_session.*`, `pg_connection_pool.*`; `query_result.h` → `db/query_result.h`.
6. [x] `tpcc_ob_tests`; CLI/help под OceanBase.
7. [x] `init`/`import`/`clean`/`check` — заглушки до фаз 2–5.

**Критерий:** `./build/tpcc run --simulate-select1=5` (линк `libobclnt`) — выполнен.

### Фаза 2 — DDL / clean / path

| PostgreSQL | OceanBase (MySQL tenant) |
|------------|--------------------------|
| `CREATE SCHEMA` + `SET search_path` | `CREATE DATABASE` + `USE` |
| `DROP TABLE ... CASCADE` | `DROP TABLE IF EXISTS` (порядок по FK) |
| `CREATE INDEX IF NOT EXISTS` | create-or-ignore / catalog check |
| `pg_indexes` | `information_schema.statistics` / `SHOW INDEX` |

**Критерий:** `tpcc init -w 10` + `tpcc clean` на OB.

### Фаза 3 — Bulk import

Вместо COPY / `pqxx::stream_to`:

1. Батчевый multi-row `INSERT` (default).
2. `LOAD DATA LOCAL INFILE` — если поддержан Connector/C + сервером.
3. OB-specific bulk — только если понадобится скорость.

Убрать `SET synchronous_commit`; `ANALYZE` → `ANALYZE TABLE`.

**Критерий:** `import -w 10` + `check --after-import`.

### Фаза 4 — SQL транзакций TPC-C

1. `$1..$n` → `?`.
2. `UPDATE ... RETURNING` → `SELECT ... FOR UPDATE` + `UPDATE`.
3. Simulation: `SELECT CAST(? AS SIGNED)` / `SELECT ?`.
4. Retry в `terminal.cpp` по `DbError` (deadlock / lock wait), не `pqxx::transaction_rollback`.

**Критерий:** `tpcc_ob_tests` зелёные; короткий `run --no-delays`.

### Фаза 5 — Consistency checks

| PostgreSQL | Замена |
|------------|--------|
| `BOOL_AND` / `BOOL_OR` | `MIN` / `MAX` / `SUM` |
| `FULL JOIN` | emulate LEFT/RIGHT UNION |

**Критерий:** `check` после import и после run.

### Фаза 6 — Интеграция, CI, polish

1. `docker compose` → `oceanbase/oceanbase-ce`.
2. Интеграционные скрипты только через **`obclient`** (не `psql`, не обязательный `mysql` CLI).
3. CI: unit-tests; smoke на OB (учитывать долгий boot).
4. README: установка LibOBClient, DSN, tenant `user@tenant`.
5. Финальный grep: нет pqxx / libpq / libmysqlclient / «PostgreSQL» в runtime-коде.
6. Тюнинг batch import и pool sizes.

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
