//===- NativeBackend.h - Registered host-native backend ---------*- C++ -*-===//

#ifndef TALOS_TOOLS_DRIVER_NATIVEBACKEND_H
#define TALOS_TOOLS_DRIVER_NATIVEBACKEND_H

#include "TargetBackend.h"

#include <memory>

namespace talos::driver {

/// Builds the host-native backend registered for this platform by CMake.
std::unique_ptr<TargetBackend> createNativeBackend();

} // namespace talos::driver

#endif // TALOS_TOOLS_DRIVER_NATIVEBACKEND_H
