//===- Passes.h - Talos conversion pass entrypoints ---------*- C++ -*-===//

#ifndef TALOS_CONVERSION_PASSES_H
#define TALOS_CONVERSION_PASSES_H

#include "mlir/Pass/Pass.h"

namespace talos {

#define GEN_PASS_DECL
#include "talos/Conversion/Passes.h.inc"

#define GEN_PASS_REGISTRATION
#include "talos/Conversion/Passes.h.inc"

} // namespace talos

#endif // TALOS_CONVERSION_PASSES_H
