#include "import.h"

#include "constants.h"
#include "db/connection.h"
#include "db/session.h"
#include "init.h"
#include "log.h"
#include "thread_pool.h"
#include "util.h"

#ifdef TPCC_HAS_TUI
#include "import_tui.h"
#include "log_backend.h"
#endif

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

namespace NTPCC {

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t MAX_LOADER_THREADS = 100;

// Rough per-row byte estimates for progress tracking (not exact, but close enough for TUI)
constexpr size_t BYTES_PER_ITEM = 40;
constexpr size_t BYTES_PER_STOCK = 280;
constexpr size_t BYTES_PER_CUSTOMER = 600;
constexpr size_t BYTES_PER_HISTORY = 46;
constexpr size_t BYTES_PER_ORDER = 48;
constexpr size_t BYTES_PER_NEW_ORDER = 12;
constexpr size_t BYTES_PER_ORDER_LINE = 54;
constexpr size_t AVG_ORDER_LINES_PER_ORDER = 10;
constexpr size_t NEW_ORDERS_PER_DISTRICT = CUSTOMERS_PER_DISTRICT - FIRST_UNPROCESSED_O_ID + 1;

class InlineExecutor : public IExecutor {
public:
    void Submit(std::function<void()> task) override {
        task();
    }
};

ObConnectionConfig MakeConfig(const TImportConfig& config) {
    auto cfg = ParseConnectionString(config.ConnectionString);
    if (!config.Path.empty()) {
        cfg.path = config.Path;
    }
    return cfg;
}

void SetSessionQueryTimeout(ObSession& session, int64_t timeoutUs) {
    try {
        session.ExecuteNonTx(fmt::format("SET SESSION ob_query_timeout = {}", timeoutUs)).Get();
    } catch (const std::exception& ex) {
        // MariaDB stand-in and other MySQL-compat servers may not have this variable.
        LOG_W("Could not set ob_query_timeout ({}); continuing with server default", ex.what());
    }
}

int64_t ImportQueryTimeoutMicros() {
    // OceanBase default ob_query_timeout is 10s; bulk TPC-C loads (e.g. 100k stock rows) need more.
    constexpr int64_t kMinSec = 600;
    return kMinSec * 1'000'000;
}

ObSession OpenSession(const TImportConfig& config, InlineExecutor& executor) {
    auto conn = ObConnection::Connect(MakeConfig(config));
    ObSession session(std::move(conn), &executor);
    SetSessionQueryTimeout(session, ImportQueryTimeoutMicros());
    return session;
}

void BulkInsert(ObSession& session,
                const std::string& table,
                const std::vector<std::string>& columns,
                BulkWriter writer) {
    session.ExecuteBulk(table, columns, std::move(writer)).Get();
    session.Commit().Get();
}

std::optional<std::string> Cell(int v) {
    return std::to_string(v);
}

std::optional<std::string> Cell(double v) {
    return fmt::format("{}", v);
}

std::optional<std::string> Cell(const std::string& v) {
    return v;
}

std::optional<std::string> Cell(std::optional<int> v) {
    if (!v) {
        return std::nullopt;
    }
    return std::to_string(*v);
}

std::optional<std::string> Cell(std::optional<std::string> v) {
    return v;
}

size_t EstimateSharedDataSize() {
    return ITEM_COUNT * BYTES_PER_ITEM;
}

size_t EstimatePerWarehouseDataSize() {
    size_t stock = ITEM_COUNT * BYTES_PER_STOCK;
    size_t perDistrict =
        CUSTOMERS_PER_DISTRICT * BYTES_PER_CUSTOMER +
        CUSTOMERS_PER_DISTRICT * BYTES_PER_HISTORY +
        CUSTOMERS_PER_DISTRICT * BYTES_PER_ORDER +
        NEW_ORDERS_PER_DISTRICT * BYTES_PER_NEW_ORDER +
        CUSTOMERS_PER_DISTRICT * AVG_ORDER_LINES_PER_ORDER * BYTES_PER_ORDER_LINE;
    return stock + DISTRICT_COUNT * perDistrict;
}

//-----------------------------------------------------------------------------

std::string RandomStringBenchbase(int strLen, char baseChar = 'a') {
    if (strLen > 1) {
        int actualLength = strLen - 1;
        std::string result;
        result.reserve(actualLength);
        for (int i = 0; i < actualLength; ++i) {
            result += static_cast<char>(baseChar + RandomNumber(0, 25));
        }
        return result;
    }
    return "";
}

std::string RandomAlphaString(int minLength, int maxLength) {
    int length = static_cast<int>(RandomNumber(minLength, maxLength));
    return RandomStringBenchbase(length, 'a');
}

std::string RandomUpperAlphaString(int minLength, int maxLength) {
    int length = static_cast<int>(RandomNumber(minLength, maxLength));
    return RandomStringBenchbase(length, 'A');
}

std::string RandomNumericString(int length) {
    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; ++i) {
        result += static_cast<char>('0' + RandomNumber(0, 9));
    }
    return result;
}

