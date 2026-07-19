# План портирования tpcc-postgres-cpp → OceanBase

Исходник: [ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp)  
Целевой режим: **OceanBase MySQL-compatible tenant** (порт 2881 / MySQL wire protocol).  
Клиент: **OceanBase Connector/C (libobclient)** или совместимый **libmysqlclient**.

## Цель

Сохранить архитектуру бенчмарка (корутины, TUI, CLI `init/import/check/run/clean`, TPC-C mix/delays/warmup) и заменить PostgreSQL/libpqxx слой на OceanBase/MySQL C API с SQL в MySQL-диалекте.

Под *портированием* понимаем то же, что в upstream README:

1. Заменить PG-специфичные типы/API на эквиваленты под OceanBase.
2. Перевести SQL с PostgreSQL-диалекта на MySQL-совместимый.
3. Заменить `libpqxx` на Connector/C (`MYSQL*`) за тонким адаптером сессии/пула.
4. Оставить ядро benchmark/TUI/корутин без изменения семантики TPC-C.

## Что уже есть в репозитории

| Компонент | Статус |
|-----------|--------|
| Baseline исходников upstream (src/, tests/, TUI, корутины) | Импортирован |
| План и инвентаризация SQL (`docs/`) | Этот документ + `SQL_DIALECT_GAPS.md` |
| Целевой интерфейс DB-адаптера (`src/db/`) | Каркас API |
| docker-compose / CI / README под OceanBase | Подготовлены |
| Реализация `ObSession` / MySQL SQL / bulk import | **Следующие фазы** |

## Архитектура upstream (сохраняем)

```
main.cpp
  ├─ init / import / clean / check / run
  ├─ Terminal (корутина) → transactions_*
  ├─ Runner + RunnerTUI / ImportTUI (ftxui)
  ├─ TaskQueue + TimerQueue + ThreadPool(IO)
  └─ PgSession / PgConnectionPool / QueryResult  ← ЗАМЕНЯЕМ
```

Важный факт: **libpq async не используется**. DB-вызовы синхронны и гоняются на IO thread pool; корутина ждёт `TFuture`. Это позволяет подставить синхронный MySQL C API без переписывания планировщика.

## Стратегия адаптера

Ввести DB-нейтральный слой и реализовать его для OceanBase:

```
src/db/
  params.h          — биндинги параметров (?, не $1)
  query_result.h    — материализованный результат (без pqxx::result)
  session.h/.cpp    — ObSession: query/modify/commit/rollback/non-tx/bulk
  connection_pool.h/.cpp
  errors.h          — классификация deadlock / lock wait / disconnect
```

Текущие `pg_session.*` / `pg_connection_pool.*` / `query_result.h` — baseline для портирования; после появления `ObSession` их удалить или оставить как тонкие typedef-алиасы на время миграции.

### Целевой API сессии (черновик)

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

Транзакции: `START TRANSACTION ISOLATION LEVEL REPEATABLE READ` (или эквивалент OB) при первом query/modify — как сейчас `pqxx::isolation_level::repeatable_read`.

## Фазы реализации

### Фаза 0 — подготовка репозитория (этот PR)

- [x] Импорт baseline upstream
- [x] Документация плана и SQL gaps
- [x] Каркас `src/db/`
- [x] Переориентация CMake / docker-compose / CI / README на OceanBase
- [x] Убрать `libpqxx` из планируемых submodule-зависимостей
- [ ] `git submodule update --init` для fmt/spdlog/gflags/ftxui/googletest

### Фаза 1 — DB-адаптер OceanBase

1. Подключить `libobclient` / `libmysqlclient` в CMake (`FindOBClient.cmake` или `find_package(MySQL)`).
2. Реализовать `Params` (позиционные `?`) и `QueryResult` поверх `MYSQL_RES` / buffered fetch.
3. Реализовать `ObSession` + `ObConnectionPool`:
   - connect: host/port/user/password/database (строка вида MySQL DSN или отдельные флаги);
   - `--path` → `USE <database>` (не PostgreSQL schema/`search_path`);
   - `CancelAll`: `KILL QUERY` через отдельное admin-соединение или закрытие сокета (нет 1:1 к `pqxx::cancel_query`).
4. Юнит-тесты адаптера на живом OB (SELECT 1, RR transaction, bind int/string/decimal).

**Критерий готовности:** `./build/tpcc run --simulate-select1=5` против OceanBase.

### Фаза 2 — DDL / clean / path

Файлы: `init.cpp`, `clean.cpp`, `path_checker.cpp`.

| PostgreSQL | OceanBase (MySQL mode) |
|------------|------------------------|
| `CREATE SCHEMA` + `SET search_path` | `CREATE DATABASE` + `USE` |
| `DROP TABLE ... CASCADE` | `DROP TABLE IF EXISTS` (порядок с учётом FK) |
| `CREATE INDEX IF NOT EXISTS` | `CREATE INDEX` + игнор «уже есть» / проверка `information_schema` |
| `pg_indexes` | `information_schema.statistics` / `SHOW INDEX` |

**Критерий:** `tpcc init -w 10` + `tpcc clean` на OB.

