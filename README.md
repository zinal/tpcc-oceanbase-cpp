# TPC-C Benchmark for OceanBase

C++23 TPC-C benchmark for **OceanBase**, ported from
[ydb-platform/tpcc-postgres-cpp](https://github.com/ydb-platform/tpcc-postgres-cpp).

Real-time TUI, coroutine-based terminals, CLI: `init` → `import` → `check` → `run` → `clean`.

> **Status:** Phase 1 complete — OceanBase Connector/C adapter (`libobclnt`) is wired;
> `run --simulate-select1` works. `init` / `import` / `check` / `clean` land in later phases.
> See [`docs/PORTING_PLAN.md`](docs/PORTING_PLAN.md).

## Docs

| Doc | Purpose |
|-----|---------|
| [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md) | Phased plan |
| [docs/LIBOBCLIENT.md](docs/LIBOBCLIENT.md) | Build/install Connector/C |
| [docs/SQL_DIALECT_GAPS.md](docs/SQL_DIALECT_GAPS.md) | SQL inventory |
| [docs/IMPLEMENTATION_CHECKLIST.md](docs/IMPLEMENTATION_CHECKLIST.md) | Checklist |

## Target stack

- Clang 16+ (C++23), CMake 3.20+
- **OceanBase Connector/C** (`libobclnt`) — required ([install notes](docs/LIBOBCLIENT.md))
- Submodules: fmt, spdlog, gflags, ftxui, googletest
- Local DB: `oceanbase/oceanbase-ce` (SQL port **2881**)

## Build

```bash
# Install LibOBClient first (see docs/LIBOBCLIENT.md), then:
git submodule update --init
export LD_LIBRARY_PATH=/opt/oceanbase/lib:${LD_LIBRARY_PATH:-}
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## Quick start (Phase 1)

```bash
docker compose up -d   # OceanBase CE; wait until healthy

./build/tpcc run \
  --connection="host=127.0.0.1;port=2881;user=root@test;password=tpcc;database=test" \
  --simulate-select1=5 --duration=1 --no-tui --skip-warmup
```

Pure coroutine smoke (no DB):

```bash
./build/tpcc run --simulate-ms=5 --duration=1 --no-tui --skip-warmup
```

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
