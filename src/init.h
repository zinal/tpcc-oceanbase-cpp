#pragma once

#include <string>

namespace NTPCC {

// Controls OceanBase TABLEGROUP + HASH partitioning for multi-node clusters.
// PartitionCount: >0 explicit count, 0 = auto (OceanBase only), -1 = plain tables.
struct TInitOptions {
    int PartitionCount = 0;
    int WarehouseCount = 1;
};

void InitSync(const std::string& connectionString, const std::string& path = {},
              const TInitOptions& options = {});
void CreateIndexes(const std::string& connectionString, const std::string& path = {},
                   bool useLocalIndexes = false);

} // namespace NTPCC
