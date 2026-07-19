#include "check.h"

#include "constants.h"
#include "db/connection.h"
#include "log.h"

#include <fmt/format.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace NTPCC {

namespace {

void CheckNoRows(ObConnection& conn, const std::string& sql, const std::string& description = {}) {
    auto result = conn.QuerySimple(sql);
    if (result.TryNextRow()) {
        throw std::runtime_error(
            description.empty() ? "Unexpected rows returned" : description);
    }
}

QueryResult QueryOne(ObConnection& conn, const std::string& sql) {
    auto result = conn.QuerySimple(sql);
    if (!result.TryNextRow()) {
        throw std::runtime_error("Expected one row, got none: " + sql);
    }
    return result;
}

//-----------------------------------------------------------------------------

void BaseCheckWarehouseTable(ObConnection& conn, int expectedWhNumber) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT COUNT(*) AS cnt, MAX(w_id) AS max_id, MIN(w_id) AS min_id FROM {}",
        TABLE_WAREHOUSE));

    const auto rowCount = r.GetInt64("cnt");
    const auto maxWh = r.GetInt32("max_id");
    const auto minWh = r.GetInt32("min_id");

    if (rowCount != expectedWhNumber || minWh != 1 || maxWh != expectedWhNumber) {
        throw std::runtime_error(fmt::format(
            "Inconsistent {}: count={}, min={}, max={}, expected={}",
            TABLE_WAREHOUSE, rowCount, minWh, maxWh, expectedWhNumber));
    }
}

void BaseCheckDistrictTable(ObConnection& conn, int expectedWhNumber) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT COUNT(*) AS cnt, "
        "MAX(d_w_id) AS max_w_id, MIN(d_w_id) AS min_w_id, "
        "MAX(d_id) AS max_d_id, MIN(d_id) AS min_d_id "
        "FROM {}", TABLE_DISTRICT));

    const int expectedCount = expectedWhNumber * DISTRICT_COUNT;
    const auto rowCount = r.GetInt64("cnt");
    if (rowCount != expectedCount) {
        throw std::runtime_error(fmt::format(
            "District count is {} and not {}", rowCount, expectedCount));
    }

    const auto maxWh = r.GetInt32("max_w_id");
    const auto minWh = r.GetInt32("min_w_id");
    const auto maxDist = r.GetInt32("max_d_id");
    const auto minDist = r.GetInt32("min_d_id");

    if (minWh != 1 || maxWh != expectedWhNumber) {
        throw std::runtime_error(fmt::format(
            "District warehouse range [{}, {}] instead of [1, {}]",
            minWh, maxWh, expectedWhNumber));
    }
    if (minDist != DISTRICT_LOW_ID || maxDist != DISTRICT_HIGH_ID) {
        throw std::runtime_error(fmt::format(
            "District ID range [{}, {}] instead of [{}, {}]",
            minDist, maxDist, DISTRICT_LOW_ID, DISTRICT_HIGH_ID));
    }
}

void BaseCheckCustomerTable(ObConnection& conn, int expectedWhNumber) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT COUNT(*) AS cnt, "
        "MAX(c_w_id) AS max_w, MIN(c_w_id) AS min_w, "
        "MAX(c_d_id) AS max_d, MIN(c_d_id) AS min_d, "
        "MAX(c_id) AS max_c, MIN(c_id) AS min_c "
        "FROM {}", TABLE_CUSTOMER));

    const int expectedCount = expectedWhNumber * CUSTOMERS_PER_DISTRICT * DISTRICT_COUNT;
    if (r.GetInt64("cnt") != expectedCount) {
        throw std::runtime_error(fmt::format(
            "Customer count is {} and not {}", r.GetInt64("cnt"), expectedCount));
    }
    if (r.GetInt32("min_w") != 1 || r.GetInt32("max_w") != expectedWhNumber) {
        throw std::runtime_error(fmt::format(
            "Customer warehouse range [{}, {}] instead of [1, {}]",
            r.GetInt32("min_w"), r.GetInt32("max_w"), expectedWhNumber));
    }
    if (r.GetInt32("min_d") != DISTRICT_LOW_ID || r.GetInt32("max_d") != DISTRICT_HIGH_ID) {
        throw std::runtime_error("Customer district range mismatch");
    }
    if (r.GetInt32("min_c") != 1 || r.GetInt32("max_c") != CUSTOMERS_PER_DISTRICT) {
        throw std::runtime_error("Customer ID range mismatch");
    }
}

