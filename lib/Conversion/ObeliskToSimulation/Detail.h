//===- Detail.h - Shared semantic-to-simulation lowering helpers -*- C++ -*===//
//
// Helpers shared by the four passes that lower elaborated `obelisk.sv`
// semantic IR to executable `obelisk_sim` SSA. Nothing here is part of the
// public conversion interface.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace obelisk::simlowering {

namespace sim = ::obelisk::sim;
namespace semantic = ::obelisk::ir;

/// Iterative Tarjan strongly connected components.
///
/// Recursion is deliberately avoided: generated process CFGs reach tens of
/// thousands of blocks, which overflows the stack long before it exhausts
/// memory. Components come back in reverse topological order with sorted
/// members, so every caller gets a deterministic result.
template <typename Node>
::mlir::SmallVector<::mlir::SmallVector<Node>>
computeStronglyConnectedComponents(
    ::mlir::ArrayRef<Node> nodes,
    const ::llvm::DenseMap<Node, ::mlir::SmallVector<Node>> &adjacency) {
  ::llvm::DenseMap<Node, unsigned> index, lowlink;
  ::llvm::DenseSet<Node> onStack;
  ::mlir::SmallVector<Node> tarjanStack;
  unsigned nextIndex = 0;
  ::mlir::SmallVector<::mlir::SmallVector<Node>> components;

  struct Frame {
    Node node;
    size_t nextSuccessor = 0;
    std::optional<Node> parent;
  };
  for (Node root : nodes) {
    if (index.count(root))
      continue;
    index[root] = lowlink[root] = nextIndex++;
    tarjanStack.push_back(root);
    onStack.insert(root);
    ::mlir::SmallVector<Frame> dfs{{root, 0, std::nullopt}};
    while (!dfs.empty()) {
      Frame &frame = dfs.back();
      auto found = adjacency.find(frame.node);
      ::mlir::ArrayRef<Node> successors =
          found == adjacency.end() ? ::mlir::ArrayRef<Node>()
                                   : ::mlir::ArrayRef<Node>(found->second);
      if (frame.nextSuccessor < successors.size()) {
        Node successor = successors[frame.nextSuccessor++];
        if (!index.count(successor)) {
          index[successor] = lowlink[successor] = nextIndex++;
          tarjanStack.push_back(successor);
          onStack.insert(successor);
          dfs.push_back({successor, 0, frame.node});
        } else if (onStack.contains(successor)) {
          lowlink[frame.node] = std::min(lowlink[frame.node], index[successor]);
        }
        continue;
      }

      Node node = frame.node;
      std::optional<Node> parent = frame.parent;
      dfs.pop_back();
      if (parent)
        lowlink[*parent] = std::min(lowlink[*parent], lowlink[node]);
      if (lowlink[node] != index[node])
        continue;
      ::mlir::SmallVector<Node> component;
      while (true) {
        Node member = tarjanStack.pop_back_val();
        onStack.erase(member);
        component.push_back(member);
        if (member == node)
          break;
      }
      ::llvm::sort(component);
      components.push_back(std::move(component));
    }
  }
  return components;
}

/// Metadata the prepare pass freezes onto each code unit for the unit
/// lowering, and that finalization removes again.
inline constexpr ::llvm::StringLiteral bindingsAttrName =
    sim::metadata::bindings;
inline constexpr ::llvm::StringLiteral delayScaleAttrName =
    sim::metadata::delayScale;
inline constexpr ::llvm::StringLiteral delayQuantumAttrName =
    sim::metadata::delayQuantum;
inline constexpr ::mlir::StringLiteral calleeAttrName = "obelisk_sim.callee";
inline constexpr ::mlir::StringLiteral calleeCapturesAttrName =
    "obelisk_sim.callee_captures";
inline constexpr ::mlir::StringLiteral calleeReadCapturesAttrName =
    "obelisk_sim.callee_read_captures";
inline constexpr ::mlir::StringLiteral calleeFormalsAttrName =
    "obelisk_sim.callee_formals";
inline constexpr ::mlir::StringLiteral staticClassPropertyAttrName =
    "obelisk_sim.static_class_property";
inline constexpr ::mlir::StringLiteral staticClassReceiverAttrName =
    "obelisk_sim.static_class_receiver";
inline constexpr ::mlir::StringLiteral dynamicCastEnumValuesAttrName =
    "obelisk_sim.dynamic_cast_enum_values";
inline constexpr ::mlir::StringLiteral dynamicCastTaskAttrName =
    "obelisk_sim.dynamic_cast_task";
inline constexpr ::mlir::StringLiteral placeholderAttrName =
    "obelisk_sim.placeholder";
inline constexpr ::mlir::StringLiteral staticNetConstantAttrName =
    "obelisk_sim.static_net_constant";
inline constexpr ::llvm::StringLiteral captureKindAttrName =
    sim::metadata::captureKind;
inline constexpr ::llvm::StringLiteral descriptorIdAttrName =
    sim::metadata::descriptorId;
