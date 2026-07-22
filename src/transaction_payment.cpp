#include "transactions.h"
#include "coro_traits.h"

#include "common_queries.h"
#include "constants.h"
#include "db/queries.h"
#include "log.h"
#include "util.h"

#include <fmt/format.h>

#include <string>

namespace NTPCC {

//-----------------------------------------------------------------------------

TFuture<bool> GetPaymentTask(
    TTransactionContext& context,
    std::chrono::microseconds& latency,
    ObSession& session)
{
    auto startTs = std::chrono::steady_clock::now();

    TTransactionInflightGuard guard;
    co_await TTaskReady(context.TaskQueue, context.TerminalID);

    const int warehouseID = context.WarehouseID;
    const int districtID = RandomNumber(DISTRICT_LOW_ID, DISTRICT_HIGH_ID);
    const double paymentAmount = static_cast<double>(RandomNumber(100, 500000)) / 100.0;

    LOG_T("Terminal {} started Payment: W={}, D={}", context.TerminalID, warehouseID, districtID);

    // Update warehouse YTD (SELECT FOR UPDATE + UPDATE; no RETURNING on OceanBase)
    auto whFuture = session.ExecuteQuery(
        QueryId::PaymentSelectWarehouseForUpdate,
        MakeParams(warehouseID));
    auto whResult = co_await TSuspendWithFuture(std::move(whFuture), context.TaskQueue, context.TerminalID);

    if (!whResult.TryNextRow()) {
        LOG_E("Terminal {} warehouse not found: W={}", context.TerminalID, warehouseID);
        RequestStopWithError();
        co_return false;
    }
    std::string warehouseName = whResult.GetString("w_name");

    co_await TSuspendWithFuture(
        session.ExecuteModify(
            QueryId::PaymentUpdateWarehouseYtd,
            MakeParams(paymentAmount, warehouseID)),
        context.TaskQueue, context.TerminalID);

    // Update district YTD
    auto distFuture = session.ExecuteQuery(
        QueryId::PaymentSelectDistrictForUpdate,
        MakeParams(warehouseID, districtID));
    auto distResult = co_await TSuspendWithFuture(std::move(distFuture), context.TaskQueue, context.TerminalID);

    if (!distResult.TryNextRow()) {
        LOG_E("Terminal {} district not found: W={}, D={}", context.TerminalID, warehouseID, districtID);
        RequestStopWithError();
        co_return false;
    }
    std::string districtName = distResult.GetString("d_name");

    co_await TSuspendWithFuture(
        session.ExecuteModify(
            QueryId::PaymentUpdateDistrictYtd,
            MakeParams(paymentAmount, warehouseID, districtID)),
        context.TaskQueue, context.TerminalID);

    // Determine customer warehouse/district
    int customerDistrictID;
    int customerWarehouseID;

    if (RandomNumber(1, 100) <= 85) {
        customerDistrictID = districtID;
        customerWarehouseID = warehouseID;
    } else {
        customerDistrictID = RandomNumber(DISTRICT_LOW_ID, DISTRICT_HIGH_ID);
        do {
            customerWarehouseID = RandomNumber(1, context.WarehouseCount);
        } while (customerWarehouseID == warehouseID && context.WarehouseCount > 1);
    }

    TCustomer customer;

    if (RandomNumber(1, 100) <= 60) {
        // Look up by last name
        std::string lastName = GetNonUniformRandomLastNameForRun();

        auto custFuture = GetCustomersByLastName(
            session, customerWarehouseID, customerDistrictID, lastName, /*forUpdate=*/true);
        auto custResult = co_await TSuspendWithFuture(std::move(custFuture), context.TaskQueue, context.TerminalID);

        auto selectedCustomer = SelectCustomerFromResultSet(custResult);
        if (!selectedCustomer) {
            LOG_E("Terminal {} no customer by name: {}", context.TerminalID, lastName);
            RequestStopWithError();
            co_return false;
        }
        customer = std::move(*selectedCustomer);
    } else {
        // Look up by ID
        int customerID = GetRandomCustomerID();

        auto custFuture = GetCustomerById(
            session, customerWarehouseID, customerDistrictID, customerID, /*forUpdate=*/true);
        auto custResult = co_await TSuspendWithFuture(std::move(custFuture), context.TaskQueue, context.TerminalID);

        if (!custResult.TryNextRow()) {
            LOG_E("Terminal {} customer not found: C={}", context.TerminalID, customerID);
            RequestStopWithError();
            co_return false;
        }
        customer = ParseCustomerFromResult(custResult);
        customer.c_id = customerID;
    }

    customer.c_balance -= paymentAmount;
    customer.c_ytd_payment += paymentAmount;
    customer.c_payment_cnt += 1;

    // Relative updates avoid lost Delivery credits if locking is delayed; customer
    // row is already locked via FOR UPDATE in the lookup helpers above.
    if (customer.c_credit == "BC") {
        // Bad credit: get and update C_DATA
        auto cDataFuture = session.ExecuteQuery(
            QueryId::PaymentSelectCustomerData,
            MakeParams(customerWarehouseID, customerDistrictID, customer.c_id));
        auto cDataResult = co_await TSuspendWithFuture(std::move(cDataFuture), context.TaskQueue, context.TerminalID);

        std::string cData;
        if (cDataResult.TryNextRow()) {
            cData = cDataResult.GetString("c_data");
        }

        std::string newData = fmt::format("{} {} {} {} {} {:.2f} | {}",
            customer.c_id, customerDistrictID, customerWarehouseID,
            districtID, warehouseID, paymentAmount, cData);
        if (newData.length() > 500) {
            newData = newData.substr(0, 500);
        }

        auto updFuture = session.ExecuteModify(
            QueryId::PaymentUpdateCustomerBc,
            MakeParams(paymentAmount, paymentAmount, newData,
                         customerWarehouseID, customerDistrictID, customer.c_id));
        co_await TSuspendWithFuture(std::move(updFuture), context.TaskQueue, context.TerminalID);
    } else {
        auto updFuture = session.ExecuteModify(
            QueryId::PaymentUpdateCustomer,
            MakeParams(paymentAmount, paymentAmount,
                         customerWarehouseID, customerDistrictID, customer.c_id));
        co_await TSuspendWithFuture(std::move(updFuture), context.TaskQueue, context.TerminalID);
    }

    // Insert history record
    std::string historyData = warehouseName + "    " + districtName;
    if (historyData.length() > 24) {
        historyData = historyData.substr(0, 24);
    }

    auto histFuture = session.ExecuteModify(
        QueryId::PaymentInsertHistory,
        MakeParams(customer.c_id, customerDistrictID, customerWarehouseID,
                     districtID, warehouseID, paymentAmount, historyData));
    co_await TSuspendWithFuture(std::move(histFuture), context.TaskQueue, context.TerminalID);

    LOG_T("Terminal {} committing Payment", context.TerminalID);

    auto commitFuture = session.Commit();
    co_await TSuspendWithFuture(std::move(commitFuture), context.TaskQueue, context.TerminalID);

    auto endTs = std::chrono::steady_clock::now();
    latency = std::chrono::duration_cast<std::chrono::microseconds>(endTs - startTs);

    co_return true;
}

} // namespace NTPCC
