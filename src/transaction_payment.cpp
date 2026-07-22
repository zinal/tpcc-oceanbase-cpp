#include "transactions.h"
#include "coro_traits.h"

#include "common_queries.h"
#include "constants.h"
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

    struct TInputs {
        int WarehouseID;
        int DistrictID;
        double PaymentAmount;
        int CustomerDistrictID;
        int CustomerWarehouseID;
        bool LookupByName;
        std::string LastName;
        int CustomerID;
    };

    const auto& in = FixedTransactionInputs<TInputs>(context, [&] {
        TInputs generated;
        generated.WarehouseID = static_cast<int>(context.WarehouseID);
        generated.DistrictID = RandomNumber(DISTRICT_LOW_ID, DISTRICT_HIGH_ID);
        generated.PaymentAmount = static_cast<double>(RandomNumber(100, 500000)) / 100.0;

        if (RandomNumber(1, 100) <= 85) {
            generated.CustomerDistrictID = generated.DistrictID;
            generated.CustomerWarehouseID = generated.WarehouseID;
        } else {
            generated.CustomerDistrictID = RandomNumber(DISTRICT_LOW_ID, DISTRICT_HIGH_ID);
            do {
                generated.CustomerWarehouseID = RandomNumber(1, context.WarehouseCount);
            } while (generated.CustomerWarehouseID == generated.WarehouseID && context.WarehouseCount > 1);
        }

        generated.LookupByName = RandomNumber(1, 100) <= 60;
        if (generated.LookupByName) {
            generated.LastName = GetNonUniformRandomLastNameForRun();
            generated.CustomerID = 0;
        } else {
            generated.CustomerID = GetRandomCustomerID();
        }
        return generated;
    });

    const int warehouseID = in.WarehouseID;
    const int districtID = in.DistrictID;
    const double paymentAmount = in.PaymentAmount;
    const int customerDistrictID = in.CustomerDistrictID;
    const int customerWarehouseID = in.CustomerWarehouseID;

    LOG_T("Terminal {} started Payment: W={}, D={}", context.TerminalID, warehouseID, districtID);

    // Update warehouse YTD (SELECT FOR UPDATE + UPDATE; no RETURNING on OceanBase)
    auto whFuture = session.ExecuteQuery(
        "SELECT w_name, w_street_1, w_street_2, w_city, w_state, w_zip "
        "FROM warehouse WHERE w_id = ? FOR UPDATE",
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
            "UPDATE warehouse SET w_ytd = w_ytd + ? WHERE w_id = ?",
            MakeParams(paymentAmount, warehouseID)),
        context.TaskQueue, context.TerminalID);

    // Update district YTD
    auto distFuture = session.ExecuteQuery(
        "SELECT d_name, d_street_1, d_street_2, d_city, d_state, d_zip "
        "FROM district WHERE d_w_id = ? AND d_id = ? FOR UPDATE",
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
            "UPDATE district SET d_ytd = d_ytd + ? WHERE d_w_id = ? AND d_id = ?",
            MakeParams(paymentAmount, warehouseID, districtID)),
        context.TaskQueue, context.TerminalID);

    TCustomer customer;

    if (in.LookupByName) {
        const std::string& lastName = in.LastName;

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
        const int customerID = in.CustomerID;

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
            "SELECT c_data FROM customer WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
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
            "UPDATE customer SET c_balance = c_balance - ?, c_ytd_payment = c_ytd_payment + ?, "
            "c_payment_cnt = c_payment_cnt + 1, c_data = ? "
            "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
            MakeParams(paymentAmount, paymentAmount, newData,
                         customerWarehouseID, customerDistrictID, customer.c_id));
        co_await TSuspendWithFuture(std::move(updFuture), context.TaskQueue, context.TerminalID);
    } else {
        auto updFuture = session.ExecuteModify(
            "UPDATE customer SET c_balance = c_balance - ?, c_ytd_payment = c_ytd_payment + ?, "
            "c_payment_cnt = c_payment_cnt + 1 "
            "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
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
        "INSERT INTO history (h_c_id, h_c_d_id, h_c_w_id, h_d_id, h_w_id, h_date, h_amount, h_data) "
        "VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP, ?, ?)",
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