void BaseCheckItemTable(ObConnection& conn) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT COUNT(*) AS cnt, MAX(i_id) AS max_id, MIN(i_id) AS min_id FROM {}",
        TABLE_ITEM));

    if (r.GetInt64("cnt") != ITEM_COUNT) {
        throw std::runtime_error(fmt::format(
            "Item count is {} and not {}", r.GetInt64("cnt"), ITEM_COUNT));
    }
    if (r.GetInt32("min_id") != 1 || r.GetInt32("max_id") != ITEM_COUNT) {
        throw std::runtime_error("Item ID range mismatch");
    }
}

void BaseCheckStockTable(ObConnection& conn, int expectedWhNumber) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT COUNT(*) AS cnt, COUNT(DISTINCT s_w_id) AS wh_cnt, "
        "MAX(s_w_id) AS max_w, MIN(s_w_id) AS min_w, "
        "MAX(s_i_id) AS max_i, MIN(s_i_id) AS min_i "
        "FROM {}", TABLE_STOCK));

    const int expectedCount = expectedWhNumber * ITEM_COUNT;
    if (r.GetInt64("cnt") != expectedCount) {
        throw std::runtime_error(fmt::format(
            "Stock count is {} and not {}", r.GetInt64("cnt"), expectedCount));
    }
    if (r.GetInt32("wh_cnt") != expectedWhNumber) {
        throw std::runtime_error(fmt::format(
            "Stock warehouse count is {} and not {}", r.GetInt32("wh_cnt"), expectedWhNumber));
    }
    if (r.GetInt32("min_w") != 1 || r.GetInt32("max_w") != expectedWhNumber) {
        throw std::runtime_error("Stock warehouse range mismatch");
    }
    if (r.GetInt32("min_i") != 1 || r.GetInt32("max_i") != ITEM_COUNT) {
        throw std::runtime_error("Stock item range mismatch");
    }
}

void BaseCheckOorderTable(ObConnection& conn, int expectedWhNumber) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT COUNT(*) AS cnt, "
        "MAX(o_w_id) AS max_w, MIN(o_w_id) AS min_w, "
        "MAX(o_d_id) AS max_d, MIN(o_d_id) AS min_d, "
        "MAX(o_id) AS max_o, MIN(o_id) AS min_o "
        "FROM {}", TABLE_OORDER));

    const int expectedCount = expectedWhNumber * CUSTOMERS_PER_DISTRICT * DISTRICT_COUNT;
    if (r.GetInt64("cnt") != expectedCount) {
        throw std::runtime_error(fmt::format(
            "Order count is {} and not {}", r.GetInt64("cnt"), expectedCount));
    }
    if (r.GetInt32("min_w") != 1 || r.GetInt32("max_w") != expectedWhNumber) {
        throw std::runtime_error("Order warehouse range mismatch");
    }
    if (r.GetInt32("min_d") != DISTRICT_LOW_ID || r.GetInt32("max_d") != DISTRICT_HIGH_ID) {
        throw std::runtime_error("Order district range mismatch");
    }
    if (r.GetInt32("min_o") != 1 || r.GetInt32("max_o") != CUSTOMERS_PER_DISTRICT) {
        throw std::runtime_error("Order ID range mismatch");
    }
}

void BaseCheckNewOrderTable(ObConnection& conn, int expectedWhNumber) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT COUNT(*) AS cnt, "
        "MAX(no_w_id) AS max_w, MIN(no_w_id) AS min_w, "
        "MAX(no_d_id) AS max_d, MIN(no_d_id) AS min_d, "
        "MAX(no_o_id) AS max_o, MIN(no_o_id) AS min_o "
        "FROM {}", TABLE_NEW_ORDER));

    const auto newOrdersPerDistrict = CUSTOMERS_PER_DISTRICT - FIRST_UNPROCESSED_O_ID + 1;
    const int expectedCount = expectedWhNumber * newOrdersPerDistrict * DISTRICT_COUNT;
    if (r.GetInt64("cnt") != expectedCount) {
        throw std::runtime_error(fmt::format(
            "New order count is {} and not {}", r.GetInt64("cnt"), expectedCount));
    }
    if (r.GetInt32("min_w") != 1 || r.GetInt32("max_w") != expectedWhNumber) {
        throw std::runtime_error("New order warehouse range mismatch");
    }
    if (r.GetInt32("min_d") != DISTRICT_LOW_ID || r.GetInt32("max_d") != DISTRICT_HIGH_ID) {
        throw std::runtime_error("New order district range mismatch");
    }
    if (r.GetInt32("min_o") < FIRST_UNPROCESSED_O_ID
        || r.GetInt32("max_o") != CUSTOMERS_PER_DISTRICT) {
        throw std::runtime_error("New order ID range mismatch");
    }
}

