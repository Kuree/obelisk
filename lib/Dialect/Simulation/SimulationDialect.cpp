//===- SimulationDialect.cpp - Executable simulation dialect ------------===//

#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
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
  return isa<
      SimSuspendDelayOp, SimSuspendChangeOp, SimSuspendEdgeOp,
      SimSuspendEdgeIffOp, SimSuspendLevelOp, SimSuspendAnyOp,
      SimSuspendClockSetOp, SimSuspendEventOp, SimSuspendEventOrderOp,
      SimSuspendMailboxOp, SimSuspendSemaphoreOp, SimSuspendForeverOp,
      SimSuspendAwaitOp, SimSuspendJoinOp, SimSuspendChildrenOp,
      SimSuspendObserveOp, SimTaskCallOp, SimClassVirtualTaskCallOp,
      SimProcessControlOp, SimControlBoundaryOp, schedule::NativeSuspendDelayOp,
      schedule::NativeSuspendChangeOp, schedule::NativeSuspendEdgeOp,
      schedule::NativeSuspendEdgeIffOp, schedule::NativeSuspendLevelOp,
      schedule::NativeSuspendAnyOp, schedule::NativeSuspendClockSetOp,
      schedule::NativeSuspendEventOp, schedule::NativeSuspendEventOrderOp,
      schedule::NativeSuspendMailboxOp, schedule::NativeSuspendSemaphoreOp,
      schedule::NativeSuspendForeverOp, schedule::NativeSuspendAwaitOp,
      schedule::NativeSuspendJoinOp, schedule::NativeSuspendChildrenOp,
      schedule::NativeSuspendObserveOp, schedule::NativeProcessControlOp,
      schedule::NativeControlBoundaryOp, schedule::NativeTaskCallOp,
      schedule::NativeClassVirtualTaskCallOp>(operation);
}

bool returnsToScheduler(Operation *operation) {
  return isa<SimSuspendDelayOp, SimSuspendChangeOp, SimSuspendEdgeOp,
             SimSuspendEdgeIffOp, SimSuspendClockSetOp, SimSuspendEventOp,
             SimSuspendEventOrderOp, SimSuspendAnyOp, SimSuspendObserveOp,
             SimSuspendForeverOp>(operation);
}

bool isDeferredReportSpawn(SimSpawnOp spawn) {
  return llvm::any_of(spawn.getOperands(), [](Value operand) {
    return operand.getDefiningOp<SimDeferredEnqueueOp>() != nullptr;
  });
}

