#pragma once

#include "db/connection.h"

#include <optional>
#include <string>

namespace NTPCC {

struct TBenchmarkSchemaInfo {
    std::optional<bool> ForeignKeysEnabled;
    std::optional<int> PartitionCount;
};

// Resolves --foreign-keys when the flag is unset (empty) vs explicit on|off.
bool ResolveForeignKeysFlag(const std::string& value, bool defaultEnabled);

const char* ForeignKeysModeLabel(bool enabled);

TBenchmarkSchemaInfo QueryBenchmarkSchemaInfo(ObConnection& conn, const std::string& database);

void WarnPartitionTopology(int partitionCount, int warehouseCount,
                           std::optional<int> tenantUnitCount);

std::optional<int> QueryTenantUnitCount(ObConnection& conn);

} // namespace NTPCC
