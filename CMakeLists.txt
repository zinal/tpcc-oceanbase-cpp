cmake_minimum_required(VERSION 3.20)
project(tpcc-oceanbase LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "This project is designed for Clang 16+. Current compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# ---------------------------------------------------------------------------
# Bundled dependencies
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

# OceanBase Connector/C is required for the tpcc binary, but optional so that
# pure unit tests (tpcc_tests) can configure/build in CI without LibOBClient.
option(TPCC_REQUIRE_OBCLIENT "Fail configure if OceanBase Connector/C is missing" OFF)
find_package(OBClient)
if(TPCC_REQUIRE_OBCLIENT AND NOT OBClient_FOUND)
    message(FATAL_ERROR
        "OceanBase Connector/C (libobclient/libobclnt) is required "
        "(TPCC_REQUIRE_OBCLIENT=ON). See docs/LIBOBCLIENT.md")
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL)

# ---------------------------------------------------------------------------
# Main binary (needs LibOBClient)
# ---------------------------------------------------------------------------

set(TPCC_SOURCES
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
    src/query_result.h
    src/db/params.h
    src/db/query_result.h
    src/db/errors.h
    src/db/connection.h
    src/db/connection.cpp
    src/db/session.h
    src/db/session.cpp
    src/db/connection_pool.h
    src/db/connection_pool.cpp
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
    src/log_backend.h
    src/log_backend.cpp
    src/main.cpp
)

if(TPCC_HAS_TUI)
    list(APPEND TPCC_SOURCES
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

if(OBClient_FOUND)
    add_executable(tpcc ${TPCC_SOURCES})

    target_include_directories(tpcc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OBClient_INCLUDE_DIRS}
    )

    target_link_libraries(tpcc PRIVATE
        ${OBClient_LIBRARIES}
        gflags
        spdlog::spdlog
        fmt::fmt
        Threads::Threads
        dl
        m
    )

    if(OpenSSL_FOUND)
        target_link_libraries(tpcc PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()

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

    # Optional tcmalloc
    set(TPCC_USE_TCMALLOC "AUTO" CACHE STRING "Link tcmalloc into the tpcc binary: AUTO, ON, or OFF")
    set_property(CACHE TPCC_USE_TCMALLOC PROPERTY STRINGS AUTO ON OFF)

    if(NOT TPCC_USE_TCMALLOC STREQUAL "OFF")
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
else()
    message(STATUS
        "OceanBase Connector/C not found: skipping tpcc / tpcc_ob_tests. "
        "Install LibOBClient (docs/LIBOBCLIENT.md) or pass -DTPCC_REQUIRE_OBCLIENT=ON")
endif()

# ---------------------------------------------------------------------------
# coroutines_vs_threads
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

    if(OBClient_FOUND)
        add_executable(tpcc_ob_tests
            src/ut/ob_connection_ut.cpp
            src/ut/ob_ddl_ut.cpp
            src/ut/ob_import_ut.cpp
            src/ut/ob_transaction_ut.cpp
            src/ut/ob_check_ut.cpp
            src/db/connection.cpp
            src/db/session.cpp
            src/db/connection_pool.cpp
            src/init.cpp
            src/clean.cpp
            src/import.cpp
            src/check.cpp
            src/path_checker.cpp
            src/common_queries.cpp
            src/transaction_neworder.cpp
            src/transaction_delivery.cpp
            src/transaction_payment.cpp
            src/transaction_orderstatus.cpp
            src/transaction_stocklevel.cpp
            src/transaction_simulation.cpp
            ${TPCC_TEST_LIB_SOURCES}
        )
        target_include_directories(tpcc_ob_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${OBClient_INCLUDE_DIRS}
        )
        target_link_libraries(tpcc_ob_tests PRIVATE
            GTest::gtest
            GTest::gtest_main
            ${OBClient_LIBRARIES}
            spdlog::spdlog
            fmt::fmt
            Threads::Threads
            dl
            m
        )
        if(OpenSSL_FOUND)
            target_link_libraries(tpcc_ob_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        endif()
        target_compile_options(tpcc_ob_tests PRIVATE -Wall -Wextra -Wpedantic)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(tpcc_ob_tests PRIVATE -fcoroutines)
        endif()
        add_test(NAME tpcc_ob_tests COMMAND tpcc_ob_tests)
        # Import w=1 loads full TPC-C scale (100k items/stock); allow several minutes.
        set_tests_properties(tpcc_ob_tests PROPERTIES LABELS "ob" TIMEOUT 900)
        message(STATUS "Test target 'tpcc_ob_tests' enabled")
    endif()
else()
    message(STATUS "third_party/googletest not found: tests disabled")
endif()

if(TARGET tpcc)
    add_test(
        NAME smoke_test
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_test.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(smoke_test PROPERTIES
        LABELS "ob"
        TIMEOUT 900
        ENVIRONMENT "TPCC_BIN=$<TARGET_FILE:tpcc>;TPCC_WAREHOUSES=1;TPCC_DURATION_SECONDS=30"
    )
endif()
