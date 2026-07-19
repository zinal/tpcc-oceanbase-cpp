cmake_minimum_required(VERSION 3.20)
project(tpcc-oceanbase LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Require clang
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "This project is designed for Clang 16+. Current compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# ---------------------------------------------------------------------------
# Bundled dependencies (git submodules under third_party/)
# libpqxx removed — runtime client is OceanBase Connector/C (libobclient) only.
# ---------------------------------------------------------------------------

set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/fmt)

set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/spdlog)

set(GFLAGS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/gflags)

# ftxui (optional TUI)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ftxui/CMakeLists.txt")
    set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/ftxui)
    set(TPCC_HAS_TUI ON)
    message(STATUS "ftxui found: TUI support enabled")
else()
    set(TPCC_HAS_TUI OFF)
    message(STATUS "ftxui not found: TUI support disabled")
endif()

find_package(OBClient)

# ---------------------------------------------------------------------------
# Core / non-DB sources (always useful)
# ---------------------------------------------------------------------------

set(TPCC_CORE_SOURCES
    src/constants.h
    src/util.h
    src/util.cpp
    src/log.h
    src/histogram.h
    src/histogram.cpp
    src/future.h
    src/spinlock.h
    src/thread_pool.h
    src/coro_traits.h
    src/task_queue.h
    src/task_queue.cpp
    src/timer_queue.h
    src/spsc_circular_queue.h
    src/log_backend.h
    src/log_backend.cpp
)

# Baseline PostgreSQL DB layer (imported from upstream; to be replaced by src/db/)
set(TPCC_PG_BASELINE_SOURCES
    src/pg_session.h
    src/pg_session.cpp
    src/query_result.h
    src/pg_connection_pool.h
    src/pg_connection_pool.cpp
)

# Target OceanBase DB layer (headers ready; .cpp in Phase 1)
set(TPCC_OB_DB_SOURCES
    src/db/params.h
    src/db/query_result.h
    src/db/errors.h
    src/db/session.h
    src/db/connection_pool.h
)

set(TPCC_APP_SOURCES
    src/init.h
    src/init.cpp
    src/import.h
    src/import.cpp
    src/clean.h
    src/clean.cpp
    src/check.h
    src/check.cpp
    src/transactions.h
    src/common_queries.h
    src/common_queries.cpp
    src/transaction_neworder.cpp
    src/transaction_delivery.cpp
    src/transaction_payment.cpp
    src/transaction_orderstatus.cpp
    src/transaction_stocklevel.cpp
    src/transaction_simulation.cpp
    src/terminal.h
    src/terminal.cpp
    src/runner.h
    src/runner.cpp
    src/runner_display_data.h
    src/path_checker.h
    src/path_checker.cpp
    src/main.cpp
)

if(TPCC_HAS_TUI)
    list(APPEND TPCC_APP_SOURCES
        src/tui_base.h
        src/tui_base.cpp
        src/runner_tui.h
        src/runner_tui.cpp
        src/import_tui.h
        src/import_tui.cpp
        src/import_display_data.h
        src/scroller.h
        src/scroller.cpp
        src/logs_scroller.h
        src/logs_scroller.cpp
    )
endif()

# ---------------------------------------------------------------------------
# Main binary — enabled once ObSession implementation exists
# ---------------------------------------------------------------------------

