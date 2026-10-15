//===- PrepareRandomIteration.h - Frozen iterative constraints -*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARERANDOMITERATION_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARERANDOMITERATION_H

#include "mlir/IR/Builders.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"

#include <optional>

namespace obelisk::simlowering {

/// Bit width of the signed values of the constant and bound evaluators below,
/// wide enough for exact sums, products, and quotients of 64-bit values.
inline constexpr unsigned randomConstantWidth = 130;

/// Evaluate an integral constraint operand built from constants, constant
/// symbols, and +, -, *, /, %. The exact value is the SystemVerilog value
/// only when every subexpression is representable in its own type, so any
/// other operand, including one that wraps, has no value.
std::optional<llvm::APSInt> evaluateRandomConstantOperand(
    mlir::Operation *expression,
    llvm::function_ref<std::optional<llvm::StringRef>(mlir::Operation *)>
        referencedConstant);

/// The operand inside the conversions of `expression` that preserve its
/// value: to a type at least as wide of the same signedness, or strictly
/// wider and signed. A `nonnegative` operand also keeps its value through a
/// conversion to an unsigned type at least as wide.
mlir::Operation *peelValuePreservingConversions(mlir::Operation *expression,
                                                bool nonnegative = false);

/// The largest value of an integral operand, recognized by `isBounded`, that
/// the hard constraints in `constraint` permit by comparing it with operands
/// whose largest values `upperBound` knows, if they bound it. `isBounded`
/// sees the compared operand, including any conversion of it.
std::optional<llvm::APSInt> getRandomUpperBound(
    mlir::Operation *constraint,
    llvm::function_ref<bool(mlir::Operation *)> isBounded,
    llvm::function_ref<std::optional<llvm::APSInt>(mlir::Operation *)>
        upperBound);

/// Most constraint instances one randomize plan expands from a static
/// foreach, and most leading elements it solves for one random dynamic array
/// or queue.
inline constexpr uint64_t maxRandomContainerShadowElements = 256;

/// The leading elements of a random dynamic array or queue, solved as one
/// fixed array.
struct RandomContainerShadow {
  /// Number of shadowed leading elements.
  uint64_t count;
  /// A size() call on the container, cloned into each validity condition.
  mlir::Operation *sizeCall;
};

/// IEEE 1800-2023 18.5.7.1 iterates a foreach constraint over the current
/// elements of a dynamic array or queue and leaves invalid indices to the
/// user. For each container reference in `shadows`, a foreach becomes a
/// static iteration over the shadowed elements, each body guarded by its
/// index being below size(); a uniqueness group (18.5.4) includes only the
/// elements below size(); and each constraint that selects an element also
/// requires, or for a soft constraint prefers, that the index is below
/// size().
mlir::LogicalResult guardRandomContainerElements(
    mlir::Operation *&root,
    const llvm::DenseMap<mlir::Operation *, RandomContainerShadow> &shadows,
    mlir::Builder &builder);

/// Expand fixed foreach dimensions in declaration order before constraint
/// dependency analysis. Each iterator becomes a typed constant in its cloned
/// body; omitted dimensions do not iterate. Runtime dimensions remain intact.
mlir::LogicalResult expandStaticRandomForeachConstraints(
    llvm::MutableArrayRef<mlir::Operation *> roots, mlir::Builder &builder);

/// Replace fixed unpacked array and slice operands of unique constraints with
/// their leaf selections. Singular operands remain singular; duplicate leaves
/// retain their identity and consequently make the constraint unsatisfiable.
mlir::LogicalResult expandStaticRandomUniquenessConstraints(
    llvm::MutableArrayRef<mlir::Operation *> roots, mlir::Builder &builder);

/// Descend through fixed unpacked array members of an inside set until each
/// member is singular (11.4.13). Preserve declared coordinates and expression
/// types; ordinary membership lowering supplies the comparison context.
mlir::LogicalResult expandStaticRandomInsideExpressions(
    llvm::MutableArrayRef<mlir::Operation *> roots, mlir::Builder &builder);

/// After foreach expansion, replace each read of a shadowed container at a
/// constant index outside its shadow by the element's default value. No
/// permitted size keeps that element, so the read is invalid and its
/// validity condition makes any constraint created over it unsatisfiable;
/// the value itself is unobservable and must not be an evaluation error.
void replaceInvalidRandomContainerReads(
    llvm::MutableArrayRef<mlir::Operation *> roots,
    llvm::function_ref<bool(mlir::Operation *)> isShadowReference,
    llvm::function_ref<std::optional<llvm::APSInt>(mlir::Operation *)> evaluate,
    mlir::Builder &builder);

} // namespace obelisk::simlowering

#endif
