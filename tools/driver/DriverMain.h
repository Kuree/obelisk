//===- DriverMain.h - Shared Obelisk driver entry point --------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_DRIVERMAIN_H
#define OBELISK_TOOLS_DRIVER_DRIVERMAIN_H

#include "obelisk/Frontend/SDF.h"

#include <cstdint>
#include <memory>

namespace obelisk::frontend {
class ProtectedEnvelopeProvider;
}

namespace obelisk::driver {

/// Build-time configuration of a driver executable. The production driver
/// uses the defaults; test drivers install a protected-envelope provider and
/// lower resource limits so tests can reach them with small inputs.
struct DriverConfiguration {
  std::shared_ptr<const frontend::ProtectedEnvelopeProvider> provider;
  uint32_t maxDepth = 64;
  uint64_t maxBytes = 64 * 1024 * 1024;
  uint32_t maxCount = 4096;
  frontend::SDFResourceLimits sdfLimits;
};

int runObeliskDriver(int argc, char **argv,
                     const DriverConfiguration &config = {});

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_DRIVERMAIN_H
