//===- CoverageDatabase.cpp - Runtime build of the native codec ----------===//
//
// The codec implementation is shared verbatim with the host reporting tools.
// Target-runtime builds compile this translation unit so native, wasm, and
// host readers consume identical generated layout descriptors and validation.
//
//===----------------------------------------------------------------------===//

#include "../../lib/Coverage/CoverageFileSupport.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace obelisk::coverage::detail {

bool atomicReplaceFile(const std::string &temporary, const std::string &output,
                       std::string &error) {
  if (std::rename(temporary.c_str(), output.c_str()) == 0)
    return true;
  error = std::strerror(errno);
  return false;
}

} // namespace obelisk::coverage::detail

#include "../../lib/Coverage/CoverageDatabase.cpp"
