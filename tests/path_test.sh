#!/usr/bin/env bash
#
# Integration test for --path (dedicated MySQL database / USE db).
#
# Plants a sentinel table in the default connection database and verifies it
# survives init/import/run/clean of the --path database.
#
# Requires a MySQL-protocol CLI for the sentinel (obclient preferred; mysql OK
# for MariaDB CI stand-in).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tests/common.sh
source "${SCRIPT_DIR}/common.sh"

DB_NAME="tpcc_path_default_$$"
PATH_DB="tpcc_bench_$$"
CONNECTION="$(make_connection "${DB_NAME}")"

cleanup() {
    echo "--- Cleaning up ---"
    "${TPCC_BIN}" clean --path="${PATH_DB}" --connection="${CONNECTION}" 2>/dev/null || true
    sql_cli -e "DROP DATABASE IF EXISTS \`${PATH_DB}\`;" 2>/dev/null || true
    sql_cli -e "DROP DATABASE IF EXISTS \`${DB_NAME}\`;" 2>/dev/null || true
}
trap cleanup EXIT

require_tpcc_bin
resolve_sql_cli

echo "=== TPC-C --path Integration Test ==="
echo "Binary:      ${TPCC_BIN}"
echo "Default DB:  ${DB_NAME}"
echo "Path DB:     ${PATH_DB}"
echo "Warehouses:  ${TPCC_WAREHOUSES}"
echo ""

echo "--- Creating default database ---"
sql_cli -e "CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`;"

# Sentinel in the default database: must survive init/clean of --path DB.
echo "--- Planting sentinel ${DB_NAME}.customer ---"
sql_cli -e "USE \`${DB_NAME}\`; DROP TABLE IF EXISTS customer; CREATE TABLE customer (sentinel int); INSERT INTO customer VALUES (4242);"

echo "--- init --path=${PATH_DB} ---"
"${TPCC_BIN}" init --path="${PATH_DB}" --connection="${CONNECTION}"

echo "--- import --path=${PATH_DB} ---"
"${TPCC_BIN}" import \
    --path="${PATH_DB}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    --no-tui \
    --connection="${CONNECTION}"

echo "--- check --path=${PATH_DB} --after-import ---"
"${TPCC_BIN}" check \
    --path="${PATH_DB}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    --after-import \
    --connection="${CONNECTION}"

# shellcheck disable=SC2046
echo "--- run --path=${PATH_DB} ---"
"${TPCC_BIN}" run \
    --path="${PATH_DB}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    $(run_duration_args) \
    --no-tui \
    --skip-warmup \
    --no-delays \
    --connection="${CONNECTION}"

echo "--- Verifying sentinel still in default DB ---"
VAL="$(sql_cli -N -e "SELECT sentinel FROM \`${DB_NAME}\`.customer;")"
if [[ "${VAL}" != "4242" ]]; then
    echo "ERROR: sentinel corrupted or missing (got '${VAL}')" >&2
    exit 1
fi

echo "--- clean --path=${PATH_DB} ---"
"${TPCC_BIN}" clean --path="${PATH_DB}" --connection="${CONNECTION}"

echo "--- Verifying sentinel after clean ---"
VAL="$(sql_cli -N -e "SELECT sentinel FROM \`${DB_NAME}\`.customer;")"
if [[ "${VAL}" != "4242" ]]; then
    echo "ERROR: sentinel corrupted after clean (got '${VAL}')" >&2
    exit 1
fi

echo ""
echo "=== Path test PASSED ==="
