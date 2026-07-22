#include "db/queries.h"

namespace NTPCC {
namespace {

struct QueryDef {
    std::string_view sql;
    bool isSelect;
};

constexpr QueryDef kQueries[] = {
    // NewOrder
    {
        "SELECT c_discount, c_last, c_credit FROM customer "
        "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
        true,
    },
    {
        "SELECT w_tax FROM warehouse WHERE w_id = ?",
        true,
    },
    {
        "SELECT d_next_o_id, d_tax FROM district "
        "WHERE d_w_id = ? AND d_id = ? FOR UPDATE",
        true,
    },
    {
        "UPDATE district SET d_next_o_id = d_next_o_id + 1 "
        "WHERE d_w_id = ? AND d_id = ?",
        false,
    },
    {
        "INSERT INTO oorder (o_w_id, o_d_id, o_id, o_c_id, o_ol_cnt, o_all_local, o_entry_d) "
        "VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)",
        false,
    },
    {
        "INSERT INTO new_order (no_w_id, no_d_id, no_o_id) VALUES (?, ?, ?)",
        false,
    },
    {
        "SELECT i_price, i_name, i_data FROM item WHERE i_id = ?",
        true,
    },
    {
        "SELECT s_quantity, s_data, s_ytd, s_order_cnt, s_remote_cnt, "
        "s_dist_01, s_dist_02, s_dist_03, s_dist_04, s_dist_05, "
        "s_dist_06, s_dist_07, s_dist_08, s_dist_09, s_dist_10 "
        "FROM stock WHERE s_w_id = ? AND s_i_id = ? FOR UPDATE",
        true,
    },
    {
        "UPDATE stock SET s_quantity = ?, s_ytd = s_ytd + ?, "
        "s_order_cnt = s_order_cnt + 1, s_remote_cnt = s_remote_cnt + ? "
        "WHERE s_w_id = ? AND s_i_id = ?",
        false,
    },
    {
        "INSERT INTO order_line (ol_w_id, ol_d_id, ol_o_id, ol_number, ol_i_id, "
        "ol_amount, ol_supply_w_id, ol_quantity, ol_dist_info) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
        false,
    },

    // Payment
    {
        "SELECT w_name, w_street_1, w_street_2, w_city, w_state, w_zip "
        "FROM warehouse WHERE w_id = ? FOR UPDATE",
        true,
    },
    {
        "UPDATE warehouse SET w_ytd = w_ytd + ? WHERE w_id = ?",
        false,
    },
    {
        "SELECT d_name, d_street_1, d_street_2, d_city, d_state, d_zip "
        "FROM district WHERE d_w_id = ? AND d_id = ? FOR UPDATE",
        true,
    },
    {
        "UPDATE district SET d_ytd = d_ytd + ? WHERE d_w_id = ? AND d_id = ?",
        false,
    },
    {
        "SELECT c_data FROM customer WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
        true,
    },
    {
        "UPDATE customer SET c_balance = c_balance - ?, c_ytd_payment = c_ytd_payment + ?, "
        "c_payment_cnt = c_payment_cnt + 1, c_data = ? "
        "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
        false,
    },
    {
        "UPDATE customer SET c_balance = c_balance - ?, c_ytd_payment = c_ytd_payment + ?, "
        "c_payment_cnt = c_payment_cnt + 1 "
        "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
        false,
    },
    {
        "INSERT INTO history (h_c_id, h_c_d_id, h_c_w_id, h_d_id, h_w_id, h_date, h_amount, h_data) "
        "VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP, ?, ?)",
        false,
    },

    // Customer lookups
    {
        "SELECT c_first, c_middle, c_last, c_street_1, c_street_2, "
        "c_city, c_state, c_zip, c_phone, c_credit, c_credit_lim, "
        "c_discount, c_balance, c_ytd_payment, c_payment_cnt, c_since "
        "FROM customer "
        "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
        true,
    },
    {
        "SELECT c_first, c_middle, c_last, c_street_1, c_street_2, "
        "c_city, c_state, c_zip, c_phone, c_credit, c_credit_lim, "
        "c_discount, c_balance, c_ytd_payment, c_payment_cnt, c_since "
        "FROM customer "
        "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ? "
        "FOR UPDATE",
        true,
    },
    {
        "SELECT c_first, c_middle, c_last, c_id, c_street_1, c_street_2, c_city, "
        "c_state, c_zip, c_phone, c_credit, c_credit_lim, c_discount, "
        "c_balance, c_ytd_payment, c_payment_cnt, c_since "
        "FROM customer "
        "WHERE c_w_id = ? AND c_d_id = ? AND c_last = ? "
        "ORDER BY c_first",
        true,
    },
    {
        "SELECT c_first, c_middle, c_last, c_id, c_street_1, c_street_2, c_city, "
        "c_state, c_zip, c_phone, c_credit, c_credit_lim, c_discount, "
        "c_balance, c_ytd_payment, c_payment_cnt, c_since "
        "FROM customer "
        "WHERE c_w_id = ? AND c_d_id = ? AND c_last = ? "
        "ORDER BY c_first "
        "FOR UPDATE",
        true,
    },

    // Delivery
    {
        "SELECT no_o_id FROM new_order "
        "WHERE no_d_id = ? AND no_w_id = ? "
        "ORDER BY no_o_id ASC LIMIT 1 "
        "FOR UPDATE",
        true,
    },
    {
        "SELECT o_c_id FROM oorder WHERE o_w_id = ? AND o_d_id = ? AND o_id = ?",
        true,
    },
    {
        "SELECT ol_number, ol_amount FROM order_line "
        "WHERE ol_w_id = ? AND ol_d_id = ? AND ol_o_id = ?",
        true,
    },
    {
        "DELETE FROM new_order WHERE no_w_id = ? AND no_d_id = ? AND no_o_id = ?",
        false,
    },
    {
        "UPDATE oorder SET o_carrier_id = ? WHERE o_w_id = ? AND o_d_id = ? AND o_id = ?",
        false,
    },
    {
        "UPDATE order_line SET ol_delivery_d = CURRENT_TIMESTAMP "
        "WHERE ol_w_id = ? AND ol_d_id = ? AND ol_o_id = ?",
        false,
    },
    {
        "UPDATE customer SET c_balance = c_balance + ?, c_delivery_cnt = c_delivery_cnt + 1 "
        "WHERE c_w_id = ? AND c_d_id = ? AND c_id = ?",
        false,
    },

    // OrderStatus
    {
        "SELECT o_id, o_carrier_id, o_entry_d FROM oorder "
        "WHERE o_w_id = ? AND o_d_id = ? AND o_c_id = ? "
        "ORDER BY o_id DESC LIMIT 1",
        true,
    },
    {
        "SELECT ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d "
        "FROM order_line WHERE ol_w_id = ? AND ol_d_id = ? AND ol_o_id = ?",
        true,
    },

    // StockLevel
    {
        "SELECT d_next_o_id FROM district WHERE d_w_id = ? AND d_id = ?",
        true,
    },
    {
        "SELECT COUNT(DISTINCT s.s_i_id) AS stock_count "
        "FROM order_line AS ol "
        "INNER JOIN stock AS s ON s.s_i_id = ol.ol_i_id "
        "WHERE ol.ol_w_id = ? AND ol.ol_d_id = ? "
        "AND ol.ol_o_id < ? AND ol.ol_o_id >= ? "
        "AND s.s_w_id = ? AND s.s_quantity < ?",
        true,
    },

    // Simulation
    {
        "SELECT CAST(? AS SIGNED) AS v",
        true,
    },
};

static_assert(
    sizeof(kQueries) / sizeof(kQueries[0]) == static_cast<size_t>(QueryId::Count),
    "kQueries must have one entry per QueryId");

} // namespace

std::string_view QuerySql(QueryId id) {
    return kQueries[static_cast<size_t>(id)].sql;
}

bool QueryIsSelect(QueryId id) {
    return kQueries[static_cast<size_t>(id)].isSelect;
}

} // namespace NTPCC
