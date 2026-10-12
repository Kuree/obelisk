//===- DriverMain.h - Shared Obelisk driver entry point --------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_DRIVERMAIN_H
#define OBELISK_TOOLS_DRIVER_DRIVERMAIN_H

#include "obelisk/Frontend/SDF.h"

namespace obelisk::driver {

/// Build-time configuration of a driver executable. The production driver
/// uses the defaults; the test driver lowers resource limits so tests can
/// reach them with small inputs.
struct DriverConfiguration {
  frontend::SDFResourceLimits sdfLimits;
};

int runObeliskDriver(int argc, char **argv,
                     const DriverConfiguration &config = {});

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_DRIVERMAIN_H
