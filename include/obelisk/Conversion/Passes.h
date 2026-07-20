//===- Passes.h - Obelisk conversion pass entrypoints ---------*- C++ -*-===//

#ifndef OBELISK_CONVERSION_PASSES_H
#define OBELISK_CONVERSION_PASSES_H

#include "mlir/Pass/Pass.h"

namespace obelisk {

#define GEN_PASS_DECL
#include "obelisk/Conversion/Passes.h.inc"

#define GEN_PASS_REGISTRATION
#include "obelisk/Conversion/Passes.h.inc"

} // namespace obelisk

#endif // OBELISK_CONVERSION_PASSES_H
