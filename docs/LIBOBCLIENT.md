# Installing OceanBase Connector/C (LibOBClient)

The `tpcc` binary links **only** against OceanBase Connector/C (`libobclnt` / `libobclient`).
`libmysqlclient` is not accepted.

## Script (local + CI)

```bash
./scripts/install_libobclient.sh                 # default: $HOME/.local/oceanbase
./scripts/install_libobclient.sh /opt/oceanbase  # custom prefix
```

The script clones [obconnector-c](https://github.com/oceanbase/obconnector-c), configures and builds it, then stages headers and `libobclnt.so*` into the install prefix. No further LibOBClient build steps are required.

`FindOBClient.cmake` searches `$HOME/.local/oceanbase`, `/opt/oceanbase`, `/u01/obclient`, and `OBCLIENT_HOME` / `LIBOBCLIENT_HOME`.

To build `tpcc` after installation, see the [Build](../README.md#build) section in `README.md`. At runtime, ensure the library is on the loader path, e.g.:

```bash
export LD_LIBRARY_PATH="${HOME}/.local/oceanbase/lib:${LD_LIBRARY_PATH:-}"
```

## Manual build (equivalent)

```bash
git clone --depth 1 https://github.com/oceanbase/obconnector-c.git
cd obconnector-c

# CMake 4.x and GCC 14+ need the same tweaks as scripts/install_libobclient.sh
perl -pi -e 's/CMAKE_MINIMUM_REQUIRED\(VERSION 2\.8/CMAKE_MINIMUM_REQUIRED(VERSION 3.5/' CMakeLists.txt

PREFIX="${HOME}/.local/oceanbase"
cmake -B build \
  -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
  -DCMAKE_C_STANDARD=11 \
  -DCMAKE_C_STANDARD_REQUIRED=ON \
  -DCMAKE_C_FLAGS="-Wno-incompatible-pointer-types" \
  -DWITH_SSL=OPENSSL \
  -DWITH_UNIT_TESTS=OFF \
  -DWITH_MYSQLCOMPAT=OFF \
  -DENABLED_LOCAL_INFILE=ON \
  -DDEFAULT_CHARSET=utf8mb4
cmake --build build -j"$(nproc)"

# Upstream `make install` is incomplete for generated headers; stage manually.
mkdir -p "${PREFIX}/include" "${PREFIX}/lib"
cp -a include/*.h "${PREFIX}/include/" 2>/dev/null || true
[[ -d include/mysql ]] && cp -a include/mysql "${PREFIX}/include/"
[[ -d include/mariadb ]] && cp -a include/mariadb "${PREFIX}/include/"
cp -a build/include/*.h "${PREFIX}/include/"
cp -a build/libmariadb/libobclnt.so* "${PREFIX}/lib/"
cp -a build/libmariadb/libobclnt.a "${PREFIX}/lib/"
ln -sfn libobclnt.so "${PREFIX}/lib/libobclient.so"
```

## CI

GitHub Actions installs LibOBClient via `scripts/install_libobclient.sh` (cached under `.deps/oceanbase`) before configure, with `-DTPCC_REQUIRE_OBCLIENT=ON`.

## RPM / YUM (RHEL-like)

Follow OceanBase docs: install `libobclient` from the OceanBase yum repo, then point CMake at the install prefix if needed.