void BaseCheckOrderLineTable(ObConnection& conn, int expectedWhNumber) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT MIN(order_count) AS min_orders, MAX(order_count) AS max_orders, "
        "COUNT(*) AS district_count "
        "FROM ("
        "  SELECT ol_w_id, ol_d_id, COUNT(DISTINCT ol_o_id) AS order_count "
        "  FROM {} GROUP BY ol_w_id, ol_d_id"
        ") sub", TABLE_ORDER_LINE));

    const int expectedDistrictCount = expectedWhNumber * DISTRICT_COUNT;
    if (r.GetInt64("district_count") != expectedDistrictCount) {
        throw std::runtime_error(fmt::format(
            "Order line district count is {} and not {}",
            r.GetInt64("district_count"), expectedDistrictCount));
    }
    if (r.GetInt64("min_orders") != CUSTOMERS_PER_DISTRICT
        || r.GetInt64("max_orders") != CUSTOMERS_PER_DISTRICT) {
        throw std::runtime_error(fmt::format(
            "Order line orders per district [{}, {}] instead of [{}, {}]",
            r.GetInt64("min_orders"), r.GetInt64("max_orders"),
            CUSTOMERS_PER_DISTRICT, CUSTOMERS_PER_DISTRICT));
    }
}

void BaseCheckHistoryTable(ObConnection& conn, int expectedWhNumber) {
    auto r = QueryOne(conn, fmt::format(
        "SELECT COUNT(*) AS cnt, MAX(h_c_w_id) AS max_w, MIN(h_c_w_id) AS min_w FROM {}",
        TABLE_HISTORY));

    const int expectedCount = expectedWhNumber * CUSTOMERS_PER_DISTRICT * DISTRICT_COUNT;
    if (r.GetInt64("cnt") != expectedCount) {
        throw std::runtime_error(fmt::format(
            "History count is {} and not {}", r.GetInt64("cnt"), expectedCount));
    }
    if (r.GetInt32("min_w") != 1 || r.GetInt32("max_w") != expectedWhNumber) {
        throw std::runtime_error("History warehouse range mismatch");
    }
}

//-----------------------------------------------------------------------------
// Consistency checks based on TPC-C spec section 3.3.2
//-----------------------------------------------------------------------------

void ConsistencyCheck3321(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT w.w_id AS w_id "
        "FROM {} AS w "
        "JOIN (SELECT d_w_id, SUM(d_ytd) AS sum_d_ytd FROM {} GROUP BY d_w_id) AS d "
        "ON w.w_id = d.d_w_id "
        "WHERE ABS(w.w_ytd - d.sum_d_ytd) > 1e-3 LIMIT 1",
        TABLE_WAREHOUSE, TABLE_DISTRICT));
}

void ConsistencyCheck3322(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT d.d_w_id AS d_w_id "
        "FROM {} AS d "
        "LEFT JOIN (SELECT o_w_id, o_d_id, MAX(o_id) AS max_o_id FROM {} "
        "           GROUP BY o_w_id, o_d_id) AS o "
        "  ON d.d_w_id = o.o_w_id AND d.d_id = o.o_d_id "
        "LEFT JOIN (SELECT no_w_id, no_d_id, MAX(no_o_id) AS max_no_o_id FROM {} "
        "           GROUP BY no_w_id, no_d_id) AS n "
        "  ON d.d_w_id = n.no_w_id AND d.d_id = n.no_d_id "
        "WHERE (d.d_next_o_id - 1) != o.max_o_id OR o.max_o_id != n.max_no_o_id "
        "LIMIT 1",
        TABLE_DISTRICT, TABLE_OORDER, TABLE_NEW_ORDER));
}