### Фаза 3 — Bulk import

Файл: `import.cpp` (+ `ExecuteBulk`).

`pqxx::stream_to` / COPY **недоступен**. Варианты (по приоритету проверки):

1. Батчевый multi-row `INSERT INTO t (cols) VALUES (...), (...), ...` (простой, предсказуемый).
2. `LOAD DATA LOCAL INFILE` (если разрешён на клиенте/сервере).
3. OceanBase-specific bulk/direct load — если понадобится паритет по скорости с PG COPY.

Также заменить:

- `SET synchronous_commit = off` → OB session/hint equivalents (или убрать).
- `ANALYZE table` → `ANALYZE TABLE table`.

**Критерий:** `tpcc import -w 10 -t N` + `tpcc check -w 10 --after-import`.

### Фаза 4 — SQL транзакций TPC-C

Файлы: `common_queries.*`, `transaction_*.cpp`.

Обязательные замены:

1. Плейсхолдеры `$1..$n` → `?`.
2. `UPDATE ... RETURNING ...` (Payment) → `SELECT ... FOR UPDATE` + `UPDATE` (+ повторный SELECT при необходимости).
3. `SELECT $1::int` (simulation) → `SELECT CAST(? AS SIGNED)` / `SELECT ?`.
4. Проверить `FOR UPDATE` + Repeatable Read на OB (deadlock / lock wait timeout codes → retry в `terminal.cpp`).

**Критерий:** `tpcc_ob_tests` (бывш. `tpcc_pg_tests`) зелёные; короткий `run` с `--no-delays`.

### Фаза 5 — Consistency checks

Файл: `check.cpp`.

| PostgreSQL | Замена |
|------------|--------|
| `BOOL_AND` / `BOOL_OR` | `MIN(cond)` / `MAX(cond)` или `SUM` |
| `FULL JOIN` | `LEFT JOIN ... UNION ALL ... WHERE ... IS NULL` |

**Критерий:** `tpcc check -w N` после import и после run.

### Фаза 6 — Интеграция, CI, polish

1. `docker compose up` → `oceanbase/oceanbase-ce` (MODE=mini/slim).
2. Переписать `tests/smoke_test.sh`, `path_test.sh`, `stress_test.sh` под `mysql`/`obclient` и DSN OceanBase.
3. CI: unit-tests без DB; smoke на OB service (долгий start_period!).
4. README: connection string, требования к tenant/user, известные отличия от PG-версии.
5. Удалить остатки pqxx/PostgreSQL из кода и комментариев.
6. Профилирование import (батч-размер) и run (pool size / IO threads).

## Риски

| Риск | Уровень | Митигация |
|------|---------|-----------|
| Семантика RR / locking отличается от PostgreSQL | Высокий | Явные тесты транзакций; карта error codes для retry |
| Производительность import без COPY | Высокий | Батчи + измерение; опционально LOAD DATA |
| `RETURNING` в Payment | Средний | Переписать на SELECT+UPDATE |
| Cancel/shutdown соединений | Средний | KILL QUERY / close; дождаться IO pool на shutdown |
| `--path` как schema | Средний | Переопределить как database name |
| Decimal/timestamp edge cases | Средний | Сверить check/after-import invariants |
| Корутины / TUI | Низкий | Не трогать без нужды |

## Параметры подключения (целевые)

Рекомендуемый формат флагов (вместо libpq conninfo):

```
--host=127.0.0.1 --port=2881 --user=root@test --password=... --database=tpcc
```

Либо одна строка:

```
--connection="host=127.0.0.1;port=2881;user=root@test;password=...;database=tpcc"
```

Default tenant в docker-compose: `test`, user `root@test`, SQL port `2881`.

## Зависимости сборки

| Было (PG) | Станет (OB) |
|-----------|-------------|
| Clang 16+, C++23 | без изменений |
| `libpq-dev` + submodule `libpqxx` | `libobclient-dev` **или** `libmysqlclient-dev` |
| fmt, spdlog, gflags, ftxui, googletest (submodules) | те же |
| tcmalloc (опционально) | без изменений |
| `postgres:18` docker | `oceanbase/oceanbase-ce` |

## Порядок работ для следующего агента/итерации

1. Инициализировать submodules (`fmt`, `spdlog`, `gflags`, `ftxui`, `googletest`).
2. Установить client library OceanBase/MySQL в окружении.
3. Реализовать Фазу 1 (`src/db/*`), переключить CMake с pqxx на новый адаптер.
4. Массово заменить includes `pg_session.h` → `db/session.h` и типы `pqxx::params` → `Params`.
5. Фазы 2→5 по чеклисту в `docs/IMPLEMENTATION_CHECKLIST.md`.
6. Прогнать smoke на docker OceanBase.

## Вне скоупа (пока)

- Oracle mode tenant OceanBase.
- Нативный async MySQL nonblocking API (epoll на сокетах) — текущая модель IO pool достаточна.
- Паритет скорости import с YDB bulk (upstream тоже отстаёт от YDB).
- Изменение TPC-C mix / бизнес-логики транзакций.
