#include "clean.h"

#include <stdexcept>

namespace NTPCC {

void CleanSync(const std::string& /*connectionString*/, const std::string& /*path*/) {
    throw std::runtime_error(
        "clean is not implemented yet (Phase 2). "
        "See docs/PORTING_PLAN.md");
}

} // namespace NTPCC