void ConsistencyCheck3323(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT no_w_id AS no_w_id "
        "FROM {} GROUP BY no_w_id, no_d_id "
        "HAVING COUNT(*) - (MAX(no_o_id) - MIN(no_o_id) + 1) != 0 LIMIT 1",
        TABLE_NEW_ORDER));
}

void ConsistencyCheck3324(ObConnection& conn, int warehouseCount) {
    // FULL JOIN emulation: LEFT JOIN mismatches UNION ALL right-only rows.
    constexpr int kRangeSize = 50;
    for (int startWh = 1; startWh <= warehouseCount; startWh += kRangeSize) {
        const int endWh = std::min(startWh + kRangeSize - 1, warehouseCount);
        const std::string sql = fmt::format(
            "SELECT * FROM ("
            "  SELECT o.o_w_id AS o_w_id, o.o_d_id AS o_d_id "
            "  FROM (SELECT o_w_id, o_d_id, SUM(o_ol_cnt) AS sum_ol_cnt "
            "        FROM {0} WHERE o_w_id >= {1} AND o_w_id <= {2} "
            "        GROUP BY o_w_id, o_d_id) AS o "
            "  LEFT JOIN (SELECT ol_w_id, ol_d_id, COUNT(*) AS ol_count "
            "             FROM {3} WHERE ol_w_id >= {1} AND ol_w_id <= {2} "
            "             GROUP BY ol_w_id, ol_d_id) AS ol "
            "    ON o.o_w_id = ol.ol_w_id AND o.o_d_id = ol.ol_d_id "
            "  WHERE COALESCE(o.sum_ol_cnt, -1) != COALESCE(ol.ol_count, -1) "
            "  UNION ALL "
            "  SELECT ol2.ol_w_id, ol2.ol_d_id "
            "  FROM (SELECT ol_w_id, ol_d_id, COUNT(*) AS ol_count "
            "        FROM {3} WHERE ol_w_id >= {1} AND ol_w_id <= {2} "
            "        GROUP BY ol_w_id, ol_d_id) AS ol2 "
            "  LEFT JOIN (SELECT o_w_id, o_d_id, SUM(o_ol_cnt) AS sum_ol_cnt "
            "             FROM {0} WHERE o_w_id >= {1} AND o_w_id <= {2} "
            "             GROUP BY o_w_id, o_d_id) AS o2 "
            "    ON o2.o_w_id = ol2.ol_w_id AND o2.o_d_id = ol2.ol_d_id "
            "  WHERE o2.o_w_id IS NULL"
            ") sub LIMIT 1",
            TABLE_OORDER, startWh, endWh, TABLE_ORDER_LINE);
        CheckNoRows(conn, sql, fmt::format("3.3.2.4 w_id [{},{}]", startWh, endWh));
    }
}

void ConsistencyCheck3325(ObConnection& conn, int warehouseCount) {
    constexpr int kRangeSize = 50;
    for (int startWh = 1; startWh <= warehouseCount; startWh += kRangeSize) {
        const int endWh = std::min(startWh + kRangeSize - 1, warehouseCount);
        const std::string sql = fmt::format(
            "SELECT * FROM ("
            "  SELECT no.no_w_id AS w_id, no.no_d_id AS d_id, no.no_o_id AS o_id "
            "  FROM {0} AS no "
            "  LEFT JOIN {1} AS o "
            "    ON no.no_w_id = o.o_w_id AND no.no_d_id = o.o_d_id AND no.no_o_id = o.o_id "
            "  WHERE no.no_w_id >= {2} AND no.no_w_id <= {3} "
            "    AND (o.o_w_id IS NULL OR (o.o_carrier_id IS NOT NULL AND o.o_carrier_id != 0)) "
            "  UNION ALL "
            "  SELECT o2.o_w_id, o2.o_d_id, o2.o_id "
            "  FROM {1} AS o2 "
            "  LEFT JOIN {0} AS no2 "
            "    ON o2.o_w_id = no2.no_w_id AND o2.o_d_id = no2.no_d_id AND o2.o_id = no2.no_o_id "
            "  WHERE o2.o_w_id >= {2} AND o2.o_w_id <= {3} "
            "    AND (o2.o_carrier_id IS NULL OR o2.o_carrier_id = 0) AND no2.no_w_id IS NULL"
            ") sub LIMIT 1",
            TABLE_NEW_ORDER, TABLE_OORDER, startWh, endWh);
        CheckNoRows(conn, sql, fmt::format("3.3.2.5 w_id [{},{}]", startWh, endWh));
    }
}

