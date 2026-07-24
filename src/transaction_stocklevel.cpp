#include "transactions.h"
#include "coro_traits.h"

#include "constants.h"
#include "db/queries.h"
#include "log.h"
#include "util.h"

#include <string>

namespace NTPCC {

//-----------------------------------------------------------------------------

TFuture<bool> GetStockLevelTask(
    TTransactionContext& context,
    std::chrono::microseconds& latency,
    ObSession& session)
{
    auto startTs = std::chrono::steady_clock::now();

    TTransactionInflightGuard guard;
    co_await TTaskReady(context.TaskQueue, context.TerminalID);

    struct TInputs {
        int WarehouseID;
        int DistrictID;
        int Threshold;
    };

    const auto& in = FixedTransactionInputs<TInputs>(context, [&] {
        return TInputs{
            .WarehouseID = static_cast<int>(context.WarehouseID),
            .DistrictID = static_cast<int>(RandomNumber(DISTRICT_LOW_ID, DISTRICT_HIGH_ID)),
            .Threshold = static_cast<int>(RandomNumber(10, 20)),
        };
    });

    const int warehouseID = in.WarehouseID;
    const int districtID = in.DistrictID;
    const int threshold = in.Threshold;

    LOG_T("Terminal {} started StockLevel: W={}, D={}", context.TerminalID, warehouseID, districtID);

    // Get next order ID from district
    auto distFuture = session.ExecuteQuery(
        QueryId::StockLevelSelectDistrict,
        MakeParams(warehouseID, districtID));
    auto distResult = co_await TSuspendWithFuture(std::move(distFuture), context.TaskQueue, context.TerminalID);

    if (!distResult.TryNextRow()) {
        LOG_E("Terminal {} district not found: W={}, D={}", context.TerminalID, warehouseID, districtID);
        RequestStopWithError();
        co_return false;
    }
    int nextOrderID = distResult.GetInt32("d_next_o_id");

    // Get stock count below threshold for recent orders
    auto stockFuture = session.ExecuteQuery(
        QueryId::StockLevelCountStock,
        MakeParams(warehouseID, districtID, nextOrderID, nextOrderID - 20,
                     warehouseID, threshold));
    auto stockResult = co_await TSuspendWithFuture(std::move(stockFuture), context.TaskQueue, context.TerminalID);

    LOG_T("Terminal {} committing StockLevel", context.TerminalID);

    auto commitFuture = session.Commit();
    co_await TSuspendWithFuture(std::move(commitFuture), context.TaskQueue, context.TerminalID);

    auto endTs = std::chrono::steady_clock::now();
    latency = std::chrono::duration_cast<std::chrono::microseconds>(endTs - startTs);

    co_return true;
}

} // namespace NTPCC