size_t HashCustomer(int warehouseId, int districtId, int customerId) {
    return std::hash<int>{}(warehouseId) ^
           (std::hash<int>{}(districtId) << 1) ^
           (std::hash<int>{}(customerId) << 2);
}

int GetRandomCount(int warehouseId, int customerId, int districtId) {
    size_t seed = HashCustomer(warehouseId, districtId, customerId);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(5, 15);
    return dist(rng);
}

std::string CurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&time_t_now, &tm_buf);
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

//-----------------------------------------------------------------------------

void LoadItems(ObSession& session) {
    LOG_I("Loading {} items...", ITEM_COUNT);

    BulkInsert(session, "item",
        {"i_id", "i_name", "i_price", "i_data", "i_im_id"},
        [](auto emit) {
            for (int i = 1; i <= ITEM_COUNT; ++i) {
                std::string data;
                int randPct = static_cast<int>(RandomNumber(1, 100));
                int len = static_cast<int>(RandomNumber(26, 50));
                if (randPct > 10) {
                    data = RandomStringBenchbase(len);
                } else {
                    int startOrig = static_cast<int>(RandomNumber(2, len - 8));
                    data = RandomStringBenchbase(startOrig) + "ORIGINAL" +
                           RandomStringBenchbase(len - startOrig - 8);
                }

                emit(BulkRow{
                    Cell(i),
                    Cell(RandomAlphaString(14, 24)),
                    Cell(RandomNumber(100, 10000) / 100.0),
                    Cell(data),
                    Cell(static_cast<int>(RandomNumber(1, 10000))),
                });
            }
        });

    LOG_I("Items loaded");
}

void LoadWarehouses(ObSession& session, int startId, int lastId) {
    LOG_I("Loading warehouses {} to {}", startId, lastId);

    BulkInsert(session, "warehouse",
        {"w_id", "w_ytd", "w_tax", "w_name", "w_street_1", "w_street_2",
         "w_city", "w_state", "w_zip"},
        [startId, lastId](auto emit) {
            for (int wh = startId; wh <= lastId; ++wh) {
                emit(BulkRow{
                    Cell(wh),
                    Cell(DISTRICT_INITIAL_YTD * DISTRICT_COUNT),
                    Cell(RandomNumber(0, 2000) / 10000.0),
                    Cell(RandomAlphaString(6, 10)),
                    Cell(RandomAlphaString(10, 20)),
                    Cell(RandomAlphaString(10, 20)),
                    Cell(RandomAlphaString(10, 20)),
                    Cell(RandomUpperAlphaString(3, 3)),
                    Cell(std::string("123456789")),
                });
            }
        });
}

