//===- CoverageFileSupport.cpp - LLVM coverage filesystem support --------===//

#include "CoverageFileSupport.h"

#include "llvm/Support/FileSystem.h"

namespace obelisk::coverage::detail {

bool atomicReplaceFile(const std::string &temporary, const std::string &output,
                       std::string &error) {
  std::error_code status = llvm::sys::fs::rename(temporary, output);
  if (!status)
    return true;
  error = status.message();
  return false;
}

} // namespace obelisk::coverage::detail
