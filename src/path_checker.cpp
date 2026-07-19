#include "path_checker.h"
#include "log.h"

namespace NTPCC {

void CheckDbForInit(const std::string& /*connectionString*/, const std::string& /*path*/) noexcept {
    LOG_W("CheckDbForInit: skipped until Phase 2 DDL port");
}

void CheckDbForImport(const std::string& /*connectionString*/, const std::string& /*path*/) noexcept {
    LOG_W("CheckDbForImport: skipped until Phase 3 import port");
}

void CheckDbForRun(const std::string& /*connectionString*/, int /*expectedWhCount*/,
                   const std::string& /*path*/) noexcept {
    LOG_W("CheckDbForRun: skipped until Phase 2 path/schema checks");
}

} // namespace NTPCC
