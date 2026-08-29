//===- DriverMain.h - Shared Obelisk driver entry point --------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_DRIVERMAIN_H
#define OBELISK_TOOLS_DRIVER_DRIVERMAIN_H

#include <cstdint>
#include <memory>

namespace obelisk::frontend {
class ProtectedEnvelopeProvider;
}

namespace obelisk::driver {

struct ProtectedEnvelopeConfiguration {
  std::shared_ptr<const frontend::ProtectedEnvelopeProvider> provider;
  uint32_t maxDepth = 64;
  uint64_t maxBytes = 64 * 1024 * 1024;
  uint32_t maxCount = 4096;
};

int runObeliskDriver(int argc, char **argv,
                     const ProtectedEnvelopeConfiguration &protectConfig = {});

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_DRIVERMAIN_H
