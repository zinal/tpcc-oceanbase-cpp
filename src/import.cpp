#include "import.h"

#include <stdexcept>

namespace NTPCC {

void ImportSync(const TImportConfig& /*config*/) {
    throw std::runtime_error(
        "import is not implemented yet (Phase 3). "
        "See docs/PORTING_PLAN.md");
}

} // namespace NTPCC