void LoadDistricts(ObSession& session, int startId, int lastId) {
    LOG_I("Loading districts for warehouses {} to {}", startId, lastId);

    BulkInsert(session, "district",
        {"d_w_id", "d_id", "d_ytd", "d_tax", "d_next_o_id", "d_name",
         "d_street_1", "d_street_2", "d_city", "d_state", "d_zip"},
        [startId, lastId](auto emit) {
            for (int wh = startId; wh <= lastId; ++wh) {
                for (int d = DISTRICT_LOW_ID; d <= DISTRICT_HIGH_ID; ++d) {
                    emit(BulkRow{
                        Cell(wh),
                        Cell(d),
                        Cell(DISTRICT_INITIAL_YTD),
                        Cell(RandomNumber(0, 2000) / 10000.0),
                        Cell(CUSTOMERS_PER_DISTRICT + 1),
                        Cell(RandomAlphaString(6, 10)),
                        Cell(RandomAlphaString(10, 20)),
                        Cell(RandomAlphaString(10, 20)),
                        Cell(RandomAlphaString(10, 20)),
                        Cell(RandomUpperAlphaString(3, 3)),
                        Cell(std::string("123456789")),
                    });
                }
            }
        });
}

void LoadStock(ObSession& session, int wh) {
    LOG_D("Loading stock for warehouse {}", wh);

    BulkInsert(session, "stock",
        {"s_w_id", "s_i_id", "s_quantity", "s_ytd", "s_order_cnt", "s_remote_cnt",
         "s_data", "s_dist_01", "s_dist_02", "s_dist_03", "s_dist_04", "s_dist_05",
         "s_dist_06", "s_dist_07", "s_dist_08", "s_dist_09", "s_dist_10"},
        [wh](auto emit) {
            for (int itemId = 1; itemId <= ITEM_COUNT; ++itemId) {
                std::string data;
                int randPct = static_cast<int>(RandomNumber(1, 100));
                int len = static_cast<int>(RandomNumber(26, 50));
                if (randPct > 10) {
                    data = RandomStringBenchbase(len);
                } else {
                    int startOrig = static_cast<int>(RandomNumber(2, len - 8));
                    data = RandomStringBenchbase(startOrig) + "ORIGINAL" +
                           RandomStringBenchbase(len - startOrig - 8);
                }

                emit(BulkRow{
                    Cell(wh),
                    Cell(itemId),
                    Cell(static_cast<int>(RandomNumber(10, 100))),
                    Cell(0.0),
                    Cell(0),
                    Cell(0),
                    Cell(data),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                    Cell(RandomStringBenchbase(24)),
                });
            }
        });
}

void LoadCustomers(ObSession& session, int wh, int district) {
    LOG_D("Loading customers for warehouse {} district {}", wh, district);

    auto ts = CurrentTimestamp();

    BulkInsert(session, "customer",
        {"c_w_id", "c_d_id", "c_id", "c_discount", "c_credit", "c_last", "c_first",
         "c_credit_lim", "c_balance", "c_ytd_payment", "c_payment_cnt", "c_delivery_cnt",
         "c_street_1", "c_street_2", "c_city", "c_state", "c_zip", "c_phone",
         "c_since", "c_middle", "c_data"},
        [wh, district, ts](auto emit) {
            for (int cid = C_FIRST_CUSTOMER_ID; cid <= CUSTOMERS_PER_DISTRICT; ++cid) {
                std::string last;
                if (cid <= 1000) {
                    last = GetLastName(cid - 1);
                } else {
                    last = GetNonUniformRandomLastNameForLoad();
                }

                std::string credit = RandomNumber(1, 100) <= 10 ? "BC" : "GC";

                emit(BulkRow{
                    Cell(wh),
                    Cell(district),
                    Cell(cid),
                    Cell(RandomNumber(1, 5000) / 10000.0),
                    Cell(credit),
                    Cell(last),
                    Cell(RandomAlphaString(8, 16)),
                    Cell(50000.00),
                    Cell(-10.00),
                    Cell(10.00),
                    Cell(1),
                    Cell(0),
                    Cell(RandomAlphaString(10, 20)),
                    Cell(RandomAlphaString(10, 20)),
                    Cell(RandomAlphaString(10, 20)),
                    Cell(RandomUpperAlphaString(3, 3)),
                    Cell(RandomNumericString(4) + "11111"),
                    Cell(RandomNumericString(16)),
                    Cell(ts),
                    Cell(std::string("OE")),
                    Cell(RandomAlphaString(300, 500)),
                });
            }
        });
}

