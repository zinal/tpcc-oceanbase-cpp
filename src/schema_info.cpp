#include "schema_info.h"

#include "log.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace NTPCC {

namespace {

std::string ToLower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::optional<int> TryQueryTenantUnitCount(ObConnection& conn) {
    try {
        auto result = conn.QuerySimple(
            "SELECT COUNT(DISTINCT unit_id) AS cnt "
            "FROM oceanbase.GV$OB_UNITS "
            "WHERE status = 'NORMAL'");
        if (result.TryNextRow()) {
            return result.GetInt32("cnt");
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<int> TryQueryTablePartitionCount(ObConnection& conn,
                                                 const std::string& database) {
    auto result = conn.Query(
        "SELECT COUNT(DISTINCT partition_name) AS cnt "
        "FROM information_schema.partitions "
        "WHERE table_schema = ? AND table_name = 'warehouse' "
        "AND partition_name IS NOT NULL",
        MakeParams(database));
    if (!result.TryNextRow()) {
        return std::nullopt;
    }
    const int count = result.GetInt32("cnt");
    return count > 0 ? std::optional<int>{count} : std::nullopt;
}

bool QueryForeignKeysEnabled(ObConnection& conn, const std::string& database) {
    auto result = conn.Query(
        "SELECT 1 AS ok FROM information_schema.table_constraints "
        "WHERE table_schema = ? AND constraint_type = 'FOREIGN KEY' LIMIT 1",
        MakeParams(database));
    return result.TryNextRow();
}

} // anonymous

bool ResolveForeignKeysFlag(const std::string& value, bool defaultEnabled) {
    if (value.empty()) {
        return defaultEnabled;
    }
    const std::string normalized = ToLower(value);
    if (normalized == "on" || normalized == "1" || normalized == "true") {
        return true;
    }
    if (normalized == "off" || normalized == "0" || normalized == "false") {
        return false;
    }
    throw std::runtime_error(
        "Invalid --foreign-keys value '" + value + "' (expected on|off)");
}

const char* ForeignKeysModeLabel(bool enabled) {
    return enabled ? "on" : "off";
}

TBenchmarkSchemaInfo QueryBenchmarkSchemaInfo(ObConnection& conn,
                                              const std::string& database) {
    TBenchmarkSchemaInfo info;
    info.ForeignKeysEnabled = QueryForeignKeysEnabled(conn, database);
    info.PartitionCount = TryQueryTablePartitionCount(conn, database);
    return info;
}

void WarnPartitionTopology(int partitionCount, int warehouseCount,
                           std::optional<int> tenantUnitCount) {
    const int warehouses = std::max(warehouseCount, 1);

    if (partitionCount > warehouses * 4) {
        LOG_W(
            "Partition count {} is significantly higher than warehouse count {}. "
            "Tune --partitions to match cluster topology (see docs/PARTITION_TUNING.md). "
            "Verify distribution with SQL Audit and GV$OB_PARTITION_AUDIT.",
            partitionCount, warehouseCount);
    } else if (partitionCount > warehouses * 2) {
        LOG_W(
            "Partition count {} exceeds 2x warehouse count {}. "
            "This may create idle partitions; consider a lower --partitions value.",
            partitionCount, warehouseCount);
    }

    if (tenantUnitCount && *tenantUnitCount > 0
        && partitionCount > *tenantUnitCount * 4) {
        LOG_W(
            "Partition count {} is significantly higher than tenant unit count ({}). "
            "Review topology with GV$OB_UNITS and GV$OB_PARTITION_AUDIT.",
            partitionCount, *tenantUnitCount);
    }
}

std::optional<int> QueryTenantUnitCount(ObConnection& conn) {
    return TryQueryTenantUnitCount(conn);
}

} // namespace NTPCC