void ConsistencyCheck3326(ObConnection& conn, int warehouseCount) {
    constexpr int kRangeSize = 50;
    for (int startWh = 1; startWh <= warehouseCount; startWh += kRangeSize) {
        const int endWh = std::min(startWh + kRangeSize - 1, warehouseCount);
        const std::string sql = fmt::format(
            "SELECT * FROM ("
            "  SELECT o.o_w_id AS w_id, o.o_d_id AS d_id, o.o_id AS o_id "
            "  FROM {0} AS o "
            "  LEFT JOIN (SELECT ol_w_id, ol_d_id, ol_o_id, COUNT(*) AS cnt "
            "             FROM {1} WHERE ol_w_id >= {2} AND ol_w_id <= {3} "
            "             GROUP BY ol_w_id, ol_d_id, ol_o_id) AS l "
            "    ON o.o_w_id = l.ol_w_id AND o.o_d_id = l.ol_d_id AND o.o_id = l.ol_o_id "
            "  WHERE o.o_w_id >= {2} AND o.o_w_id <= {3} "
            "    AND o.o_ol_cnt != COALESCE(l.cnt, 0) "
            "  UNION ALL "
            "  SELECT l2.ol_w_id, l2.ol_d_id, l2.ol_o_id "
            "  FROM (SELECT DISTINCT ol_w_id, ol_d_id, ol_o_id FROM {1} "
            "        WHERE ol_w_id >= {2} AND ol_w_id <= {3}) AS l2 "
            "  LEFT JOIN {0} AS o2 "
            "    ON l2.ol_w_id = o2.o_w_id AND l2.ol_d_id = o2.o_d_id AND l2.ol_o_id = o2.o_id "
            "  WHERE o2.o_w_id IS NULL"
            ") sub LIMIT 1",
            TABLE_OORDER, TABLE_ORDER_LINE, startWh, endWh);
        CheckNoRows(conn, sql, fmt::format("3.3.2.6 w_id [{},{}]", startWh, endWh));
    }
}

void ConsistencyCheck3327(ObConnection& conn, int warehouseCount) {
    // BOOL_AND / BOOL_OR -> MIN / MAX over 0/1 predicates.
    constexpr int kRangeSize = 10;
    for (int startWh = 1; startWh <= warehouseCount; startWh += kRangeSize) {
        const int endWh = std::min(startWh + kRangeSize - 1, warehouseCount);
        const std::string sql = fmt::format(
            "SELECT l.ol_w_id AS ol_w_id "
            "FROM ("
            "  SELECT ol_w_id, ol_d_id, ol_o_id, "
            "    MIN(CASE WHEN ol_delivery_d IS NOT NULL THEN 1 ELSE 0 END) AS all_delivered, "
            "    MAX(CASE WHEN ol_delivery_d IS NULL THEN 1 ELSE 0 END) AS some_null "
            "  FROM {} WHERE ol_w_id >= {} AND ol_w_id <= {} "
            "  GROUP BY ol_w_id, ol_d_id, ol_o_id"
            ") AS l "
            "JOIN {} AS o ON l.ol_w_id = o.o_w_id AND l.ol_d_id = o.o_d_id AND l.ol_o_id = o.o_id "
            "WHERE (o.o_carrier_id IS NULL AND l.all_delivered = 1) "
            "   OR (o.o_carrier_id IS NOT NULL AND l.some_null = 1) "
            "LIMIT 1",
            TABLE_ORDER_LINE, startWh, endWh, TABLE_OORDER);
        CheckNoRows(conn, sql, fmt::format("3.3.2.7 w_id [{},{}]", startWh, endWh));
    }
}