void LoadHistory(ObSession& session, int wh, int district) {
    LOG_D("Loading history for warehouse {} district {}", wh, district);

    auto ts = CurrentTimestamp();

    BulkInsert(session, "history",
        {"h_c_id", "h_c_d_id", "h_c_w_id", "h_d_id", "h_w_id", "h_date", "h_amount", "h_data"},
        [wh, district, ts](auto emit) {
            for (int cid = C_FIRST_CUSTOMER_ID; cid <= CUSTOMERS_PER_DISTRICT; ++cid) {
                emit(BulkRow{
                    Cell(cid),
                    Cell(district),
                    Cell(wh),
                    Cell(district),
                    Cell(wh),
                    Cell(ts),
                    Cell(10.00),
                    Cell(RandomAlphaString(10, 24)),
                });
            }
        });
}

void LoadOrders(ObSession& session, int wh, int district) {
    LOG_D("Loading orders for warehouse {} district {}", wh, district);

    // Generate shuffled customer IDs (TPC-C 4.3.3.1)
    std::vector<int> customerIds;
    customerIds.reserve(CUSTOMERS_PER_DISTRICT);
    for (int i = 1; i <= CUSTOMERS_PER_DISTRICT; ++i) {
        customerIds.push_back(i);
    }
    thread_local std::mt19937 rng(std::random_device{}());
    std::shuffle(customerIds.begin(), customerIds.end(), rng);

    auto ts = CurrentTimestamp();

    BulkInsert(session, "oorder",
        {"o_w_id", "o_d_id", "o_id", "o_c_id", "o_carrier_id", "o_ol_cnt",
         "o_all_local", "o_entry_d"},
        [wh, district, &customerIds, &ts](auto emit) {
            for (int oid = 1; oid <= CUSTOMERS_PER_DISTRICT; ++oid) {
                int cid = customerIds[oid - 1];
                std::optional<int> carrierId;
                if (oid < FIRST_UNPROCESSED_O_ID) {
                    carrierId = static_cast<int>(RandomNumber(1, 10));
                }
                int olCnt = GetRandomCount(wh, oid, district);

                emit(BulkRow{
                    Cell(wh),
                    Cell(district),
                    Cell(oid),
                    Cell(cid),
                    Cell(carrierId),
                    Cell(olCnt),
                    Cell(1),
                    Cell(ts),
                });
            }
        });

    BulkInsert(session, "new_order",
        {"no_w_id", "no_d_id", "no_o_id"},
        [wh, district](auto emit) {
            for (int oid = FIRST_UNPROCESSED_O_ID; oid <= CUSTOMERS_PER_DISTRICT; ++oid) {
                emit(BulkRow{Cell(wh), Cell(district), Cell(oid)});
            }
        });

    BulkInsert(session, "order_line",
        {"ol_w_id", "ol_d_id", "ol_o_id", "ol_number", "ol_i_id", "ol_delivery_d",
         "ol_amount", "ol_supply_w_id", "ol_quantity", "ol_dist_info"},
        [wh, district, &ts](auto emit) {
            for (int oid = 1; oid <= CUSTOMERS_PER_DISTRICT; ++oid) {
                int olCnt = GetRandomCount(wh, oid, district);
                for (int lineNum = 1; lineNum <= olCnt; ++lineNum) {
                    int itemId = static_cast<int>(RandomNumber(1, ITEM_COUNT));

                    std::optional<std::string> deliveryDate;
                    double amount;
                    if (oid < FIRST_UNPROCESSED_O_ID) {
                        deliveryDate = ts;
                        amount = 0.0;
                    } else {
                        amount = RandomNumber(1, 999999) / 100.0;
                    }

                    emit(BulkRow{
                        Cell(wh),
                        Cell(district),
                        Cell(oid),
                        Cell(lineNum),
                        Cell(itemId),
                        Cell(deliveryDate),
                        Cell(amount),
                        Cell(wh),
                        Cell(5.0),
                        Cell(RandomStringBenchbase(24)),
                    });
                }
            }
        });
}

