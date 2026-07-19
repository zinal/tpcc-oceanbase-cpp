# Installing OceanBase Connector/C (LibOBClient)

The `tpcc` binary links **only** against OceanBase Connector/C (`libobclnt` / `libobclient`).
`libmysqlclient` is not accepted.

## Script (local + CI)

```bash
./scripts/install_libobclient.sh                 # default: $HOME/.local/oceanbase
./scripts/install_libobclient.sh /opt/oceanbase  # custom prefix
```

Then:

```bash
export LD_LIBRARY_PATH="${HOME}/.local/oceanbase/lib:${LD_LIBRARY_PATH:-}"
cmake -B build \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DTPCC_REQUIRE_OBCLIENT=ON
cmake --build build -j"$(nproc)"
```

`FindOBClient.cmake` searches `$HOME/.local/oceanbase`, `/opt/oceanbase`, `/u01/obclient`, and `OBCLIENT_HOME` / `LIBOBCLIENT_HOME`.

## Manual build (equivalent)

```bash
git clone --depth 1 https://github.com/oceanbase/obconnector-c.git
cd obconnector-c
cmake -B build \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/oceanbase" \
  -DWITH_SSL=OPENSSL \
  -DWITH_UNIT_TESTS=OFF \
  -DDEFAULT_CHARSET=utf8mb4
cmake --build build -j"$(nproc)"
# headers + libobclnt.so* staged by scripts/install_libobclient.sh
```

## CI

GitHub Actions installs LibOBClient via `scripts/install_libobclient.sh` (cached under `.deps/oceanbase`) before configure, with `-DTPCC_REQUIRE_OBCLIENT=ON`.

## RPM / YUM (RHEL-like)

Follow OceanBase docs: install `libobclient` from the OceanBase yum repo, then point CMake at the install prefix if needed.
