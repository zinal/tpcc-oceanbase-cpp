#!/usr/bin/env bash
#
# Integration test for the --path option on OceanBase.
# In the OceanBase port, --path means a MySQL database name (USE db),
# not a PostgreSQL schema / search_path.
#
# Prerequisites: same as smoke_test.sh (Phase 1+).

set -euo pipefail

TPCC_BIN="${TPCC_BIN:-./build/tpcc}"
TPCC_WAREHOUSES="${TPCC_WAREHOUSES:-1}"
TPCC_DURATION="${TPCC_DURATION:-1}"

OB_HOST="${OB_HOST:-127.0.0.1}"
OB_PORT="${OB_PORT:-2881}"
OB_USER="${OB_USER:-root@test}"
OB_PASSWORD="${OB_PASSWORD:-tpcc}"

# Default connection database (should stay free of TPC-C tables when --path is used).
DB_NAME="tpcc_path_default_$$"
# --path target database for benchmark tables.
PATH_DB="tpcc_bench_$$"

mysql_cli() {
    if command -v obclient >/dev/null 2>&1; then
        obclient -h"${OB_HOST}" -P"${OB_PORT}" -u"${OB_USER}" -p"${OB_PASSWORD}" "$@"
    else
        mysql -h"${OB_HOST}" -P"${OB_PORT}" -u"${OB_USER}" -p"${OB_PASSWORD}" "$@"
    fi
}

CONNECTION="host=${OB_HOST};port=${OB_PORT};user=${OB_USER};password=${OB_PASSWORD};database=${DB_NAME}"

cleanup() {
    echo "--- Cleaning up ---"
    "${TPCC_BIN}" clean --path="${PATH_DB}" --connection="${CONNECTION}" 2>/dev/null || true
    mysql_cli -e "DROP DATABASE IF EXISTS \`${PATH_DB}\`;" 2>/dev/null || true
    mysql_cli -e "DROP DATABASE IF EXISTS \`${DB_NAME}\`;" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== TPC-C --path Integration Test (OceanBase) ==="
echo "Binary:      ${TPCC_BIN}"
echo "Default DB:  ${DB_NAME}"
echo "Path DB:     ${PATH_DB}"
echo "Warehouses:  ${TPCC_WAREHOUSES}"
echo ""

if [[ ! -x "${TPCC_BIN}" ]]; then
    echo "ERROR: ${TPCC_BIN} not found or not executable" >&2
    exit 1
fi

echo "--- Creating databases ---"
mysql_cli -e "CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`;"
mysql_cli -e "CREATE DATABASE IF NOT EXISTS \`${PATH_DB}\`;"

# Sentinel in the default database: must survive init/clean of --path DB.
echo "--- Planting sentinel ${DB_NAME}.customer ---"
mysql_cli "${DB_NAME}" -e "CREATE TABLE customer (sentinel int);"
mysql_cli "${DB_NAME}" -e "INSERT INTO customer VALUES (4242);"

echo "--- init --path=${PATH_DB} ---"
"${TPCC_BIN}" init --path="${PATH_DB}" --connection="${CONNECTION}"

echo "--- import --path=${PATH_DB} ---"
"${TPCC_BIN}" import --path="${PATH_DB}" --warehouses="${TPCC_WAREHOUSES}" --no_tui --connection="${CONNECTION}"

echo "--- check --path=${PATH_DB} --after_import ---"
"${TPCC_BIN}" check --path="${PATH_DB}" --warehouses="${TPCC_WAREHOUSES}" --after_import --connection="${CONNECTION}"

echo "--- run --path=${PATH_DB} ---"
"${TPCC_BIN}" run \
    --path="${PATH_DB}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    --duration="${TPCC_DURATION}" \
    --no_tui \
    --connection="${CONNECTION}"

echo "--- Verifying sentinel still in default DB ---"
VAL="$(mysql_cli -N -e "SELECT sentinel FROM \`${DB_NAME}\`.customer;")"
if [[ "${VAL}" != "4242" ]]; then
    echo "ERROR: sentinel corrupted or missing (got '${VAL}')" >&2
    exit 1
fi

echo "--- clean --path=${PATH_DB} ---"
"${TPCC_BIN}" clean --path="${PATH_DB}" --connection="${CONNECTION}"

echo "--- Verifying sentinel after clean ---"
VAL="$(mysql_cli -N -e "SELECT sentinel FROM \`${DB_NAME}\`.customer;")"
if [[ "${VAL}" != "4242" ]]; then
    echo "ERROR: sentinel corrupted after clean (got '${VAL}')" >&2
    exit 1
fi

echo ""
echo "=== Path test PASSED ==="
