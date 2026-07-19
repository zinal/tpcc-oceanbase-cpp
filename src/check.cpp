#include "check.h"

#include <stdexcept>

namespace NTPCC {

void CheckSync(const std::string& /*connectionString*/, int /*warehouseCount*/, bool /*afterImport*/,
               const std::string& /*path*/) {
    throw std::runtime_error(
        "check is not implemented yet (Phase 5). "
        "See docs/PORTING_PLAN.md");
}

} // namespace NTPCC