inline constexpr ::mlir::StringLiteral observerResultAttrName =
    "obelisk_sim.observer_result";
inline constexpr ::mlir::StringLiteral observerEventPrimaryAttrName =
    "obelisk_sim.event_primary";
inline constexpr ::mlir::StringLiteral randomizeAttrName =
    "obelisk_sim.randomize";
inline constexpr ::mlir::StringLiteral randomizeDispatchAttrName =
    "obelisk_sim.randomize_dispatch";
inline constexpr ::mlir::StringLiteral randomizePlanClassAttrName =
    "obelisk_sim.randomize_plan_class";
inline constexpr ::mlir::StringLiteral objectRandomDispatchClassesAttrName =
    "obelisk_sim.object_random_dispatch_classes";
inline constexpr ::mlir::StringLiteral randomizeCheckerOnlyAttrName =
    "obelisk_sim.randomize_checker_only";
inline constexpr ::mlir::StringLiteral randomReceiverIndexAttrName =
    "obelisk_sim.random_receiver_index";
inline constexpr ::mlir::StringLiteral randomPreHookAttrName =
    "obelisk_sim.random_pre_hook";
inline constexpr ::mlir::StringLiteral randomPreHookOwnerAttrName =
    "obelisk_sim.random_pre_hook_owner";
inline constexpr ::mlir::StringLiteral randomPreHookSourceAttrName =
    "obelisk_sim.random_pre_hook_source";
inline constexpr ::mlir::StringLiteral randomPreHookCapturesAttrName =
    "obelisk_sim.random_pre_hook_captures";
inline constexpr ::mlir::StringLiteral randomPreHookReadCapturesAttrName =
    "obelisk_sim.random_pre_hook_read_captures";
inline constexpr ::mlir::StringLiteral randomPostHookAttrName =
    "obelisk_sim.random_post_hook";
inline constexpr ::mlir::StringLiteral randomPostHookOwnerAttrName =
    "obelisk_sim.random_post_hook_owner";
inline constexpr ::mlir::StringLiteral randomPostHookSourceAttrName =
    "obelisk_sim.random_post_hook_source";
inline constexpr ::mlir::StringLiteral randomPostHookCapturesAttrName =
    "obelisk_sim.random_post_hook_captures";
inline constexpr ::mlir::StringLiteral randomPostHookReadCapturesAttrName =
    "obelisk_sim.random_post_hook_read_captures";
inline constexpr ::mlir::StringLiteral randomModeAttrName =
    "obelisk_sim.rand_mode";
inline constexpr ::mlir::StringLiteral randomModePropertyAttrName =
    "obelisk_sim.rand_mode_property";
inline constexpr ::mlir::StringLiteral constraintModeAttrName =
    "obelisk_sim.constraint_mode";
inline constexpr ::mlir::StringLiteral constraintModeBlockAttrName =
    "obelisk_sim.constraint_mode_block";
inline constexpr ::mlir::StringLiteral constraintModeStaticStorageAttrName =
    "obelisk_sim.constraint_mode_static_storage";
inline constexpr ::mlir::StringLiteral constraintModeStaticStoragesAttrName =
    "obelisk_sim.constraint_mode_static_storages";
inline constexpr ::mlir::StringLiteral staticConstraintStorageAttrName =
    "obelisk_sim.static_constraint_storage";
inline constexpr ::mlir::StringLiteral randomConstraintBlockAttrName =
    "obelisk_sim.random_constraint_block";
inline constexpr ::mlir::StringLiteral randomConstraintCountAttrName =
    "obelisk_sim.random_constraint_count";
inline constexpr ::mlir::StringLiteral randomPropertiesAttrName =
    "obelisk_sim.random_properties";
inline constexpr ::mlir::StringLiteral randomTotalWidthAttrName =
    "obelisk_sim.random_total_width";
inline constexpr ::mlir::StringLiteral randomVariableAttrName =
    "obelisk_sim.random_variable";
inline constexpr ::mlir::StringLiteral randomVariableBitOffsetAttrName =
    "obelisk_sim.random_variable_bit_offset";
inline constexpr ::mlir::StringLiteral randomFunctionStateAttrName =
    "obelisk_sim.random_function_state";
inline constexpr ::mlir::StringLiteral randomFunctionOrderAttrName =
    "obelisk_sim.random_function_order";

/// Result representation frozen by prepare for computed timing observers.
/// Keep this strongly typed at both ends of the private pass boundary so a
/// malformed integer attribute cannot silently select value semantics.
enum class ObserverResult : uint32_t {
  None,
  Value,
  Truth,
  Event,
};

inline std::optional<ObserverResult>
parseObserverResult(::mlir::IntegerAttr attribute) {
  if (!attribute)
    return std::nullopt;
  switch (attribute.getValue().getZExtValue()) {
  case static_cast<uint32_t>(ObserverResult::Value):
    return ObserverResult::Value;
  case static_cast<uint32_t>(ObserverResult::Truth):
    return ObserverResult::Truth;
  case static_cast<uint32_t>(ObserverResult::Event):
    return ObserverResult::Event;
  default:
    return std::nullopt;
  }
}

