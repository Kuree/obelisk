//===- SimulationVerifiers.h - Shared verifier helpers ---------*- C++ -*-===//
//
// Helpers shared by the simulation dialect's verifier translation units.
// These are implementation details of ObeliskSimulationIR and are not part of
// the dialect's public interface.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_DIALECT_SIMULATION_SIMULATIONVERIFIERS_H
#define OBELISK_LIB_DIALECT_SIMULATION_SIMULATIONVERIFIERS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include <optional>

namespace obelisk::sim {

/// Verifies that a postponed-region function reads state without writing it.
mlir::LogicalResult verifyPostponedReadOnly(SimFuncOp root);

/// True when \p type is a four-state logic type or an aggregate containing one.
bool containsFourStateLeaf(mlir::Type type);

/// True when \p type is one of the normalized value types an executable
/// simulation op may produce.
bool isNormalizedValueType(mlir::Type type);

/// Returns the element type of a dynamic array, queue, or associative array.
mlir::Type getContainerElement(mlir::Type type);

/// Returns the capture kind recorded on an argument's attribute dictionary.
std::optional<CaptureKind> getCaptureKind(mlir::DictionaryAttr attrs);

/// Returns the 32-bit index attribute used to name aggregate subelements.
mlir::IntegerAttr getSubelementIndexAttr(mlir::MLIRContext *context,
                                         unsigned index);

/// Materializes the default value of \p type at \p location.
mlir::Value materializeDefaultValue(mlir::OpBuilder &builder,
                                    mlir::Location location, mlir::Type type);

/// Verifies that \p continuationOperands match the arguments of the block a
/// suspension or task-call op resumes into.
mlir::LogicalResult verifyContinuation(mlir::Operation *op,
                                       mlir::ValueRange continuationOperands,
                                       mlir::Block *continuation);

mlir::LogicalResult verifyNonnegative(mlir::Operation *op,
                                      mlir::IntegerAttr attr,
                                      mlir::StringRef name);
mlir::LogicalResult verifyPositive(mlir::Operation *op, mlir::IntegerAttr attr,
                                   mlir::StringRef name);
mlir::LogicalResult verifyNormalizedIndex(mlir::Operation *op, mlir::Type type);
mlir::LogicalResult verifyMatchingStateDomain(mlir::Operation *op,
                                              mlir::Type input,
                                              mlir::Type result);
mlir::LogicalResult verifyAssocKey(mlir::Operation *op, AssocArrayType array,
                                   mlir::Type key);
mlir::LogicalResult
verifyElementType(llvm::function_ref<mlir::InFlightDiagnostic()> emitError,
                  mlir::Type elementType);

} // namespace obelisk::sim

#endif // OBELISK_LIB_DIALECT_SIMULATION_SIMULATIONVERIFIERS_H
