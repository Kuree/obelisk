//===- RuntimeOps.h - Typed runtime ABI operations ------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEOPS_H
#define OBELISK_DIALECT_RUNTIME_RUNTIMEOPS_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Dialect/Runtime/RuntimeAttrs.h"
#include "obelisk/Dialect/Runtime/RuntimeTypes.h"

namespace obelisk::runtime {

#define OBELISK_RT_RESOURCE(Name, Text)                                        \
  struct Name : public ::mlir::SideEffects::Resource::Base<Name> {             \
    ::llvm::StringRef getName() final { return Text; }                         \
  }

OBELISK_RT_RESOURCE(RuntimeResource, "obelisk_rt.runtime");
OBELISK_RT_RESOURCE(IOResource, "obelisk_rt.io");

#undef OBELISK_RT_RESOURCE

} // namespace obelisk::runtime

#define GET_OP_CLASSES
#include "obelisk/Dialect/Runtime/RuntimeOps.h.inc"

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEOPS_H
