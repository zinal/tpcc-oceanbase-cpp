#include "transactions.h"
#include "coro_traits.h"

#include "constants.h"
#include "db/queries.h"
#include "log.h"
#include "util.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace NTPCC {

namespace {

struct TOrderData {
    int OrderID = 0;
    int CustomerId = 0;
    double TotalAmount = 0;
    std::vector<int> OrderLineNumbers;
};

} // anonymous

//-----------------------------------------------------------------------------

TFuture<bool> GetDeliveryTask(
    TTransactionContext& context,
    std::chrono::microseconds& latency,
    ObSession& session)
{
    auto startTs = std::chrono::steady_clock::now();

    TTransactionInflightGuard guard;
    co_await TTaskReady(context.TaskQueue, context.TerminalID);

    struct TInputs {
        int WarehouseID;
        int CarrierID;
    };

    const auto& in = FixedTransactionInputs<TInputs>(context, [&] {
        return TInputs{
            .WarehouseID = static_cast<int>(context.WarehouseID),
            .CarrierID = static_cast<int>(RandomNumber(1, 10)),
        };
    });

    const int warehouseID = in.WarehouseID;
    const int carrierID = in.CarrierID;

    LOG_T("Terminal {} started Delivery: W={}", context.TerminalID, warehouseID);

    std::array<std::optional<TOrderData>, DISTRICT_COUNT> orders;

    for (int districtID = DISTRICT_LOW_ID; districtID <= DISTRICT_HIGH_ID; ++districtID) {
        // Get oldest new order (FOR UPDATE avoids concurrent delivery double-credit).
        auto noFuture = session.ExecuteQuery(
            QueryId::DeliverySelectNewOrderForUpdate,
            MakeParams(districtID, warehouseID));
        auto noResult = co_await TSuspendWithFuture(std::move(noFuture), context.TaskQueue, context.TerminalID);

        if (!noResult.TryNextRow()) {
            LOG_T("Terminal {} no new orders for district {}", context.TerminalID, districtID);
            continue;
        }

        auto& order = orders[districtID - DISTRICT_LOW_ID].emplace();
        order.OrderID = noResult.GetInt32("no_o_id");

        // Get customer ID from order
        auto cidFuture = session.ExecuteQuery(
            QueryId::DeliverySelectOrderCustomer,
            MakeParams(warehouseID, districtID, order.OrderID));
        auto cidResult = co_await TSuspendWithFuture(std::move(cidFuture), context.TaskQueue, context.TerminalID);

        if (!cidResult.TryNextRow()) {
            LOG_E("Terminal {} order not found: W={}, D={}, O={}", context.TerminalID, warehouseID, districtID, order.OrderID);
            RequestStopWithError();
            co_return false;
        }
        order.CustomerId = cidResult.GetInt32("o_c_id");

        // Get order lines
        auto olFuture = session.ExecuteQuery(
            QueryId::DeliverySelectOrderLines,
            MakeParams(warehouseID, districtID, order.OrderID));
        auto olResult = co_await TSuspendWithFuture(std::move(olFuture), context.TaskQueue, context.TerminalID);

        while (olResult.TryNextRow()) {
            order.OrderLineNumbers.push_back(olResult.GetInt32("ol_number"));
            order.TotalAmount += olResult.GetDouble("ol_amount");
        }

        if (order.OrderLineNumbers.empty()) {
            LOG_E("Terminal {} no order lines: W={}, D={}, O={}", context.TerminalID, warehouseID, districtID, order.OrderID);
            RequestStopWithError();
            co_return false;
        }
    }

    // Now perform the writes for each district
    for (int districtID = DISTRICT_LOW_ID; districtID <= DISTRICT_HIGH_ID; ++districtID) {
        if (!orders[districtID - DISTRICT_LOW_ID]) continue;
        auto& order = *orders[districtID - DISTRICT_LOW_ID];

        // Delete new order
        auto delFuture = session.ExecuteModify(
            QueryId::DeliveryDeleteNewOrder,
            MakeParams(warehouseID, districtID, order.OrderID));
        co_await TSuspendWithFuture(std::move(delFuture), context.TaskQueue, context.TerminalID);

        // Update carrier ID
        auto updFuture = session.ExecuteModify(
            QueryId::DeliveryUpdateCarrier,
            MakeParams(carrierID, warehouseID, districtID, order.OrderID));
        co_await TSuspendWithFuture(std::move(updFuture), context.TaskQueue, context.TerminalID);

        // Update delivery date on order lines
        auto updOlFuture = session.ExecuteModify(
            QueryId::DeliveryUpdateOrderLineDelivery,
            MakeParams(warehouseID, districtID, order.OrderID));
        co_await TSuspendWithFuture(std::move(updOlFuture), context.TaskQueue, context.TerminalID);

        // Update customer balance and delivery count
        auto updCustFuture = session.ExecuteModify(
            QueryId::DeliveryUpdateCustomerBalance,
            MakeParams(order.TotalAmount, warehouseID, districtID, order.CustomerId));
        co_await TSuspendWithFuture(std::move(updCustFuture), context.TaskQueue, context.TerminalID);
    }

    LOG_T("Terminal {} committing Delivery", context.TerminalID);

    auto commitFuture = session.Commit();
    co_await TSuspendWithFuture(std::move(commitFuture), context.TaskQueue, context.TerminalID);

    auto endTs = std::chrono::steady_clock::now();
    latency = std::chrono::duration_cast<std::chrono::microseconds>(endTs - startTs);

    co_return true;
}

} // namespace NTPCC
