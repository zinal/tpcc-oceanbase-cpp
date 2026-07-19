#!/usr/bin/env bash
# Shared helpers for tests/*.sh (OceanBase / MySQL-tenant wire protocol).
#
# Prefer OceanBase CLI `obclient` for ad-hoc SQL. Fall back to `mysql` only when
# running against a MariaDB stand-in in CI (same wire protocol as OB MySQL mode).

set -euo pipefail

TPCC_BIN="${TPCC_BIN:-./build/tpcc}"
TPCC_WAREHOUSES="${TPCC_WAREHOUSES:-1}"
# Minutes (used when TPCC_DURATION_SECONDS is unset/0).
TPCC_DURATION="${TPCC_DURATION:-1}"
# Seconds override for short CI smokes (maps to tpcc --duration-seconds).
TPCC_DURATION_SECONDS="${TPCC_DURATION_SECONDS:-0}"

OB_HOST="${OB_HOST:-127.0.0.1}"
OB_PORT="${OB_PORT:-2881}"
OB_USER="${OB_USER:-root@test}"
OB_PASSWORD="${OB_PASSWORD:-tpcc}"

require_tpcc_bin() {
    if [[ ! -x "${TPCC_BIN}" ]]; then
        echo "ERROR: ${TPCC_BIN} not found or not executable" >&2
        echo "Build with OceanBase Connector/C (see docs/LIBOBCLIENT.md)." >&2
        exit 1
    fi
}

# Resolve a MySQL-protocol CLI: obclient preferred, mysql as CI stand-in.
resolve_sql_cli() {
    if command -v obclient >/dev/null 2>&1; then
        SQL_CLI=(obclient)
        return 0
    fi
    if command -v mysql >/dev/null 2>&1; then
        SQL_CLI=(mysql)
        echo "NOTE: obclient not found; using mysql CLI (MariaDB/MySQL stand-in)" >&2
        return 0
    fi
    echo "ERROR: neither obclient nor mysql CLI found" >&2
    exit 1
}

sql_cli() {
    if [[ -z "${SQL_CLI+x}" ]]; then
        resolve_sql_cli
    fi
    "${SQL_CLI[@]}" -h"${OB_HOST}" -P"${OB_PORT}" -u"${OB_USER}" -p"${OB_PASSWORD}" "$@"
}

make_connection() {
    local database="$1"
    echo "host=${OB_HOST};port=${OB_PORT};user=${OB_USER};password=${OB_PASSWORD};database=${database}"
}

run_duration_args() {
    if [[ "${TPCC_DURATION_SECONDS}" =~ ^[1-9][0-9]*$ ]]; then
        echo --duration-seconds="${TPCC_DURATION_SECONDS}"
    else
        echo --duration="${TPCC_DURATION}"
    fi
}
