#!/usr/bin/env bash
#
# Multi-warehouse stress test for TPC-C on OceanBase.
# Prerequisites: same as smoke_test.sh (Phase 1+ complete).

set -euo pipefail

TPCC_BIN="${TPCC_BIN:-./build/tpcc}"
TPCC_WAREHOUSES="${TPCC_WAREHOUSES:-5}"
TPCC_DURATION="${TPCC_DURATION:-1}"

OB_HOST="${OB_HOST:-127.0.0.1}"
OB_PORT="${OB_PORT:-2881}"
OB_USER="${OB_USER:-root@test}"
OB_PASSWORD="${OB_PASSWORD:-tpcc}"
DB_NAME="tpcc_stress_$$"

if ! command -v obclient >/dev/null 2>&1; then
    echo "ERROR: obclient not found (OceanBase CLI required)" >&2
    exit 1
fi

ob_cli() {
    obclient -h"${OB_HOST}" -P"${OB_PORT}" -u"${OB_USER}" -p"${OB_PASSWORD}" "$@"
}

CONNECTION="host=${OB_HOST};port=${OB_PORT};user=${OB_USER};password=${OB_PASSWORD};database=${DB_NAME}"

cleanup() {
    echo "--- Cleaning up ---"
    "${TPCC_BIN}" clean --connection="${CONNECTION}" 2>/dev/null || true
    ob_cli -e "DROP DATABASE IF EXISTS \`${DB_NAME}\`;" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== TPC-C Stress Test (OceanBase) ==="
echo "Binary:     ${TPCC_BIN}"
echo "Database:   ${DB_NAME}"
echo "Warehouses: ${TPCC_WAREHOUSES}"
echo "Duration:   ${TPCC_DURATION} min"
echo ""

if [[ ! -x "${TPCC_BIN}" ]]; then
    echo "ERROR: ${TPCC_BIN} not found or not executable" >&2
    exit 1
fi

echo "--- Creating database ---"
ob_cli -e "CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`;"

echo "--- Initializing schema ---"
"${TPCC_BIN}" init --connection="${CONNECTION}"

echo "--- Importing data (${TPCC_WAREHOUSES} warehouses) ---"
"${TPCC_BIN}" import --warehouses="${TPCC_WAREHOUSES}" --connection="${CONNECTION}"

echo "--- Checking after import ---"
"${TPCC_BIN}" check --warehouses="${TPCC_WAREHOUSES}" --after_import --connection="${CONNECTION}"

echo "--- Running benchmark (${TPCC_DURATION} min, no delays, no TUI) ---"
"${TPCC_BIN}" run \
    --warehouses="${TPCC_WAREHOUSES}" \
    --duration="${TPCC_DURATION}" \
    --no_delays \
    --no_tui \
    --connection="${CONNECTION}"

echo "--- Checking after benchmark ---"
"${TPCC_BIN}" check --warehouses="${TPCC_WAREHOUSES}" --connection="${CONNECTION}"

echo ""
echo "=== Stress test PASSED ==="
