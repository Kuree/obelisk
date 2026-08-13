//===- SimulationDialect.cpp - Executable simulation dialect ------------===//

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

#include "obelisk/Dialect/Simulation/SimulationDialect.cpp.inc"
#include "obelisk/Dialect/Simulation/SimulationEnums.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationAttrs.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationTypes.cpp.inc"

// The op definitions are compiled by the SimulationOpDefs*.cpp shards.

namespace obelisk::sim {

bool isSuspensionOp(Operation *operation) {
  return isa<SimSuspendDelayOp, SimSuspendChangeOp, SimSuspendEdgeOp,
             SimSuspendEdgeIffOp, SimSuspendLevelOp, SimSuspendAnyOp,
             SimSuspendEventOp, SimSuspendMailboxOp, SimSuspendSemaphoreOp,
             SimSuspendForeverOp, SimSuspendAwaitOp, SimSuspendJoinOp,
             SimSuspendChildrenOp, SimSuspendObserveOp, SimTaskCallOp,
             SimClassVirtualTaskCallOp, SimProcessControlOp>(operation);
}

bool isStartupEntryKind(EntryKind kind) {
  switch (kind) {
  case EntryKind::Always:
  case EntryKind::AlwaysFF:
  case EntryKind::Continuous:
  case EntryKind::PortInput:
  case EntryKind::PortOutput:
  case EntryKind::PortInitialize:
    return true;
  default:
    return false;
  }
}

uint32_t getWaitEntryCount(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<SimSuspendChangeOp, SimSuspendLevelOp, SimSuspendEdgeOp,
            SimSuspendEventOp, SimSuspendMailboxOp, SimSuspendSemaphoreOp,
            SimSuspendAwaitOp>([](auto) { return 1; })
      .Case<SimSuspendEdgeIffOp>([](auto) { return 2; })
      .Case<SimSuspendAnyOp>(
          [](auto op) { return static_cast<uint32_t>(op.getWatched().size()); })
      .Case<SimSuspendJoinOp>([](auto op) {
        return static_cast<uint32_t>(op.getProcesses().size());
      })
      .Default([](Operation *) { return 0; });
}

namespace {

bool hasLateInlineMetadata(SimDesignOp design) {
  if (design.getComputeGraphAttr())
    return true;
  bool found = false;
  design.walk([&](Operation *operation) {
    if (auto function = dyn_cast<SimFuncOp>(operation))
      found |= static_cast<bool>(function.getEffectSummaryAttr()) ||
               static_cast<bool>(function.getFragmentAbiAttr());
    for (NamedAttribute named : operation->getAttrs())
      found |=
          isa<ContinuationSiteAttr, TimingSiteAttr, NBASiteAttr, EventSiteAttr>(
              named.getValue());
  });
  return found;
}

bool hasUnknownInlineMetadata(Operation *operation) {
  for (NamedAttribute named : operation->getAttrs()) {
    StringRef name = named.getName().strref();
    if (!name.starts_with("obelisk_sim."))
      continue;
    if (metadata::isKnownOperation(name))
      continue;
    return true;
  }
  return false;
}

bool hasUnknownInlineBoundaryMetadata(ArrayAttr dictionaries) {
  if (!dictionaries)
    return false;
  for (Attribute attribute : dictionaries) {
    auto dictionary = dyn_cast<DictionaryAttr>(attribute);
    if (!dictionary)
      return true;
    for (NamedAttribute named : dictionary) {
      StringRef name = named.getName().strref();
      if (name.starts_with("obelisk_sim.") && !metadata::isKnownBoundary(name))
        return true;
    }
  }
  return false;
}

template <typename Callback>
void forEachDirectCall(SimFuncOp function, Callback &&callback) {
  function.getBody().walk([&](Operation *operation) {
    if (isa<SimFuncOp>(operation))
      return WalkResult::skip();
    if (auto call = dyn_cast<SimCallOp>(operation))
      callback(call);
    return WalkResult::advance();
  });
}

bool reaches(SimFuncOp from, SimFuncOp target,
             const llvm::StringMap<SimFuncOp> &functions) {
  SmallVector<SimFuncOp> pending{from};
  llvm::SmallPtrSet<Operation *, 16> visited;
  while (!pending.empty()) {
    SimFuncOp current = pending.pop_back_val();
    if (!visited.insert(current.getOperation()).second)
      continue;
    if (current == target)
      return true;
    forEachDirectCall(current, [&](SimCallOp call) {
      auto found = functions.find(call.getCallee());
      if (found != functions.end())
        pending.push_back(found->second);
    });
  }
  return false;
}

bool isRecursive(SimFuncOp function, SimDesignOp design) {
  llvm::StringMap<SimFuncOp> functions;
  for (SimFuncOp candidate : design.getBody().front().getOps<SimFuncOp>())
    functions[candidate.getSymName()] = candidate;
  bool recursive = false;
  forEachDirectCall(function, [&](SimCallOp call) {
    auto found = functions.find(call.getCallee());
    if (found != functions.end() && reaches(found->second, function, functions))
      recursive = true;
  });
  return recursive;
}

} // namespace

LogicalResult verifyPostponedReadOnly(SimFuncOp root) {
  SmallVector<SimFuncOp> pending{root};
  llvm::SmallPtrSet<Operation *, 16> visited;
  while (!pending.empty()) {
    SimFuncOp function = pending.pop_back_val();
    if (!visited.insert(function.getOperation()).second)
      continue;
    WalkResult readOnly = function.getBody().walk([&](Operation *operation) {
      if (isa<SimFuncOp>(operation))
        return WalkResult::skip();
      if (isa<SimManagedStoreOp, SimManagedNBAEnqueueOp,
              SimReferencePathNBAEnqueueOp, SimArgumentRefStoreOp,
              SimRefStoreOp, SimDriverDriveOp, SimDriverDriveChangedOp,
              SimNBAEnqueueOp, SimSpawnOp, SimEventTriggerOp, SimSuspendDelayOp,
              SimTaskCallOp, SimClassVirtualTaskCallOp,
              SimProcessControlOp, SimProcessSetRandomStateOp>(operation)) {
        operation->emitOpError(
            "is not permitted in a read-only postponed code unit");
        return WalkResult::interrupt();
      }
      if (auto call = dyn_cast<SimCallOp>(operation)) {
        auto callee = SymbolTable::lookupNearestSymbolFrom<SimFuncOp>(
            call, call.getCalleeAttr());
        if (!callee || callee.isExternal()) {
          call.emitOpError(
              "cannot prove external call is read-only in a postponed code "
              "unit");
          return WalkResult::interrupt();
        }
        pending.push_back(callee);
      } else if (auto call = dyn_cast<SimClassDirectCallOp>(operation)) {
        auto callee = SymbolTable::lookupNearestSymbolFrom<SimFuncOp>(
            call, call.getCalleeAttr());
        if (!callee || callee.isExternal()) {
          call.emitOpError(
              "cannot prove method call is read-only in a postponed code "
              "unit");
          return WalkResult::interrupt();
        }
        pending.push_back(callee);
      } else if (isa<SimClassVirtualCallOp>(operation)) {
        operation->emitOpError(
            "virtual calls are not permitted in a read-only postponed code "
            "unit");
        return WalkResult::interrupt();
      } else if (auto call = dyn_cast<SimDPICallOp>(operation);
                 call && !call.getIsPure()) {
        call.emitOpError(
            "impure DPI calls are not permitted in a read-only postponed code "
            "unit");
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    if (readOnly.wasInterrupted())
      return failure();
  }
  return success();
}

InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee) {
  SimFuncOp caller = call ? call->getParentOfType<SimFuncOp>() : SimFuncOp{};
  auto design = caller ? caller->getParentOfType<SimDesignOp>() : SimDesignOp{};
  // Function entries are zero-time, but process.control is an intentional
  // control-flow terminator that may be inlined into the calling process CFG.
  // Legality therefore focuses on recursion and frozen metadata rather than a
  // second operation-family allowlist.
  if (!caller || !callee || !design || callee.isExternal() ||
      callee->getParentOfType<SimDesignOp>() != design ||
      callee.getEntryKind() != EntryKind::Function)
    return InlineLegality::NotDefinedFunction;
  if (hasLateInlineMetadata(design))
    return InlineLegality::LateMetadata;
  if (isRecursive(callee, design))
    return InlineLegality::Recursive;
  if (hasUnknownInlineMetadata(callee))
    return InlineLegality::UnknownMetadata;

  InlineLegality legality = InlineLegality::Legal;
  callee.getBody().walk([&](Operation *operation) {
    if (isa<SimFuncOp>(operation))
      return WalkResult::skip();
    if (legality != InlineLegality::Legal)
      return WalkResult::interrupt();
    if (auto display = dyn_cast<SimDisplayOp>(operation);
        display && !display.getScopeAttr())
      legality = InlineLegality::UnfrozenDisplayScope;
    else if (hasUnknownInlineMetadata(operation))
      legality = InlineLegality::UnknownMetadata;
    return legality == InlineLegality::Legal ? WalkResult::advance()
                                             : WalkResult::interrupt();
  });
  if (legality != InlineLegality::Legal)
    return legality;
  if (hasUnknownInlineMetadata(call) ||
      hasUnknownInlineBoundaryMetadata(call.getArgAttrsAttr()) ||
      hasUnknownInlineBoundaryMetadata(call.getResAttrsAttr()) ||
      hasUnknownInlineBoundaryMetadata(callee.getArgAttrsAttr()) ||
      hasUnknownInlineBoundaryMetadata(callee.getResAttrsAttr()))
    return InlineLegality::UnknownBoundaryMetadata;
  return InlineLegality::Legal;
}

StringRef getInlineLegalityReason(InlineLegality legality) {
  switch (legality) {
  case InlineLegality::Legal:
    return {};
  case InlineLegality::NotDefinedFunction:
    return "callee is not a defined zero-time function";
  case InlineLegality::LateMetadata:
    return "compute-graph or compiled-site metadata already exists";
  case InlineLegality::Recursive:
    return "call is in a recursive SCC";
  case InlineLegality::UnknownMetadata:
    return "callee contains unknown obelisk_sim metadata";
  case InlineLegality::UnfrozenDisplayScope:
    return "display has no frozen lexical scope";
  case InlineLegality::UnknownBoundaryMetadata:
    return "call boundary contains unknown obelisk_sim metadata";
  }
  llvm_unreachable("unknown simulation inline legality");
}

LogicalResult normalizeClassDirectCall(SimClassDirectCallOp call) {
  SimFuncOp caller = call->getParentOfType<SimFuncOp>();
  SimFuncOp callee = SymbolTable::lookupNearestSymbolFrom<SimFuncOp>(
      call, call.getCalleeAttr());
  if (!caller || caller.getBody().empty() ||
      caller.getBody().front().getNumArguments() == 0 ||
      !isa<ContextType>(caller.getBody().front().getArgument(0).getType()))
    return call.emitError(
        "direct class call has no dominating simulation context");
  if (!callee || callee.getEntryKind() != EntryKind::Function)
    return call.emitError(
        "direct class call does not reference a zero-time function");

  SmallVector<Value> operands{caller.getBody().front().getArgument(0),
                              call.getReceiver()};
  llvm::append_range(operands, call.getArguments());
  IRRewriter rewriter(call.getContext());
  rewriter.setInsertionPoint(call);
  auto replacement = SimCallOp::create(
      rewriter, call.getLoc(), call.getResultTypes(), call.getCalleeAttr(),
      operands, ArrayAttr{}, ArrayAttr{});
  rewriter.replaceOp(call, replacement.getResults());
  return success();
}

LogicalResult normalizeClassDirectCalls(Operation *root, uint64_t *count) {
  SmallVector<SimClassDirectCallOp> calls;
  root->walk([&](SimClassDirectCallOp call) { calls.push_back(call); });
  for (SimClassDirectCallOp call : calls) {
    if (failed(normalizeClassDirectCall(call)))
      return failure();
    if (count)
      ++*count;
  }
  return success();
}

/// Enforce unconditional simulation legality for every MLIR inlining client
/// and supply CFG/SSA rewriting mechanics. The Obelisk pass separately owns
/// profitability, budgets, diagnostics, and statistics.
struct ObeliskSimulationInlinerInterface final
    : public DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;

  bool isLegalToInline(Operation *call, Operation *callable, bool) const final {
    auto callOp = dyn_cast<SimCallOp>(call);
    auto function = dyn_cast<SimFuncOp>(callable);
    return callOp && function &&
           getInlineLegality(callOp, function) == InlineLegality::Legal;
  }

  bool isLegalToInline(Region *dest, Region *src, bool,
                       IRMapping &) const final {
    return isa<SimFuncOp>(dest->getParentOp()) &&
           isa<SimFuncOp>(src->getParentOp());
  }

  bool isLegalToInline(Operation *, Region *dest, bool,
                       IRMapping &) const final {
    return isa<SimFuncOp>(dest->getParentOp());
  }

  void handleTerminator(Operation *op, Block *newDest) const final {
    auto returnOp = dyn_cast<SimReturnOp>(op);
    if (!returnOp)
      return;
    OpBuilder builder(op);
    cf::BranchOp::create(builder, op->getLoc(), newDest,
                         returnOp.getOperands());
    op->erase();
  }

  void handleTerminator(Operation *op, ValueRange valuesToReplace) const final {
    auto returnOp = cast<SimReturnOp>(op);
    assert(returnOp.getNumOperands() == valuesToReplace.size());
    for (auto [replacement, value] :
         llvm::zip_equal(valuesToReplace, returnOp.getOperands()))
      replacement.replaceAllUsesWith(value);
  }
};

void ObeliskSimulationDialect::initialize() {
  addInterfaces<ObeliskSimulationInlinerInterface>();
  addAttributes<
#define GET_ATTRDEF_LIST
#include "obelisk/Dialect/Simulation/SimulationAttrs.cpp.inc"
      >();
  addTypes<
#define GET_TYPEDEF_LIST
#include "obelisk/Dialect/Simulation/SimulationTypes.cpp.inc"
      >();
  // Registration for the sharded op definitions. Each shard registers its
  // own slice, so no translation unit sees every op class.
  registerObeliskSimulationDialectOperations(this);
}

Operation *ObeliskSimulationDialect::materializeConstant(OpBuilder &builder,
                                                         Attribute value,
                                                         Type type,
                                                         Location location) {
  // FrozenConstantAttr is transient preparation metadata. Packed aggregates
  // require a scalar constant followed by packed.unflatten, which is not a
  // zero-operand constant-like operation and therefore cannot be returned from
  // this dialect hook. Unit lowering materializes it explicitly instead.
  if (isa<BytesType>(type)) {
    auto bytes = dyn_cast<StringAttr>(value);
    return bytes ? SimBytesConstantOp::create(builder, location, type, bytes)
                 : nullptr;
  }
  // A four-state constant needs two planes, so it folds to and materializes
  // from a two-element array of same-width integers.
  if (auto logic = dyn_cast<LogicType>(type)) {
    auto planes = dyn_cast<ArrayAttr>(value);
    if (!planes || planes.size() != 2)
      return nullptr;
    auto valuePlane = dyn_cast<IntegerAttr>(planes[0]);
    auto unknownPlane = dyn_cast<IntegerAttr>(planes[1]);
    if (!valuePlane || unknownPlane == nullptr ||
        valuePlane.getValue().getBitWidth() != logic.getWidth() ||
        unknownPlane.getValue().getBitWidth() != logic.getWidth())
      return nullptr;
    return SimLogicConstantOp::create(builder, location, logic, valuePlane,
                                      unknownPlane);
  }
  if (isa<TimeType>(type)) {
    auto ticks = dyn_cast<IntegerAttr>(value);
    if (!ticks || ticks.getValue().isNegative())
      return nullptr;
    return SimTimeConstantOp::create(builder, location, type, ticks);
  }
  // Integer-valued folders in this dialect materialize ordinary builtin
  // constants rather than introducing another simulation-specific constant.
  if (auto integer = dyn_cast<IntegerType>(type)) {
    auto attr = dyn_cast<IntegerAttr>(value);
    if (!integer.isSignless() || !attr || attr.getType() != integer)
      return nullptr;
    return arith::ConstantOp::create(builder, location, integer, attr);
  }
  return nullptr;
}

OpFoldResult SimBytesConstantOp::fold(FoldAdaptor) { return getValueAttr(); }

} // namespace obelisk::sim
