# Installing OceanBase Connector/C (LibOBClient)

The `tpcc` binary links **only** against OceanBase Connector/C (`libobclnt` / `libobclient`).
`libmysqlclient` is not accepted.

## Build from source (recommended on Ubuntu)

```bash
git clone --depth 1 https://github.com/oceanbase/obconnector-c.git
cd obconnector-c
cmake -B build \
  -DCMAKE_INSTALL_PREFIX=/opt/oceanbase \
  -DWITH_SSL=OPENSSL \
  -DWITH_UNIT_TESTS=OFF \
  -DDEFAULT_CHARSET=utf8mb4
cmake --build build -j"$(nproc)"

# Install headers + library (upstream `make install` may miss generated headers)
sudo mkdir -p /opt/oceanbase/{include,lib}
sudo cp -a include/*.h include/mysql include/mariadb /opt/oceanbase/include/ 2>/dev/null || true
sudo cp -a build/include/*.h /opt/oceanbase/include/
sudo cp -a build/libmariadb/libobclnt.so* build/libmariadb/libobclnt.a /opt/oceanbase/lib/
sudo ln -sf libobclnt.so /opt/oceanbase/lib/libobclient.so
```

Then configure this project:

```bash
export LD_LIBRARY_PATH=/opt/oceanbase/lib:${LD_LIBRARY_PATH:-}
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

`FindOBClient.cmake` searches `/opt/oceanbase`, `/u01/obclient`, and `OBCLIENT_HOME` / `LIBOBCLIENT_HOME`.

## RPM / YUM (RHEL-like)

Follow OceanBase docs: install `libobclient` from the OceanBase yum repo, then point CMake at the install prefix if needed.
