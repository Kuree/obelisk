//===- NativeBackend.h - Host-native Linux ELF backend ----------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H
#define OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H

#include "TargetBackend.h"

#include <memory>

namespace obelisk::driver {

/// Builds the host-native backend registered for this platform by CMake.
std::unique_ptr<TargetBackend> createNativeBackend();

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H
