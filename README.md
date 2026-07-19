# TPC-C Benchmark for OceanBase

C++23 TPC-C benchmark for **OceanBase** (MySQL-compatible tenants), ported from
[ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

Real-time TUI, coroutine-based terminals, CLI: `init` → `import` → `check` → `run` → `clean`.

> **Status:** repository prepared for the port. Baseline PostgreSQL sources are
> imported; OceanBase adapter and SQL dialect work are tracked in
> [`docs/PORTING_PLAN.md`](docs/PORTING_PLAN.md).

## Porting docs

| Doc | Purpose |
|-----|---------|
| [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md) | Phased plan (RU), risks, connection model |
| [docs/SQL_DIALECT_GAPS.md](docs/SQL_DIALECT_GAPS.md) | PG → MySQL/OB SQL inventory |
| [docs/IMPLEMENTATION_CHECKLIST.md](docs/IMPLEMENTATION_CHECKLIST.md) | Implementation checkbox list |
| [src/db/README.md](src/db/README.md) | Target `ObSession` / pool API |

## Target stack

- Clang 16+ (C++23), CMake 3.20+
- OceanBase Connector/C (`libobclient`) or MySQL client library
- Bundled submodules: fmt, spdlog, gflags, ftxui, googletest
- Local DB: `oceanbase/oceanbase-ce` via Docker Compose (SQL port **2881**)

PostgreSQL / `libpqxx` are **not** target dependencies (removed from `.gitmodules`).

## Quick start (after Phase 1+)

```bash
git submodule update --init
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

docker compose up -d   # OceanBase CE, wait until healthy

./build/tpcc init   --host=127.0.0.1 --port=2881 --user=root@test --password=... --database=tpcc -w 10
./build/tpcc import --host=127.0.0.1 --port=2881 --user=root@test --password=... --database=tpcc -w 10 -t 5
./build/tpcc check  --host=127.0.0.1 --port=2881 --user=root@test --password=... --database=tpcc -w 10 --after-import
./build/tpcc run    --host=127.0.0.1 --port=2881 --user=root@test --password=... --database=tpcc -w 10 --duration=5
./build/tpcc clean  --host=127.0.0.1 --port=2881 --user=root@test --password=... --database=tpcc
```

Exact CLI flags may still match the PostgreSQL baseline (`--connection=...`) until
the adapter phase lands — see the plan.

## Upstream features retained

- Low overhead coroutine terminals (no callback hell / thread-per-terminal)
- Live TUI: tpmC, latency histograms, import progress
- Spec-oriented checks (`check`, `--after-import`)
- Adaptive warmup, inflight limits, optional `--no-delays`

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
