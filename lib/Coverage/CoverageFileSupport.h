//===- CoverageFileSupport.h - Coverage filesystem boundary ----*- C++ -*-===//

#ifndef OBELISK_LIB_COVERAGE_COVERAGEFILESUPPORT_H
#define OBELISK_LIB_COVERAGE_COVERAGEFILESUPPORT_H

#include <string>

namespace obelisk::coverage::detail {

/// Atomically publish `temporary` as `output`, replacing an existing output.
/// This narrow boundary keeps the native codec itself independent of LLVM and
/// platform headers while allowing host tools to use LLVM's filesystem layer.
bool atomicReplaceFile(const std::string &temporary, const std::string &output,
                       std::string &error);

} // namespace obelisk::coverage::detail

#endif