void ConsistencyCheck3328(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT w.w_id AS w_id "
        "FROM {} AS w "
        "JOIN (SELECT h_w_id, SUM(h_amount) AS sum_h FROM {} GROUP BY h_w_id) AS h "
        "  ON w.w_id = h.h_w_id "
        "WHERE ABS(w.w_ytd - h.sum_h) > 1e-3 LIMIT 1",
        TABLE_WAREHOUSE, TABLE_HISTORY));
}

void ConsistencyCheck3329(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT d.d_w_id AS d_w_id "
        "FROM {} AS d "
        "JOIN (SELECT h_w_id, h_d_id, SUM(h_amount) AS sum_h FROM {} "
        "      GROUP BY h_w_id, h_d_id) AS h "
        "  ON d.d_w_id = h.h_w_id AND d.d_id = h.h_d_id "
        "WHERE ABS(d.d_ytd - h.sum_h) > 1e-3 LIMIT 1",
        TABLE_DISTRICT, TABLE_HISTORY));
}

void ConsistencyCheck33210(ObConnection& conn, int warehouseCount) {
    constexpr int kRangeSize = 10;
    for (int startWh = 1; startWh <= warehouseCount; startWh += kRangeSize) {
        const int endWh = std::min(startWh + kRangeSize - 1, warehouseCount);
        const std::string sql = fmt::format(
            "SELECT c.c_w_id AS c_w_id "
            "FROM {0} AS c "
            "LEFT JOIN ("
            "  SELECT o.o_w_id AS w_id, o.o_d_id AS d_id, o.o_c_id AS c_id, "
            "         SUM(ol.ol_amount) AS ol_sum "
            "  FROM {1} AS o "
            "  JOIN {2} AS ol ON ol.ol_w_id = o.o_w_id AND ol.ol_d_id = o.o_d_id "
            "                 AND ol.ol_o_id = o.o_id "
            "  WHERE ol.ol_delivery_d IS NOT NULL AND o.o_w_id >= {3} AND o.o_w_id <= {4} "
            "  GROUP BY o.o_w_id, o.o_d_id, o.o_c_id"
            ") AS ols ON c.c_w_id = ols.w_id AND c.c_d_id = ols.d_id AND c.c_id = ols.c_id "
            "LEFT JOIN ("
            "  SELECT h_c_w_id, h_c_d_id, h_c_id, SUM(h_amount) AS h_sum "
            "  FROM {5} WHERE h_c_w_id >= {3} AND h_c_w_id <= {4} "
            "  GROUP BY h_c_w_id, h_c_d_id, h_c_id"
            ") AS hs ON c.c_w_id = hs.h_c_w_id AND c.c_d_id = hs.h_c_d_id AND c.c_id = hs.h_c_id "
            "WHERE c.c_w_id >= {3} AND c.c_w_id <= {4} "
            "  AND ABS(c.c_balance - (COALESCE(ols.ol_sum, 0) - COALESCE(hs.h_sum, 0))) > 1e-3 "
            "LIMIT 1",
            TABLE_CUSTOMER, TABLE_OORDER, TABLE_ORDER_LINE, startWh, endWh, TABLE_HISTORY);
        CheckNoRows(conn, sql, fmt::format("3.3.2.10 w_id [{},{}]", startWh, endWh));
    }
}

void ConsistencyCheck33211(ObConnection& conn, int warehouseCount) {
    constexpr int kRangeSize = 50;
    for (int startWh = 1; startWh <= warehouseCount; startWh += kRangeSize) {
        const int endWh = std::min(startWh + kRangeSize - 1, warehouseCount);
        const std::string sql = fmt::format(
            "SELECT o.o_w_id AS o_w_id "
            "FROM (SELECT o_w_id, o_d_id, COUNT(*) AS order_cnt FROM {} "
            "      WHERE o_w_id >= {} AND o_w_id <= {} GROUP BY o_w_id, o_d_id) AS o "
            "JOIN (SELECT no_w_id, no_d_id, COUNT(*) AS new_order_cnt FROM {} "
            "      WHERE no_w_id >= {} AND no_w_id <= {} GROUP BY no_w_id, no_d_id) AS n "
            "  ON o.o_w_id = n.no_w_id AND o.o_d_id = n.no_d_id "
            "WHERE (o.order_cnt - n.new_order_cnt) != {} LIMIT 1",
            TABLE_OORDER, startWh, endWh,
            TABLE_NEW_ORDER, startWh, endWh,
            FIRST_UNPROCESSED_O_ID - 1);
        CheckNoRows(conn, sql, fmt::format("3.3.2.11 w_id [{},{}]", startWh, endWh));
    }
}