bool isColdRuntimeCheckpoint(Operation *operation) {
  if (auto spawn = dyn_cast<SimSpawnOp>(operation))
    return isDeferredReportSpawn(spawn);
  return isa<SimFinishOp, SimStopOp, SimFatalOp, SimProgramExitOp, SimErrorOp,
             SimStatusCheckOp, SimDisplayOp, SimSampledReadOp,
             SimSampledHistoryOp, SimClockedSampleReadOp,
             SimDeferredEnqueueOp>(operation);
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

bool isVPIVisibleEntryKind(EntryKind kind) {
  switch (kind) {
  case EntryKind::Initial:
  case EntryKind::Final:
  case EntryKind::Always:
  case EntryKind::AlwaysComb:
  case EntryKind::AlwaysFF:
  case EntryKind::AlwaysLatch:
  case EntryKind::Function:
  case EntryKind::Task:
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
      .Case<SimSuspendClockSetOp>([](auto op) {
        return static_cast<uint32_t>(op.getPrimaries().size() +
                                     op.getConditions().size());
      })
      .Case<SimSuspendEventOrderOp>(
          [](auto op) { return static_cast<uint32_t>(op.getEvents().size()); })
      .Case<SimSuspendJoinOp>([](auto op) {
        return static_cast<uint32_t>(op.getProcesses().size());
      })
      .Default([](Operation *) { return 0; });
}

namespace {

bool containsLateInlineMetadata(SimDesignOp design) {
  if (design.getComputeGraphAttr())
    return true;
  bool found = false;
  design.walk([&](Operation *operation) {
    if (auto function = dyn_cast<SimFuncOp>(operation))
      found |= static_cast<bool>(function.getEffectSummaryAttr()) ||
               static_cast<bool>(function.getFragmentAbiAttr());
    for (NamedAttribute named : operation->getAttrs())
      found |=
          isa<schedule::ContinuationSiteAttr, schedule::TimingSiteAttr,
              schedule::NBASiteAttr, schedule::EventSiteAttr>(named.getValue());
  });
  return found;
}

bool hasUnknownInlineMetadata(Operation *operation) {
  for (NamedAttribute named : operation->getAttrs()) {
    StringRef name = named.getName().strref();
    if (!metadata::isCompilerMetadata(name))
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
      if (metadata::isCompilerMetadata(name) &&
          !metadata::isKnownBoundary(name))
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

bool hasLateInlineMetadata(SimDesignOp design) {
  return containsLateInlineMetadata(design);
}

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
              SimManagedNBAEnqueueMaskedOp, SimReferencePathNBAEnqueueOp,
              SimArgumentRefStoreOp, SimRefStoreOp, SimRefCopyOp,
              SimDriverDriveOp, SimDriverDriveInertialOp,
              SimDriverDriveInertialPathOp, SimRefStoreInertialPathOp,
              SimDriverDriveInertialStrengthPairOp,
              SimDriverDriveInertialPathStrengthPairOp,
              SimDriverDriveDelayedNetOp, SimDriverDriveChangedOp,
              SimMosDriveDelayedOp, SimNBAEnqueueOp, SimSpawnOp,
              SimEventTriggerOp, SimSuspendDelayOp, SimTaskCallOp,
              SimClassVirtualTaskCallOp, SimProcessControlOp,
              SimProcessSetRandomStateOp, SimRandomSetStateOp>(operation)) {
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

InlineLegality getInlineCalleeLegality(SimFuncOp callee, bool calleeIsRecursive,
                                       bool designHasLateMetadata) {
  if (designHasLateMetadata)
    return InlineLegality::LateMetadata;
  if (calleeIsRecursive)
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
    else if (auto format = dyn_cast<SimStringOutputFormatOp>(operation);
             format && !format.getScopeAttr())
      legality = InlineLegality::UnfrozenDisplayScope;
    else if (hasUnknownInlineMetadata(operation))
      legality = InlineLegality::UnknownMetadata;
    return legality == InlineLegality::Legal ? WalkResult::advance()
                                             : WalkResult::interrupt();
  });
  return legality;
}

InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee,
                                 InlineLegality calleeLegality) {
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
  if (calleeLegality != InlineLegality::Legal)
    return calleeLegality;
  if (hasUnknownInlineMetadata(call) ||
      hasUnknownInlineBoundaryMetadata(call.getArgAttrsAttr()) ||
      hasUnknownInlineBoundaryMetadata(call.getResAttrsAttr()) ||
      hasUnknownInlineBoundaryMetadata(callee.getArgAttrsAttr()) ||
      hasUnknownInlineBoundaryMetadata(callee.getResAttrsAttr()))
    return InlineLegality::UnknownBoundaryMetadata;
  return InlineLegality::Legal;
}

InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee,
                                 bool calleeIsRecursive) {
  auto design = callee ? callee->getParentOfType<SimDesignOp>() : SimDesignOp{};
  InlineLegality calleeLegality =
      callee && design
          ? getInlineCalleeLegality(callee, calleeIsRecursive,
                                    containsLateInlineMetadata(design))
          : InlineLegality::NotDefinedFunction;
  return getInlineLegality(call, callee, calleeLegality);
}

InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee) {
  auto design = callee ? callee->getParentOfType<SimDesignOp>() : SimDesignOp{};
  return getInlineLegality(call, callee, design && isRecursive(callee, design));
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
    return "callee contains unknown simulation metadata";
  case InlineLegality::UnfrozenDisplayScope:
    return "display has no frozen lexical scope";
  case InlineLegality::UnknownBoundaryMetadata:
    return "call boundary contains unknown simulation metadata";
  }
  llvm_unreachable("unknown simulation inline legality");
}

