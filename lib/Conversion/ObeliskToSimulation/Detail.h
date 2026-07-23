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
inline constexpr ::mlir::StringLiteral placeholderAttrName =
    "obelisk_sim.placeholder";
inline constexpr ::llvm::StringLiteral captureKindAttrName =
    sim::metadata::captureKind;
inline constexpr ::llvm::StringLiteral descriptorIdAttrName =
    sim::metadata::descriptorId;

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
::mlir::FailureOr<::mlir::Type>
normalizeSemanticType(::mlir::Type type, ::mlir::Location location);

/// Classify a canonical source-semantic type for the initial DPI-C ABI.
/// Diagnostics are issued at `location` for unsupported categories.
::mlir::FailureOr<DPIABIKind>
getDPIABIKind(::mlir::Type type, ::mlir::Location location);

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

using DescriptorProvenance = ::obelisk::analysis::DescriptorProvenance;
using DescriptorProvenanceMap = ::obelisk::analysis::DescriptorProvenanceMap;

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_DETAIL_H
