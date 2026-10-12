//===- DriverTestMain.cpp - Test-only driver ------------------------------===//
//
// The production driver with resource limits lowered from the command line,
// so tests can reach the SDF limits with small inputs.
//
//===----------------------------------------------------------------------===//

#include "DriverMain.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <charconv>
#include <cstdint>

namespace {

template <typename Integer>
bool parseLimit(llvm::StringRef argument, llvm::StringRef prefix,
                Integer &value) {
  if (!argument.consume_front(prefix))
    return false;
  auto result = std::from_chars(argument.begin(), argument.end(), value, 10);
  if (result.ec != std::errc() || result.ptr != argument.end()) {
    llvm::errs() << "invalid test-only resource limit\n";
    value = 0;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM initLLVM(argc, argv);
  obelisk::driver::DriverConfiguration config;
  if (argc == 2 && llvm::StringRef(argv[1]) == "--test-print-sdf-limits") {
    // The production driver runs with these defaults.
    const obelisk::frontend::SDFResourceLimits &limits = config.sdfLimits;
    llvm::outs() << "file-bytes=" << limits.maxFileBytes << '\n'
                 << "cache-bytes=" << limits.maxCacheBytes << '\n'
                 << "cache-files=" << limits.maxCacheFiles << '\n'
                 << "cache-entries=" << limits.maxCacheEntries << '\n'
                 << "matched-updates=" << limits.maxMatchedUpdates << '\n'
                 << "application-work=" << limits.maxApplicationWork << '\n';
    return 0;
  }

  llvm::SmallVector<char *> forwarded;
  forwarded.push_back(argv[0]);
  for (int index = 1; index < argc; ++index) {
    llvm::StringRef argument(argv[index]);
    if (parseLimit(argument, "--test-max-sdf-file-bytes=",
                   config.sdfLimits.maxFileBytes) ||
        parseLimit(argument, "--test-max-sdf-cache-bytes=",
                   config.sdfLimits.maxCacheBytes) ||
        parseLimit(argument, "--test-max-sdf-cache-files=",
                   config.sdfLimits.maxCacheFiles) ||
        parseLimit(argument, "--test-max-sdf-cache-entries=",
                   config.sdfLimits.maxCacheEntries) ||
        parseLimit(argument, "--test-max-sdf-matched-updates=",
                   config.sdfLimits.maxMatchedUpdates) ||
        parseLimit(argument, "--test-max-sdf-application-work=",
                   config.sdfLimits.maxApplicationWork))
      continue;
    forwarded.push_back(argv[index]);
  }
  return obelisk::driver::runObeliskDriver(static_cast<int>(forwarded.size()),
                                           forwarded.data(), config);
}
