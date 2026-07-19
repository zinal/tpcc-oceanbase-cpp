# TPC-C Benchmark for OceanBase

C++23 TPC-C benchmark for **OceanBase**, ported from
[ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

Real-time TUI, coroutine-based terminals, CLI: `init` → `import` → `check` → `run` → `clean`.

> **Status:** Phases 1–5 complete (Connector/C adapter, DDL, bulk import, transactions,
> consistency checks). Phase 6: CI uses MariaDB as a MySQL wire-protocol stand-in;
> optional OceanBase CE smoke via `docker compose`. See [`docs/PORTING_PLAN.md`](docs/PORTING_PLAN.md).

## Docs

| Doc | Purpose |
|-----|---------|
| [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md) | Phased plan |
| [docs/LIBOBCLIENT.md](docs/LIBOBCLIENT.md) | Build/install Connector/C |
| [docs/SQL_DIALECT_GAPS.md](docs/SQL_DIALECT_GAPS.md) | SQL inventory |
| [docs/IMPLEMENTATION_CHECKLIST.md](docs/IMPLEMENTATION_CHECKLIST.md) | Checklist |
| [docs/UPSTREAM.md](docs/UPSTREAM.md) | Upstream baseline commit |

## Target stack

- Clang 16+ (C++23), CMake 3.20+
- **OceanBase Connector/C** (`libobclnt`) — required ([install notes](docs/LIBOBCLIENT.md))
- Submodules: fmt, spdlog, gflags, ftxui, googletest
- Local DB: `oceanbase/oceanbase-ce` (SQL port **2881**, tenant user `root@test`)

## Build

```bash
# Install LibOBClient first (see docs/LIBOBCLIENT.md), then:
git submodule update --init --recursive
export LD_LIBRARY_PATH=/opt/oceanbase/lib:${LD_LIBRARY_PATH:-}
# export LD_LIBRARY_PATH=/home/demo/.local/oceanbase/lib
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  -DTPCC_REQUIRE_OBCLIENT=ON
cmake --build build -j"$(nproc)"
```

## Quick start (OceanBase CE)

```bash
docker compose up -d
./scripts/wait_for_oceanbase.sh   # first boot can take several minutes

CONN='host=127.0.0.1;port=2881;user=root@test;password=tpcc;database=tpcc'
./build/tpcc init --connection="$CONN" --path=tpcc_bench
./build/tpcc import --connection="$CONN" --path=tpcc_bench -w 1 --no-tui
./build/tpcc check --connection="$CONN" --path=tpcc_bench -w 1 --after-import
./build/tpcc run --connection="$CONN" --path=tpcc_bench -w 1 \
  --duration-seconds=30 --no-tui --skip-warmup --no-delays
./build/tpcc check --connection="$CONN" --path=tpcc_bench -w 1
./build/tpcc clean --connection="$CONN" --path=tpcc_bench
```

Or run the packaged smoke script:

```bash
TPCC_BIN=./build/tpcc TPCC_WAREHOUSES=1 TPCC_DURATION_SECONDS=30 \
  ./tests/smoke_test.sh
```

Connection DSN shape:

```
host=127.0.0.1;port=2881;user=root@test;password=tpcc;database=tpcc
```

`--path` selects a dedicated MySQL database (`CREATE`/`USE`/`DROP`) for benchmark tables.

## Initial database setup (OBD cluster)

For production or lab clusters deployed with [OBD](https://www.oceanbase.com/docs/obd),
prepare a **MySQL-compatible user tenant**, database, and account before running `tpcc`.

### 1. Inspect the cluster

```bash
obd cluster display ob-yc-prod   # replace with your cluster name
```

The output lists observer endpoints, the `root@sys` password (`root_password`), and
ready-to-use `obclient` connect commands. Use it to fill in host, port, and the sys
password below.

### 2. Connect as the sys administrator

Direct connection to port **2881** (or via ODP on **2883** — see `obd cluster display`):

```bash
obclient -h<OB_HOST> -P2881 -uroot@sys -p'<SYS_PASSWORD>' -Doceanbase -A
```

`-D oceanbase` selects the sys catalog; `-A` disables auto-rehash (same as the MySQL
client).

### 3. Create a user tenant (recommended)

Use the sys tenant only for administration. Create a dedicated MySQL tenant, for
example `tpcc`:

```bash
obd cluster tenant create ob-yc-prod -n tpcc
```

Adjust CPU/memory via OBD options if needed (`obd cluster tenant create --help`).

### 4. Create the benchmark database and user

Connect to the **user tenant** (default admin is `root@<tenant>`):

```bash
obclient -h<OB_HOST> -P2881 -uroot@tpcc -p -Doceanbase -A
```

Then run:

```sql
-- Set the tenant root password (empty by default on a new tenant)
ALTER USER root IDENTIFIED BY '<PASSWORD>';

-- Dedicated database for TPC-C
CREATE DATABASE IF NOT EXISTS tpcc DEFAULT CHARACTER SET utf8mb4;
```

Optionally use a non-root account:

```sql
CREATE USER IF NOT EXISTS tpcc IDENTIFIED BY '<PASSWORD>';
GRANT ALL PRIVILEGES ON tpcc.* TO tpcc;
```

`tpcc init` can also create the database named by `--path` on first run if the account
has `CREATE` privilege; pre-creating `tpcc` is still recommended for production.

### 5. DSN for `tpcc`

```
host=<OB_HOST>;port=2881;user=root@tpcc;password=<PASSWORD>;database=tpcc
```

Use `--path=<name>` to isolate benchmark tables in a separate database (`CREATE`/`USE`/`DROP`);
the connection `database=` is the login catalog and can match `--path` or stay on a shared
catalog.

## Integration tests

| Script | Purpose |
|--------|---------|
| `tests/smoke_test.sh` | init → import → check → run → check → clean |
| `tests/path_test.sh` | `--path` isolation (needs `obclient` or `mysql` CLI) |
| `tests/stress_test.sh` | multi-warehouse stress |

Env defaults target OceanBase CE (`2881` / `root@test`). For MariaDB stand-in set
`OB_PORT=3306` and `OB_USER=root`.

## Tuning defaults

| Knob | Default | Notes |
|------|---------|-------|
| Bulk import batch | 200 rows | `ObSession::ExecuteBulk` multi-row `INSERT` |
| Terminals / warehouse | 10 | TPC-C |
| `--max-inflight` | 100 | run |
| Import loader threads | `min(warehouses, CPUs, 100)` | `--threads` |

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
