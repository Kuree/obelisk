//===- ContainerElementDescriptor.h - Managed element layouts -*- C++ -*-===//
#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_CONTAINERELEMENTDESCRIPTOR_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_CONTAINERELEMENTDESCRIPTOR_H
#include "mlir/IR/Location.h"
#include "mlir/IR/Types.h"
#include "obelisk/Dialect/Simulation/SimulationEnums.h"
#include "llvm/ADT/SmallVector.h"
namespace obelisk::simlowering {
struct ContainerElementDescriptor {
  uint64_t typeID;
  sim::ElementKind kind;
  sim::ElementFlags flags;
  uint64_t valueSize;
  uint64_t alignment;
  uint64_t bitWidth;
  ::mlir::SmallVector<int64_t, 2> traceOffsets;
  ::mlir::SmallVector<int32_t, 2> traceKinds;
};

::mlir::FailureOr<ContainerElementDescriptor>
describeContainerElement(::mlir::Type type, ::mlir::Location location);

} // namespace obelisk::simlowering
#endif
