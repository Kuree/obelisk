//===- RandomArithmetic.h - Integral constraint value and unknown planes ===//

#ifndef OBELISK_CONVERSION_RANDOMARITHMETIC_H
#define OBELISK_CONVERSION_RANDOMARITHMETIC_H

#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <functional>
#include <memory>

namespace obelisk::simlowering {

inline constexpr llvm::StringLiteral randomArithmeticNodeAttr =
    "simulation.random_arithmetic_node";
inline constexpr llvm::StringLiteral randomArithmeticPlaneAttr =
    "simulation.random_arithmetic_plane";

/// Compiler-only, two-state expressions for IEEE integral arithmetic. A null
/// unknown plane means every bit is known. Unknown value bits are canonical
/// zero, so a two-state cast can consume them without a target-side solver.
struct RandomArithmeticPlanes {
  mlir::Operation *value;
  mlir::Operation *unknown;
};

class RandomArithmetic {
public:
  using Width = std::function<mlir::FailureOr<unsigned>(mlir::Operation *)>;
  using Candidate = std::function<bool(mlir::Operation *)>;
  RandomArithmetic(mlir::MLIRContext *context, Width width,
                   Candidate candidate);
  ~RandomArithmetic();

  bool needed(mlir::Operation *expression);
  mlir::FailureOr<RandomArithmeticPlanes> get(mlir::Operation *expression);
  mlir::FailureOr<mlir::Operation *> known(mlir::Operation *expression);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace obelisk::simlowering

#endif