set(TPCC_OB_SESSION_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/db/session.cpp")
set(TPCC_OB_POOL_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/db/connection_pool.cpp")

if(EXISTS "${TPCC_OB_SESSION_CPP}" AND EXISTS "${TPCC_OB_POOL_CPP}" AND OBClient_FOUND)
    add_executable(tpcc
        ${TPCC_CORE_SOURCES}
        ${TPCC_OB_DB_SOURCES}
        ${TPCC_OB_SESSION_CPP}
        ${TPCC_OB_POOL_CPP}
        ${TPCC_APP_SOURCES}
    )

    target_include_directories(tpcc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OBClient_INCLUDE_DIRS}
    )

    target_link_libraries(tpcc PRIVATE
        ${OBClient_LIBRARIES}
        gflags
        spdlog::spdlog
        fmt::fmt
    )

    if(TPCC_HAS_TUI)
        target_link_libraries(tpcc PRIVATE
            ftxui::screen
            ftxui::dom
            ftxui::component
        )
        target_compile_definitions(tpcc PRIVATE TPCC_HAS_TUI=1)
    endif()

    target_compile_options(tpcc PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(tpcc PRIVATE -fcoroutines)
    endif()

    message(STATUS "tpcc: OceanBase Connector/C build enabled")
else()
    message(STATUS
        "tpcc binary not configured yet — complete Phase 1 in docs/PORTING_PLAN.md "
        "(implement src/db/*.cpp, install OceanBase Connector/C / libobclient; "
        "libmysqlclient is not accepted). "
        "Temporary PostgreSQL baseline under src/pg_*.cpp will be deleted in Phase 1.")
endif()

# Optional: link tcmalloc when the main binary exists
set(TPCC_USE_TCMALLOC "AUTO" CACHE STRING "Link tcmalloc into the tpcc binary: AUTO, ON, or OFF")
set_property(CACHE TPCC_USE_TCMALLOC PROPERTY STRINGS AUTO ON OFF)

if(TARGET tpcc AND NOT TPCC_USE_TCMALLOC STREQUAL "OFF")
    if(TPCC_USE_TCMALLOC STREQUAL "ON")
        find_library(TCMALLOC_LIB NAMES tcmalloc tcmalloc_minimal REQUIRED)
    else()
        find_library(TCMALLOC_LIB NAMES tcmalloc tcmalloc_minimal)
    endif()

    if(TCMALLOC_LIB)
        target_link_libraries(tpcc PRIVATE ${TCMALLOC_LIB})
        target_compile_options(tpcc PRIVATE
            -fno-builtin-malloc
            -fno-builtin-calloc
            -fno-builtin-realloc
            -fno-builtin-free
        )
        message(STATUS "tpcc: linking tcmalloc (${TCMALLOC_LIB})")
    else()
        message(STATUS "tpcc: tcmalloc not found, using system malloc")
    endif()
endif()

# ---------------------------------------------------------------------------
# coroutines_vs_threads microbenchmark (no DB)
# ---------------------------------------------------------------------------

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/coroutines_vs_threads/main.cpp")
    add_executable(coroutines_vs_threads
        src/coroutines_vs_threads/main.cpp
        src/coroutines_vs_threads/workload.cpp
        src/histogram.cpp
        src/task_queue.cpp
        src/log_backend.cpp
        src/util.cpp
    )

    target_include_directories(coroutines_vs_threads PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/coroutines_vs_threads
    )

    target_compile_definitions(coroutines_vs_threads PRIVATE TPCC_NO_SPDLOG)
    target_link_libraries(coroutines_vs_threads PRIVATE gflags fmt::fmt)
    target_compile_options(coroutines_vs_threads PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(coroutines_vs_threads PRIVATE -fcoroutines)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest/CMakeLists.txt")
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/googletest)
    enable_testing()

    set(TPCC_TEST_LIB_SOURCES
        src/histogram.cpp
        src/task_queue.cpp
        src/log_backend.cpp
        src/util.cpp
    )

    add_executable(tpcc_tests
        src/ut/spsc_circular_queue_ut.cpp
        src/ut/histogram_ut.cpp
        src/ut/timer_queue_ut.cpp
        src/ut/task_queue_ut.cpp
        src/ut/log_capture_ut.cpp
        ${TPCC_TEST_LIB_SOURCES}
    )

    target_include_directories(tpcc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(tpcc_tests PRIVATE TPCC_NO_SPDLOG)
    target_link_libraries(tpcc_tests PRIVATE GTest::gtest GTest::gtest_main fmt::fmt)
    target_compile_options(tpcc_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(tpcc_tests PRIVATE -fcoroutines)
    endif()

    add_test(NAME tpcc_tests COMMAND tpcc_tests)
    message(STATUS "Test target 'tpcc_tests' enabled")
else()
    message(STATUS "third_party/googletest not found: tests disabled (run git submodule update --init)")
endif()

# Smoke tests are registered once the tpcc binary exists (OceanBase).
if(TARGET tpcc)
    add_test(
        NAME smoke_test
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_test.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(smoke_test PROPERTIES
        LABELS "ob"
        TIMEOUT 600
        ENVIRONMENT "TPCC_BIN=$<TARGET_FILE:tpcc>"
    )
endif()