void ConsistencyCheck33212(ObConnection& conn, int warehouseCount) {
    constexpr int kRangeSize = 10;
    for (int startWh = 1; startWh <= warehouseCount; startWh += kRangeSize) {
        const int endWh = std::min(startWh + kRangeSize - 1, warehouseCount);
        const std::string sql = fmt::format(
            "SELECT c.c_w_id AS c_w_id "
            "FROM {0} AS c "
            "JOIN ("
            "  SELECT o.o_w_id AS w_id, o.o_d_id AS d_id, o.o_c_id AS c_id, "
            "         SUM(ol.ol_amount) AS ol_sum "
            "  FROM {1} AS o "
            "  JOIN {2} AS ol ON ol.ol_w_id = o.o_w_id AND ol.ol_d_id = o.o_d_id "
            "                 AND ol.ol_o_id = o.o_id "
            "  WHERE ol.ol_delivery_d IS NOT NULL AND o.o_w_id >= {3} AND o.o_w_id <= {4} "
            "  GROUP BY o.o_w_id, o.o_d_id, o.o_c_id"
            ") AS l ON c.c_w_id = l.w_id AND c.c_d_id = l.d_id AND c.c_id = l.c_id "
            "WHERE c.c_w_id >= {3} AND c.c_w_id <= {4} "
            "  AND ABS(c.c_balance + c.c_ytd_payment - l.ol_sum) > 1e-3 "
            "LIMIT 1",
            TABLE_CUSTOMER, TABLE_OORDER, TABLE_ORDER_LINE, startWh, endWh);
        CheckNoRows(conn, sql, fmt::format("3.3.2.12 w_id [{},{}]", startWh, endWh));
    }
}

//-----------------------------------------------------------------------------
// Post-import checks: stricter invariants that hold only on freshly loaded data
//-----------------------------------------------------------------------------

void PostImportCheckNextOrderId(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT d_w_id AS d_w_id FROM {} WHERE d_next_o_id != {} LIMIT 1",
        TABLE_DISTRICT, CUSTOMERS_PER_DISTRICT + 1),
        fmt::format("D_NEXT_O_ID must be {} for all districts after import",
                    CUSTOMERS_PER_DISTRICT + 1));
}

void PostImportCheckWarehouseYtd(ObConnection& conn) {
    const double expectedYtd = DISTRICT_INITIAL_YTD * DISTRICT_COUNT;
    CheckNoRows(conn, fmt::format(
        "SELECT w_id AS w_id FROM {} WHERE ABS(w_ytd - {}) > 1e-3 LIMIT 1",
        TABLE_WAREHOUSE, expectedYtd),
        fmt::format("W_YTD must be {} after import", expectedYtd));
}

void PostImportCheckDistrictYtd(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT d_w_id AS d_w_id FROM {} WHERE ABS(d_ytd - {}) > 1e-3 LIMIT 1",
        TABLE_DISTRICT, DISTRICT_INITIAL_YTD),
        fmt::format("D_YTD must be {} after import", DISTRICT_INITIAL_YTD));
}

void PostImportCheckNoCarriers(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT o_w_id AS o_w_id FROM {} "
        "WHERE o_id >= {} AND o_carrier_id IS NOT NULL LIMIT 1",
        TABLE_OORDER, FIRST_UNPROCESSED_O_ID),
        "Unprocessed orders must have NULL O_CARRIER_ID after import");
}

void PostImportCheckNoDeliveryDates(ObConnection& conn) {
    CheckNoRows(conn, fmt::format(
        "SELECT ol_w_id AS ol_w_id FROM {} "
        "WHERE ol_o_id >= {} AND ol_delivery_d IS NOT NULL LIMIT 1",
        TABLE_ORDER_LINE, FIRST_UNPROCESSED_O_ID),
        "Unprocessed order lines must have NULL OL_DELIVERY_D after import");
}

} // anonymous

//-----------------------------------------------------------------------------

