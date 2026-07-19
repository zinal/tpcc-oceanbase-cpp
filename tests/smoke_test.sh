#!/usr/bin/env bash
#
# End-to-end smoke test for TPC-C against OceanBase (MySQL mode).
#
# Prerequisites (after Phase 1 of docs/PORTING_PLAN.md):
#   - OceanBase reachable (e.g. docker compose up -d)
#   - tpcc binary built against libobclient/libmysqlclient
#   - mysql or obclient CLI available to create/drop the test database
#
# Usage:
#   tests/smoke_test.sh
#   TPCC_BIN=./build/tpcc OB_HOST=127.0.0.1 tests/smoke_test.sh
#
# Environment:
#   TPCC_BIN, TPCC_WAREHOUSES, TPCC_DURATION
#   OB_HOST (default 127.0.0.1), OB_PORT (2881)
#   OB_USER (root@test), OB_PASSWORD (tpcc), OB_DATABASE prefix

set -euo pipefail

TPCC_BIN="${TPCC_BIN:-./build/tpcc}"
TPCC_WAREHOUSES="${TPCC_WAREHOUSES:-10}"
TPCC_DURATION="${TPCC_DURATION:-2}"

OB_HOST="${OB_HOST:-127.0.0.1}"
OB_PORT="${OB_PORT:-2881}"
OB_USER="${OB_USER:-root@test}"
OB_PASSWORD="${OB_PASSWORD:-tpcc}"
DB_NAME="tpcc_smoke_$$"

mysql_cli() {
    if command -v obclient >/dev/null 2>&1; then
        obclient -h"${OB_HOST}" -P"${OB_PORT}" -u"${OB_USER}" -p"${OB_PASSWORD}" "$@"
    else
        mysql -h"${OB_HOST}" -P"${OB_PORT}" -u"${OB_USER}" -p"${OB_PASSWORD}" "$@"
    fi
}

# Prefer discrete flags once Phase 1 lands; fall back to a single --connection DSN.
CONNECTION="host=${OB_HOST};port=${OB_PORT};user=${OB_USER};password=${OB_PASSWORD};database=${DB_NAME}"

cleanup() {
    echo "--- Cleaning up ---"
    "${TPCC_BIN}" clean --connection="${CONNECTION}" 2>/dev/null || true
    mysql_cli -e "DROP DATABASE IF EXISTS \`${DB_NAME}\`;" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== TPC-C Smoke Test (OceanBase) ==="
echo "Binary:     ${TPCC_BIN}"
echo "Database:   ${DB_NAME}"
echo "Warehouses: ${TPCC_WAREHOUSES}"
echo "Duration:   ${TPCC_DURATION} min"
echo ""

if [[ ! -x "${TPCC_BIN}" ]]; then
    echo "ERROR: ${TPCC_BIN} not found or not executable" >&2
    echo "Complete Phase 1 (ObSession) before running this smoke test." >&2
    exit 1
fi

echo "--- Creating database ---"
mysql_cli -e "CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`;"

echo "--- Initializing schema ---"
"${TPCC_BIN}" init --connection="${CONNECTION}"

echo "--- Importing data (${TPCC_WAREHOUSES} warehouse(s)) ---"
"${TPCC_BIN}" import --warehouses="${TPCC_WAREHOUSES}" --no_tui --connection="${CONNECTION}"

echo "--- Checking after import ---"
"${TPCC_BIN}" check --warehouses="${TPCC_WAREHOUSES}" --after_import --connection="${CONNECTION}"

echo "--- Running benchmark (${TPCC_DURATION} min, no TUI) ---"
"${TPCC_BIN}" run \
    --warehouses="${TPCC_WAREHOUSES}" \
    --duration="${TPCC_DURATION}" \
    --no_tui \
    --connection="${CONNECTION}"

echo "--- Checking after benchmark ---"
"${TPCC_BIN}" check --warehouses="${TPCC_WAREHOUSES}" --connection="${CONNECTION}"

echo ""
echo "=== Smoke test PASSED ==="
