//===- ProtectedEnvelopeTestMain.cpp - Test-only driver ------------------===//

#include "ProtectedEnvelopeTestProvider.h"

#include "DriverMain.h"

#include "obelisk/Frontend/ProtectedEnvelope.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <charconv>
#include <cstdint>
#include <string_view>

namespace {

bool isZeroed(const char *data, size_t capacity) {
  for (size_t index = 0; index < capacity; ++index) {
    if (data[index] != 0)
      return false;
  }
  return true;
}

bool testProtectedSourceBuffer() {
  using namespace obelisk::frontend;

  ProtectedEnvelopeResult reserved;
  reserved.source.reserve(257);
  reserved.source.resize(257, 'S');
  reserved.source.resize(3);
  char *reservedData = reserved.source.data();
  size_t reservedCapacity = reserved.source.capacity();
  if (!isZeroed(reservedData + 3, 254))
    return false;
  reserved.clear();
  if (!isZeroed(reservedData, reservedCapacity))
    return false;

  ProtectedEnvelopeResult movedFrom;
  movedFrom.source.reserve(193);
  movedFrom.source.resize(193, 'M');
  movedFrom.source.resize(5);
  char *movedData = movedFrom.source.data();
  size_t movedCapacity = movedFrom.source.capacity();
  ProtectedEnvelopeResult movedTo;
  movedTo.source.resize(71, 'D');
  movedTo = std::move(movedFrom);
  movedTo.clear();
  if (!isZeroed(movedData, movedCapacity))
    return false;

  ProtectedEnvelopeResult shrunk;
  shrunk.source.reserve(131);
  shrunk.source.resize(131, 'T');
  shrunk.source.resize(7);
  shrunk.source.shrink_to_fit();
  char *shrunkData = shrunk.source.data();
  size_t shrunkCapacity = shrunk.source.capacity();
  shrunk.clear();
  return isZeroed(shrunkData, shrunkCapacity);
}

template <typename Integer>
bool parseLimit(llvm::StringRef argument, llvm::StringRef prefix,
                Integer &value) {
  if (!argument.consume_front(prefix))
    return false;
  auto result = std::from_chars(argument.begin(), argument.end(), value, 10);
  if (result.ec != std::errc() || result.ptr != argument.end()) {
    llvm::errs() << "invalid test-only protected-envelope limit\n";
    value = 0;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM initLLVM(argc, argv);
  if (argc == 2 && llvm::StringRef(argv[1]) == "--test-protect-secure-buffer") {
    if (!testProtectedSourceBuffer()) {
      llvm::errs() << "protected source buffer wipe failed\n";
      return 1;
    }
    llvm::outs() << "protected source buffer wipe passed\n";
    return 0;
  }
  obelisk::driver::ProtectedEnvelopeConfiguration config;
  config.provider = createProtectedEnvelopeTestProvider();

  llvm::SmallVector<char *> forwarded;
  forwarded.push_back(argv[0]);
  for (int index = 1; index < argc; ++index) {
    llvm::StringRef argument(argv[index]);
    if (parseLimit(argument, "--test-max-protect-depth=", config.maxDepth) ||
        parseLimit(argument, "--test-max-protect-bytes=", config.maxBytes) ||
        parseLimit(argument, "--test-max-protect-count=", config.maxCount))
      continue;
    forwarded.push_back(argv[index]);
  }
  return obelisk::driver::runObeliskDriver(static_cast<int>(forwarded.size()),
                                           forwarded.data(), config);
}
