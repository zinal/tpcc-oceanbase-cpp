# TPC-C Benchmark for OceanBase

C++23 TPC-C benchmark for **OceanBase**, ported from
[ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

Real-time TUI, coroutine-based terminals, CLI: `init` → `import` → `check` → `run` → `clean`.

> **Status:** OceanBase-only port in progress. No PostgreSQL backend in the final
> product. Client library: **OceanBase Connector/C (LibOBClient)** — not libmysqlclient.
> See [`docs/PORTING_PLAN.md`](docs/PORTING_PLAN.md).

## Porting docs

| Doc | Purpose |
|-----|---------|
| [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md) | Phased plan (RU), scope, risks |
| [docs/SQL_DIALECT_GAPS.md](docs/SQL_DIALECT_GAPS.md) | PG → OB SQL inventory |
| [docs/IMPLEMENTATION_CHECKLIST.md](docs/IMPLEMENTATION_CHECKLIST.md) | Checkbox list |
| [src/db/README.md](src/db/README.md) | `ObSession` / pool API |

## Target stack

- Clang 16+ (C++23), CMake 3.20+
- **OceanBase Connector/C** (`libobclient` / `libobclnt`) — required
- Bundled submodules: fmt, spdlog, gflags, ftxui, googletest
- Local DB: `oceanbase/oceanbase-ce` (MySQL-compatible tenant, SQL port **2881**)
- CLI for scripts: `obclient`

Explicitly **out of scope:** PostgreSQL, libpq/libpqxx, libmysqlclient as a substitute driver.

## Quick start (after Phase 1+)

```bash
# Install LibOBClient (OceanBase Connector/C), then:
git submodule update --init
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

docker compose up -d   # wait until healthy

./build/tpcc init   --host=127.0.0.1 --port=2881 --user=root@test --password=tpcc --database=tpcc -w 10
./build/tpcc import --host=127.0.0.1 --port=2881 --user=root@test --password=tpcc --database=tpcc -w 10 -t 5
./build/tpcc check  --host=127.0.0.1 --port=2881 --user=root@test --password=tpcc --database=tpcc -w 10 --after-import
./build/tpcc run    --host=127.0.0.1 --port=2881 --user=root@test --password=tpcc --database=tpcc -w 10 --duration=5
./build/tpcc clean  --host=127.0.0.1 --port=2881 --user=root@test --password=tpcc --database=tpcc
```

## Upstream features retained

- Coroutine terminals (no callback hell / thread-per-terminal)
- Live TUI: tpmC, latency histograms, import progress
- Spec-oriented checks (`check`, `--after-import`)
- Adaptive warmup, inflight limits, optional `--no-delays`

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
