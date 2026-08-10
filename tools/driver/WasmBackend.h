//===- WasmBackend.h - wasm64 backend ---------------------------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_WASMBACKEND_H
#define OBELISK_TOOLS_DRIVER_WASMBACKEND_H

#include "TargetBackend.h"

#include <memory>

namespace obelisk::driver {

/// Builds the wasm64 backend. Only available when the LLVM distribution this
/// was built against includes the WebAssembly target and LLD's wasm driver.
std::unique_ptr<TargetBackend> createWasmBackend();

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_WASMBACKEND_H
