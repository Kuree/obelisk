//===- DriverMain.h - Shared Talos driver entry point --------*- C++ -*-===//

#ifndef TALOS_TOOLS_DRIVER_DRIVERMAIN_H
#define TALOS_TOOLS_DRIVER_DRIVERMAIN_H

#include "talos/Frontend/SDF.h"

namespace talos::driver {

/// Build-time configuration of a driver executable. The production driver
/// uses the defaults; the test driver lowers resource limits so tests can
/// reach them with small inputs.
struct DriverConfiguration {
  frontend::SDFResourceLimits sdfLimits;
};

int runTalosDriver(int argc, char **argv,
                     const DriverConfiguration &config = {});

} // namespace talos::driver

#endif // TALOS_TOOLS_DRIVER_DRIVERMAIN_H
