//===- ContainerElementDescriptor.h - Managed element layouts -*- C++ -*-===//
#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_CONTAINERELEMENTDESCRIPTOR_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_CONTAINERELEMENTDESCRIPTOR_H
#include "mlir/IR/Location.h"
#include "mlir/IR/Types.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"
#include "llvm/ADT/SmallVector.h"
namespace obelisk::simlowering {
using ContainerElementDescriptor = sim::ElementDescriptor;

::mlir::FailureOr<ContainerElementDescriptor>
describeContainerElement(::mlir::Type type, ::mlir::Location location);

} // namespace obelisk::simlowering
#endif
