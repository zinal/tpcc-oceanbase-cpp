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
