#!/usr/bin/env bash
# Build and install OceanBase Connector/C (LibOBClient / libobclnt).
# Usage: scripts/install_libobclient.sh [INSTALL_PREFIX]
set -euo pipefail

PREFIX="${1:-${HOME}/.local/oceanbase}"
SRC_DIR="${OBCONNECTOR_SRC:-/tmp/obconnector-c}"
REPO_URL="${OBCONNECTOR_REPO:-https://github.com/oceanbase/obconnector-c.git}"

echo "Installing OceanBase Connector/C into ${PREFIX}"

if [[ ! -d "${SRC_DIR}/.git" ]]; then
    rm -rf "${SRC_DIR}"
    git clone --depth 1 "${REPO_URL}" "${SRC_DIR}"
fi

# obconnector-c still declares CMAKE_MINIMUM_REQUIRED(2.8) on Linux; CMake 4.x rejects that.
perl -pi -e 's/CMAKE_MINIMUM_REQUIRED\(VERSION 2\.8/CMAKE_MINIMUM_REQUIRED(VERSION 3.5/' \
    "${SRC_DIR}/CMakeLists.txt"

# obconnector-c typedefs `bool` in ma_global.h; that breaks with C23 (GCC 14+ default).
# GCC 14+ also errors on char** vs uchar** in net_field_length() call sites.
cmake -S "${SRC_DIR}" -B "${SRC_DIR}/build" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DCMAKE_C_STANDARD=11 \
    -DCMAKE_C_STANDARD_REQUIRED=ON \
    -DCMAKE_C_FLAGS="-Wno-incompatible-pointer-types" \
    -DWITH_SSL=OPENSSL \
    -DWITH_UNIT_TESTS=OFF \
    -DWITH_MYSQLCOMPAT=OFF \
    -DENABLED_LOCAL_INFILE=ON \
    -DDEFAULT_CHARSET=utf8mb4

cmake --build "${SRC_DIR}/build" -j"$(nproc)"

# Upstream `make install` is incomplete for generated headers; stage manually.
mkdir -p "${PREFIX}/include" "${PREFIX}/lib"
cp -a "${SRC_DIR}/include/"*.h "${PREFIX}/include/" 2>/dev/null || true
if [[ -d "${SRC_DIR}/include/mysql" ]]; then
    cp -a "${SRC_DIR}/include/mysql" "${PREFIX}/include/"
fi
if [[ -d "${SRC_DIR}/include/mariadb" ]]; then
    cp -a "${SRC_DIR}/include/mariadb" "${PREFIX}/include/"
fi
cp -a "${SRC_DIR}/build/include/"*.h "${PREFIX}/include/"
cp -a "${SRC_DIR}/build/libmariadb"/libobclnt.so* "${PREFIX}/lib/"
cp -a "${SRC_DIR}/build/libmariadb/libobclnt.a" "${PREFIX}/lib/"
ln -sfn libobclnt.so "${PREFIX}/lib/libobclient.so"

echo "Installed:"
ls -la "${PREFIX}/lib"/libobclnt* "${PREFIX}/include/mysql.h"