void LoadWarehouse(ObSession& session, int wh, TImportState& state) {
    if (state.StopToken.stop_requested()) return;

    LoadStock(session, wh);
    state.DataSizeLoaded.fetch_add(
        ITEM_COUNT * BYTES_PER_STOCK, std::memory_order_relaxed);

    for (int d = DISTRICT_LOW_ID; d <= DISTRICT_HIGH_ID; ++d) {
        if (state.StopToken.stop_requested()) return;

        LoadCustomers(session, wh, d);
        LoadHistory(session, wh, d);
        LoadOrders(session, wh, d);

        size_t districtBytes =
            CUSTOMERS_PER_DISTRICT * BYTES_PER_CUSTOMER +
            CUSTOMERS_PER_DISTRICT * BYTES_PER_HISTORY +
            CUSTOMERS_PER_DISTRICT * BYTES_PER_ORDER +
            NEW_ORDERS_PER_DISTRICT * BYTES_PER_NEW_ORDER +
            CUSTOMERS_PER_DISTRICT * AVG_ORDER_LINES_PER_ORDER * BYTES_PER_ORDER_LINE;
        state.DataSizeLoaded.fetch_add(districtBytes, std::memory_order_relaxed);
    }

    state.WarehousesLoaded.fetch_add(1, std::memory_order_relaxed);
}

int64_t AnalyzeQueryTimeoutMicros(size_t warehouseCount) {
    // OceanBase default ob_query_timeout is 10s; ANALYZE on large TPC-C tables needs more.
    constexpr int64_t kMinSec = 600;
    constexpr int64_t kSecPerWarehouse = 120;
    const int64_t timeoutSec =
        std::max(kMinSec, static_cast<int64_t>(warehouseCount) * kSecPerWarehouse);
    return timeoutSec * 1'000'000;
}

void SetAnalyzeQueryTimeout(ObSession& session, size_t warehouseCount) {
    SetSessionQueryTimeout(session, AnalyzeQueryTimeoutMicros(warehouseCount));
}

void AnalyzeTables(const TImportConfig& config) {
    LOG_I("Running ANALYZE TABLE on TPC-C tables...");
    InlineExecutor executor;
    auto session = OpenSession(config, executor);
    SetAnalyzeQueryTimeout(session, config.WarehouseCount);
    for (const auto* table : TPCC_TABLES) {
        LOG_I("Analyzing table `{}`...", table);
        // ANALYZE TABLE returns a result set; ExecuteNonTx consumes it.
        session.ExecuteNonTx(fmt::format("ANALYZE TABLE `{}`", table)).Get();
    }
}

} // anonymous

//-----------------------------------------------------------------------------

