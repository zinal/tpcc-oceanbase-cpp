#!/usr/bin/env bash
#
# Multi-warehouse stress test for TPC-C on OceanBase / MariaDB stand-in.
# Prerequisites: same as smoke_test.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tests/common.sh
source "${SCRIPT_DIR}/common.sh"

TPCC_WAREHOUSES="${TPCC_WAREHOUSES:-5}"

DB_NAME="tpcc_stress_$$"
CONNECTION="$(make_connection tpcc)"

cleanup() {
    echo "--- Cleaning up ---"
    "${TPCC_BIN}" clean --connection="${CONNECTION}" --path="${DB_NAME}" 2>/dev/null || true
}
trap cleanup EXIT

require_tpcc_bin

echo "=== TPC-C Stress Test ==="
echo "Binary:     ${TPCC_BIN}"
echo "Database:   ${DB_NAME} (--path)"
echo "Warehouses: ${TPCC_WAREHOUSES}"
if [[ "${TPCC_DURATION_SECONDS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "Duration:   ${TPCC_DURATION_SECONDS}s"
else
    echo "Duration:   ${TPCC_DURATION} min"
fi
echo ""

echo "--- Initializing schema ---"
"${TPCC_BIN}" init --connection="${CONNECTION}" --path="${DB_NAME}"

echo "--- Importing data (${TPCC_WAREHOUSES} warehouses) ---"
"${TPCC_BIN}" import \
    --connection="${CONNECTION}" \
    --path="${DB_NAME}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    --no-tui

echo "--- Checking after import ---"
"${TPCC_BIN}" check \
    --connection="${CONNECTION}" \
    --path="${DB_NAME}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    --after-import

# shellcheck disable=SC2046
echo "--- Running benchmark (no delays) ---"
"${TPCC_BIN}" run \
    --connection="${CONNECTION}" \
    --path="${DB_NAME}" \
    --warehouses="${TPCC_WAREHOUSES}" \
    $(run_duration_args) \
    --no-delays \
    --no-tui \
    --skip-warmup

echo "--- Checking after benchmark ---"
"${TPCC_BIN}" check \
    --connection="${CONNECTION}" \
    --path="${DB_NAME}" \
    --warehouses="${TPCC_WAREHOUSES}"

echo ""
echo "=== Stress test PASSED ==="
