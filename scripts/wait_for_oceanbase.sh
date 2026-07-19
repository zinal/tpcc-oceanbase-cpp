#!/usr/bin/env bash
# Wait until OceanBase CE (docker compose) accepts SQL on port 2881.
set -euo pipefail

OB_HOST="${OB_HOST:-127.0.0.1}"
OB_PORT="${OB_PORT:-2881}"
OB_USER="${OB_USER:-root@test}"
OB_PASSWORD="${OB_PASSWORD:-tpcc}"
TIMEOUT_SEC="${TIMEOUT_SEC:-600}"

if command -v obclient >/dev/null 2>&1; then
    CLI=(obclient)
elif command -v mysql >/dev/null 2>&1; then
    CLI=(mysql)
else
    echo "ERROR: need obclient or mysql to probe OceanBase" >&2
    exit 1
fi

echo "Waiting for OceanBase at ${OB_HOST}:${OB_PORT} (timeout ${TIMEOUT_SEC}s)..."
deadline=$((SECONDS + TIMEOUT_SEC))
while (( SECONDS < deadline )); do
    if "${CLI[@]}" -h"${OB_HOST}" -P"${OB_PORT}" -u"${OB_USER}" -p"${OB_PASSWORD}" \
        -e "SELECT 1;" >/dev/null 2>&1; then
        echo "OceanBase is ready."
        exit 0
    fi
    sleep 5
done

echo "ERROR: OceanBase not ready within ${TIMEOUT_SEC}s" >&2
exit 1
