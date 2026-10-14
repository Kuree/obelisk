//===- RandomArithmetic.h - Integral constraint value and unknown planes ===//

#ifndef OBELISK_CONVERSION_RANDOMARITHMETIC_H
#define OBELISK_CONVERSION_RANDOMARITHMETIC_H

#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <functional>
#include <memory>
#include <optional>

namespace obelisk::simlowering {

enum class RandomArithmeticPlane : int32_t {
  Value,
  Unknown,
  FrozenUnknown,
  HighImpedance,
  FrozenHighImpedance,
};

/// Compiler-only, two-state expression graphs for IEEE integral arithmetic. A
/// null unknown plane means every bit is known. Unknown value bits are
/// canonical zero, so a two-state cast can consume them without a target-side
/// solver.
struct RandomArithmeticPlanes {
  mlir::Operation *value;
  mlir::Operation *unknown;
  // A subset of unknown bits needed by symbol-counting system functions.
  // Arithmetic produces X; casts, shifts and concatenation may preserve Z.
  mlir::Operation *highImpedance = nullptr;
};

class RandomArithmetic {
public:
  using Width = std::function<mlir::FailureOr<unsigned>(mlir::Operation *)>;
  using Candidate = std::function<bool(mlir::Operation *)>;
  using Binding = std::function<mlir::FailureOr<std::optional<unsigned>>(
      mlir::Operation *)>;
  RandomArithmetic(mlir::MLIRContext *context, Width width, Candidate candidate,
                   Binding binding, bool frozenVariables);
  ~RandomArithmetic();

  bool needed(mlir::Operation *expression);
  /// The generated operations are shallow lowering templates. Their region
  /// contains formal operands; the actual graph edges are returned here and
  /// must be bound before calling ordinary semantic expression lowering.
  llvm::SmallVector<mlir::Operation *> operands(mlir::Operation *expression);
  bool isNode(mlir::Operation *expression) const;
  bool isShared(mlir::Operation *expression) const;
  /// Intern immutable expression graphs without rewalking shared subgraphs.
  /// Safe captures can reuse a value across copies of the same expression.
  mlir::Operation *canonical(mlir::Operation *expression);
  mlir::FailureOr<RandomArithmeticPlanes> get(mlir::Operation *expression);
  mlir::FailureOr<mlir::Operation *> known(mlir::Operation *expression);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace obelisk::simlowering

#endif
