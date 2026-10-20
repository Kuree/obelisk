//===- CoverageDatabase.cpp - Runtime build of the native codec ----------===//
//
// The codec implementation is shared verbatim with the host reporting tools.
// Target-runtime builds compile this translation unit so native, wasm, and
// host readers consume identical generated layout descriptors and validation.
//
//===----------------------------------------------------------------------===//

#include "../../lib/Coverage/CoverageFileSupport.h"

#include <filesystem>
#include <system_error>

namespace obelisk::coverage::detail {

bool atomicReplaceFile(const std::string &temporary, const std::string &output,
                       std::string &error) {
  std::error_code code;
  std::filesystem::rename(temporary, output, code);
  if (!code)
    return true;
  error = code.message();
  return false;
}

} // namespace obelisk::coverage::detail

#include "../../lib/Coverage/CoverageDatabase.cpp"