void CheckSync(const std::string& connectionString, int warehouseCount, bool afterImport,
               const std::string& path) {
    if (warehouseCount <= 0) {
        std::cerr << "Zero warehouses specified, nothing to check" << std::endl;
        return;
    }

    auto cfg = ParseConnectionString(connectionString);
    if (!path.empty()) {
        cfg.path = path;
    }
    auto conn = ObConnection::Connect(cfg);

    int failedCount = 0;

    auto runCheck = [&](const std::string& name, const std::function<void(ObConnection&)>& fn) {
        std::cout << "Checking " << name << " " << std::flush;
        try {
            fn(*conn);
            std::cout << "[OK]" << std::endl;
        } catch (const std::exception& ex) {
            std::cout << "[Failed]: " << ex.what() << std::endl;
            ++failedCount;
        }
    };

    // Static tables: row counts never change during benchmark.
    runCheck(TABLE_WAREHOUSE, [&](auto& c) { BaseCheckWarehouseTable(c, warehouseCount); });
    runCheck(TABLE_DISTRICT, [&](auto& c) { BaseCheckDistrictTable(c, warehouseCount); });
    runCheck(TABLE_CUSTOMER, [&](auto& c) { BaseCheckCustomerTable(c, warehouseCount); });
    runCheck(TABLE_ITEM, [&](auto& c) { BaseCheckItemTable(c); });
    runCheck(TABLE_STOCK, [&](auto& c) { BaseCheckStockTable(c, warehouseCount); });

    // Dynamic tables: exact counts are valid only right after import.
    if (afterImport) {
        runCheck(TABLE_OORDER, [&](auto& c) { BaseCheckOorderTable(c, warehouseCount); });
        runCheck(TABLE_NEW_ORDER, [&](auto& c) { BaseCheckNewOrderTable(c, warehouseCount); });
        runCheck(TABLE_ORDER_LINE, [&](auto& c) { BaseCheckOrderLineTable(c, warehouseCount); });
        runCheck(TABLE_HISTORY, [&](auto& c) { BaseCheckHistoryTable(c, warehouseCount); });
    }

    if (failedCount > 0) {
        std::cout << "Base checks failed, aborting consistency checks!" << std::endl;
        throw std::runtime_error("Base checks failed");
    }

    runCheck("3.3.2.1", [&](auto& c) { ConsistencyCheck3321(c); });
    runCheck("3.3.2.2", [&](auto& c) { ConsistencyCheck3322(c); });
    runCheck("3.3.2.3", [&](auto& c) { ConsistencyCheck3323(c); });
    runCheck("3.3.2.4", [&](auto& c) { ConsistencyCheck3324(c, warehouseCount); });
    runCheck("3.3.2.5", [&](auto& c) { ConsistencyCheck3325(c, warehouseCount); });
    runCheck("3.3.2.6", [&](auto& c) { ConsistencyCheck3326(c, warehouseCount); });
    runCheck("3.3.2.7", [&](auto& c) { ConsistencyCheck3327(c, warehouseCount); });
    runCheck("3.3.2.8", [&](auto& c) { ConsistencyCheck3328(c); });
    runCheck("3.3.2.9", [&](auto& c) { ConsistencyCheck3329(c); });
    runCheck("3.3.2.10", [&](auto& c) { ConsistencyCheck33210(c, warehouseCount); });
    runCheck("3.3.2.12", [&](auto& c) { ConsistencyCheck33212(c, warehouseCount); });

    if (afterImport) {
        runCheck("3.3.2.11", [&](auto& c) { ConsistencyCheck33211(c, warehouseCount); });
        runCheck("post-import: D_NEXT_O_ID", [&](auto& c) { PostImportCheckNextOrderId(c); });
        runCheck("post-import: W_YTD", [&](auto& c) { PostImportCheckWarehouseYtd(c); });
        runCheck("post-import: D_YTD", [&](auto& c) { PostImportCheckDistrictYtd(c); });
        runCheck("post-import: O_CARRIER_ID", [&](auto& c) { PostImportCheckNoCarriers(c); });
        runCheck("post-import: OL_DELIVERY_D", [&](auto& c) { PostImportCheckNoDeliveryDates(c); });
    }

    if (failedCount == 0) {
        std::cout << "Everything is good!" << std::endl;
    } else {
        throw std::runtime_error(fmt::format("{} checks failed", failedCount));
    }
}

} // namespace NTPCC