void ImportSync(const TImportConfig& config) {
    if (config.WarehouseCount == 0) {
        LOG_E("Specified zero warehouses");
        throw std::runtime_error("Warehouse count must be greater than zero");
    }

    size_t threadCount = config.LoadThreadCount;
    if (threadCount == 0) {
        threadCount = std::min({config.WarehouseCount, NumberOfMyCpus(), MAX_LOADER_THREADS});
    }
    threadCount = std::max(threadCount, size_t(1));
    threadCount = std::min(threadCount, config.WarehouseCount);

    LOG_I("Starting TPC-C data import for {} warehouses using {} threads",
          config.WarehouseCount, threadCount);

    auto startTime = Clock::now();

    TImportState state{GetGlobalInterruptSource().get_token()};
    state.ApproximateDataSize =
        EstimateSharedDataSize() + config.WarehouseCount * EstimatePerWarehouseDataSize();

    // Load small / shared tables on the main connection.
    {
        InlineExecutor executor;
        auto session = OpenSession(config, executor);
        LoadItems(session);
        state.DataSizeLoaded.fetch_add(EstimateSharedDataSize(), std::memory_order_relaxed);
        LoadWarehouses(session, 1, static_cast<int>(config.WarehouseCount));
        LoadDistricts(session, 1, static_cast<int>(config.WarehouseCount));
    }

    // Load per-warehouse data in parallel (one Connector/C connection per thread).
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (size_t tid = 0; tid < threadCount; ++tid) {
        int whStart = static_cast<int>(tid * config.WarehouseCount / threadCount + 1);
        int whEnd = static_cast<int>((tid + 1) * config.WarehouseCount / threadCount);

        threads.emplace_back([&config, &state, whStart, whEnd]() {
            try {
                InlineExecutor executor;
                auto session = OpenSession(config, executor);
                for (int wh = whStart; wh <= whEnd; ++wh) {
                    if (state.StopToken.stop_requested()) return;
                    LoadWarehouse(session, wh, state);

                    LOG_I("Warehouse {} loaded ({}/{})",
                          wh, state.WarehousesLoaded.load(), config.WarehouseCount);
                }
            } catch (const std::exception& ex) {
                LOG_E("Import thread failed: {}", ex.what());
                RequestStopWithError();
            }
        });
    }

#ifdef TPCC_HAS_TUI
    TLogCapture logCapture(TUI_LOG_LINES);
    std::unique_ptr<TImportTui> tui;
    if (config.UseTui) {
        StartLogCapture(logCapture);
        TImportDisplayData initData(state);
        tui = std::make_unique<TImportTui>(
            logCapture, config.WarehouseCount, threadCount, initData);
    }
#endif

    {
#ifdef TPCC_HAS_TUI
        size_t prevLoaded = state.DataSizeLoaded.load(std::memory_order_relaxed);
        auto prevTime = Clock::now();
#endif

        while (state.WarehousesLoaded.load(std::memory_order_relaxed) < config.WarehouseCount
               && !state.StopToken.stop_requested())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

#ifdef TPCC_HAS_TUI
            if (tui) {
                auto now = Clock::now();
                auto elapsed = std::chrono::duration<double>(now - startTime);
                size_t loaded = state.DataSizeLoaded.load(std::memory_order_relaxed);

                TImportDisplayData data(state);
                auto& s = data.StatusData;
                s.CurrentDataSizeLoaded = loaded;
                s.PercentLoaded = state.ApproximateDataSize > 0
                    ? 100.0 * loaded / state.ApproximateDataSize : 0;

                auto sincePrev = std::chrono::duration<double>(now - prevTime);
                if (sincePrev.count() > 0.01) {
                    s.InstantSpeedMiBs =
                        (loaded - prevLoaded) / (1024.0 * 1024.0) / sincePrev.count();
                }
                if (elapsed.count() > 0.01) {
                    s.AvgSpeedMiBs = loaded / (1024.0 * 1024.0) / elapsed.count();
                }

                int totalSec = static_cast<int>(elapsed.count());
                s.ElapsedMinutes = totalSec / 60;
                s.ElapsedSeconds = totalSec % 60;

                if (s.AvgSpeedMiBs > 0.01 && state.ApproximateDataSize > loaded) {
                    double remainSec =
                        (state.ApproximateDataSize - loaded) / (s.AvgSpeedMiBs * 1024 * 1024);
                    int etaSec = static_cast<int>(remainSec);
                    s.EstimatedTimeLeftMinutes = etaSec / 60;
                    s.EstimatedTimeLeftSeconds = etaSec % 60;
                }

                tui->Update(data);
                prevLoaded = loaded;
                prevTime = now;
            }
#endif
        }
    }

    bool wasInterrupted = GetGlobalInterruptSource().stop_requested();

#ifdef TPCC_HAS_TUI
    tui.reset();
    StopLogCapture();
#endif

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    if (wasInterrupted) {
        throw std::runtime_error("Import was interrupted or failed. See logs.");
    }

    AnalyzeTables(config);
    CreateIndexes(config.ConnectionString, config.Path);

    auto elapsed = Clock::now() - startTime;
    auto seconds = std::chrono::duration<double>(elapsed).count();
    LOG_I("TPC-C data import completed successfully in {:.1f}s", seconds);
}

} // namespace NTPCC
