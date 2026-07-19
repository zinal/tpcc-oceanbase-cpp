# Find OceanBase Connector/C (libobclient) or fall back to libmysqlclient.
#
# Sets:
#   OBClient_FOUND
#   OBClient_INCLUDE_DIRS
#   OBClient_LIBRARIES
#   OBClient_VENDOR  ("obclient" | "mysqlclient")

find_path(OBClient_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql mariadb oceanbase
    PATHS
        /usr/include
        /usr/local/include
        /usr/oceanbase/include
)

find_library(OBClient_LIBRARY
    NAMES obclient mysqlclient mariadb
    PATHS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/oceanbase/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OBClient
    REQUIRED_VARS OBClient_LIBRARY OBClient_INCLUDE_DIR
)

if(OBClient_FOUND)
    set(OBClient_INCLUDE_DIRS ${OBClient_INCLUDE_DIR})
    set(OBClient_LIBRARIES ${OBClient_LIBRARY})
    get_filename_component(_ob_lib_name "${OBClient_LIBRARY}" NAME_WE)
    if(_ob_lib_name MATCHES "obclient")
        set(OBClient_VENDOR "obclient")
    else()
        set(OBClient_VENDOR "mysqlclient")
    endif()
    message(STATUS "OBClient: ${OBClient_LIBRARIES} (vendor=${OBClient_VENDOR})")
endif()

mark_as_advanced(OBClient_INCLUDE_DIR OBClient_LIBRARY)
