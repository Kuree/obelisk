//===- NativeBackend.h - Hermetic x86-64 ELF backend ------------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H
#define OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H

#include "TargetBackend.h"

#include <memory>

namespace obelisk::driver {

/// Builds the x86-64 backend. Only available when the LLVM distribution this
/// was built against includes the X86 target and LLD's ELF driver.
std::unique_ptr<TargetBackend> createNativeBackend();

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H