static LogicalResult normalizeClassDirectCallImpl(SimClassDirectCallOp call,
                                                  SimFuncOp callee) {
  SimFuncOp caller = call->getParentOfType<SimFuncOp>();
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

LogicalResult normalizeClassDirectCall(SimClassDirectCallOp call) {
  return normalizeClassDirectCallImpl(
      call, SymbolTable::lookupNearestSymbolFrom<SimFuncOp>(
                call, call.getCalleeAttr()));
}

LogicalResult normalizeClassDirectCalls(Operation *root, uint64_t *count) {
  SmallVector<SimClassDirectCallOp> calls;
  DenseMap<Operation *, llvm::StringMap<SimFuncOp>> functionsByDesign;
  root->walk([&](Operation *operation) {
    if (auto call = dyn_cast<SimClassDirectCallOp>(operation))
      calls.push_back(call);
    else if (auto function = dyn_cast<SimFuncOp>(operation)) {
      auto design = function->getParentOfType<SimDesignOp>();
      if (design)
        functionsByDesign[design.getOperation()][function.getSymName()] =
            function;
    }
  });
  for (SimClassDirectCallOp call : calls) {
    SimFuncOp callee;
    auto design = call->getParentOfType<SimDesignOp>();
    auto functions = design ? functionsByDesign.find(design.getOperation())
                            : functionsByDesign.end();
    if (functions != functionsByDesign.end()) {
      auto found = functions->second.find(call.getCallee());
      if (found != functions->second.end())
        callee = found->second;
    }
    if (failed(normalizeClassDirectCallImpl(call, callee)))
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
    if (!callOp || !function)
      return false;
    if (auto cached = sim::get<sim::metadata::InlineLegalityCache>(function))
      return getInlineLegality(callOp, function,
                               static_cast<InlineLegality>(cached.getInt())) ==
             InlineLegality::Legal;
    // The generic inliner owns recursive-SCC convergence. Recursive expansion
    // is a policy rejection, not a semantic illegality of cloning one call.
    return getInlineLegality(callOp, function,
                             /*calleeIsRecursive=*/false) ==
           InlineLegality::Legal;
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

  void processInlinedCallBlocks(
      Operation *call,
      iterator_range<Region::iterator> inlinedBlocks) const final {
    auto sourceOwner =
        ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(call);
    if (!sourceOwner)
      return;
    for (Block &block : inlinedBlocks)
      block.walk([&](Operation *operation) {
        // A nested fused call may already carry the more precise identity of
        // the logical process from which it was cloned. Preserve that fact;
        // otherwise inherit the caller's identity through arbitrary helper
        // depth before the call operation is erased by the inliner.
        if (::obelisk::schedule::has<schedule::metadata::evalSourceOwner>(
                operation))
          return;
        Attribute inheritedOwner = sourceOwner;
        for (Operation *parent = operation->getParentOp(); parent;
             parent = parent->getParentOp())
          if (auto parentOwner =
                  ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(
                      parent)) {
            inheritedOwner = parentOwner;
            break;
          }
        ::obelisk::schedule::set<schedule::metadata::evalSourceOwner>(
            operation, cast<schedule::SourceOwnerAttr>(inheritedOwner));
      });
  }
};

/// Payload checks of the declared simulation attributes.
static const llvm::StringMap<discardable::PayloadCheck> &declarations() {
  return discardable::declarations<
      metadata::IntegerRange, metadata::ResultRanges, metadata::ResultExprs,
      metadata::KnownLogicResult, metadata::GCPolled,
      metadata::InlineLegalityCache, metadata::PulseRejectPercent,
      metadata::PulseErrorPercent, metadata::PulseOnDetect,
      metadata::PulseShowCancelled, metadata::ConstraintMode,
      metadata::ConstraintModeBlock, metadata::ConstraintModeField,
      metadata::ConstraintModeStaticStorage,
      metadata::ConstraintModeStaticStorages,
      metadata::ObjectRandomDispatchClasses, metadata::RandMode,
      metadata::RandModeAncestorMask, metadata::RandModeArrayField,
      metadata::RandModeArrayStorage, metadata::RandModeArrayType,
      metadata::RandModeFamilyMask, metadata::RandModeProperty,
      metadata::RandModeSelectionMask, metadata::RandModeStaticDispatch,
      metadata::RandModeStaticStorage, metadata::RandModeStructDepth,
      metadata::RandModeStructElementFields, metadata::RandomArithmeticNode,
      metadata::RandomArithmeticOperand, metadata::RandomArithmeticPlane,
      metadata::RandomArrayElementWidth, metadata::RandomArrayModeField,
      metadata::RandomConstraintBlock, metadata::RandomConstraintCount,
      metadata::RandomContainerProperties, metadata::RandomCycleKeyField,
      metadata::RandomCyclePositionField, metadata::RandomEvaluationError,
      metadata::RandomEvaluationInputs, metadata::RandomFixedConstraintModes,
      metadata::RandomFunctionCaseCondition,
      metadata::RandomFunctionConditional, metadata::RandomFunctionExpression,
      metadata::RandomFunctionOrder, metadata::RandomFunctionState,
      metadata::RandomGuardInputs, metadata::RandomIncrementField,
      metadata::RandomModeField, metadata::RandomModeIndex,
      metadata::RandomNestedConstraintModes, metadata::RandomNestedHooks,
      metadata::RandomNestedStateConcreteType, metadata::RandomNestedStateField,
      metadata::RandomNestedStatePath, metadata::RandomNestedStateStorageType,
      metadata::RandomObjectEdge, metadata::RandomPostHook,
      metadata::RandomPreHook, metadata::RandomPriorityArguments,
      metadata::RandomPriorityInputs, metadata::RandomProperties,
      metadata::RandomRealFinite, metadata::RandomReceiverIndex,
      metadata::RandomRecursiveAliasGuards, metadata::RandomStateField,
      metadata::RandomStructMembers, metadata::RandomTotalWidth,
      metadata::RandomVariable, metadata::RandomVariableBitOffset,
      metadata::RandomVariableKind, metadata::RandomVariableSigned,
      metadata::Randomize, metadata::RandomizeCheckerOnly,
      metadata::RandomizeConstraintTemplate, metadata::RandomizeDispatch,
      metadata::RandomizeExplicitProperties, metadata::RandomizeHelper,
      metadata::RandomizeHelperCaptures, metadata::RandomizeHelperReadCaptures,
      metadata::RandomizeHelperReceiver, metadata::RandomizeNestedDispatch,
      metadata::RandomizeNestedDispatchField,
      metadata::RandomizeNestedDispatchPath,
      metadata::RandomizeNestedDispatchSelectionPath,
      metadata::RandomizeNestedDispatchStorageType,
      metadata::RandomizeNestedPlans, metadata::RandomizePlanClass,
      metadata::RandomizeScope, metadata::StaticConstraintStorage,
      metadata::StaticRandomArrayModeStorage, metadata::StaticRandomModeStorage,
      metadata::AssertionActionControlled, metadata::AssertionControlAction,
      metadata::AssertionControlActionArgument,
      metadata::AssertionControlAssertionTypes,
      metadata::AssertionControlAssertionTypesArgument,
      metadata::AssertionControlDepths,
      metadata::AssertionControlDirectiveTypes,
      metadata::AssertionControlDirectiveTypesArgument,
      metadata::AssertionControlIds, metadata::AssertionControlLevelsArgument,
      metadata::AssertionControlTargetId, metadata::AssertionControlled,
      metadata::AssertionCoverageReport, metadata::AssertionKillControlled,
      metadata::AssertionLocalTypes, metadata::AssertionPath,
      metadata::AssertionTargetId, metadata::AsynchronousPropertyAbort,
      metadata::BooleanMinimizations, metadata::BoundedAntecedentHorizon,
      metadata::BranchingAntecedentAlternatives,
      metadata::BranchingAntecedentBackedge,
      metadata::BranchingAntecedentChannel,
      metadata::BranchingAntecedentConsequentEosStrength,
      metadata::BranchingAntecedentConsequentTrigger,
      metadata::BranchingAntecedentEosCoalescer,
      metadata::BranchingAntecedentEosReport,
      metadata::BranchingAntecedentEosResult,
      metadata::BranchingAntecedentEosSourceAge,
      metadata::BranchingAntecedentExistentialFailure,
      metadata::BranchingAntecedentExistentialSuccess,
      metadata::BranchingAntecedentMatchChannels,
      metadata::BranchingAntecedentMatchedHistory,
      metadata::BranchingAntecedentMonitor,
      metadata::BranchingAntecedentResultCancel,
      metadata::BranchingAntecedentResultCoalescer,
      metadata::BranchingAntecedentResultHorizon,
      metadata::BranchingAntecedentUniversalFailure,
      metadata::BranchingAntecedentUniversalSuccess,
      metadata::BranchingAntecedentVacuity,
      metadata::BranchingConsequentAlternative,
      metadata::BranchingConsequentAlternativeCancel,
      metadata::BranchingConsequentAlternatives,
      metadata::BranchingConsequentBackedge,
      metadata::BranchingConsequentEosCoalescer,
      metadata::BranchingConsequentIntrinsicEosStrength,
      metadata::BranchingConsequentMonitor,
      metadata::BranchingConsequentNonoverlapped,
      metadata::BranchingConsequentTrigger,
      metadata::BranchingSequenceAlternatives,
      metadata::BranchingSequenceMonitor, metadata::ClockingEventMonitor,
      metadata::ClockingEventMonitorPath,
      metadata::CombinedBooleanBranchingMonitor,
      metadata::CombinedBooleanBranchingPairs,
      metadata::CombinedBooleanBranchingPairsBeforeMinimization,
      metadata::CombinedBoundedBranchingMonitor,
      metadata::CombinedBoundedBranchingPairs,
      metadata::CombinedBoundedBranchingPairsBeforeMinimization,
      metadata::ConcurrentAbortCounted, metadata::ConcurrentActionClass,
      metadata::ConcurrentActionControl, metadata::ConcurrentAttemptActionState,
      metadata::ConcurrentAttemptActionStateLoad,
      metadata::ConcurrentAttemptActionStateNext,
      metadata::ConcurrentAttemptActionStateStorage,
      metadata::ConcurrentAttemptActionStateStore,
      metadata::ConcurrentAttemptEnable, metadata::ConcurrentAttemptStart,
      metadata::ConcurrentEosForcedCompletion, metadata::ConcurrentEosReport,
      metadata::ConcurrentEosVacuous, metadata::ConcurrentKillEpoch,
      metadata::ConcurrentKillEpochCheck, metadata::ConcurrentKillEpochStorage,
      metadata::ConcurrentMatchCall, metadata::ConcurrentMatchCallChain,
      metadata::ConcurrentMatchCallKillEpoch,
      metadata::ConcurrentReportKillEpoch,
      metadata::ConsequentEndOfSimulationStrength,
      metadata::ConsequentFirstMatchEquivalence,
      metadata::CoverSequencePerMatch, metadata::DefaultAssertionFailure,
      metadata::DeferredEpochStorage, metadata::DeferredEvaluator,
      metadata::DeferredResult,
      metadata::EmptyAntecedentNonoverlap, metadata::EndOfSimulationStrength,
      metadata::ExpectBoundedAlternatives, metadata::ExpectBoundedBranching,
      metadata::ExpectBoundedHorizon, metadata::ExpectBoundedStateWords,
      metadata::ExpectDonePath, metadata::ExpectEosCoordinator,
      metadata::ExpectMonitor, metadata::ExpectMonitorActor,
      metadata::ExpectOneCycleAlternatives, metadata::ExpectOneCycleBranching,
      metadata::ExpectOperandStrength, metadata::ExpectResultPath,
      metadata::FirstMatchMonitor, metadata::FirstMatchPriority,
      metadata::FirstMatchPriorityGroups, metadata::FollowedByMonitor,
      metadata::GlobalFutureMonitor, metadata::GlobalFutureResolver,
      metadata::GlobalFutureWait, metadata::ImplicationAntecedent,
      metadata::ImplicationAntecedentFailure,
      metadata::MixedEmptyAntecedentCurrentTickChannels,
      metadata::MixedEmptyAntecedentHandoffChannels,
      metadata::MixedEmptyAntecedentNonoverlap, metadata::MonitorCallback,
      metadata::MonitorObservationComplete,
      metadata::MulticlockSequenceCoordinator,
      metadata::MulticlockSequenceEosCoordinator,
      metadata::NegatedOperandEndOfSimulationStrength,
      metadata::OverlappedEmptyMatchesIgnored,
      metadata::PersistentAntecedentFinalHandoff,
      metadata::PersistentAntecedentHandoff,
      metadata::PersistentAntecedentHandoffStates,
      metadata::PersistentDelayAggregateTokens,
      metadata::PersistentDelayAllMatches, metadata::PersistentDelayImplication,
      metadata::PersistentDelayMinimum, metadata::PersistentDelayMonitor,
      metadata::PersistentDelayNonoverlapped,
      metadata::PersistentDelayPrefixHorizon,
      metadata::PersistentFirstMatchEquivalence,
      metadata::PersistentImplicationActivation,
      metadata::PersistentImplicationAntecedentResult,
      metadata::PersistentImplicationHandoff, metadata::PersistentMonitor,
      metadata::PersistentRepetitionAllMatches,
      metadata::PersistentRepetitionAntecedent,
      metadata::PersistentRepetitionDfa,
      metadata::PersistentRepetitionImplication,
      metadata::PersistentRepetitionKind, metadata::PersistentRepetitionMax,
      metadata::PersistentRepetitionMin, metadata::PersistentRepetitionMonitor,
      metadata::PersistentRepetitionNonoverlapped,
      metadata::PersistentRepetitionStates,
      metadata::PersistentRepetitionUnbounded,
      metadata::PersistentUnaryAggregateTokens,
      metadata::PersistentUnaryImplication, metadata::PersistentUnaryKind,
      metadata::PersistentUnaryMinimum, metadata::PersistentUnaryMonitor,
      metadata::PersistentUnaryNonoverlapped,
      metadata::PersistentUntilAggregateTokens,
      metadata::PersistentUntilImplication, metadata::PersistentUntilInclusive,
      metadata::PersistentUntilKind, metadata::PersistentUntilMonitor,
      metadata::PersistentUntilNonoverlapped, metadata::PersistentUntilStrong,
      metadata::ProceduralAssertionAttempt,
      metadata::ProceduralAssertionEpochReference,
      metadata::ProceduralAssertionEpochValue, metadata::PropertyAbortAction,
      metadata::SampleDefaultConstant, metadata::SampleDefaultCurrent,
      metadata::SampleDefaultDynamic, metadata::SampleDefaultSnapshot,
      metadata::SequenceEndpointDefaultClock, metadata::SequenceEndpointEvent,
      metadata::SequenceEndpointMonitor, metadata::SequenceEndpointPath,
      metadata::StrongWeakMonitor, metadata::SvaTransitionNormalForm,
      metadata::SynchronousPropertyAbort, metadata::TemporalPropertyNegation,
      metadata::TemporalPropertyNegationOutsideAbort,
      metadata::VacuousSequenceAlternatives, metadata::SteadyUnknown,
      metadata::ContinuousStore, metadata::DelayQuantum, metadata::DelayScale,
      metadata::DelayedNet, metadata::InitialDriverX,
      metadata::InterconnectLeafPath, metadata::InterconnectLeaves,
      metadata::InterconnectNettype, metadata::InterconnectVpiType,
      metadata::MosTopologyIds, metadata::NegativeTimingAdjusted,
      metadata::NegativeTimingDelayCommit, metadata::NegativeTimingDelayMonitor,
      metadata::NegativeTimingTransportActivation, metadata::PassSwitchIds,
      metadata::PropagationDelays, metadata::ResolutionFunctionPath,
      metadata::ResolutionFunctionSymbol, metadata::SkewDeadlineHelper,
      metadata::StrengthBank, metadata::StrengthDriverBank,
      metadata::StrengthGroup, metadata::TimingAdjustedTicks,
      metadata::TimingCheckArgTicks, metadata::TimingCheckCoordinator,
      metadata::TimingDelayedSourceDelays, metadata::TimingDelayedStorageIds,
      metadata::TimingDelayedTerminalIndices, metadata::TimingPathMonitorRules,
      metadata::TimingPathRules, metadata::TimingTimerHelper,
      metadata::TimingTimerStorage, metadata::UdpMetadata,
      metadata::UserDefinedNet, metadata::UserNetDriver,
      metadata::UserNetRawDrive, metadata::AggregateMemberInitializers,
      metadata::AutomaticReferenceCapture, metadata::Bindings, metadata::Callee,
      metadata::CalleeCaptures, metadata::CalleeFormals,
      metadata::CalleeReadCaptures, metadata::CalleeWrittenCaptures,
      metadata::CaptureKind, metadata::ClassBitstreamAllowHiddenRoot,
      metadata::ClassBitstreamBytecodeFunction,
      metadata::ClassBitstreamBytecodeSite, metadata::ClassBitstreamMember,
      metadata::EmbeddedCovergroup, metadata::CheckerProcedure,
      metadata::ClassBitstreamSource, metadata::ClassBitstreamVisibility,
      metadata::ClassBitstreamWidth, metadata::ClassField,
      metadata::ClassInstance, metadata::ClassMethod, metadata::ClassSignature,
      metadata::ClassSlot, metadata::ClassSuper, metadata::ClassVirtual,
      metadata::ConstantValue, metadata::Constructor, metadata::ControlTargetId,
      metadata::CovergroupBlockEventSampleEvaluator,
      metadata::CovergroupEventSampleEvaluator,
      metadata::CovergroupStrobeSampleEvaluator,
      metadata::EventExplicitInitializer,
      metadata::ForeachIteratorObserverCapture, metadata::ForkCodeUnitId,
      metadata::InitializeField, metadata::InitializeNet,
      metadata::InitializeSampleDefault, metadata::InitializeStatic,
      metadata::InitializeSubelement, metadata::InitializeTypeOptionKind,
      metadata::InitializeTypeOptionOwner, metadata::IsTask, metadata::Lowered,
      metadata::NbaEventCodeUnitId, metadata::NbaEventHierarchy,
      metadata::Observer, metadata::ObserverCaptures,
      metadata::ObserverCoercedType, metadata::ObserverDependencies,
      metadata::ObserverResult, metadata::Placeholder,
      metadata::PreparedInitializer, metadata::ResumableControlTarget,
      metadata::ExternalControlTarget, metadata::ReturnVariablePath,
      metadata::SampledObserver, metadata::StaticClassProperty,
      metadata::StaticClassReceiver, metadata::StaticInitializer,
      metadata::StaticNetConstant, metadata::StaticSiteId,
      metadata::SubroutineStorage, metadata::ThisArgument,
      metadata::TypeOptionItemQualifier, metadata::VirtualInterfaceCallees,
      metadata::VirtualInterfaceClockEvents,
      metadata::VirtualInterfaceClockingOutput,
      metadata::VirtualInterfaceMember, metadata::VirtualInterfaceParentMember,
      metadata::VirtualInterfaceReceiverMembers, metadata::VoidFunction,
      metadata::ActivationDefaultSlot, metadata::BytecodeReadOnly,
      metadata::DescriptorAggregateType, metadata::DescriptorId,
      metadata::DescriptorIndices, metadata::DescriptorLow,
      metadata::DescriptorPackedLow, metadata::DescriptorRootType,
      metadata::FlushedPrivateSlot, metadata::GcPollFree,
      metadata::HierarchicalName, metadata::Rematerialized, metadata::VpiAnchor,
      metadata::VpiAutomatic, metadata::VpiCellInstance,
      metadata::VpiDefinitionName, metadata::VpiIdentityDelegated,
      metadata::VpiSourceTypeIdentity, metadata::VpiTop,
      metadata::DumpportsScope, metadata::DynamicCastEnumValues,
      metadata::DynamicCastTask, metadata::EnumFormatNames,
      metadata::EnumFormatValues, metadata::EnumFormats,
      metadata::EnumMethodNames, metadata::EnumMethodValues>();
}

/// Check that a simulation attribute is declared and has its payload kind.
static LogicalResult verifyDeclaredAttribute(Operation *operation,
                                             NamedAttribute attribute) {
  using namespace metadata;
  StringRef name = attribute.getName().strref();
  if (failed(discardable::verify(declarations(), operation, attribute)))
    return failure();
  Attribute value = attribute.getValue();
  auto error = [&]() {
    return operation->emitOpError("attribute '") << name << "' ";
  };
  auto allOf = [&](auto pred) {
    return llvm::all_of(cast<ArrayAttr>(value), pred);
  };
  if (name == ResultRanges::getNameStr() &&
      !allOf(llvm::IsaPred<IntegerRangeAttr, UnitAttr>))
    return error() << "must contain #simulation.integer_range or unit entries";
  if (name == ResultExprs::getNameStr() &&
      !allOf(llvm::IsaPred<IntegerExpressionAttr, UnitAttr>))
    return error()
           << "must contain #simulation.integer_expression or unit entries";
  if (name == BooleanMinimizations::getNameStr() &&
      !allOf(llvm::IsaPred<BooleanMinimizationAttr>))
    return error() << "must contain #simulation.boolean_minimization entries";
  if (name == VirtualInterfaceReceiverMembers::getNameStr() &&
      !allOf(llvm::IsaPred<VirtualInterfaceMemberAttr>))
    return error()
           << "must contain #simulation.virtual_interface_member entries";
  if (name == VirtualInterfaceClockEvents::getNameStr() &&
      !allOf(llvm::IsaPred<VirtualInterfaceEventAttr>))
    return error()
           << "must contain #simulation.virtual_interface_event entries";
  if (name == RandModeStaticDispatch::getNameStr() &&
      !allOf(llvm::IsaPred<RandModeDispatchAttr>))
    return error() << "must contain #simulation.rand_mode_dispatch entries";
  if (name == RandModeStructElementFields::getNameStr() &&
      !allOf(llvm::IsaPred<RandomElementModesAttr>))
    return error() << "must contain #simulation.random_element_modes entries";
  if (name == ObjectRandomDispatchClasses::getNameStr() &&
      !allOf(llvm::IsaPred<FlatSymbolRefAttr>))
    return error() << "must contain class symbols";
  if (name == InterconnectLeaves::getNameStr() &&
      !allOf(llvm::IsaPred<InterconnectLeafAttr>))
    return error() << "must contain #simulation.interconnect_leaf entries";
  if (name == TimingPathRules::getNameStr() &&
      !allOf(llvm::IsaPred<TimingPathRuleAttr>))
    return error() << "must contain #simulation.timing_path_rule entries";
  if (name == TimingPathMonitorRules::getNameStr() &&
      !allOf(llvm::IsaPred<TimingPathMonitorAttr>))
    return error() << "must contain #simulation.timing_path_monitor entries";
  if (name == CalleeFormals::getNameStr() &&
      !allOf(llvm::IsaPred<CalleeFormalAttr>))
    return error() << "must contain #simulation.callee_formal entries";
  if (name == VirtualInterfaceCallees::getNameStr() &&
      !allOf(llvm::IsaPred<VirtualInterfaceCalleeAttr>))
    return error()
           << "must contain #simulation.virtual_interface_callee entries";
  if (name == RandomProperties::getNameStr() &&
      !allOf(llvm::IsaPred<RandomPropertyAttr>))
    return error() << "must contain #simulation.random_property entries";
  if (name == RandomContainerProperties::getNameStr() &&
      !allOf(llvm::IsaPred<RandomContainerAttr>))
    return error() << "must contain #simulation.random_container entries";
  if (name == RandomStructMembers::getNameStr() &&
      !allOf(llvm::IsaPred<RandomStructMemberAttr>))
    return error() << "must contain #simulation.random_struct_member entries";
  if (name == RandomNestedHooks::getNameStr() &&
      !allOf(llvm::IsaPred<RandomNestedHooksAttr>))
    return error() << "must contain #simulation.random_nested_hooks entries";
  if (name == RandomNestedConstraintModes::getNameStr() &&
      !allOf(llvm::IsaPred<RandomNestedConstraintsAttr>))
    return error()
           << "must contain #simulation.random_nested_constraints entries";
  if (name == RandomRecursiveAliasGuards::getNameStr() &&
      !allOf(llvm::IsaPred<RandomAliasGuardAttr>))
    return error() << "must contain #simulation.random_alias_guard entries";
  if (name == RandomizeNestedPlans::getNameStr() &&
      !allOf(llvm::IsaPred<RandomNestedSelectionAttr>))
    return error()
           << "must contain #simulation.random_nested_selection entries";
  if ((name == RandomizeNestedDispatchPath::getNameStr() ||
       name == RandomNestedStatePath::getNameStr()) &&
      !allOf(llvm::IsaPred<RandomObjectStepAttr>))
    return error() << "must contain #simulation.random_object_step entries";
  if (name == RandomizeNestedDispatchSelectionPath::getNameStr() &&
      !allOf(llvm::IsaPred<FlatSymbolRefAttr>))
    return error() << "must contain field symbols";
  if (name == SteadyUnknown::getNameStr()) {
    auto storage = dyn_cast<SimStorageDeclOp>(operation);
    if (!storage)
      return error() << "belongs on a storage declaration";
    auto ranges = cast<DenseI64ArrayAttr>(attribute.getValue()).asArrayRef();
    std::optional<uint64_t> span = getProvenanceSpan(storage.getType());
    int64_t end = 0;
    for (size_t index = 0; index + 1 < ranges.size(); index += 2)
      if (ranges[index] < end || ranges[index + 1] <= 0 || !span ||
          uint64_t(ranges[index] + ranges[index + 1]) > *span)
        return error() << "must hold sorted, disjoint ranges inside the root";
      else
        end = ranges[index] + ranges[index + 1];
    if (ranges.size() % 2)
      return error() << "must hold (offset, width) pairs";
  }
  if ((name == GCPolled::getNameStr() ||
       name == PulseRejectPercent::getNameStr() ||
       name == PulseErrorPercent::getNameStr() ||
       name == PulseOnDetect::getNameStr() ||
       name == PulseShowCancelled::getNameStr()) &&
      !isa<ModuleOp>(operation))
    return error() << "belongs on a module";
  return success();
}

LogicalResult
ObeliskSimulationDialect::verifyOperationAttribute(Operation *operation,
                                                   NamedAttribute attribute) {
  if (attribute.getName().strref() == metadata::IntegerRange::getNameStr())
    return operation->emitOpError("attribute '")
           << attribute.getName().strref() << "' belongs on an argument";
  return verifyDeclaredAttribute(operation, attribute);
}

LogicalResult ObeliskSimulationDialect::verifyRegionArgAttribute(
    Operation *operation, unsigned, unsigned, NamedAttribute attribute) {
  return verifyDeclaredAttribute(operation, attribute);
}

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
  // Handle-like nulls have no payload beyond their result type.  UnitAttr is
  // their fold representation; the type selects the precise null operation
  // when the operation folder needs to rematerialize one.
  if (isa<UnitAttr>(value)) {
    if (isa<CovergroupHandleType>(type))
      return SimCovergroupNullOp::create(builder, location, type);
    if (isa<VirtualInterfaceType>(type))
      return SimVirtualInterfaceNullOp::create(builder, location, type);
    if (isa<ChandleType>(type))
      return SimChandleNullOp::create(builder, location);
    if (isa<ClassHandleType>(type))
      return SimClassNullOp::create(builder, location, type);
    if (isManagedHandleType(type))
      return SimManagedNullOp::create(builder, location, type);
    if (isa<EventType>(type))
      return SimEventNullOp::create(builder, location, type);
    if (isa<ManagedWatchType>(type))
      return SimManagedWatchNullOp::create(builder, location);
    if (isa<ProcessType>(type))
      return SimProcessNullOp::create(builder, location);
    return nullptr;
  }
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
  if (auto real = dyn_cast<FloatType>(type)) {
    auto attr = dyn_cast<FloatAttr>(value);
    if (!attr || attr.getType() != real)
      return nullptr;
    return arith::ConstantOp::create(builder, location, real, attr);
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

#define OBELISK_NULL_FOLD(OP)                                                  \
  OpFoldResult OP::fold(FoldAdaptor) { return UnitAttr::get(getContext()); }

OBELISK_NULL_FOLD(SimCovergroupNullOp)
OBELISK_NULL_FOLD(SimVirtualInterfaceNullOp)
OBELISK_NULL_FOLD(SimChandleNullOp)
OBELISK_NULL_FOLD(SimClassNullOp)
OBELISK_NULL_FOLD(SimManagedNullOp)
OBELISK_NULL_FOLD(SimEventNullOp)
OBELISK_NULL_FOLD(SimManagedWatchNullOp)
OBELISK_NULL_FOLD(SimProcessNullOp)

#undef OBELISK_NULL_FOLD

} // namespace obelisk::sim
