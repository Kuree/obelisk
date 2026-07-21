//===- Detail.h - Shared semantic-to-simulation lowering helpers -*- C++ -*===//
//
// Helpers shared by the four passes that lower elaborated `obelisk.sv`
// semantic IR to executable `obelisk_sim` SSA. Nothing here is part of the
// public conversion interface.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H

#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <optional>

namespace obelisk::simlowering {

namespace sim = ::obelisk::sim;
namespace semantic = ::obelisk::ir;

/// Metadata the prepare pass freezes onto each code unit for the unit
/// lowering, and that finalization removes again.
inline constexpr ::mlir::StringLiteral bindingsAttrName =
    "obelisk_sim.bindings";
inline constexpr ::mlir::StringLiteral delayScaleAttrName =
    "obelisk_sim.delay_scale";
inline constexpr ::mlir::StringLiteral calleeAttrName = "obelisk_sim.callee";
inline constexpr ::mlir::StringLiteral calleeCapturesAttrName =
    "obelisk_sim.callee_captures";
inline constexpr ::mlir::StringLiteral calleeFormalsAttrName =
    "obelisk_sim.callee_formals";
inline constexpr ::mlir::StringLiteral placeholderAttrName =
    "obelisk_sim.placeholder";
inline constexpr ::mlir::StringLiteral captureKindAttrName =
    "obelisk_sim.capture_kind";
inline constexpr ::mlir::StringLiteral descriptorIdAttrName =
    "obelisk_sim.descriptor_id";

/// True for any operation in the elaborated semantic dialect.
bool isSemanticOp(::mlir::Operation *op);

/// True for the semantic symbols that become one `obelisk_sim.func`.
bool isCodeUnit(::mlir::Operation *op);

/// Source location of a semantic node, falling back to its MLIR location.
::mlir::Location getSemanticLocation(::mlir::Operation *op);

/// Operations in the first block of an AST node's inventory region.
::mlir::SmallVector<::mlir::Operation *> getChildren(::mlir::Operation *op);

/// Whether a packed semantic type is signed.
bool isSignedSemanticType(::mlir::Type type);

/// Normalized type of a semantic node's `semantic_type` attribute.
::mlir::FailureOr<::mlir::Type>
getNormalizedSemanticType(::mlir::Operation *op);

/// Elaborated hierarchical path of a semantic symbol, or its plain name.
::mlir::StringRef getHierarchyName(::mlir::Operation *op);

/// Declared name of a semantic symbol, for diagnostics and debug info.
::mlir::StringRef getDebugName(::mlir::Operation *op);

/// Value and unknown planes of a parsed SystemVerilog integer literal.
struct ParsedConstant {
  ::llvm::APInt value;
  ::llvm::APInt unknown;
};

/// Parse a SystemVerilog integer literal into `width` bits. Fails when the
/// literal is malformed or does not fit, rather than silently truncating.
::mlir::FailureOr<ParsedConstant> parseSVInteger(::mlir::StringRef spelling,
                                                 unsigned width,
                                                 ::mlir::Location location);

/// The all-unknown (or zero) initial value of a normalized type.
::mlir::Value createDefaultValue(::mlir::OpBuilder &builder,
                                 ::mlir::Location location, ::mlir::Type type);

/// Argument metadata describing how an entry argument is bound.
::mlir::DictionaryAttr
captureMetadata(::mlir::OpBuilder &builder, sim::CaptureKind kind,
                std::optional<uint64_t> descriptorId = std::nullopt);

/// True for the terminators that end a fragment and resume a continuation.
bool isSuspensionTerminator(::mlir::Operation *op);

/// Shared suspension/action metadata access. Keeping the operation family in
/// one place prevents graph construction and verification from drifting when
/// a new suspension form is introduced.
sim::ComputeActionKind getFragmentActionKind(::mlir::Operation *terminator);
sim::ContinuationSiteAttr getContinuationSite(::mlir::Operation *operation);
void setContinuationSite(::mlir::Operation *operation,
                         sim::ContinuationSiteAttr site);

/// Blocks that control can return to later in the process lifetime, including
/// across suspension boundaries. Computed once per function in linear time.
using ReexecutingBlockSet = ::llvm::DenseSet<::mlir::Block *>;
ReexecutingBlockSet getReexecutingBlocks(sim::SimFuncOp function);

/// Whether a time value is transitively carried from compile-time constants,
/// including through continuation block arguments added by frame threading.
bool isConstantTimeValue(::mlir::Value value);

/// Concrete descriptor provenance recomputed from executable SSA/CFG.
struct DescriptorProvenance {
  sim::ComputeResourceKind resource = sim::ComputeResourceKind::Unknown;
  std::optional<uint64_t> descriptor;
  std::optional<unsigned> formal;
  uint64_t low = 0;
  uint64_t width = 0;
  uint64_t rootWidth = 0;
  bool dynamic = false;
};
using DescriptorProvenanceMap =
    ::llvm::DenseMap<::mlir::Value, DescriptorProvenance>;

/// Recompute whole-program effects and four-state knownness from executable IR
/// and compare them with a derived compute graph. Optionally returns the same
/// provenance facts so later verifier checks do not duplicate SSA reasoning.
::mlir::LogicalResult
verifyRecomputedComputeAnalysis(sim::SimDesignOp design,
                                sim::ComputeGraphAttr graph,
                                DescriptorProvenanceMap *provenance = nullptr);

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H
