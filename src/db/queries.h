#pragma once

#include <cstdint>
#include <string_view>

namespace NTPCC {

// Static query identifiers for TPC-C transactional SQL.
// Each ID maps to one prepared statement per physical connection.
enum class QueryId : uint16_t {
    // NewOrder
    NewOrderSelectCustomer,
    NewOrderSelectWarehouseTax,
    NewOrderSelectDistrictForUpdate,
    NewOrderUpdateDistrict,
    NewOrderInsertOorder,
    NewOrderInsertNewOrder,
    NewOrderSelectItem,
    NewOrderSelectStockForUpdate,
    NewOrderUpdateStock,
    NewOrderInsertOrderLine,

    // Payment
    PaymentSelectWarehouseForUpdate,
    PaymentUpdateWarehouseYtd,
    PaymentSelectDistrictForUpdate,
    PaymentUpdateDistrictYtd,
    PaymentSelectCustomerData,
    PaymentUpdateCustomerBc,
    PaymentUpdateCustomer,
    PaymentInsertHistory,

    // Customer lookups (shared)
    CustomerSelectById,
    CustomerSelectByIdForUpdate,
    CustomerSelectByLastName,
    CustomerSelectByLastNameForUpdate,

    // Delivery
    DeliverySelectNewOrderForUpdate,
    DeliverySelectOrderCustomer,
    DeliverySelectOrderLines,
    DeliveryDeleteNewOrder,
    DeliveryUpdateCarrier,
    DeliveryUpdateOrderLineDelivery,
    DeliveryUpdateCustomerBalance,

    // OrderStatus
    OrderStatusSelectLatestOrder,
    OrderStatusSelectOrderLines,

    // StockLevel
    StockLevelSelectDistrict,
    StockLevelCountStock,

    // Simulation / diagnostics
    SimulationSelectCastInt,

    Count
};

std::string_view QuerySql(QueryId id);
bool QueryIsSelect(QueryId id);

} // namespace NTPCC
