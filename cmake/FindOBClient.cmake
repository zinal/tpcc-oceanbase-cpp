# Find OceanBase Connector/C (LibOBClient).
#
# Official client only — do NOT fall back to libmysqlclient / MariaDB.
# Library names seen in the wild: obclient, obclnt.
# Headers: mysql.h (Connector/C API surface), often under include/ or include/mysql/.
#
# Sets:
#   OBClient_FOUND
#   OBClient_INCLUDE_DIRS
#   OBClient_LIBRARIES

find_path(OBClient_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql oceanbase obclient
    PATHS
        /opt/oceanbase/include
        /u01/obclient/include
        /usr/oceanbase/include
        /usr/local/include
        /usr/include
        $ENV{OBCLIENT_HOME}/include
        $ENV{LIBOBCLIENT_HOME}/include
)

find_library(OBClient_LIBRARY
    NAMES obclnt obclient
    PATHS
        /opt/oceanbase/lib
        /u01/obclient/lib
        /usr/oceanbase/lib
        /usr/local/lib
        /usr/local/lib64
        /usr/lib
        /usr/lib64
        $ENV{OBCLIENT_HOME}/lib
        $ENV{LIBOBCLIENT_HOME}/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OBClient
    REQUIRED_VARS OBClient_LIBRARY OBClient_INCLUDE_DIR
    FAIL_MESSAGE
        "OceanBase Connector/C (libobclient/libobclnt) not found. \
Install LibOBClient from OceanBase packages or build from \
https://github.com/oceanbase/obclient (libmariadb / Connector/C). \
libmysqlclient is not an accepted substitute."
)

if(OBClient_FOUND)
    set(OBClient_INCLUDE_DIRS ${OBClient_INCLUDE_DIR})
    set(OBClient_LIBRARIES ${OBClient_LIBRARY})
    message(STATUS "OBClient (OceanBase Connector/C): ${OBClient_LIBRARIES}")
    message(STATUS "OBClient include: ${OBClient_INCLUDE_DIRS}")
endif()

mark_as_advanced(OBClient_INCLUDE_DIR OBClient_LIBRARY)
