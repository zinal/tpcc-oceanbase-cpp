#!/usr/bin/env bash
#
# End-to-end smoke test for TPC-C against OceanBase (MySQL-compatible tenant)
# or a MariaDB stand-in (same wire protocol via OceanBase Connector/C).
#
# Prerequisites:
#   - Server reachable (docker compose up -d for OceanBase CE, or MariaDB)
#   - tpcc built against OceanBase Connector/C (libobclnt)
#
# Usage:
#   tests/smoke_test.sh
#   TPCC_BIN=./build/tpcc OB_HOST=127.0.0.1 TPCC_WAREHOUSES=1 tests/smoke_test.sh
#
# Environment:
#   TPCC_BIN, TPCC_WAREHOUSES (default 1)
#   TPCC_DURATION (minutes, default 1) or TPCC_DURATION_SECONDS (CI override)
#   OB_HOST (default 127.0.0.1), OB_PORT (2881 for OB, 3306 for MariaDB)
#   OB_USER (root@test / root), OB_PASSWORD (tpcc)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tests/common.sh
source "${SCRIPT_DIR}/common.sh"

DB_NAME="tpcc_smoke_$$"
CONNECTION="$(make_connection tpcc)"

cleanup() {
    echo "--- Cleaning up ---"
    "${TPCC_BIN}" clean --connection="${CONNECTION}" --path="${DB_NAME}" 2>/dev/null || true
}
trap cleanup EXIT

require_tpcc_bin

echo "=== TPC-C Smoke Test ==="
echo "Binary:     ${TPCC_BIN}"
echo "Database:   ${DB_NAME} (--path)"
echo "Warehouses: ${TPCC_WAREHOUSES}"
if [[ "${TPCC_DURATION_SECONDS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "Duration:   ${TPCC_DURATION_SECONDS}s"
else
    echo "Duration:   ${TPCC_DURATION} min"
fi
echo "Endpoint:   ${OB_HOST}:${OB_PORT} user=${OB_USER}"
echo ""

echo "--- Initializing schema ---"
"${TPCC_BIN}" init --connection="${CONNECTION}" --path="${DB_NAME}"

echo "--- Importing data (${TPCC_WAREHOUSES} warehouse(s)) ---"
"${TPCC_BIN}" import \
    --connection="${CONNECTION}" \
    --path="${DB_NAME}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    -t 1 \
    --no-tui

echo "--- Checking after import ---"
"${TPCC_BIN}" check \
    --connection="${CONNECTION}" \
    --path="${DB_NAME}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    --after-import

# shellcheck disable=SC2046
echo "--- Running benchmark ---"
"${TPCC_BIN}" run \
    --connection="${CONNECTION}" \
    --path="${DB_NAME}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    $(run_duration_args) \
    --no-tui \
    --skip-warmup \
    --no-delays \
    -t 2

echo "--- Checking after benchmark ---"
"${TPCC_BIN}" check \
    --connection="${CONNECTION}" \
    --path="${DB_NAME}" \
    --warehouses="${TPCC_WAREHOUSES}"

echo ""
echo "=== Smoke test PASSED ==="
