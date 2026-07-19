#include "init.h"

#include <stdexcept>

namespace NTPCC {

void CreateIndexes(const std::string& /*connectionString*/, const std::string& /*path*/) {
    throw std::runtime_error(
        "CreateIndexes is not implemented yet (Phase 2). "
        "See docs/PORTING_PLAN.md");
}

void InitSync(const std::string& /*connectionString*/, const std::string& /*path*/) {
    throw std::runtime_error(
        "init is not implemented yet (Phase 2). "
        "See docs/PORTING_PLAN.md");
}

} // namespace NTPCC
