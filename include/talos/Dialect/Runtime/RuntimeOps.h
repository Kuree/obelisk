//===- RuntimeOps.h - Typed runtime ABI operations ------------*- C++ -*-===//

#ifndef TALOS_DIALECT_RUNTIME_RUNTIMEOPS_H
#define TALOS_DIALECT_RUNTIME_RUNTIMEOPS_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "talos/Dialect/Runtime/RuntimeAttrs.h"
#include "talos/Dialect/Runtime/RuntimeTypes.h"

namespace talos::runtime {

#define TALOS_RT_RESOURCE(Name, Text)                                        \
  struct Name : public ::mlir::SideEffects::Resource::Base<Name> {             \
    ::llvm::StringRef getName() final { return Text; }                         \
  }

TALOS_RT_RESOURCE(RuntimeResource, "runtime.runtime");
TALOS_RT_RESOURCE(IOResource, "runtime.io");

#undef TALOS_RT_RESOURCE

} // namespace talos::runtime

#define GET_OP_CLASSES
#include "talos/Dialect/Runtime/RuntimeOps.h.inc"

#endif // TALOS_DIALECT_RUNTIME_RUNTIMEOPS_H
