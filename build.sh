#!/usr/bin/env bash
# Full clean rebuild of tpcc-oceanbase-cpp.
# Usage: ./build.sh [extra cmake configure args...]
#
# Environment:
#   BUILD_DIR       Build directory (default: build)
#   BUILD_TYPE      CMake build type (default: Release)
#   CXX_COMPILER    C++ compiler (default: first of clang++-20, clang++-18, clang++)
#   JOBS            Parallel build jobs (default: nproc)
#   OBCLIENT_HOME   OceanBase Connector/C prefix (also LIBOBCLIENT_HOME)
#   SKIP_SUBMODULES Set to 1 to skip git submodule update
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"

pick_compiler() {
    if [[ -n "${CXX_COMPILER:-}" ]]; then
        echo "${CXX_COMPILER}"
        return
    fi
    for c in clang++-20 clang++-18 clang++-16 clang++; do
        if command -v "${c}" >/dev/null 2>&1; then
            echo "${c}"
            return
        fi
    done
    echo "error: clang++ not found (need Clang 16+)" >&2
    exit 1
}

detect_obclient_prefix() {
    if [[ -n "${OBCLIENT_HOME:-}" ]]; then
        echo "${OBCLIENT_HOME}"
        return
    fi
    if [[ -n "${LIBOBCLIENT_HOME:-}" ]]; then
        echo "${LIBOBCLIENT_HOME}"
        return
    fi
    for p in \
        "${HOME}/.local/oceanbase" \
        /opt/oceanbase \
        /u01/obclient \
        "${ROOT}/.deps/oceanbase"
    do
        if [[ -e "${p}/lib/libobclnt.so" || -e "${p}/lib/libobclient.so" ]]; then
            echo "${p}"
            return
        fi
    done
    echo ""
}

CXX_COMPILER="$(pick_compiler)"
OB_PREFIX="$(detect_obclient_prefix)"

if [[ -n "${OB_PREFIX}" ]]; then
    export OBCLIENT_HOME="${OB_PREFIX}"
    export LD_LIBRARY_PATH="${OB_PREFIX}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    echo "Using OceanBase Connector/C at ${OB_PREFIX}"
else
    echo "warning: LibOBClient prefix not found; cmake will search default paths" >&2
    echo "  install with: ./scripts/install_libobclient.sh" >&2
fi

if [[ "${SKIP_SUBMODULES:-0}" != "1" ]]; then
    echo "Updating git submodules..."
    git submodule update --init --recursive
fi

echo "Cleaning ${BUILD_DIR}/ ..."
rm -rf "${BUILD_DIR}"

CMAKE_ARGS=(
    -B "${BUILD_DIR}"
    -DCMAKE_CXX_COMPILER="${CXX_COMPILER}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DTPCC_REQUIRE_OBCLIENT=ON
)

if [[ -n "${OB_PREFIX}" ]]; then
    if [[ -f "${OB_PREFIX}/include/mysql.h" ]]; then
        CMAKE_ARGS+=(-DOBClient_INCLUDE_DIR="${OB_PREFIX}/include")
    elif [[ -f "${OB_PREFIX}/include/mysql/mysql.h" ]]; then
        CMAKE_ARGS+=(-DOBClient_INCLUDE_DIR="${OB_PREFIX}/include/mysql")
    fi
    if [[ -e "${OB_PREFIX}/lib/libobclnt.so" ]]; then
        CMAKE_ARGS+=(-DOBClient_LIBRARY="${OB_PREFIX}/lib/libobclnt.so")
    elif [[ -e "${OB_PREFIX}/lib/libobclient.so" ]]; then
        CMAKE_ARGS+=(-DOBClient_LIBRARY="${OB_PREFIX}/lib/libobclient.so")
    fi
fi

# Pass through any extra cmake args from the caller.
CMAKE_ARGS+=("$@")

echo "Configuring (${BUILD_TYPE}, ${CXX_COMPILER})..."
cmake "${CMAKE_ARGS[@]}"

echo "Building (-j${JOBS})..."
cmake --build "${BUILD_DIR}" -j"${JOBS}"

echo "Done: ${ROOT}/${BUILD_DIR}/tpcc"
