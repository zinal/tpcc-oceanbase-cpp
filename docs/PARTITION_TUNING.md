# Partition tuning for OceanBase TPC-C

TPC-C tables on OceanBase can use a `TABLEGROUP` with `PARTITION BY HASH` on the
warehouse key (`w_id` and related columns). Partition count affects data
distribution, cross-partition traffic, and DDL/import time.

## CLI

| `--partitions` | Behavior |
|----------------|----------|
| `0` (default) | Auto on OceanBase: **one partition per warehouse** (`max(1, -w)`) |
| `N > 0` | Explicit partition count on all TPC-C tables |
| `-1` | Disable partitioning (plain tables; non-OceanBase targets) |

`tpcc init` is the only command that reads `--partitions`. Re-run `init` (and
`import`) after changing it.

## Topology-driven sizing

There is **no universal optimum** (for example, 16 partitions is not always
correct). Start from workload scale and cluster shape:

1. **Warehouses (`-w`)** — natural upper bound for warehouse-keyed hashing; more
   partitions than warehouses often creates idle partitions.
2. **Tenant units / OBServer nodes** — aim for enough partitions to use the
   cluster, but avoid large gaps versus unit count.
3. **CPU and terminals** — `-t` / `--max-inflight` drive client concurrency;
   partitions should match backend capacity, not client thread count alone.

Suggested workflow:

1. Pick an initial `--partitions` (often `≤ warehouses`, or slightly above unit
   count for multi-node).
2. Run `import` + a short `run`.
3. Inspect distribution and hot partitions:
   - **SQL Audit** — partition-related access patterns and skew.
   - **`GV$OB_PARTITION_AUDIT`** — per-partition row counts and balance.
   - **`GV$OB_UNITS`** — tenant unit layout when sizing for a specific tenant.

`tpcc init` logs a **warning** when partition count is significantly higher than
warehouse count or tenant unit count (see `WarnPartitionTopology` in
`src/schema_info.cpp`).

## Examples

```bash
# 4 warehouses, 4 partitions (auto)
tpcc init -w 4 --connection="$CONN" --path=tpcc_bench

# 32 warehouses on an 8-node layout — explicit override
tpcc init -w 32 --partitions=16 --connection="$CONN" --path=tpcc_bench

# Single-node / MariaDB stand-in — no partitioning
tpcc init --partitions=-1 --connection="$CONN" --path=tpcc_bench
```

## Foreign keys and performance runs

Foreign keys add referential checks on `new_order` and `history` inserts. For
throughput experiments you can disable them with `--foreign-keys=off`, but only
when consistency is still validated:

```bash
tpcc performance-run -w 10 --foreign-keys=off --no-delays --duration-seconds=60
```

`performance-run` always runs `check --after-import` before and `check` after the
benchmark. The final `run` report includes `Foreign keys: on|off` and
`HASH partitions: N` when detectable from the live schema.
