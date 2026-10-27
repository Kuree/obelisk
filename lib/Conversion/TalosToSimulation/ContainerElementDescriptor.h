//===- ContainerElementDescriptor.h - Managed element layouts -*- C++ -*-===//
#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_CONTAINERELEMENTDESCRIPTOR_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_CONTAINERELEMENTDESCRIPTOR_H
#include "mlir/IR/Location.h"
#include "mlir/IR/Types.h"
#include "talos/Dialect/Simulation/SimulationTypes.h"
#include "llvm/ADT/SmallVector.h"
namespace talos::simlowering {
using ContainerElementDescriptor = sim::ElementDescriptor;

::mlir::FailureOr<ContainerElementDescriptor>
describeContainerElement(::mlir::Type type, ::mlir::Location location);

} // namespace talos::simlowering
#endif
