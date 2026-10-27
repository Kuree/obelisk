//===- WasmBackend.h - wasm32 backend ---------------------------*- C++ -*-===//

#ifndef TALOS_TOOLS_DRIVER_WASMBACKEND_H
#define TALOS_TOOLS_DRIVER_WASMBACKEND_H

#include "TargetBackend.h"

#include <memory>

namespace talos::driver {

/// Builds the wasm32 backend. Only available when the LLVM distribution this
/// was built against includes the WebAssembly target and LLD's wasm driver.
std::unique_ptr<TargetBackend> createWasmBackend();

} // namespace talos::driver

#endif // TALOS_TOOLS_DRIVER_WASMBACKEND_H