/// True for any operation in the elaborated semantic dialect.
bool isSemanticOp(::mlir::Operation *op);

/// True for the semantic symbols that become one `obelisk_sim.func`.
bool isCodeUnit(::mlir::Operation *op);

/// True for an elaborated design member nested in Slang's synthetic instance
/// used only to describe a parameterized virtual-interface type. Declarations
/// nested in an actual runtime type (for example a class) remain executable.
bool isCompileTimeOnlyInstanceMember(::mlir::Operation *op);

/// Source location of a semantic node, falling back to its MLIR location.
::mlir::Location getSemanticLocation(::mlir::Operation *op);

/// Operations in the first block of an AST node's inventory region.
::mlir::SmallVector<::mlir::Operation *> getChildren(::mlir::Operation *op);

/// Literal spelling of an integer node, including constants frozen by the
/// prepare pass after their defining symbol is no longer in scope.
std::optional<::mlir::StringRef>
getConstantSpelling(::mlir::Operation *operation);

/// Fold a pure, already-lowered SSA chain without rewriting its surrounding
/// CFG. This also follows constants frozen from elaborated parameters.
::mlir::Attribute foldConstantValue(::mlir::Value value);
std::optional<bool> foldConstantTruth(::mlir::Value value);

/// Whether an expression denotes stable storage that the scheduler can watch
/// directly, without a computed observer.
bool isAddressableExpression(::mlir::Operation *operation);

/// Whether a semantic endpoint is the unbounded `$` literal, ignoring
/// source-level conversion wrappers.
bool isUnboundedEndpoint(::mlir::Operation *operation);

/// Deterministic nonzero identifier for an outlined semantic code unit.
uint64_t stableCodeUnitID(::mlir::StringRef key);

/// Whether overriding a captured reference mutates design/static storage
/// rather than activation-local automatic storage.
bool isStaticallyAllocatedOverrideTarget(::mlir::Value value);

/// Whether a packed semantic type is signed.
bool isSignedSemanticType(::mlir::Type type);

/// Fixed SystemVerilog bitstream width of a semantic type.
///
/// Unlike the packed-width query used for SSA normalization, this includes
/// fixed unpacked arrays and aggregates, as required by `$bits`. Dynamic
/// bitstreams have no type-only width and return std::nullopt.
std::optional<uint64_t> getSemanticBitstreamWidth(::mlir::Type type);

/// One source-level dimension of an elaborated SystemVerilog type.
///
/// Dimensions are ordered outermost first, matching the one-based dimension
/// index accepted by the array query system functions. Fixed dimensions retain
/// their declared direction and endpoints. The remaining kinds require an
/// object value (or an open-array descriptor) to answer range queries.
enum class SemanticDimensionKind : uint8_t {
  Fixed,
  String,
  DynamicArray,
  Queue,
  AssociativeArray,
  OpenArray,
};

struct SemanticDimension {
  SemanticDimensionKind kind = SemanticDimensionKind::Fixed;
  bool unpacked = false;
  int64_t left = 0;
  int64_t right = 0;
  ::mlir::Type indexType;

  bool isFixed() const { return kind == SemanticDimensionKind::Fixed; }
};

/// Source-level dimensions of a semantic type, ordered outermost first.
///
/// This is a shape query only: it never evaluates an object. Dynamic container
/// dimensions are represented explicitly so callers cannot accidentally fold
/// a value-dependent query as if it were a fixed range.
::mlir::SmallVector<SemanticDimension> getSemanticDimensions(::mlir::Type type);

/// Normalized type of a semantic node's `semantic_type` attribute.
::mlir::FailureOr<::mlir::Type>
getNormalizedSemanticType(::mlir::Operation *op);
::mlir::FailureOr<::mlir::Type>
normalizeSemanticType(::mlir::Type type, ::mlir::Location location);

/// Stable flat executable symbol for a semantic class reference. Semantic
/// paths may be nested and coexist with the generated design between passes;
/// executable descriptors therefore use their own collision-free namespace.
::mlir::StringAttr
getSimulationClassSymbol(::mlir::SymbolRefAttr semanticClass);
::mlir::StringAttr
getSimulationCovergroupSymbol(::mlir::SymbolRefAttr semanticCovergroup);

/// Classify a canonical source-semantic type for the initial DPI-C ABI.
/// Diagnostics are issued at `location` for unsupported categories.
::mlir::FailureOr<DPIABIKind> getDPIABIKind(::mlir::Type type,
                                            ::mlir::Location location);

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

/// Freeze the elaborated value and normalized type of a constant symbol.
/// The symbol must carry semantic_type and constant_value attributes.
::mlir::FailureOr<sim::FrozenConstantAttr>
freezeSemanticConstant(::mlir::Operation *symbol);

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

using DescriptorProvenance = ::obelisk::analysis::DescriptorProvenance;
using DescriptorProvenanceMap = ::obelisk::analysis::DescriptorProvenanceMap;

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H
