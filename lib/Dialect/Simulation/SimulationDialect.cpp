//===- SimulationDialect.cpp - Executable simulation dialect ------------===//

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <limits>

using namespace mlir;

#include "obelisk/Dialect/Simulation/SimulationDialect.cpp.inc"
#include "obelisk/Dialect/Simulation/SimulationEnums.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationAttrs.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationTypes.cpp.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Simulation/SimulationOps.cpp.inc"

namespace obelisk::sim {

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
              SimRefStoreOp, SimDriverDriveOp, SimNBAEnqueueOp, SimSpawnOp,
              SimEventTriggerOp, SimSuspendDelayOp, SimTaskCallOp>(operation)) {
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

} // namespace

InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee) {
  SimFuncOp caller = call ? call->getParentOfType<SimFuncOp>() : SimFuncOp{};
  auto design = caller ? caller->getParentOfType<SimDesignOp>() : SimDesignOp{};
  // SimFuncOp verification guarantees that Function entries are zero-time and
  // contain no suspension operations, so legality does not duplicate that
  // invariant with a second operation-family allowlist.
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
  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/Simulation/SimulationOps.cpp.inc"
      >();
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

static LogicalResult
verifyEffectArray(llvm::function_ref<InFlightDiagnostic()> emitError,
                  ArrayAttr effects, StringRef owner) {
  if (!effects)
    return emitError() << owner << " requires an effect array";
  if (llvm::any_of(effects, [](Attribute attr) {
        return !isa<ComputeEffectAttr>(attr);
      }))
    return emitError() << owner << " contains a non-effect attribute";
  return success();
}

LogicalResult ComputeEffectAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    ComputeEffectKind effect, ComputeResourceKind resource,
    ComputeTargetKind target, uint64_t descriptor, uint32_t formal,
    uint64_t low, uint64_t width, bool dynamic, bool deferred,
    ComputeTriggerKind trigger) {
  if (target != ComputeTargetKind::Descriptor && descriptor != 0)
    return emitError() << "non-descriptor effect has a descriptor value";
  if (target != ComputeTargetKind::Formal && formal != 0)
    return emitError() << "non-formal effect has a formal index";
  if (resource == ComputeResourceKind::Unknown &&
      target != ComputeTargetKind::Unknown)
    return emitError() << "unknown effect has a concrete target";
  if (resource == ComputeResourceKind::Local &&
      target != ComputeTargetKind::Local)
    return emitError() << "local effect has a non-local target";
  if (resource != ComputeResourceKind::Unknown &&
      resource != ComputeResourceKind::Local &&
      target != ComputeTargetKind::Descriptor &&
      target != ComputeTargetKind::Formal)
    return emitError() << "concrete effect has no descriptor or formal target";
  if (resource == ComputeResourceKind::Unknown &&
      (low != 0 || width != 0 || dynamic))
    return emitError() << "unknown effect must not claim a concrete range";
  if (resource != ComputeResourceKind::Unknown && width == 0)
    return emitError() << "concrete effect has zero width";
  bool watches = effect == ComputeEffectKind::Watch;
  if (watches != (trigger != ComputeTriggerKind::None))
    return emitError() << "watch effects require exactly one trigger kind";
  if (deferred && effect != ComputeEffectKind::NBA &&
      effect != ComputeEffectKind::Trigger)
    return emitError() << "only NBA and trigger effects may be deferred";
  return success();
}

LogicalResult
DPIABIAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                   DPIABIKind kind, DPIArgumentDirection, uint32_t width,
                   bool fourState, bool) {
  if (width == 0)
    return emitError() << "DPI ABI width must be nonzero";
  auto require = [&](uint32_t expectedWidth,
                     bool expectedFourState) -> LogicalResult {
    if (width != expectedWidth)
      return emitError() << "DPI ABI category requires width " << expectedWidth;
    if (fourState != expectedFourState)
      return emitError() << "DPI ABI category has incompatible state kind";
    return success();
  };
  switch (kind) {
  case DPIABIKind::Bit:
    return require(1, false);
  case DPIABIKind::Logic:
    return require(1, true);
  case DPIABIKind::Byte:
    return require(8, false);
  case DPIABIKind::ShortInt:
    return require(16, false);
  case DPIABIKind::Int:
    return require(32, false);
  case DPIABIKind::LongInt:
    return require(64, false);
  case DPIABIKind::BitVector:
    return fourState
               ? emitError() << "bit-vector DPI ABI category must be two-state"
               : success();
  case DPIABIKind::LogicVector:
    return !fourState
               ? emitError()
                     << "logic-vector DPI ABI category must be four-state"
               : success();
  }
  llvm_unreachable("unknown DPI ABI category");
}

uint64_t getDPISignatureHash(ArrayAttr signature, uint64_t logicalInputs) {
  auto append = [](uint64_t hash, uint64_t value, unsigned bytes) {
    for (unsigned index = 0; index != bytes; ++index) {
      hash ^= static_cast<uint8_t>(value >> (index * 8));
      hash *= UINT64_C(1099511628211);
    }
    return hash;
  };
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = append(hash, logicalInputs, 8);
  hash = append(hash, signature.size() - logicalInputs, 8);
  for (Attribute attribute : signature) {
    auto abi = cast<DPIABIAttr>(attribute);
    hash = append(hash, static_cast<uint32_t>(abi.getKind()), 4);
    hash = append(hash, static_cast<uint32_t>(abi.getDirection()), 4);
    hash = append(hash, abi.getWidth(), 4);
    hash = append(hash, abi.getFourState() ? 1 : 0, 1);
    hash = append(hash, abi.getIsSigned() ? 1 : 0, 1);
  }
  return hash ? hash : 1;
}

LogicalResult ComputeFragmentAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    FlatSymbolRefAttr function, uint32_t block, ComputeRegionKind region,
    ComputeActionKind action, ComputeTierKind tier, uint64_t cost,
    uint32_t lane, bool twoState, ArrayAttr effects) {
  if (!function)
    return emitError() << "fragment requires a function symbol";
  return verifyEffectArray(emitError, effects, "fragment");
}

LogicalResult ComputeNBACommitAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    DenseI64ArrayAttr slots, DenseI64ArrayAttr accumulatorSites,
    DenseI64ArrayAttr frontierSites, ComputeEffectAttr effect) {
  if (!slots || !accumulatorSites || !frontierSites || !effect ||
      effect.getEffect() != ComputeEffectKind::Write)
    return emitError()
           << "NBA commit requires staging inventories and one write effect";
  if (slots.empty() && accumulatorSites.empty() && frontierSites.empty())
    return emitError() << "NBA commit requires at least one site";
  llvm::SmallDenseSet<int64_t> sites;
  for (DenseI64ArrayAttr inventory : {slots, accumulatorSites, frontierSites})
    for (int64_t site : inventory.asArrayRef())
      if (site < 0 || !sites.insert(site).second)
        return emitError() << "NBA commit has an invalid or duplicate site";
  return success();
}

LogicalResult ComputeEventCommitAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    DenseI64ArrayAttr sites, ComputeEffectAttr effect) {
  if (!sites || !effect || effect.getEffect() != ComputeEffectKind::Trigger ||
      !effect.getDeferred())
    return emitError()
           << "event commit requires sites and one deferred trigger effect";
  if (sites.empty())
    return emitError() << "event commit requires at least one site";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t site : sites.asArrayRef())
    if (site < 0 || !unique.insert(site).second)
      return emitError() << "event commit has an invalid or duplicate site";
  return success();
}

LogicalResult
ComputeEdgeAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        uint32_t source, uint32_t target, ComputeEdgeKind kind,
                        ComputeEffectAttr resource) {
  bool needsResource = kind == ComputeEdgeKind::Sensitivity ||
                       kind == ComputeEdgeKind::NBAStage ||
                       kind == ComputeEdgeKind::NBAActivate ||
                       kind == ComputeEdgeKind::Conflict ||
                       kind == ComputeEdgeKind::DeferredStage ||
                       kind == ComputeEdgeKind::DeferredActivate;
  if (needsResource && !resource)
    return emitError() << "edge kind requires a resource effect";
  if ((kind == ComputeEdgeKind::ProcessOrder ||
       kind == ComputeEdgeKind::Resume || kind == ComputeEdgeKind::Spawn) &&
      resource)
    return emitError() << "control-only edge cannot carry a resource";
  return success();
}

LogicalResult
ComputeGroupAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         DenseI64ArrayAttr fragments,
                         ComputeScheduleKind schedule, ArrayAttr feedback) {
  if (!fragments || fragments.empty() || !feedback)
    return emitError() << "schedule group must contain fragments and feedback";
  llvm::SmallDenseSet<int64_t> members;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !members.insert(fragment).second)
      return emitError() << "schedule group has an invalid or duplicate member";
  if (failed(verifyEffectArray(emitError, feedback, "schedule feedback")))
    return failure();
  if (schedule != ComputeScheduleKind::Convergence && !feedback.empty())
    return emitError() << "only convergence groups may carry feedback";
  return success();
}

LogicalResult
ComputeRegionAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ComputeRegionKind kind, ArrayAttr groups) {
  if (!groups || llvm::any_of(groups, [](Attribute attr) {
        return !isa<ComputeGroupAttr>(attr);
      }))
    return emitError() << "event region contains a non-group attribute";
  return success();
}

LogicalResult
ComputeGraphAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         uint32_t version, ComputeVPIMode vpi, uint32_t workers,
                         ArrayAttr nodes, ArrayAttr edges, ArrayAttr regions) {
  if (version != 1)
    return emitError() << "unsupported compute-graph version";
  if (workers == 0 || workers > 65535)
    return emitError() << "worker count is outside the lane ID range";
  if (!nodes || llvm::any_of(nodes, [](Attribute attr) {
        return !isa<ComputeFragmentAttr, ComputeNBACommitAttr,
                    ComputeEventCommitAttr>(attr);
      }))
    return emitError() << "compute graph contains a non-node attribute";
  if (!edges || llvm::any_of(edges, [](Attribute attr) {
        return !isa<ComputeEdgeAttr>(attr);
      }))
    return emitError() << "compute graph contains a non-edge attribute";
  if (!regions || regions.size() != 5)
    return emitError() << "compute graph requires all five event regions";
  static constexpr ComputeRegionKind expectedRegions[] = {
      ComputeRegionKind::Active, ComputeRegionKind::NBA,
      ComputeRegionKind::Observed, ComputeRegionKind::Reactive,
      ComputeRegionKind::Postponed};
  for (auto [attribute, expected] : llvm::zip(regions, expectedRegions)) {
    auto region = dyn_cast<ComputeRegionAttr>(attribute);
    if (!region || region.getKind() != expected)
      return emitError() << "compute graph event regions are out of order";
  }
  return success();
}

LogicalResult
FragmentABIAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        uint32_t version, DenseI64ArrayAttr fragments) {
  if (version != 1 || !fragments)
    return emitError() << "invalid fragment ABI version or inventory";
  llvm::SmallDenseSet<int64_t> ids;
  for (int64_t id : fragments.asArrayRef())
    if (id < 0 || !ids.insert(id).second)
      return emitError() << "fragment ABI has an invalid or duplicate ID";
  return success();
}

LogicalResult
NBASiteAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                    uint64_t id, uint32_t commit, ComputeNBAStorageKind storage,
                    TimingSiteAttr timing) {
  if (timing && timing.getKind() != ComputeTimingKind::DelayedNBA)
    return emitError() << "NBA timing site must have delayed_nba kind";
  return success();
}

LogicalResult
EventSiteAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                      uint64_t id, uint32_t commit, TimingSiteAttr timing) {
  if (timing && timing.getKind() != ComputeTimingKind::DelayedEvent)
    return emitError()
           << "deferred-event timing site must have delayed_event kind";
  return success();
}

std::optional<unsigned> getPackedWidth(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.isSignless() ? std::optional<unsigned>(integer.getWidth())
                                : std::nullopt;
  if (auto logic = dyn_cast<LogicType>(type))
    return logic.getWidth();
  auto checkedProduct = [](uint64_t lhs,
                           uint64_t rhs) -> std::optional<unsigned> {
    if (lhs && rhs > std::numeric_limits<unsigned>::max() / lhs)
      return std::nullopt;
    uint64_t result = lhs * rhs;
    if (result == 0 || result > std::numeric_limits<unsigned>::max())
      return std::nullopt;
    return static_cast<unsigned>(result);
  };
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    std::optional<unsigned> element = getPackedWidth(array.getElementType());
    std::optional<unsigned> count =
        getArrayElementOrdinal(array, array.getRight());
    if (!element || !count)
      return std::nullopt;
    return checkedProduct(static_cast<uint64_t>(*count) + 1, *element);
  }
  auto aggregateWidth = [&](ArrayAttr fields) -> std::optional<unsigned> {
    uint64_t width = 0;
    for (Attribute attribute : fields) {
      auto field = dyn_cast<FieldAttr>(attribute);
      std::optional<unsigned> fieldWidth =
          field ? getPackedWidth(field.getType()) : std::nullopt;
      if (!fieldWidth || field.getPackedOffset() >
                             std::numeric_limits<unsigned>::max() - *fieldWidth)
        return std::nullopt;
      width = std::max<uint64_t>(width, field.getPackedOffset() +
                                            static_cast<uint64_t>(*fieldWidth));
    }
    if (width == 0 || width > std::numeric_limits<unsigned>::max())
      return std::nullopt;
    return static_cast<unsigned>(width);
  };
  if (auto structure = dyn_cast<PackedStructType>(type))
    return aggregateWidth(structure.getFields());
  if (auto unionType = dyn_cast<PackedUnionType>(type)) {
    std::optional<unsigned> payload = aggregateWidth(unionType.getFields());
    if (!payload || unionType.getTagBits() >
                        std::numeric_limits<unsigned>::max() - *payload)
      return std::nullopt;
    return *payload + unionType.getTagBits();
  }
  return std::nullopt;
}

static bool containsFourStateLeaf(Type type) {
  if (isa<LogicType>(type))
    return true;
  if (!isAggregateType(type))
    return false;
  for (unsigned index = 0, end = getAggregateNumElements(type); index < end;
       ++index)
    if (containsFourStateLeaf(getAggregateElementType(type, index)))
      return true;
  return false;
}

Type getPackedScalarType(Type type) {
  std::optional<unsigned> width = getPackedWidth(type);
  if (!width)
    return {};
  if (!isAggregateType(type))
    return type;
  if (containsFourStateLeaf(type))
    return LogicType::get(type.getContext(), *width);
  return IntegerType::get(type.getContext(), *width);
}

bool isAggregateType(Type type) {
  return isa<PackedArrayType, UnpackedArrayType, PackedStructType,
             UnpackedStructType, PackedUnionType, UnpackedUnionType>(type);
}

static ArrayAttr getAggregateFields(Type type) {
  return llvm::TypeSwitch<Type, ArrayAttr>(type)
      .Case<PackedStructType, UnpackedStructType, PackedUnionType,
            UnpackedUnionType>(
          [](auto aggregate) { return aggregate.getFields(); })
      .Default([](Type) { return ArrayAttr{}; });
}

unsigned getAggregateNumElements(Type type) {
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    std::optional<unsigned> last =
        getArrayElementOrdinal(array, array.getRight());
    return last ? *last + 1 : 0;
  }
  if (auto array = dyn_cast<UnpackedArrayType>(type)) {
    std::optional<unsigned> last =
        getArrayElementOrdinal(array, array.getRight());
    return last ? *last + 1 : 0;
  }
  ArrayAttr fields = getAggregateFields(type);
  return fields ? fields.size() : 0;
}

Type getAggregateElementType(Type type, unsigned index) {
  if (auto array = dyn_cast<PackedArrayType>(type))
    return index < getAggregateNumElements(type) ? array.getElementType()
                                                 : Type{};
  if (auto array = dyn_cast<UnpackedArrayType>(type))
    return index < getAggregateNumElements(type) ? array.getElementType()
                                                 : Type{};
  ArrayAttr fields = getAggregateFields(type);
  if (!fields || index >= fields.size())
    return {};
  auto field = dyn_cast<FieldAttr>(fields[index]);
  return field ? field.getType() : Type{};
}

std::optional<unsigned> getArrayElementOrdinal(Type type, int64_t sourceIndex) {
  int64_t left;
  int64_t right;
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    left = array.getLeft();
    right = array.getRight();
  } else if (auto array = dyn_cast<UnpackedArrayType>(type)) {
    left = array.getLeft();
    right = array.getRight();
  } else {
    return std::nullopt;
  }
  uint64_t ordinal;
  if (left >= right) {
    if (sourceIndex > left || sourceIndex < right)
      return std::nullopt;
    ordinal = static_cast<uint64_t>(left) - static_cast<uint64_t>(sourceIndex);
  } else {
    if (sourceIndex < left || sourceIndex > right)
      return std::nullopt;
    ordinal = static_cast<uint64_t>(sourceIndex) - static_cast<uint64_t>(left);
  }
  if (ordinal > std::numeric_limits<unsigned>::max())
    return std::nullopt;
  return static_cast<unsigned>(ordinal);
}

std::optional<uint64_t> getProvenanceSpan(Type type) {
  if (auto reference = dyn_cast<RefType>(type))
    return getProvenanceSpan(reference.getElementType());
  if (auto net = dyn_cast<NetType>(type))
    return getProvenanceSpan(net.getElementType());
  if (auto driver = dyn_cast<DriverType>(type))
    return getProvenanceSpan(driver.getElementType());
  if (isa<EventType>(type))
    return uint64_t{1};
  if (std::optional<unsigned> packed = getPackedWidth(type))
    return *packed;
  if (isa<TimeType>(type) || type.isF64())
    return uint64_t{64};
  if (type.isF32())
    return uint64_t{32};
  if (isManagedHandleType(type))
    return uint64_t{64};
  auto checkedAlign = [](uint64_t value,
                         uint64_t alignment) -> std::optional<uint64_t> {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
      return std::nullopt;
    return (value + alignment - 1) & ~(alignment - 1);
  };
  auto checkedAdd = [](uint64_t &total, uint64_t amount) {
    if (amount > std::numeric_limits<uint64_t>::max() - total)
      return false;
    total += amount;
    return true;
  };
  if (isa<UnpackedArrayType>(type)) {
    uint64_t count = getAggregateNumElements(type);
    Type elementType = getAggregateElementType(type, 0);
    std::optional<uint64_t> element = getProvenanceSpan(elementType);
    std::optional<uint64_t> alignment = getProvenanceAlignment(elementType);
    std::optional<uint64_t> stride = element && alignment
                                         ? checkedAlign(*element, *alignment)
                                         : std::nullopt;
    if (!stride ||
        (count && *stride > std::numeric_limits<uint64_t>::max() / count))
      return std::nullopt;
    return count * *stride;
  }
  if (isa<UnpackedStructType>(type)) {
    uint64_t total = 0;
    uint64_t alignment = 1;
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      Type childType = getAggregateElementType(type, index);
      std::optional<uint64_t> child = getProvenanceSpan(childType);
      std::optional<uint64_t> childAlignment =
          getProvenanceAlignment(childType);
      std::optional<uint64_t> offset =
          childAlignment ? checkedAlign(total, *childAlignment) : std::nullopt;
      if (!child || !offset || !checkedAdd(*offset, *child))
        return std::nullopt;
      total = *offset;
      alignment = std::max(alignment, *childAlignment);
    }
    return checkedAlign(total, alignment);
  }
  if (isa<UnpackedUnionType>(type)) {
    uint64_t maximum = 0;
    uint64_t alignment = 1;
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      Type childType = getAggregateElementType(type, index);
      std::optional<uint64_t> child = getProvenanceSpan(childType);
      std::optional<uint64_t> childAlignment =
          getProvenanceAlignment(childType);
      if (!child || !childAlignment)
        return std::nullopt;
      maximum = std::max(maximum, *child);
      alignment = std::max(alignment, *childAlignment);
    }
    return checkedAlign(maximum, alignment);
  }
  return std::nullopt;
}

std::optional<uint64_t> getProvenanceAlignment(Type type) {
  if (auto reference = dyn_cast<RefType>(type))
    return getProvenanceAlignment(reference.getElementType());
  if (auto net = dyn_cast<NetType>(type))
    return getProvenanceAlignment(net.getElementType());
  if (auto driver = dyn_cast<DriverType>(type))
    return getProvenanceAlignment(driver.getElementType());
  if (type.isF32())
    return uint64_t{32};
  if (type.isF64())
    return uint64_t{64};
  if (isManagedHandleType(type))
    return uint64_t{64};
  if (isa<UnpackedArrayType>(type))
    return getProvenanceAlignment(getAggregateElementType(type, 0));
  if (isa<UnpackedStructType, UnpackedUnionType>(type)) {
    uint64_t alignment = 1;
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      std::optional<uint64_t> child =
          getProvenanceAlignment(getAggregateElementType(type, index));
      if (!child)
        return std::nullopt;
      alignment = std::max(alignment, *child);
    }
    return alignment;
  }
  return getProvenanceSpan(type) ? std::optional<uint64_t>{1} : std::nullopt;
}

std::optional<std::pair<uint64_t, uint64_t>>
getAggregateProvenanceSubelement(Type type, unsigned index) {
  Type element = getAggregateElementType(type, index);
  std::optional<uint64_t> span = getProvenanceSpan(element);
  if (!element || !span)
    return std::nullopt;
  uint64_t offset = 0;
  if (isa<PackedStructType, PackedUnionType>(type)) {
    auto field = cast<FieldAttr>(getAggregateFields(type)[index]);
    offset = field.getPackedOffset();
  } else if (isa<PackedArrayType>(type)) {
    uint64_t count = getAggregateNumElements(type);
    if (*span &&
        count - index - 1 > std::numeric_limits<uint64_t>::max() / *span)
      return std::nullopt;
    offset = (count - index - 1) * *span;
  } else if (isa<UnpackedArrayType>(type)) {
    std::optional<uint64_t> alignment = getProvenanceAlignment(element);
    if (!alignment || *alignment == 0 ||
        *span > std::numeric_limits<uint64_t>::max() - (*alignment - 1))
      return std::nullopt;
    uint64_t stride = (*span + *alignment - 1) & ~(*alignment - 1);
    if (stride && index > std::numeric_limits<uint64_t>::max() / stride)
      return std::nullopt;
    offset = index * stride;
  } else if (isa<UnpackedStructType>(type)) {
    for (unsigned previous = 0; previous < index; ++previous) {
      Type previousType = getAggregateElementType(type, previous);
      std::optional<uint64_t> previousSpan = getProvenanceSpan(previousType);
      std::optional<uint64_t> previousAlignment =
          getProvenanceAlignment(previousType);
      if (!previousSpan || !previousAlignment ||
          offset >
              std::numeric_limits<uint64_t>::max() - (*previousAlignment - 1))
        return std::nullopt;
      offset = (offset + *previousAlignment - 1) & ~(*previousAlignment - 1);
      if (*previousSpan > std::numeric_limits<uint64_t>::max() - offset)
        return std::nullopt;
      offset += *previousSpan;
    }
    std::optional<uint64_t> alignment = getProvenanceAlignment(element);
    if (!alignment ||
        offset > std::numeric_limits<uint64_t>::max() - (*alignment - 1))
      return std::nullopt;
    offset = (offset + *alignment - 1) & ~(*alignment - 1);
  } else if (!isa<UnpackedUnionType>(type)) {
    return std::nullopt;
  }
  return std::pair<uint64_t, uint64_t>{offset, *span};
}

bool getManagedHandleOffsets(Type type,
                             llvm::SmallVectorImpl<uint64_t> &offsets) {
  if (isManagedHandleType(type)) {
    offsets.push_back(0);
    return true;
  }
  if (!isAggregateType(type))
    return true;
  // Union members overlap. A flat list of root offsets cannot distinguish an
  // active class member from ordinary bits in another member, and treating
  // those bits as a pointer would make the collector conservative (and could
  // admit an invalid host address). Keep the failure explicit until the trace
  // ABI carries active-member guards.
  if (isa<PackedUnionType, UnpackedUnionType>(type)) {
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      SmallVector<uint64_t, 2> nested;
      if (!getManagedHandleOffsets(getAggregateElementType(type, index),
                                   nested))
        return false;
      if (!nested.empty())
        return false;
    }
    return true;
  }
  for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
    std::optional<std::pair<uint64_t, uint64_t>> child =
        getAggregateProvenanceSubelement(type, index);
    if (!child)
      return false;
    SmallVector<uint64_t, 2> nested;
    if (!getManagedHandleOffsets(getAggregateElementType(type, index), nested))
      return false;
    for (uint64_t nestedOffset : nested) {
      if (nestedOffset > std::numeric_limits<uint64_t>::max() - child->first)
        return false;
      offsets.push_back(child->first + nestedOffset);
    }
  }
  llvm::sort(offsets);
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
  return true;
}

bool isManagedHandleType(Type type) {
  return isa<ClassHandleType, StringType, DynamicArrayType, QueueType,
             AssocArrayType, ReferencePathType>(type);
}

LogicalResult SimManagedNullOp::verify() {
  if (!isManagedHandleType(getResult().getType()) ||
      isa<ClassHandleType>(getResult().getType()))
    return emitOpError("result must be a non-class managed handle type");
  return success();
}

LogicalResult SimManagedIsNullOp::verify() {
  if (!isManagedHandleType(getInput().getType()))
    return emitOpError("input must be a managed handle type");
  return success();
}

static Type getSequentialContainerElement(Type type) {
  if (auto array = dyn_cast<DynamicArrayType>(type))
    return array.getElementType();
  if (auto queue = dyn_cast<QueueType>(type))
    return queue.getElementType();
  return {};
}

static Type getContainerElement(Type type) {
  if (Type element = getSequentialContainerElement(type))
    return element;
  if (auto array = dyn_cast<AssocArrayType>(type))
    return array.getElementType();
  return {};
}

LogicalResult SimContainerSizeOp::verify() {
  if (!getContainerElement(getContainer().getType()))
    return emitOpError(
        "container must be a dynamic array, queue, or associative array");
  return success();
}

LogicalResult SimContainerCreateLikeOp::verify() {
  Type type = getResult().getType();
  if (!getSequentialContainerElement(type))
    return emitOpError("result must be a dynamic array or queue");
  if (getPreferred().getType() != type || getFallback().getType() != type)
    return emitOpError("source and result container types must match");
  return success();
}

LogicalResult SimContainerCreateOp::verify() {
  Type type = getResult().getType();
  Type element = getSequentialContainerElement(type);
  if (!element)
    return emitOpError("result must be a dynamic array or queue");
  if (getTypeId() == 0)
    return emitOpError("element type ID must be nonzero");
  if (getElementKind() < 1 || getElementKind() > 8)
    return emitOpError("element kind is outside the runtime ABI");
  if ((getElementFlags() & ~3u) != 0)
    return emitOpError("element flags contain an unknown runtime ABI bit");
  if (getValueSize() == 0 || getAlignment() == 0 ||
      !llvm::isPowerOf2_64(getAlignment()) ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element size and alignment are invalid");
  if (getContainerKind() != 1 && getContainerKind() != 2)
    return emitOpError("container kind is outside the runtime ABI");
  ArrayRef<int64_t> traceOffsets = getTraceOffsets();
  ArrayRef<int32_t> traceKinds = getTraceKinds();
  if (traceOffsets.size() != traceKinds.size())
    return emitOpError("trace offset and kind inventories must match");
  if (getElementKind() != 7 && !traceOffsets.empty())
    return emitOpError("only aggregate elements carry explicit trace slots");
  int64_t previousOffset = -1;
  for (auto [offset, kind] : llvm::zip_equal(traceOffsets, traceKinds)) {
    if (offset < 0 || static_cast<uint64_t>(offset) > getValueSize() ||
        sizeof(void *) > getValueSize() - static_cast<uint64_t>(offset))
      return emitOpError("trace slot is outside the element value plane");
    if (static_cast<uint64_t>(offset) % alignof(void *) != 0)
      return emitOpError("trace slot is not pointer aligned");
    if (kind < 1 || kind > 3)
      return emitOpError("trace slot kind is outside the runtime ABI");
    if (offset <= previousOffset)
      return emitOpError("trace slot offsets must be strictly increasing");
    previousOffset = offset;
  }
  if (isa<DynamicArrayType>(type) && getContainerKind() != 1)
    return emitOpError("dynamic-array result requires dynamic-array metadata");
  if (isa<QueueType>(type) && getContainerKind() != 2)
    return emitOpError("queue result requires queue metadata");
  uint32_t expectedKind = 0;
  uint64_t expectedSize = 0;
  uint64_t expectedWidth = 0;
  bool fourState = false;
  SmallVector<int64_t, 2> expectedTraceOffsets;
  SmallVector<int32_t, 2> expectedTraceKinds;
  if (auto integer = dyn_cast<IntegerType>(element)) {
    expectedKind = 1;
    expectedSize = (integer.getWidth() + 7) / 8;
    expectedWidth = integer.getWidth();
  } else if (auto logic = dyn_cast<LogicType>(element)) {
    expectedKind = 2;
    expectedSize = (logic.getWidth() + 7) / 8;
    expectedWidth = logic.getWidth();
    fourState = true;
  } else if (auto real = dyn_cast<FloatType>(element)) {
    expectedKind = 3;
    expectedSize = real.getWidth() / 8;
    expectedWidth = real.getWidth();
  } else if (isa<ClassHandleType>(element)) {
    expectedKind = 4;
    expectedSize = sizeof(void *);
  } else if (isa<StringType>(element)) {
    expectedKind = 5;
    expectedSize = sizeof(void *);
  } else if (isa<DynamicArrayType, QueueType, AssocArrayType>(element)) {
    expectedKind = 6;
    expectedSize = sizeof(void *);
  } else if (isa<EventType>(element)) {
    expectedKind = 8;
    expectedSize = sizeof(uint64_t);
  } else if (Type scalar = getPackedScalarType(element)) {
    std::optional<unsigned> width = getPackedWidth(element);
    if (!width || *width == 0)
      return emitOpError("packed element has no canonical width");
    fourState = isa<LogicType>(scalar);
    expectedKind = fourState ? 2 : 1;
    expectedSize = (*width + 7) / 8;
    expectedWidth = *width;
  } else if (isAggregateType(element)) {
    std::optional<uint64_t> width = getProvenanceSpan(element);
    if (!width || *width == 0)
      return emitOpError("aggregate element has no canonical layout");
    element.walk([&](LogicType) { fourState = true; });
    expectedKind = 7;
    expectedSize = (*width + 7) / 8;
    expectedWidth = expectedSize * 8;
    std::function<LogicalResult(Type, uint64_t)> collectTrace =
        [&](Type nested, uint64_t baseBitOffset) -> LogicalResult {
      if (isManagedHandleType(nested)) {
        if ((baseBitOffset & 7) != 0 || baseBitOffset / 8 > uint64_t{INT64_MAX})
          return failure();
        int32_t kind = 1;
        if (isa<StringType>(nested))
          kind = 2;
        else if (isa<DynamicArrayType, QueueType, AssocArrayType,
                     ReferencePathType>(nested))
          kind = 3;
        expectedTraceOffsets.push_back(static_cast<int64_t>(baseBitOffset / 8));
        expectedTraceKinds.push_back(kind);
        return success();
      }
      if (!isAggregateType(nested))
        return success();
      if (isa<PackedUnionType, UnpackedUnionType>(nested)) {
        SmallVector<uint64_t, 2> offsets;
        return getManagedHandleOffsets(nested, offsets) ? success() : failure();
      }
      for (unsigned index = 0; index < getAggregateNumElements(nested);
           ++index) {
        auto child = getAggregateProvenanceSubelement(nested, index);
        if (!child || child->first > UINT64_MAX - baseBitOffset ||
            failed(collectTrace(getAggregateElementType(nested, index),
                                baseBitOffset + child->first)))
          return failure();
      }
      return success();
    };
    if (failed(collectTrace(element, 0)))
      return emitOpError("aggregate element has no canonical trace layout");
  }
  if (expectedKind != 0 &&
      (getElementKind() != expectedKind || getValueSize() != expectedSize ||
       getBitWidth() != expectedWidth ||
       ((getElementFlags() & 1u) != 0) != fourState))
    return emitOpError(
        "element metadata does not match the result container element type");
  if (traceOffsets != ArrayRef<int64_t>(expectedTraceOffsets) ||
      traceKinds != ArrayRef<int32_t>(expectedTraceKinds))
    return emitOpError(
        "trace inventory does not match the result container element type");
  return success();
}

LogicalResult SimContainerCloneOp::verify() {
  if (!getContainerElement(getInput().getType()) ||
      getInput().getType() != getResult().getType())
    return emitOpError("input and result must be the same container type");
  return success();
}

LogicalResult SimContainerDeleteOp::verify() {
  if (!getContainerElement(getContainer().getType()))
    return emitOpError(
        "operand must be a dynamic array, queue, or associative array");
  return success();
}

LogicalResult SimQueueDeleteOp::verify() {
  if (!isa<QueueType>(getQueue().getType()))
    return emitOpError("queue operand must have queue type");
  return success();
}

LogicalResult SimContainerReadOp::verify() {
  Type element = getSequentialContainerElement(getContainer().getType());
  if (!element)
    return emitOpError("container must be a dynamic array or queue");
  if (element != getResult().getType())
    return emitOpError("result type must match the container element");
  return success();
}

LogicalResult SimContainerWriteOp::verify() {
  Type element = getSequentialContainerElement(getContainer().getType());
  if (!element)
    return emitOpError("container must be a dynamic array or queue");
  if (element != getValue().getType())
    return emitOpError("value type must match the container element");
  return success();
}

static LogicalResult verifyAssocKey(Operation *op, AssocArrayType array,
                                    Type key) {
  if (array.getWildcardIndex())
    return op->emitOpError("wildcard associative arrays are not executable");
  if (array.getKeyType() != key)
    return op->emitOpError("key type must match the associative array key");
  return success();
}

LogicalResult SimAssocCreateOp::verify() {
  AssocArrayType array = getResult().getType();
  if (array.getWildcardIndex())
    return emitOpError("wildcard associative arrays are not executable");
  if (getTypeId() == 0 || getElementKind() < 1 || getElementKind() > 8)
    return emitOpError("element descriptor is outside the runtime ABI");
  if ((getElementFlags() & ~3u) != 0 || getValueSize() == 0 ||
      getAlignment() == 0 || !llvm::isPowerOf2_64(getAlignment()) ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element descriptor has an invalid layout");
  if (getTraceOffsets().size() != getTraceKinds().size())
    return emitOpError("trace offset and kind inventories must match");
  int64_t previousOffset = -1;
  for (auto [offset, kind] :
       llvm::zip_equal(getTraceOffsets(), getTraceKinds())) {
    if (offset < 0 || static_cast<uint64_t>(offset) > getValueSize() ||
        sizeof(void *) > getValueSize() - static_cast<uint64_t>(offset))
      return emitOpError("trace slot is outside the element value plane");
    if (static_cast<uint64_t>(offset) % alignof(void *) != 0)
      return emitOpError("trace slot is not pointer aligned");
    if (kind < 1 || kind > 3)
      return emitOpError("trace slot kind is outside the runtime ABI");
    if (offset <= previousOffset)
      return emitOpError("trace slot offsets must be strictly increasing");
    previousOffset = offset;
  }
  Type element = array.getElementType();
  uint32_t expectedKind = 0;
  uint64_t expectedSize = 0;
  uint64_t expectedWidth = 0;
  bool fourState = false;
  SmallVector<int64_t, 2> expectedTraceOffsets;
  SmallVector<int32_t, 2> expectedTraceKinds;
  if (auto integer = dyn_cast<IntegerType>(element)) {
    expectedKind = 1;
    expectedSize = (integer.getWidth() + 7) / 8;
    expectedWidth = integer.getWidth();
  } else if (auto logic = dyn_cast<LogicType>(element)) {
    expectedKind = 2;
    expectedSize = (logic.getWidth() + 7) / 8;
    expectedWidth = logic.getWidth();
    fourState = true;
  } else if (auto real = dyn_cast<FloatType>(element)) {
    expectedKind = 3;
    expectedSize = real.getWidth() / 8;
    expectedWidth = real.getWidth();
  } else if (isa<ClassHandleType>(element)) {
    expectedKind = 4;
    expectedSize = sizeof(void *);
  } else if (isa<StringType>(element)) {
    expectedKind = 5;
    expectedSize = sizeof(void *);
  } else if (isa<DynamicArrayType, QueueType, AssocArrayType>(element)) {
    expectedKind = 6;
    expectedSize = sizeof(void *);
  } else if (isa<EventType>(element)) {
    expectedKind = 8;
    expectedSize = sizeof(uint64_t);
  } else if (Type scalar = getPackedScalarType(element)) {
    std::optional<unsigned> width = getPackedWidth(element);
    if (!width || *width == 0)
      return emitOpError("packed element has no canonical width");
    fourState = isa<LogicType>(scalar);
    expectedKind = fourState ? 2 : 1;
    expectedSize = (*width + 7) / 8;
    expectedWidth = *width;
  } else if (isAggregateType(element)) {
    std::optional<uint64_t> width = getProvenanceSpan(element);
    if (!width || *width == 0)
      return emitOpError("aggregate element has no canonical layout");
    element.walk([&](LogicType) { fourState = true; });
    expectedKind = 7;
    expectedSize = (*width + 7) / 8;
    expectedWidth = expectedSize * 8;
    std::function<LogicalResult(Type, uint64_t)> collectTrace =
        [&](Type nested, uint64_t baseBitOffset) -> LogicalResult {
      if (isManagedHandleType(nested)) {
        if ((baseBitOffset & 7) != 0 || baseBitOffset / 8 > uint64_t{INT64_MAX})
          return failure();
        int32_t kind = 1;
        if (isa<StringType>(nested))
          kind = 2;
        else if (isa<DynamicArrayType, QueueType, AssocArrayType,
                     ReferencePathType>(nested))
          kind = 3;
        expectedTraceOffsets.push_back(static_cast<int64_t>(baseBitOffset / 8));
        expectedTraceKinds.push_back(kind);
        return success();
      }
      if (!isAggregateType(nested))
        return success();
      if (isa<PackedUnionType, UnpackedUnionType>(nested)) {
        SmallVector<uint64_t, 2> offsets;
        return getManagedHandleOffsets(nested, offsets) ? success() : failure();
      }
      for (unsigned index = 0; index < getAggregateNumElements(nested);
           ++index) {
        auto child = getAggregateProvenanceSubelement(nested, index);
        if (!child || child->first > UINT64_MAX - baseBitOffset ||
            failed(collectTrace(getAggregateElementType(nested, index),
                                baseBitOffset + child->first)))
          return failure();
      }
      return success();
    };
    if (failed(collectTrace(element, 0)))
      return emitOpError("aggregate element has no canonical trace layout");
  }
  if (expectedKind != 0 &&
      (getElementKind() != expectedKind || getValueSize() != expectedSize ||
       getBitWidth() != expectedWidth ||
       ((getElementFlags() & 1u) != 0) != fourState))
    return emitOpError(
        "element metadata does not match the associative element type");
  if (getElementKind() != 7 && !getTraceOffsets().empty())
    return emitOpError("only aggregate elements carry explicit trace slots");
  if (getTraceOffsets() != ArrayRef<int64_t>(expectedTraceOffsets) ||
      getTraceKinds() != ArrayRef<int32_t>(expectedTraceKinds))
    return emitOpError(
        "trace inventory does not match the associative element type");
  Type key = array.getKeyType();
  if (isa<StringType>(key)) {
    if (getKeyKind() != 3 || getKeyWidth() != 0)
      return emitOpError("string key metadata is inconsistent");
  } else {
    std::optional<unsigned> width = getPackedWidth(key);
    if (!width || *width == 0 || *width > 64 ||
        (getKeyKind() != 1 && getKeyKind() != 2) ||
        getKeyWidth() != *width)
      return emitOpError("integral key metadata is inconsistent");
  }
  return success();
}

LogicalResult SimAssocReadOp::verify() {
  AssocArrayType array = getArray().getType();
  if (failed(verifyAssocKey(getOperation(), array, getKey().getType())))
    return failure();
  if (getResult().getType() != array.getElementType())
    return emitOpError("result type must match the associative element");
  return success();
}

LogicalResult SimAssocWriteOp::verify() {
  AssocArrayType array = getArray().getType();
  if (failed(verifyAssocKey(getOperation(), array, getKey().getType())))
    return failure();
  if (getValue().getType() != array.getElementType())
    return emitOpError("value type must match the associative element");
  return success();
}

LogicalResult SimAssocExistsOp::verify() {
  return verifyAssocKey(getOperation(), getArray().getType(),
                        getKey().getType());
}

LogicalResult SimAssocDeleteOp::verify() {
  return verifyAssocKey(getOperation(), getArray().getType(),
                        getKey().getType());
}

LogicalResult SimAssocSetDefaultOp::verify() {
  if (getValue().getType() != getArray().getType().getElementType())
    return emitOpError("default type must match the associative element");
  return success();
}

LogicalResult SimAssocTraverseOp::verify() {
  AssocArrayType array = getArray().getType();
  if (failed(verifyAssocKey(getOperation(), array, getKey().getType())))
    return failure();
  if (getResultKey().getType() != array.getKeyType())
    return emitOpError("result key type must match the associative key");
  int32_t direction = static_cast<int32_t>(getDirection());
  if (direction != -1 && direction != 1)
    return emitOpError("direction must be -1 or 1");
  return success();
}

LogicalResult SimStringFromPackedOp::verify() {
  Type scalar = getPackedScalarType(getInput().getType());
  if (!scalar || !getPackedWidth(scalar))
    return emitOpError("input must be a fixed packed value");
  return success();
}

LogicalResult SimStringConcatOp::verify() {
  if (getInputs().size() > std::numeric_limits<uint32_t>::max())
    return emitOpError("input count exceeds the managed string ABI");
  return success();
}

static LogicalResult verifyStringRadix(Operation *operation, uint32_t radix) {
  if (radix != 2 && radix != 8 && radix != 10 && radix != 16)
    return operation->emitOpError("radix must be 2, 8, 10, or 16");
  return success();
}

LogicalResult SimStringParseIntegerOp::verify() {
  return verifyStringRadix(getOperation(), getRadix());
}

LogicalResult SimStringFormatIntegerOp::verify() {
  return verifyStringRadix(getOperation(), getRadix());
}

static bool isNormalizedValueType(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.isSignless();
  return isa<FloatType>(type) || isa<LogicType>(type) ||
         isManagedHandleType(type) || isAggregateType(type);
}

LogicalResult
DynamicArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         Type elementType) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  return success();
}

LogicalResult
QueueType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  Type elementType, uint32_t) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  return success();
}

LogicalResult
AssocArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                       Type keyType, Type elementType, bool signedKey,
                       bool wildcardIndex) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  if (wildcardIndex)
    return emitError()
           << "wildcard associative-array indices are not executable";
  bool supportedKey = isa<StringType>(keyType);
  if (auto integer = dyn_cast<IntegerType>(keyType))
    supportedKey = integer.isSignless() && integer.getWidth() <= 64;
  if (auto logic = dyn_cast<LogicType>(keyType))
    supportedKey = logic.getWidth() <= 64;
  if (!supportedKey)
    return emitError()
           << "key must be a string or normalized integral type up to 64 bits";
  if (isa<StringType>(keyType) && signedKey)
    return emitError() << "string key cannot be signed";
  return success();
}

LogicalResult
ReferencePathType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          Type elementType) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element must be a normalized simulation value";
  return success();
}

LogicalResult
FrozenConstantAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           Type type, Attribute value, bool) {
  if (!type)
    return emitError() << "frozen constant type must not be null";
  if (!value)
    return emitError() << "frozen constant payload must not be null";
  if (isa<StringType>(type)) {
    if (!isa<StringAttr>(value))
      return emitError()
             << "string frozen constant requires a string attribute payload";
    return success();
  }
  if (isa<FloatType>(type)) {
    auto floating = dyn_cast<FloatAttr>(value);
    if (!floating || floating.getType() != type)
      return emitError()
             << "floating frozen constant requires a matching payload";
    return success();
  }

  Type scalar = getPackedScalarType(type);
  if (!scalar)
    return emitError()
           << "frozen constant type must be floating or a fixed packed value, got "
           << type;
  std::optional<unsigned> width = getPackedWidth(scalar);
  auto planes = dyn_cast<ArrayAttr>(value);
  if (!width || !planes || planes.size() != 2)
    return emitError() << "packed frozen constant requires exactly two integer "
                          "planes matching its scalar width";
  auto valuePlane = dyn_cast<IntegerAttr>(planes[0]);
  auto unknownPlane = dyn_cast<IntegerAttr>(planes[1]);
  if (!valuePlane || !unknownPlane ||
      valuePlane.getValue().getBitWidth() != *width ||
      unknownPlane.getValue().getBitWidth() != *width ||
      !valuePlane.getType().isSignlessInteger(*width) ||
      !unknownPlane.getType().isSignlessInteger(*width))
    return emitError() << "packed frozen constant planes must be signless i"
                       << *width << " integer attributes";
  if (isa<IntegerType>(scalar) && !unknownPlane.getValue().isZero())
    return emitError()
           << "two-state frozen constant must have a zero unknown plane";
  return success();
}

LogicalResult
ArgumentBindingAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            StringAttr path, uint64_t, UnitArgumentKind kind,
                            bool copyOut, IntegerAttr lvalueNode) {
  if (!path || path.getValue().empty())
    return emitError() << "argument binding path must not be empty";
  if (copyOut && kind != UnitArgumentKind::FormalLocal)
    return emitError()
           << "copy-out is valid only for a formal-local argument binding";
  if (lvalueNode && kind != UnitArgumentKind::LValueOnly)
    return emitError()
           << "lvalue node ID is valid only for an lvalue-only binding";
  if (lvalueNode && (lvalueNode.getValue().isNegative() ||
                     lvalueNode.getValue().getActiveBits() > 64))
    return emitError() << "lvalue node ID must be an unsigned 64-bit value";
  return success();
}

LogicalResult
LocalBindingAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         StringAttr path, Type type, bool automatic,
                         bool patternVariable, bool) {
  if (!path || path.getValue().empty())
    return emitError() << "local binding path must not be empty";
  if (!type || !isNormalizedValueType(type))
    return emitError()
           << "local binding type must be a normalized simulation value";
  if (patternVariable && !automatic)
    return emitError() << "pattern-variable binding must be automatic";
  return success();
}

LogicalResult
ConstantBindingAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            StringAttr path, FrozenConstantAttr value) {
  if (!path || path.getValue().empty())
    return emitError() << "constant binding path must not be empty";
  if (!value)
    return emitError() << "constant binding value must not be null";
  return success();
}

StringRef getUnitBindingPath(Attribute binding) {
  if (auto argument = dyn_cast<ArgumentBindingAttr>(binding))
    return argument.getPath().getValue();
  if (auto local = dyn_cast<LocalBindingAttr>(binding))
    return local.getPath().getValue();
  if (auto constant = dyn_cast<ConstantBindingAttr>(binding))
    return constant.getPath().getValue();
  return {};
}

FailureOr<Value> materializeFrozenConstant(OpBuilder &builder,
                                           Location location,
                                           FrozenConstantAttr constant) {
  if (!constant)
    return failure();
  Type type = constant.getType();
  if (isa<StringType>(type)) {
    auto value = dyn_cast<StringAttr>(constant.getValue());
    if (!value)
      return failure();
    return SimStringLiteralOp::create(builder, location, type, value)
        .getResult();
  }
  if (isa<FloatType>(type)) {
    auto value = dyn_cast<FloatAttr>(constant.getValue());
    if (!value)
      return failure();
    return arith::ConstantOp::create(builder, location, type, value)
        .getResult();
  }

  Type scalar = getPackedScalarType(type);
  auto planes = dyn_cast<ArrayAttr>(constant.getValue());
  if (!scalar || !planes || planes.size() != 2)
    return failure();
  auto valuePlane = dyn_cast<IntegerAttr>(planes[0]);
  auto unknownPlane = dyn_cast<IntegerAttr>(planes[1]);
  if (!valuePlane || !unknownPlane)
    return failure();

  Value value;
  if (auto integer = dyn_cast<IntegerType>(scalar))
    value = arith::ConstantOp::create(builder, location, integer, valuePlane);
  else if (isa<LogicType>(scalar))
    value = SimLogicConstantOp::create(builder, location, scalar, valuePlane,
                                       unknownPlane);
  else
    return failure();
  if (type != scalar)
    value = SimPackedUnflattenOp::create(builder, location, type, value);
  return value;
}

static LogicalResult verifyNormalizedIndex(Operation *op, Type type) {
  if (isa<LogicType>(type))
    return success();
  auto integer = dyn_cast<IntegerType>(type);
  if (!integer)
    return op->emitOpError(
        "index must be a signless builtin integer or four-state logic");
  if (!integer.isSignless())
    return op->emitOpError("builtin integer index must be signless");
  return success();
}

static LogicalResult verifyMatchingStateDomain(Operation *op, Type input,
                                               Type result) {
  Type inputScalar = getPackedScalarType(input);
  Type resultScalar = getPackedScalarType(result);
  if (!inputScalar || !resultScalar ||
      isa<LogicType>(inputScalar) != isa<LogicType>(resultScalar))
    return op->emitOpError(
        "input and result element types must use the same state domain");
  return success();
}

static LogicalResult
verifyElementType(llvm::function_ref<InFlightDiagnostic()> emitError,
                  Type elementType) {
  if (auto integer = dyn_cast<IntegerType>(elementType);
      integer && !integer.isSignless())
    return emitError() << "builtin integer element types must be signless";
  if (!isNormalizedValueType(elementType))
    return emitError() << "element type must be a normalized scalar or fixed "
                          "aggregate, got "
                       << elementType;
  return success();
}

LogicalResult
FieldAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  StringAttr name, Type type, uint32_t, uint64_t) {
  if (!name || name.getValue().empty())
    return emitError() << "aggregate field name must not be empty";
  if (!type)
    return emitError() << "aggregate field type must not be null";
  return success();
}

static std::optional<uint64_t> getInclusiveRangeWidth(int64_t left,
                                                      int64_t right) {
  uint64_t distance =
      left >= right
          ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
          : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
  if (distance == std::numeric_limits<uint64_t>::max())
    return std::nullopt;
  return distance + 1;
}

static LogicalResult
verifyArrayType(llvm::function_ref<InFlightDiagnostic()> emitError,
                Type elementType, int64_t left, int64_t right, bool packed) {
  std::optional<uint64_t> width = getInclusiveRangeWidth(left, right);
  if (!width || *width > std::numeric_limits<unsigned>::max())
    return emitError() << "fixed array range is too large";
  if (failed(verifyElementType(emitError, elementType)))
    return failure();
  if (packed && isa<FloatType>(elementType))
    return emitError() << "real-valued aggregate elements are not supported";
  if (packed) {
    std::optional<unsigned> elementWidth = getPackedWidth(elementType);
    if (!elementWidth)
      return emitError() << "packed array element must be packed, got "
                         << elementType;
    if (*width > std::numeric_limits<unsigned>::max() / *elementWidth)
      return emitError() << "packed array width exceeds the supported limit";
  }
  return success();
}

LogicalResult
PackedArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        Type elementType, int64_t left, int64_t right) {
  return verifyArrayType(emitError, elementType, left, right, true);
}

LogicalResult
UnpackedArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          Type elementType, int64_t left, int64_t right) {
  return verifyArrayType(emitError, elementType, left, right, false);
}

static LogicalResult
verifyRecordType(llvm::function_ref<InFlightDiagnostic()> emitError,
                 ArrayAttr fields, bool packed, bool isUnion, bool isTagged,
                 uint32_t tagBits) {
  if (!fields || fields.empty())
    return emitError() << "aggregate requires at least one field";
  if (!packed && tagBits != 0)
    return emitError() << "unpacked union cannot reserve packed tag bits";
  if (!isTagged && tagBits != 0)
    return emitError() << "only a tagged union can reserve tag bits";
  llvm::SmallDenseSet<StringRef, 8> names;
  SmallVector<std::pair<uint64_t, uint64_t>> intervals;
  for (auto [ordinal, attribute] : llvm::enumerate(fields)) {
    auto field = dyn_cast<FieldAttr>(attribute);
    if (!field)
      return emitError() << "aggregate fields must use #obelisk_sim.field";
    if (field.getOrdinal() != ordinal)
      return emitError()
             << "aggregate field ordinals must be dense and ordered";
    if (!names.insert(field.getName().getValue()).second)
      return emitError() << "aggregate field names must be unique";
    if (failed(verifyElementType(emitError, field.getType())))
      return failure();
    if (packed && isa<FloatType>(field.getType()))
      return emitError() << "real-valued aggregate fields are not supported";
    if (!packed) {
      if (field.getPackedOffset() != 0)
        return emitError() << "unpacked aggregate field has a packed offset";
      continue;
    }
    std::optional<unsigned> width = getPackedWidth(field.getType());
    if (!width)
      return emitError() << "packed aggregate field must be packed, got "
                         << field.getType();
    if (field.getPackedOffset() > std::numeric_limits<uint64_t>::max() - *width)
      return emitError() << "packed aggregate field range overflows uint64_t";
    if (field.getPackedOffset() + *width > std::numeric_limits<unsigned>::max())
      return emitError()
             << "packed aggregate width exceeds the supported limit";
    intervals.push_back(
        {field.getPackedOffset(), field.getPackedOffset() + *width});
  }
  if (packed && !isUnion) {
    llvm::sort(intervals);
    if (intervals.front().first != 0)
      return emitError() << "packed struct fields must cover bit zero";
    for (auto [previous, current] :
         llvm::zip(intervals, llvm::drop_begin(intervals)))
      if (previous.second > current.first)
        return emitError() << "packed struct fields overlap";
      else if (previous.second < current.first)
        return emitError() << "packed struct fields must be contiguous";
  }
  if (packed && isUnion) {
    for (auto interval : intervals)
      if (interval.first != 0)
        return emitError() << "packed union fields must start at bit zero";
    if (!isTagged) {
      uint64_t width = intervals.front().second;
      for (auto interval : llvm::drop_begin(intervals))
        if (interval.second != width)
          return emitError()
                 << "untagged packed union fields must have equal widths";
    }
    uint32_t expectedTagBits = static_cast<uint32_t>(
        llvm::bit_width(static_cast<uint64_t>(fields.size() - 1)));
    if (isTagged && tagBits != expectedTagBits)
      return emitError() << "packed tagged union requires " << expectedTagBits
                         << " tag bits";
    uint64_t payloadWidth = 0;
    for (auto interval : intervals)
      payloadWidth = std::max(payloadWidth, interval.second);
    if (tagBits > std::numeric_limits<unsigned>::max() - payloadWidth)
      return emitError()
             << "packed tagged union width exceeds the supported limit";
  }
  return success();
}

LogicalResult
PackedStructType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         ArrayAttr fields) {
  return verifyRecordType(emitError, fields, true, false, false, 0);
}

LogicalResult
UnpackedStructType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           ArrayAttr fields) {
  return verifyRecordType(emitError, fields, false, false, false, 0);
}

LogicalResult
PackedUnionType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        ArrayAttr fields, bool isTagged, uint32_t tagBits) {
  return verifyRecordType(emitError, fields, true, true, isTagged, tagBits);
}

LogicalResult
UnpackedUnionType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ArrayAttr fields, bool isTagged, uint32_t tagBits) {
  return verifyRecordType(emitError, fields, false, true, isTagged, tagBits);
}

static IntegerAttr getSubelementIndexAttr(MLIRContext *context,
                                          unsigned index) {
  return IntegerAttr::get(IntegerType::get(context, 32), index);
}

static std::optional<DenseMap<Attribute, Type>>
getSubelementIndexMap(Type type, bool limitArray) {
  unsigned count = getAggregateNumElements(type);
  if (limitArray && count > 64)
    return std::nullopt;
  DenseMap<Attribute, Type> elements;
  for (unsigned index = 0; index < count; ++index)
    elements.insert({getSubelementIndexAttr(type.getContext(), index),
                     getAggregateElementType(type, index)});
  return elements;
}

static Type getTypeAtSubelementIndex(Type type, Attribute index) {
  auto integer = dyn_cast<IntegerAttr>(index);
  if (!integer || !integer.getType().isInteger(32) ||
      integer.getValue().isNegative() ||
      integer.getValue().getActiveBits() > 32)
    return {};
  return getAggregateElementType(type, static_cast<unsigned>(integer.getInt()));
}

#define OBELISK_DEFINE_ARRAY_DESTRUCTURABLE(TypeName)                          \
  std::optional<DenseMap<Attribute, Type>> TypeName::getSubelementIndexMap()   \
      const {                                                                  \
    return ::obelisk::sim::getSubelementIndexMap(*this, true);                 \
  }                                                                            \
  Type TypeName::getTypeAtIndex(Attribute index) const {                       \
    return getTypeAtSubelementIndex(*this, index);                             \
  }

#define OBELISK_DEFINE_RECORD_DESTRUCTURABLE(TypeName)                         \
  std::optional<DenseMap<Attribute, Type>> TypeName::getSubelementIndexMap()   \
      const {                                                                  \
    return ::obelisk::sim::getSubelementIndexMap(*this, false);                \
  }                                                                            \
  Type TypeName::getTypeAtIndex(Attribute index) const {                       \
    return getTypeAtSubelementIndex(*this, index);                             \
  }

OBELISK_DEFINE_ARRAY_DESTRUCTURABLE(PackedArrayType)
OBELISK_DEFINE_ARRAY_DESTRUCTURABLE(UnpackedArrayType)
OBELISK_DEFINE_RECORD_DESTRUCTURABLE(PackedStructType)
OBELISK_DEFINE_RECORD_DESTRUCTURABLE(UnpackedStructType)
OBELISK_DEFINE_RECORD_DESTRUCTURABLE(PackedUnionType)
OBELISK_DEFINE_RECORD_DESTRUCTURABLE(UnpackedUnionType)

#undef OBELISK_DEFINE_ARRAY_DESTRUCTURABLE
#undef OBELISK_DEFINE_RECORD_DESTRUCTURABLE

LogicalResult
LogicType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  unsigned width) {
  if (width == 0)
    return emitError() << "logic width must be greater than zero";
  return success();
}

LogicalResult
RefType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                Type elementType) {
  return verifyElementType(emitError, elementType);
}

LogicalResult
NetType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                Type elementType) {
  if (elementType.isF64())
    return emitError() << "real-valued nets are not supported";
  return verifyElementType(emitError, elementType);
}

LogicalResult
DriverType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                   Type elementType) {
  if (elementType.isF64())
    return emitError() << "real-valued drivers are not supported";
  return verifyElementType(emitError, elementType);
}

LogicalResult
ClassHandleType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        SymbolRefAttr className) {
  if (!className || className.getRootReference().empty())
    return emitError() << "class handle requires a class symbol";
  return success();
}

LogicalResult
ManagedRefType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                       Type elementType, SymbolRefAttr ownerClass) {
  if (!ownerClass || ownerClass.getRootReference().empty())
    return emitError() << "managed reference requires an owner class";
  if (!isNormalizedValueType(elementType))
    return emitError()
           << "managed reference element must be a normalized value";
  return success();
}

LogicalResult
ArgumentRefType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        Type elementType) {
  return verifyElementType(emitError, elementType);
}

LogicalResult
ObserverType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                     Type resultType) {
  if (!isa<IntegerType, LogicType, FloatType>(resultType))
    return emitError() << "observer result must be a scalar value";
  if (auto integer = dyn_cast<IntegerType>(resultType);
      integer && (!integer.isSignless() || integer.getWidth() == 0))
    return emitError()
           << "observer integer result must be nonempty and signless";
  return success();
}

static LogicalResult verifyNonnegative(Operation *op, IntegerAttr attr,
                                       StringRef name) {
  if (attr.getValue().isNegative())
    return op->emitOpError() << name << " must be nonnegative";
  return success();
}

static LogicalResult verifyPositive(Operation *op, IntegerAttr attr,
                                    StringRef name) {
  if (!attr.getValue().isStrictlyPositive())
    return op->emitOpError() << name << " must be positive";
  return success();
}

static std::optional<CaptureKind> getCaptureKind(DictionaryAttr attrs) {
  if (!attrs)
    return std::nullopt;
  auto value =
      dyn_cast_or_null<CaptureKindAttr>(attrs.get(metadata::captureKind));
  if (!value)
    return std::nullopt;
  return value.getValue();
}

LogicalResult SimScopeDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "scope ID")))
    return failure();
  if (getParentAttr() &&
      failed(verifyNonnegative(*this, getParentAttr(), "parent scope ID")))
    return failure();
  if (getParentAttr() && getParentAttr() == getIdAttr())
    return emitOpError("scope cannot be its own parent");
  return success();
}

LogicalResult SimCodeUnitDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "code-unit ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  if (getId() == 0)
    return emitOpError("code-unit ID must be nonzero");
  if (getHierarchicalName().empty())
    return emitOpError("requires a nonempty hierarchical name");
  return success();
}

LogicalResult SimStorageDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "storage ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  return verifyElementType([&] { return emitOpError(); }, getType());
}

LogicalResult SimNetDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "net ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  if (getType().isF64())
    return emitOpError("real-valued nets are not supported");
  return verifyElementType([&] { return emitOpError(); }, getType());
}

LogicalResult SimNetConnectDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "connection ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getLhsNetIdAttr(), "left net ID")) ||
      failed(verifyNonnegative(*this, getLhsOffsetAttr(), "left offset")) ||
      failed(verifyNonnegative(*this, getRhsNetIdAttr(), "right net ID")) ||
      failed(verifyNonnegative(*this, getRhsOffsetAttr(), "right offset")) ||
      failed(verifyNonnegative(*this, getWidthAttr(), "width")))
    return failure();
  if (getWidth() == 0)
    return emitOpError("width must be positive");
  return success();
}

LogicalResult SimDriverDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "driver ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getNetIdAttr(), "net ID")))
    return failure();
  if (static_cast<bool>(getDrivenLowAttr()) !=
      static_cast<bool>(getDrivenWidthAttr()))
    return emitOpError(
        "driven low and width must either both be present or both be absent");
  if (getDrivenLowAttr()) {
    if (failed(verifyNonnegative(*this, getDrivenLowAttr(), "driven low")) ||
        failed(verifyNonnegative(*this, getDrivenWidthAttr(), "driven width")))
      return failure();
    uint64_t low = getDrivenLowAttr().getValue().getZExtValue();
    uint64_t width = getDrivenWidthAttr().getValue().getZExtValue();
    std::optional<unsigned> typeWidth = getPackedWidth(getType());
    if (width == 0)
      return emitOpError("driven width must be positive");
    if (!typeWidth || low > *typeWidth || width > *typeWidth - low)
      return emitOpError("driven range exceeds the driver type");
  }
  if (getType().isF64())
    return emitOpError("real-valued drivers are not supported");
  return verifyElementType([&] { return emitOpError(); }, getType());
}

static SimClassDeclOp lookupClass(Operation *operation, SymbolRefAttr symbol) {
  return symbol ? SymbolTable::lookupNearestSymbolFrom<SimClassDeclOp>(
                      operation, symbol)
                : SimClassDeclOp{};
}

static bool classDerivesFrom(SimClassDeclOp derived, SimClassDeclOp base) {
  llvm::SmallPtrSet<Operation *, 8> visited;
  for (SimClassDeclOp current = derived;
       current && visited.insert(current).second;) {
    if (current == base)
      return true;
    current = current.getBaseAttr()
                  ? lookupClass(current, current.getBaseAttr())
                  : SimClassDeclOp{};
  }
  return false;
}

LogicalResult SimClassDeclOp::verify() {
  if (failed(verifyPositive(*this, getIdAttr(), "class ID")))
    return failure();
  if (getIsInterface() && !getIsAbstract())
    return emitOpError("interface classes must be abstract");
  if (getIsInterface() && getBaseAttr())
    return emitOpError("interface classes cannot have a base class");
  if (getBaseAttr() && getBase() == getSymName())
    return emitOpError("class cannot extend itself");
  if (getWeakReferentAttr() && !lookupClass(*this, getWeakReferentAttr()))
    return emitOpError("weak wrapper references an unknown referent class");
  if (ArrayAttr interfaces = getInterfacesAttr()) {
    SmallVector<StringRef> unique;
    for (Attribute attribute : interfaces) {
      auto interface = dyn_cast<FlatSymbolRefAttr>(attribute);
      if (!interface)
        return emitOpError(
            "implemented interface list must contain flat symbol references");
      if (llvm::is_contained(unique, interface.getValue()))
        return emitOpError("implemented interface list contains a duplicate");
      unique.push_back(interface.getValue());
    }
  }
  return success();
}

LogicalResult SimClassFieldDeclOp::verify() {
  if (!lookupClass(*this, getOwnerAttr()))
    return emitOpError("references an unknown owner class");
  if (getOffsetAttr() &&
      failed(verifyNonnegative(*this, getOffsetAttr(), "field offset")))
    return failure();
  if (getIsStatic() && getOffsetAttr())
    return emitOpError("static properties cannot have an instance offset");
  if (getIsWeak() && !isa<ClassHandleType>(getType()))
    return emitOpError("weak properties must have class-handle type");
  if (!isNormalizedValueType(getType()))
    return emitOpError("property must have a normalized executable type");
  return success();
}

LogicalResult SimClassMethodDeclOp::verify() {
  SimClassDeclOp owner = lookupClass(*this, getOwnerAttr());
  if (!owner)
    return emitOpError("references an unknown owner class");
  auto functionType = dyn_cast<FunctionType>(getFunctionType());
  if (!functionType)
    return emitOpError("method signature must be a function type");
  if (functionType.getNumInputs() == 0 ||
      !isa<ContextType>(functionType.getInput(0)))
    return emitOpError("method signature must begin with context");
  if (!getIsStatic()) {
    if (functionType.getNumInputs() < 2)
      return emitOpError("instance method signature requires explicit this");
    auto thisType = dyn_cast<ClassHandleType>(functionType.getInput(1));
    if (!thisType ||
        thisType.getClassName().getRootReference() != owner.getSymName())
      return emitOpError("instance method this type must name its owner class");
  }
  if (getIsTask() && functionType.getNumResults() != 0)
    return emitOpError("task method cannot have value results");
  if (getIsPure() && !getIsVirtual())
    return emitOpError("pure methods must be virtual");
  if (getIsStatic() && getIsVirtual())
    return emitOpError("static methods cannot be virtual");
  if (getIsFinal() && !getIsVirtual())
    return emitOpError("final methods must be virtual");
  if (getIsVirtual() != static_cast<bool>(getSlotAttr()))
    return emitOpError(
        "virtual methods require a slot and nonvirtual methods forbid one");
  if (getIsVirtual() != static_cast<bool>(getSignatureIdAttr()) ||
      (getSignatureIdAttr() && getSignatureId() == 0))
    return emitOpError(
        "virtual methods require a nonzero signature ID and nonvirtual "
        "methods forbid one");
  if (getIsPure() == static_cast<bool>(getImplementationAttr()))
    return emitOpError(
        "pure methods forbid an implementation and concrete methods require "
        "one");
  return success();
}

LogicalResult SimClassAllocOp::verify() {
  auto type = getResult().getType();
  SimClassDeclOp descriptor = lookupClass(*this, type.getClassName());
  if (!descriptor)
    return emitOpError("result type references an unknown class");
  if (descriptor.getIsAbstract() || descriptor.getIsInterface())
    return emitOpError("cannot allocate an abstract or interface class");
  return success();
}

LogicalResult SimClassCopyOp::verify() {
  if (getSource().getType() != getResult().getType())
    return emitOpError(
        "source and result must have the same static class type");
  return success();
}

LogicalResult SimWeakCreateOp::verify() {
  SimClassDeclOp wrapper =
      lookupClass(*this, getResult().getType().getClassName());
  if (!wrapper || !wrapper.getWeakReferentAttr())
    return emitOpError("result must be a declared weak_reference wrapper");
  if (wrapper.getWeakReferentAttr() != getReferent().getType().getClassName())
    return emitOpError(
        "referent type does not match the weak_reference specialization");
  return success();
}

LogicalResult SimWeakGetOp::verify() {
  SimClassDeclOp wrapper =
      lookupClass(*this, getWeak().getType().getClassName());
  if (!wrapper || !wrapper.getWeakReferentAttr())
    return emitOpError("operand must be a declared weak_reference wrapper");
  if (wrapper.getWeakReferentAttr() != getResult().getType().getClassName())
    return emitOpError(
        "result type does not match the weak_reference specialization");
  return success();
}

LogicalResult SimWeakClearOp::verify() {
  SimClassDeclOp wrapper =
      lookupClass(*this, getWeak().getType().getClassName());
  if (!wrapper || !wrapper.getWeakReferentAttr())
    return emitOpError("operand must be a declared weak_reference wrapper");
  return success();
}

LogicalResult SimClassIsInstanceOp::verify() {
  if (!lookupClass(*this, getTargetAttr()))
    return emitOpError("references an unknown target class");
  return success();
}

LogicalResult SimClassCastOp::verify() {
  auto source = getObject().getType();
  auto target = getResult().getType();
  SimClassDeclOp sourceClass = lookupClass(*this, source.getClassName());
  SimClassDeclOp targetClass = lookupClass(*this, target.getClassName());
  if (!sourceClass || !targetClass)
    return emitOpError("cast references an unknown class");
  bool targetInterface = targetClass.getIsInterface();
  bool sourceInterface = sourceClass.getIsInterface();
  if (!targetInterface && !sourceInterface &&
      !classDerivesFrom(sourceClass, targetClass) &&
      !classDerivesFrom(targetClass, sourceClass))
    return emitOpError("cast classes are unrelated");
  return success();
}

LogicalResult SimClassFieldRefOp::verify() {
  auto field = SymbolTable::lookupNearestSymbolFrom<SimClassFieldDeclOp>(
      *this, getFieldAttr());
  if (!field)
    return emitOpError("references an unknown class property");
  if (field.getIsStatic())
    return emitOpError(
        "cannot form an instance reference to a static property");
  auto objectType = getObject().getType();
  SimClassDeclOp objectClass = lookupClass(*this, objectType.getClassName());
  SimClassDeclOp fieldOwner = lookupClass(*this, field.getOwnerAttr());
  if (!objectClass || !fieldOwner || !classDerivesFrom(objectClass, fieldOwner))
    return emitOpError("property is not a member of the receiver class");
  auto resultType = getResult().getType();
  if (resultType.getElementType() != field.getType() ||
      resultType.getOwnerClass() != objectType.getClassName())
    return emitOpError("managed reference type does not match the property");
  return success();
}

LogicalResult SimManagedLoadOp::verify() {
  if (getReference().getType().getElementType() != getResult().getType())
    return emitOpError("result type must match the referenced element");
  return success();
}

LogicalResult SimManagedStoreOp::verify() {
  if (getReference().getType().getElementType() != getValue().getType())
    return emitOpError("value type must match the referenced element");
  return success();
}

LogicalResult SimManagedNBAEnqueueOp::verify() {
  if (getDestination().getType().getElementType() != getValue().getType())
    return emitOpError("value type must match the referenced element");
  return success();
}

LogicalResult SimReferencePathNBAEnqueueOp::verify() {
  if (getDestination().getType().getElementType() != getValue().getType())
    return emitOpError("value type must match the referenced element");
  return success();
}

LogicalResult SimArgumentRefFromRefOp::verify() {
  if (getInput().getType().getElementType() !=
      getResult().getType().getElementType())
    return emitOpError("input and result element types must match");
  return success();
}

LogicalResult SimArgumentRefFromManagedOp::verify() {
  if (getInput().getType().getElementType() !=
      getResult().getType().getElementType())
    return emitOpError("input and result element types must match");
  return success();
}

LogicalResult SimReferencePathIndexOp::verify() {
  Type containerType = getContainer().getType();
  Type elementType;
  if (auto array = dyn_cast<DynamicArrayType>(containerType))
    elementType = array.getElementType();
  else if (auto queue = dyn_cast<QueueType>(containerType))
    elementType = queue.getElementType();
  else
    return emitOpError("container must be a dynamic array or queue");
  if (elementType != getResult().getType().getElementType())
    return emitOpError("result element must match the container element");
  if (getOwnerReference().getType().getElementType() != containerType)
    return emitOpError("owner reference must refer to the container type");
  return success();
}

LogicalResult SimReferencePathAssocOp::verify() {
  AssocArrayType array = getArray().getType();
  if (failed(verifyAssocKey(getOperation(), array, getKey().getType())))
    return failure();
  if (array.getElementType() != getResult().getType().getElementType())
    return emitOpError("result element must match the associative element");
  if (getOwnerReference().getType().getElementType() != array)
    return emitOpError("owner reference must refer to the associative array");
  return success();
}

LogicalResult SimArgumentRefFromPathOp::verify() {
  if (getInput().getType().getElementType() !=
      getResult().getType().getElementType())
    return emitOpError("input and result element types must match");
  return success();
}

LogicalResult SimArgumentRefLoadOp::verify() {
  if (getReference().getType().getElementType() != getResult().getType())
    return emitOpError("result type must match the referenced element");
  return success();
}

LogicalResult SimArgumentRefStoreOp::verify() {
  if (getReference().getType().getElementType() != getValue().getType())
    return emitOpError("value type must match the referenced element");
  return success();
}

LogicalResult SimClassRootBindOp::verify() {
  Type type = getObject().getType();
  SmallVector<uint64_t, 2> offsets;
  if (isa<ManagedRefType, ArgumentRefType>(type))
    offsets.push_back(0);
  else if (!getManagedHandleOffsets(type, offsets))
    return emitOpError("rooted value has no fixed managed layout");
  uint64_t selected = getBitOffset();
  if (!llvm::is_contained(offsets, selected))
    return emitOpError("bit offset does not select a managed handle");
  return success();
}

LogicalResult SimClassDirectCallOp::verify() {
  auto callee =
      SymbolTable::lookupNearestSymbolFrom<SimFuncOp>(*this, getCalleeAttr());
  if (!callee)
    return emitOpError("references an unknown method implementation");
  FunctionType type = callee.getFunctionType();
  SmallVector<Type> inputs;
  inputs.push_back(getReceiver().getType());
  llvm::append_range(inputs, getArguments().getTypes());
  // Context is supplied by the containing executable function.
  if (type.getNumInputs() != inputs.size() + 1 ||
      !isa<ContextType>(type.getInput(0)) ||
      !llvm::equal(type.getInputs().drop_front(), inputs) ||
      !llvm::equal(type.getResults(), getResultTypes()))
    return emitOpError("operands or results do not match the method");
  return success();
}

LogicalResult SimClassVirtualCallOp::verify() {
  auto method = SymbolTable::lookupNearestSymbolFrom<SimClassMethodDeclOp>(
      *this, getMethodAttr());
  if (!method || !method.getIsVirtual() || !method.getSlot() ||
      *method.getSlot() != getSlot())
    return emitOpError("references an unknown or incompatible virtual slot");
  if (getSignatureId() == 0 || !method.getSignatureIdAttr() ||
      *method.getSignatureId() != getSignatureId())
    return emitOpError("signature ID does not match the virtual method");
  auto type = cast<FunctionType>(method.getFunctionType());
  SmallVector<Type> inputs;
  inputs.push_back(getReceiver().getType());
  llvm::append_range(inputs, getArguments().getTypes());
  if (type.getNumInputs() != inputs.size() + 1 ||
      !llvm::equal(type.getInputs().drop_front(), inputs) ||
      !llvm::equal(type.getResults(), getResultTypes()))
    return emitOpError()
           << "operands or results do not match the method slot (expected "
           << type << ", got inputs " << TypeRange(inputs) << " and results "
           << getResultTypes() << ")";
  return success();
}

LogicalResult SimDesignOp::verifyRegions() {
  if (auto precision = getTimePrecisionFsAttr();
      precision &&
      (precision.getValue().isNegative() || precision.getValue().isZero()))
    return emitOpError("time precision must be a positive femtosecond value");
  llvm::DenseSet<uint64_t> scopeIds, codeUnitIds, storageIds, netIds, driverIds,
      connectionIds, classIds;
  llvm::DenseMap<uint64_t, SimCodeUnitDeclOp> codeUnits;
  llvm::DenseMap<uint64_t, Type> storageTypes, netTypes, driverTypes;
  llvm::DenseMap<uint64_t, NetResolutionKind> netResolutions;
  llvm::StringMap<SimClassDeclOp> classes;
  SmallVector<SimFuncOp> functions;
  bool sawRoot = false;
  for (Operation &op : getBody().front()) {
    auto addId = [&](IntegerAttr id, llvm::DenseSet<uint64_t> &ids,
                     StringRef kind) -> LogicalResult {
      uint64_t value = id.getValue().getZExtValue();
      if (!ids.insert(value).second)
        return op.emitOpError() << "duplicate " << kind << " ID " << value;
      return success();
    };
    if (auto scope = dyn_cast<SimScopeDeclOp>(op)) {
      if (failed(addId(scope.getIdAttr(), scopeIds, "scope")))
        return failure();
      if (!scope.getParentAttr()) {
        if (sawRoot)
          return scope.emitOpError(
              "design must contain exactly one root scope");
        sawRoot = true;
      }
    } else if (auto codeUnit = dyn_cast<SimCodeUnitDeclOp>(op)) {
      if (failed(addId(codeUnit.getIdAttr(), codeUnitIds, "code-unit")))
        return failure();
      codeUnits[codeUnit.getId()] = codeUnit;
    } else if (auto storage = dyn_cast<SimStorageDeclOp>(op)) {
      if (failed(addId(storage.getIdAttr(), storageIds, "storage")))
        return failure();
      storageTypes[storage.getId()] = storage.getType();
    } else if (auto net = dyn_cast<SimNetDeclOp>(op)) {
      if (failed(addId(net.getIdAttr(), netIds, "net")))
        return failure();
      netTypes[net.getId()] = net.getType();
      netResolutions[net.getId()] = net.getResolutionKind();
    } else if (auto driver = dyn_cast<SimDriverDeclOp>(op)) {
      if (failed(addId(driver.getIdAttr(), driverIds, "driver")))
        return failure();
      driverTypes[driver.getId()] = driver.getType();
    } else if (auto connection = dyn_cast<SimNetConnectDeclOp>(op)) {
      if (failed(
              addId(connection.getIdAttr(), connectionIds, "net connection")))
        return failure();
    } else if (auto classDecl = dyn_cast<SimClassDeclOp>(op)) {
      if (failed(addId(classDecl.getIdAttr(), classIds, "class")))
        return failure();
      classes[classDecl.getSymName()] = classDecl;
    } else if (auto function = dyn_cast<SimFuncOp>(op)) {
      functions.push_back(function);
    }
  }
  struct ElementShape {
    Type type;
    uint32_t kind;
    uint32_t flags;
    uint64_t valueSize;
    uint64_t alignment;
    uint64_t bitWidth;
    SmallVector<int64_t, 2> traceOffsets;
    SmallVector<int32_t, 2> traceKinds;
  };
  llvm::DenseMap<uint64_t, ElementShape> elementShapes;
  auto recordElementShape =
      [&](Operation *operation, uint64_t typeId, Type type, uint32_t kind,
          uint32_t flags, uint64_t valueSize, uint64_t alignment,
          uint64_t bitWidth, ArrayRef<int64_t> traceOffsets,
          ArrayRef<int32_t> traceKinds) -> WalkResult {
        ElementShape shape{type, kind, flags, valueSize, alignment, bitWidth,
                           SmallVector<int64_t, 2>(traceOffsets),
                           SmallVector<int32_t, 2>(traceKinds)};
        auto [found, inserted] = elementShapes.try_emplace(typeId, shape);
        if (!inserted && (found->second.type != shape.type ||
                          found->second.kind != shape.kind ||
                          found->second.flags != shape.flags ||
                          found->second.valueSize != shape.valueSize ||
                          found->second.alignment != shape.alignment ||
                          found->second.bitWidth != shape.bitWidth ||
                          found->second.traceOffsets != shape.traceOffsets ||
                          found->second.traceKinds != shape.traceKinds)) {
          operation->emitOpError()
              << "element type ID " << typeId
              << " conflicts with another container descriptor in the design";
          return WalkResult::interrupt();
        }
        return WalkResult::advance();
      };
  WalkResult descriptors = walk([&](Operation *operation) {
    if (auto create = dyn_cast<SimContainerCreateOp>(operation))
      return recordElementShape(
          operation, create.getTypeId(),
          getContainerElement(create.getResult().getType()),
          static_cast<uint32_t>(create.getElementKind()),
          static_cast<uint32_t>(create.getElementFlags()),
          create.getValueSize(), create.getAlignment(), create.getBitWidth(),
          create.getTraceOffsets(), create.getTraceKinds());
    if (auto create = dyn_cast<SimAssocCreateOp>(operation))
      return recordElementShape(
          operation, create.getTypeId(),
          create.getResult().getType().getElementType(),
          static_cast<uint32_t>(create.getElementKind()),
          static_cast<uint32_t>(create.getElementFlags()),
          create.getValueSize(), create.getAlignment(), create.getBitWidth(),
          create.getTraceOffsets(), create.getTraceKinds());
    return WalkResult::advance();
  });
  if (descriptors.wasInterrupted())
    return failure();
  if (!sawRoot)
    return emitOpError("design must contain a root scope descriptor");
  auto verifyDense = [&](const llvm::DenseSet<uint64_t> &ids,
                         StringRef kind) -> LogicalResult {
    for (uint64_t id = 0; id < ids.size(); ++id)
      if (!ids.count(id))
        return emitOpError()
               << kind << " IDs must be dense from zero; missing " << id;
    return success();
  };
  if (failed(verifyDense(scopeIds, "scope")) ||
      failed(verifyDense(storageIds, "storage")) ||
      failed(verifyDense(netIds, "net")) ||
      failed(verifyDense(driverIds, "driver")) ||
      failed(verifyDense(connectionIds, "net connection")))
    return failure();
  for (uint64_t id = 1; id <= classIds.size(); ++id)
    if (!classIds.count(id))
      return emitOpError() << "class IDs must be dense from one; missing "
                           << id;

  llvm::StringMap<SimFuncOp> functionsByName;
  for (SimFuncOp function : functions)
    functionsByName[function.getSymName()] = function;
  llvm::StringMap<llvm::DenseSet<uint64_t>> fieldOrdinals, methodSlots;
  for (Operation &op : getBody().front()) {
    if (auto classDecl = dyn_cast<SimClassDeclOp>(op)) {
      if (auto base = classDecl.getBase()) {
        auto found = classes.find(*base);
        if (found == classes.end())
          return classDecl.emitOpError("references an unknown base class");
        if (found->second.getIsInterface())
          return classDecl.emitOpError("cannot extend an interface class");
        if (found->second.getIsFinal())
          return classDecl.emitOpError("cannot extend a final class");
      }
      if (ArrayAttr interfaces = classDecl.getInterfacesAttr()) {
        for (Attribute attribute : interfaces) {
          auto reference = cast<FlatSymbolRefAttr>(attribute);
          auto found = classes.find(reference.getValue());
          if (found == classes.end() || !found->second.getIsInterface())
            return classDecl.emitOpError(
                "implements list references a non-interface class");
        }
      }
      llvm::SmallPtrSet<Operation *, 8> path;
      for (SimClassDeclOp current = classDecl; current;
           current = current.getBaseAttr()
                         ? lookupClass(current, current.getBaseAttr())
                         : SimClassDeclOp{})
        if (!path.insert(current).second)
          return classDecl.emitOpError("class inheritance contains a cycle");
    } else if (auto field = dyn_cast<SimClassFieldDeclOp>(op)) {
      if (!fieldOrdinals[field.getOwner()].insert(field.getOrdinal()).second)
        return field.emitOpError(
            "owner class contains a duplicate direct-property ordinal");
    } else if (auto method = dyn_cast<SimClassMethodDeclOp>(op)) {
      if (method.getSlot() &&
          !methodSlots[method.getOwner()].insert(*method.getSlot()).second)
        return method.emitOpError(
            "owner class contains a duplicate virtual-method slot");
      if (auto implementation = method.getImplementation()) {
        auto found = functionsByName.find(*implementation);
        if (found == functionsByName.end() ||
            found->second.getFunctionType() != method.getFunctionType())
          return method.emitOpError(
              "implementation is missing or has an incompatible signature");
      }
    }
  }
  for (Operation &op : getBody().front()) {
    if (auto scope = dyn_cast<SimScopeDeclOp>(op)) {
      if (scope.getParentAttr() && !scopeIds.count(*scope.getParent()))
        return scope.emitOpError("references an unknown parent scope ID");
      if (scope.getParentAttr() && *scope.getParent() >= scope.getId())
        return scope.emitOpError(
            "parent scope ID must precede the child scope ID");
    } else if (auto codeUnit = dyn_cast<SimCodeUnitDeclOp>(op)) {
      if (!scopeIds.count(codeUnit.getScopeId()))
        return codeUnit.emitOpError("references an unknown scope ID");
    } else if (auto storage = dyn_cast<SimStorageDeclOp>(op)) {
      if (!scopeIds.count(storage.getScopeId()))
        return storage.emitOpError("references an unknown scope ID");
    } else if (auto net = dyn_cast<SimNetDeclOp>(op)) {
      if (!scopeIds.count(net.getScopeId()))
        return net.emitOpError("references an unknown scope ID");
    } else if (auto driver = dyn_cast<SimDriverDeclOp>(op)) {
      auto netType = netTypes.find(driver.getNetId());
      if (!scopeIds.count(driver.getScopeId()) || netType == netTypes.end() ||
          netType->second != driver.getType())
        return driver.emitOpError(
            "references an incompatible scope or net descriptor");
    } else if (auto connection = dyn_cast<SimNetConnectDeclOp>(op)) {
      auto lhs = netTypes.find(connection.getLhsNetId());
      auto rhs = netTypes.find(connection.getRhsNetId());
      if (!scopeIds.count(connection.getScopeId()) || lhs == netTypes.end() ||
          rhs == netTypes.end())
        return connection.emitOpError(
            "references an unknown scope or net descriptor");
      std::optional<unsigned> lhsWidth = getPackedWidth(lhs->second);
      std::optional<unsigned> rhsWidth = getPackedWidth(rhs->second);
      uint64_t width = connection.getWidth();
      uint64_t lhsOffset = connection.getLhsOffset();
      uint64_t rhsOffset = connection.getRhsOffset();
      bool lhsValid =
          lhsWidth && lhsOffset <= *lhsWidth && width <= *lhsWidth - lhsOffset;
      bool rhsValid = rhsWidth && (connection.getRhsReversed()
                                       ? width <= rhsOffset + 1
                                       : rhsOffset <= *rhsWidth &&
                                             width <= *rhsWidth - rhsOffset);
      if (!lhsValid || !rhsValid)
        return connection.emitOpError("contains an out-of-range bit run");
      if (containsFourStateLeaf(lhs->second) !=
          containsFourStateLeaf(rhs->second))
        return connection.emitOpError(
            "connects incompatible two-state and four-state nets");
      bool lhsUWire = netResolutions.lookup(connection.getLhsNetId()) ==
                      NetResolutionKind::UWire;
      bool rhsUWire = netResolutions.lookup(connection.getRhsNetId()) ==
                      NetResolutionKind::UWire;
      if (lhsUWire != rhsUWire)
        return connection.emitOpError(
            "mixes uwire with resolved wire/tri topology");
    }
  }

  // Descriptor tables live on this operation, so descriptor references are
  // resolved here rather than in a function-local verifier: an operation pass
  // on one function may run concurrently with passes on its siblings, and a
  // nested verifier must not reach into shared parent state. Callee symbols
  // instead use SymbolUserOpInterface, which the framework verifies against
  // this symbol table with a cached SymbolTableCollection.
  llvm::DenseMap<uint64_t, SimFuncOp> executableCodeUnits;
  for (SimFuncOp function : functions) {
    if (!function.isExternal() &&
        function.getEntryKind() != EntryKind::RootInitializer &&
        !function.getCodeUnitIdAttr())
      return function.emitOpError(
          "defined non-root function requires a code-unit ID");
    if (auto id = function.getCodeUnitId()) {
      auto declaration = codeUnits.find(*id);
      if (declaration == codeUnits.end())
        return function.emitOpError("references an unknown code-unit ID");
      if (declaration->second.getCodeUnitKind() != function.getEntryKind())
        return function.emitOpError(
            "entry kind does not match its code-unit declaration");
      if (!function.isExternal()) {
        auto [first, inserted] = executableCodeUnits.try_emplace(*id, function);
        if (!inserted) {
          function.emitOpError()
              << "code-unit ID " << *id
              << " is referenced by multiple executable functions";
          first->second.emitRemark("first executable function is here");
          return failure();
        }
      }
    }
    WalkResult result = function.walk([&](Operation *op) {
      auto verifyDescriptor = [&](uint64_t id, Type elementType,
                                  const llvm::DenseMap<uint64_t, Type> &table,
                                  StringRef kind) {
        auto descriptor = table.find(id);
        if (descriptor != table.end() && descriptor->second == elementType)
          return WalkResult::advance();
        op->emitOpError() << "references an unknown or incompatible " << kind
                          << " descriptor";
        return WalkResult::interrupt();
      };
      if (auto lookup = dyn_cast<SimContextStorageOp>(op))
        return verifyDescriptor(lookup.getId(),
                                lookup.getResult().getType().getElementType(),
                                storageTypes, "storage");
      if (auto lookup = dyn_cast<SimContextNetOp>(op))
        return verifyDescriptor(lookup.getId(),
                                lookup.getResult().getType().getElementType(),
                                netTypes, "net");
      if (auto lookup = dyn_cast<SimContextDriverOp>(op))
        return verifyDescriptor(lookup.getId(),
                                lookup.getResult().getType().getElementType(),
                                driverTypes, "driver");
      return WalkResult::advance();
    });
    if (result.wasInterrupted())
      return failure();

    for (unsigned index = 1; index < function.getNumArguments(); ++index) {
      std::optional<CaptureKind> kind =
          getCaptureKind(function.getArgAttrDict(index));
      if (!kind)
        return failure(); // Already diagnosed by the function verifier.
      auto descriptor =
          function.getArgAttrOfType<IntegerAttr>(index, metadata::descriptorId);
      std::optional<uint64_t> descriptorId;
      if (descriptor && !descriptor.getValue().isNegative() &&
          descriptor.getValue().getBitWidth() <= 64)
        descriptorId = descriptor.getValue().getZExtValue();
      Type argument = function.getArgumentTypes()[index];
      Type expected;
      switch (*kind) {
      case CaptureKind::Storage:
        if (descriptorId && storageTypes.count(*descriptorId)) {
          Type storageType = storageTypes.lookup(*descriptorId);
          auto rootType = function.getArgAttrOfType<TypeAttr>(
              index, metadata::descriptorRootType);
          auto low = function.getArgAttrOfType<IntegerAttr>(
              index, metadata::descriptorLow);
          if (!rootType) {
            if (low ||
                function.getArgAttr(index, metadata::descriptorIndices) ||
                function.getArgAttr(index, metadata::descriptorAggregateType) ||
                function.getArgAttr(index, metadata::descriptorPackedLow))
              break;
            expected = RefType::get(getContext(), storageType);
            break;
          }
          auto reference = dyn_cast<RefType>(argument);
          std::optional<uint64_t> rootSpan = getProvenanceSpan(storageType);
          std::optional<uint64_t> viewSpan =
              reference ? getProvenanceSpan(reference.getElementType())
                        : std::nullopt;
          if (rootType.getValue() != storageType || !low ||
              low.getValue().isNegative() ||
              low.getValue().getActiveBits() > 64 || !rootSpan || !viewSpan)
            break;

          Type selected = storageType;
          uint64_t computedLow = 0;
          auto indices = function.getArgAttrOfType<DenseI64ArrayAttr>(
              index, metadata::descriptorIndices);
          bool validView = true;
          if (indices) {
            for (int64_t rawIndex : indices.asArrayRef()) {
              if (rawIndex < 0 || static_cast<uint64_t>(rawIndex) >
                                      std::numeric_limits<unsigned>::max()) {
                validView = false;
                break;
              }
              auto subelement = getAggregateProvenanceSubelement(
                  selected, static_cast<unsigned>(rawIndex));
              if (!subelement || subelement->first > UINT64_MAX - computedLow) {
                validView = false;
                break;
              }
              computedLow += subelement->first;
              selected = getAggregateElementType(
                  selected, static_cast<unsigned>(rawIndex));
            }
          }
          auto aggregateType = function.getArgAttrOfType<TypeAttr>(
              index, metadata::descriptorAggregateType);
          if ((indices && !aggregateType) ||
              (aggregateType && aggregateType.getValue() != selected))
            validView = false;

          auto packedLow = function.getArgAttrOfType<IntegerAttr>(
              index, metadata::descriptorPackedLow);
          Type viewElement = reference ? reference.getElementType() : Type{};
          if (validView && selected != viewElement) {
            std::optional<unsigned> selectedWidth = getPackedWidth(selected);
            std::optional<unsigned> resultWidth = getPackedWidth(viewElement);
            Type selectedScalar = getPackedScalarType(selected);
            Type resultScalar = getPackedScalarType(viewElement);
            if (!packedLow || packedLow.getValue().isNegative() ||
                packedLow.getValue().getActiveBits() > 64 || !selectedWidth ||
                !resultWidth || !selectedScalar || !resultScalar ||
                isa<LogicType>(selectedScalar) !=
                    isa<LogicType>(resultScalar)) {
              validView = false;
            } else {
              uint64_t packed = packedLow.getValue().getZExtValue();
              if (packed > *selectedWidth ||
                  *resultWidth > *selectedWidth - packed ||
                  packed > UINT64_MAX - computedLow)
                validView = false;
              else
                computedLow += packed;
            }
          } else if (packedLow && (packedLow.getValue().isNegative() ||
                                   packedLow.getValue().getActiveBits() > 64 ||
                                   packedLow.getValue().getZExtValue() != 0)) {
            validView = false;
          }

          uint64_t encodedLow = low.getValue().getZExtValue();
          if (validView && encodedLow == computedLow &&
              encodedLow <= *rootSpan && *viewSpan <= *rootSpan - encodedLow)
            expected = argument;
        }
        break;
      case CaptureKind::Net:
        if (descriptorId && netTypes.count(*descriptorId))
          expected = NetType::get(getContext(), netTypes.lookup(*descriptorId));
        break;
      case CaptureKind::Driver:
        if (descriptorId && driverTypes.count(*descriptorId))
          expected =
              DriverType::get(getContext(), driverTypes.lookup(*descriptorId));
        break;
      case CaptureKind::Event:
        if (isa<EventType>(argument))
          expected = argument;
        break;
      case CaptureKind::Context:
      case CaptureKind::Formal:
      case CaptureKind::Value:
        continue;
      }
      if (!expected || expected != argument)
        return function.emitOpError()
               << "argument #" << index
               << " has an incompatible capture descriptor";
    }
  }
  return success();
}

void SimFuncOp::build(OpBuilder &builder, OperationState &state, StringRef name,
                      FunctionType type, EntryKind entryKind,
                      ArrayRef<NamedAttribute> attrs,
                      ArrayRef<DictionaryAttr> argAttrs) {
  state.addAttribute(SymbolTable::getSymbolAttrName(),
                     builder.getStringAttr(name));
  state.addAttribute(getFunctionTypeAttrName(state.name), TypeAttr::get(type));
  state.addAttribute(getEntryKindAttrName(state.name),
                     EntryKindAttr::get(builder.getContext(), entryKind));
  state.attributes.append(attrs.begin(), attrs.end());
  if (!argAttrs.empty())
    state.addAttribute(getArgAttrsAttrName(state.name),
                       builder.getArrayAttr(SmallVector<Attribute>(
                           argAttrs.begin(), argAttrs.end())));
  Region *body = state.addRegion();
  // Construct the entry block without changing the caller's insertion point.
  // Function builders are routinely invoked while populating their enclosing
  // symbol table; using OpBuilder::createBlock here would redirect subsequent
  // sibling creation into this function body.
  body->push_back(new Block());
  Block *entry = &body->front();
  SmallVector<Location> locations(type.getNumInputs(), state.location);
  entry->addArguments(type.getInputs(), locations);
}

ParseResult SimFuncOp::parse(OpAsmParser &parser, OperationState &result) {
  auto buildType =
      [](Builder &builder, ArrayRef<Type> inputs, ArrayRef<Type> results,
         function_interface_impl::VariadicFlag, std::string &) -> Type {
    return builder.getFunctionType(inputs, results);
  };
  return function_interface_impl::parseFunctionOp(
      parser, result, /*allowVariadic=*/false,
      getFunctionTypeAttrName(result.name), buildType,
      getArgAttrsAttrName(result.name), getResAttrsAttrName(result.name));
}

void SimFuncOp::print(OpAsmPrinter &printer) {
  function_interface_impl::printFunctionOp(
      printer, *this, /*isVariadic=*/false, getFunctionTypeAttrName(),
      getArgAttrsAttrName(), getResAttrsAttrName());
}

LogicalResult verifyUnitBindings(SimFuncOp function) {
  Attribute raw = function->getAttr(metadata::bindings);
  if (!raw)
    return success();
  auto bindings = dyn_cast<ArrayAttr>(raw);
  if (!bindings)
    return function.emitOpError()
           << "has malformed " << metadata::bindings << ": expected an array";

  FunctionType type = function.getFunctionType();
  enum class ProviderKind { Direct, FormalLocal, Local, Constant };
  struct PathState {
    std::optional<unsigned> provider;
    std::optional<ProviderKind> providerKind;
    bool formalCopiesOut = false;
    std::optional<unsigned> taskStaticDirect;
    std::optional<unsigned> lvalueOnly;
    std::optional<unsigned> copyOutDestination;
  };
  llvm::StringMap<PathState> paths;
  SmallVector<std::optional<unsigned>> argumentBindings(type.getNumInputs());
  std::optional<unsigned> returnBinding;

  auto claimProvider = [&](StringRef path, unsigned index,
                           ProviderKind kind) -> LogicalResult {
    PathState &state = paths[path];
    if (state.provider) {
      // A static task formal also has descriptor-backed storage. Preparation
      // deliberately emits the activation-local formal first and the direct
      // static-storage binding second; UnitLowering copies in between them.
      if (function.getEntryKind() == EntryKind::Task &&
          state.providerKind == ProviderKind::FormalLocal &&
          kind == ProviderKind::Direct && !state.taskStaticDirect) {
        state.taskStaticDirect = index;
        return success();
      }
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entries #"
             << *state.provider << " and #" << index
             << ": both provide the source value for path '" << path << "'";
    }
    state.provider = index;
    state.providerKind = kind;
    return success();
  };

  for (auto [index, binding] : llvm::enumerate(bindings)) {
    if (auto local = dyn_cast<LocalBindingAttr>(binding)) {
      if (failed(claimProvider(local.getPath().getValue(), index,
                               ProviderKind::Local)))
        return failure();
      if (local.getIsReturn()) {
        if (function.getEntryKind() != EntryKind::Function)
          return function.emitOpError()
                 << "has malformed " << metadata::bindings << " entry #"
                 << index
                 << ": only a function may have a return-local binding";
        if (returnBinding)
          return function.emitOpError()
                 << "has malformed " << metadata::bindings << " entries #"
                 << *returnBinding << " and #" << index
                 << ": multiple local bindings are marked as the function "
                    "return";
        returnBinding = index;
      }
      continue;
    }
    if (auto constant = dyn_cast<ConstantBindingAttr>(binding)) {
      if (failed(claimProvider(constant.getPath().getValue(), index,
                               ProviderKind::Constant)))
        return failure();
      continue;
    }
    auto argument = dyn_cast<ArgumentBindingAttr>(binding);
    if (!argument)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #" << index
             << ": expected an argument, local, or constant binding";
    if (argument.getArgument() >= type.getNumInputs())
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #" << index
             << ": argument index " << argument.getArgument()
             << " is outside the function signature";
    if (argument.getArgument() == 0)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #" << index
             << ": context argument cannot carry a source binding";

    unsigned argumentIndex = argument.getArgument();
    if (argumentBindings[argumentIndex])
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entries #"
             << *argumentBindings[argumentIndex] << " and #" << index
             << ": both bind function argument #" << argumentIndex;
    argumentBindings[argumentIndex] = index;

    Type argumentType = type.getInput(argument.getArgument());
    StringRef path = argument.getPath().getValue();
    PathState &state = paths[path];
    switch (argument.getKind()) {
    case UnitArgumentKind::Direct:
      if (isa<ContextType>(argumentType))
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entry #" << index
               << ": direct binding cannot target a context argument";
      if (failed(claimProvider(path, index, ProviderKind::Direct)))
        return failure();
      break;
    case UnitArgumentKind::LValueOnly:
      if (!isa<RefType, ArgumentRefType, NetType, DriverType>(argumentType))
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entry #" << index
               << ": lvalue-only binding requires a storage, net, or driver "
                  "argument";
      state.lvalueOnly = index;
      break;
    case UnitArgumentKind::FormalLocal:
      if (!isNormalizedValueType(argumentType))
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entry #" << index
               << ": formal-local binding requires a normalized value "
                  "argument";
      if (failed(claimProvider(path, index, ProviderKind::FormalLocal)))
        return failure();
      state.formalCopiesOut = argument.getCopyOut();
      break;
    case UnitArgumentKind::CopyOutDestination:
      if (!isa<RefType>(argumentType))
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entry #" << index
               << ": copy-out destination requires a storage argument";
      if (state.copyOutDestination)
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entries #"
               << *state.copyOutDestination << " and #" << index
               << ": multiple copy-out destinations bind path '" << path << "'";
      state.copyOutDestination = index;
      break;
    }
  }

  for (const auto &entry : paths) {
    StringRef path = entry.getKey();
    const PathState &state = entry.getValue();
    if (state.lvalueOnly && state.providerKind &&
        *state.providerKind != ProviderKind::Direct)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #"
             << *state.lvalueOnly << ": lvalue-only path '" << path
             << "' conflicts with a non-direct value binding";
    if (state.copyOutDestination &&
        (!state.providerKind ||
         *state.providerKind != ProviderKind::FormalLocal ||
         !state.formalCopiesOut))
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #"
             << *state.copyOutDestination << ": copy-out destination path '"
             << path << "' requires a copy-out formal-local binding";
    if (state.copyOutDestination && function.getEntryKind() != EntryKind::Task)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #"
             << *state.copyOutDestination
             << ": copy-out destination bindings are valid only on tasks";
    if (function.getEntryKind() == EntryKind::Task && state.formalCopiesOut &&
        !state.copyOutDestination)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #"
             << *state.provider << ": task copy-out formal path '" << path
             << "' requires a destination binding";
  }
  return success();
}

LogicalResult SimFuncOp::verify() {
  FunctionType type = getFunctionType();
  if (getDomain() == ExecutionDomain::Program &&
      getHomeRegion() != EventRegion::Reactive)
    return emitOpError(
        "program-domain code units must have reactive home region");
  if (getDomain() == ExecutionDomain::Design &&
      getHomeRegion() != EventRegion::Active &&
      getHomeRegion() != EventRegion::Observed &&
      getHomeRegion() != EventRegion::Reactive &&
      getHomeRegion() != EventRegion::Postponed)
    return emitOpError(
        "design-domain code units must have active, observed, reactive, or "
        "postponed home region");
  if (getHomeRegion() == EventRegion::Postponed &&
      failed(verifyPostponedReadOnly(*this)))
    return failure();
  if (getHomeRegion() == EventRegion::Observed) {
    WalkResult noDelay = getBody().walk([&](SimSuspendDelayOp operation) {
      operation.emitOpError("is not permitted in an observed-region code unit");
      return WalkResult::interrupt();
    });
    if (noDelay.wasInterrupted())
      return failure();
  }
  if (getCodeUnitIdAttr() &&
      failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")))
    return failure();
  if (type.getNumInputs() == 0 || !isa<ContextType>(type.getInput(0)))
    return emitOpError("first argument must be !obelisk_sim.context");
  for (Type input : type.getInputs()) {
    if (!isa<ContextType, RefType, ArgumentRefType, NetType, DriverType,
             EventType, ProcessType, ManagedRefType, IntegerType, LogicType,
             TimeType>(input) &&
        !isManagedHandleType(input) && !isa<FloatType>(input) &&
        !isAggregateType(input))
      return emitOpError() << "contains non-normalized argument type " << input;
    if (auto integer = dyn_cast<IntegerType>(input);
        integer && !integer.isSignless())
      return emitOpError("builtin integer arguments must be signless");
  }
  for (Type result : type.getResults()) {
    if (!isa<IntegerType, LogicType, TimeType, EventType, ProcessType,
             ManagedRefType, ArgumentRefType>(result) &&
        !isManagedHandleType(result) && !isa<FloatType>(result) &&
        !isAggregateType(result))
      return emitOpError() << "contains non-normalized result type " << result;
    if (auto integer = dyn_cast<IntegerType>(result);
        integer && !integer.isSignless())
      return emitOpError("builtin integer results must be signless");
  }
  if (failed(verifyUnitBindings(*this)))
    return failure();

  bool zeroTimeResultEntry = getEntryKind() == EntryKind::Function ||
                             getEntryKind() == EntryKind::Observer;
  if (!zeroTimeResultEntry && !type.getResults().empty())
    return emitOpError("process and root entries must not return values");
  if (getEntryKind() == EntryKind::RootInitializer && type.getNumInputs() != 1)
    return emitOpError("root initializer accepts only the context argument");
  if (getEntryKind() == EntryKind::Observer) {
    if (type.getNumResults() != 1 ||
        !isa<IntegerType, LogicType, FloatType>(type.getResult(0)))
      return emitOpError("observer entry must return one scalar result");
  }
  if (getEntryKind() == EntryKind::Function ||
      getEntryKind() == EntryKind::Observer) {
    // Only the time-controlled statements are illegal in a SystemVerilog
    // function. Nonblocking assignment, nonblocking event trigger, and
    // `fork ... join_none` are all legal there and consume no simulation
    // time, so they stay representable and are handled by the schedule.
    WalkResult blocking = getBody().walk([&](Operation *op) {
      if (isa<SimSuspendDelayOp, SimSuspendChangeOp, SimSuspendEdgeOp,
              SimSuspendEdgeIffOp, SimSuspendLevelOp, SimSuspendAnyOp,
              SimSuspendEventOp, SimSuspendObserveOp, SimSuspendForeverOp,
              SimSuspendAwaitOp, SimSuspendJoinOp, SimSuspendChildrenOp>(op)) {
        op->emitOpError(getEntryKind() == EntryKind::Function
                            ? "is not permitted in a zero-time function entry"
                            : "is not permitted in a zero-time observer entry");
        return WalkResult::interrupt();
      }
      if (getEntryKind() == EntryKind::Observer && isa<SimTaskCallOp>(op)) {
        op->emitOpError("task calls are not permitted in an observer entry");
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    if (blocking.wasInterrupted())
      return failure();
  }

  ArrayAttr argAttrs = getArgAttrsAttr();
  if (!argAttrs || argAttrs.size() != type.getNumInputs())
    return emitOpError(
        "requires one argument metadata dictionary per argument");
  for (auto [index, attr] : llvm::enumerate(argAttrs)) {
    auto dictionary = dyn_cast<DictionaryAttr>(attr);
    std::optional<CaptureKind> kind = getCaptureKind(dictionary);
    if (!kind)
      return emitOpError() << "argument #" << index
                           << " requires obelisk_sim.capture_kind metadata";
    if (index == 0 && *kind != CaptureKind::Context)
      return emitOpError("argument #0 must have context capture metadata");
    if (index != 0 && *kind == CaptureKind::Context)
      return emitOpError() << "argument #" << index
                           << " cannot have context capture metadata";
    bool needsDescriptor =
        *kind == CaptureKind::Storage || *kind == CaptureKind::Net ||
        *kind == CaptureKind::Driver || *kind == CaptureKind::Event;
    auto descriptor = dictionary.getAs<IntegerAttr>(metadata::descriptorId);
    if (needsDescriptor && !descriptor)
      return emitOpError() << "argument #" << index
                           << " requires obelisk_sim.descriptor_id metadata";
    if (!needsDescriptor && descriptor)
      return emitOpError() << "argument #" << index
                           << " must not have descriptor metadata";
    if (descriptor && descriptor.getValue().isNegative())
      return emitOpError() << "argument #" << index
                           << " has a negative descriptor ID";
    if (descriptor && descriptor.getValue().getBitWidth() > 64)
      return emitOpError() << "argument #" << index
                           << " has a descriptor ID wider than 64 bits";
  }
  return success();
}

LogicalResult SimReturnOp::verify() {
  auto function = (*this)->getParentOfType<SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in obelisk_sim.func");
  if (getOperandTypes() != function.getFunctionType().getResults())
    return emitOpError(
        "operand types must match the enclosing function results");
  return success();
}

LogicalResult SimCallOp::verify() {
  Operation *parent = getOperation()->getParentOp();
  if (!getOperation()->getParentOfType<SimFuncOp>() &&
      (!parent || (parent->getName().getStringRef() != "func.func" &&
                   parent->getName().getStringRef() != "llvm.func")))
    return emitOpError("must be nested in obelisk_sim.func");
  return success();
}

LogicalResult SimCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(getOperation(),
                                                               getCalleeAttr());
  if (!callee || callee.getEntryKind() != EntryKind::Function)
    return emitOpError("callee must name a sibling function entry");
  if (getOperandTypes() != callee.getFunctionType().getInputs() ||
      getResultTypes() != callee.getFunctionType().getResults())
    return emitOpError("operand and result types must match callee signature");
  return success();
}

Operation::operand_range SimObserverBindOp::getCaptures() {
  size_t count = getCaptureCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getCaptureCount(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimObserverBindOp::getDependencies() {
  size_t count = getCaptureCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getCaptureCount(), getNumOperands());
  return getValues().drop_front(count);
}

LogicalResult SimObserverBindOp::verify() {
  if (getCaptureCountAttr().getValue().isNegative() ||
      static_cast<uint64_t>(getCaptureCount()) > getNumOperands())
    return emitOpError("capture count exceeds the operand inventory");
  for (Value capture : getCaptures()) {
    Type type = capture.getType();
    Type element;
    if (auto reference = dyn_cast<RefType>(type))
      element = reference.getElementType();
    else if (auto net = dyn_cast<NetType>(type))
      element = net.getElementType();
    else if (auto driver = dyn_cast<DriverType>(type))
      element = driver.getElementType();
    else if (isa<EventType>(type))
      continue;
    else
      return emitOpError(
          "captures must use storage, net, driver, or named-event handles");
    if (!isa<FloatType>(element) && !getPackedWidth(element))
      return emitOpError(
          "captured handles must refer to packed or floating values");
  }
  for (Value dependency : getDependencies())
    if (!isa<RefType, NetType, EventType>(dependency.getType()))
      return emitOpError(
          "dependencies must be storage, net, or named-event handles");
  if ((*this)->hasAttr("obelisk_sim.event_primary")) {
    auto observer = cast<ObserverType>(getResult().getType());
    auto integer = dyn_cast<IntegerType>(observer.getResultType());
    if (!integer || integer.getWidth() != 1 ||
        llvm::none_of(getDependencies(), [](Value dependency) {
          return isa<EventType>(dependency.getType());
        }))
      return emitOpError(
          "event-primary bindings must return i1 and depend on an event");
  }
  return success();
}

LogicalResult
SimObserverBindOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto evaluator = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(
      getOperation(), getEvaluatorAttr());
  if (!evaluator || evaluator.getEntryKind() != EntryKind::Observer)
    return emitOpError("evaluator must name a sibling observer entry");
  FunctionType type = evaluator.getFunctionType();
  if (type.getNumInputs() == 0 || !isa<ContextType>(type.getInput(0)))
    return emitOpError("observer evaluator is missing its context argument");
  if (getCaptures().getTypes() != type.getInputs().drop_front())
    return emitOpError(
        "capture types must match evaluator arguments after context");
  if (type.getNumResults() != 1 ||
      type.getResult(0) != getResult().getType().getResultType())
    return emitOpError("result type must match the evaluator result");
  return success();
}

static LogicalResult verifyContinuation(Operation *op,
                                        ValueRange continuationOperands,
                                        Block *continuation);

Operation::operand_range SimTaskCallOp::getArguments() {
  size_t count = getArgumentCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimTaskCallOp::getContinuationOperands() {
  size_t count = getArgumentCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return getValues().drop_front(count);
}

MutableOperandRange SimTaskCallOp::getContinuationOperandsMutable() {
  unsigned count =
      getArgumentCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return MutableOperandRange(getOperation(), count, getNumOperands() - count);
}

LogicalResult SimTaskCallOp::verify() {
  auto function = getOperation()->getParentOfType<SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in obelisk_sim.func");
  if (function.getEntryKind() == EntryKind::Function)
    return emitOpError("is not permitted in a zero-time function entry");
  if (getArgumentCountAttr().getValue().isNegative() ||
      static_cast<uint64_t>(getArgumentCount()) > getNumOperands())
    return emitOpError("argument count exceeds the operand inventory");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

LogicalResult
SimTaskCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(getOperation(),
                                                               getCalleeAttr());
  if (!callee || callee.getEntryKind() != EntryKind::Task)
    return emitOpError("callee must name a sibling task entry");
  if (getArguments().getTypes() != callee.getFunctionType().getInputs())
    return emitOpError("argument types must match the task signature");
  if (!callee.getFunctionType().getResults().empty())
    return emitOpError("task entry must not return SSA results");
  return success();
}

void SimDPICallOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  if (getIsPure())
    return;
  effects.emplace_back(MemoryEffects::Read::get(), ExternalResource::get());
  effects.emplace_back(MemoryEffects::Write::get(), ExternalResource::get());
  effects.emplace_back(MemoryEffects::Read::get(), SchedulerResource::get());
}

LogicalResult SimDPICallOp::verify() {
  if (!getOperation()->getParentOfType<SimFuncOp>())
    return emitOpError("must be nested in obelisk_sim.func");
  if (getImportId() == 0)
    return emitOpError("import ID must be nonzero");
  if (getCIdentifier().empty())
    return emitOpError("C identifier must not be empty");
  if (getIsPure() && (getIsContext() || getIsTask()))
    return emitOpError("pure DPI imports cannot be context imports or tasks");
  if (getNumResults() == 0 ||
      !isa<runtime::StatusType>(getResults().back().getType()))
    return emitOpError("must return a trailing runtime status");
  auto physicalCount = getOperation()->getAttrOfType<IntegerAttr>(
      "obelisk.dpi.logical_operand_count");
  uint64_t logicalInputs = physicalCount
                               ? physicalCount.getValue().getZExtValue()
                               : getArguments().size();
  if (logicalInputs > getAbiSignature().size())
    return emitOpError("logical operand count exceeds the ABI signature");

  SmallVector<DPIABIAttr> signature;
  signature.reserve(getAbiSignature().size());
  for (Attribute attr : getAbiSignature()) {
    auto abi = dyn_cast<DPIABIAttr>(attr);
    if (!abi)
      return emitOpError("ABI signature entries must be DPI ABI attributes");
    signature.push_back(abi);
  }

  uint64_t outputCursor = logicalInputs;
  if (!getIsTask()) {
    if (outputCursor >= signature.size() ||
        signature[outputCursor].getDirection() != DPIArgumentDirection::Result)
      return emitOpError(
          "a DPI function signature must place its result first");
    ++outputCursor;
  }
  for (uint64_t index = 0; index != logicalInputs; ++index) {
    DPIABIAttr input = signature[index];
    if (input.getDirection() == DPIArgumentDirection::Result)
      return emitOpError("a DPI formal cannot have result direction");
    if (input.getDirection() == DPIArgumentDirection::Input)
      continue;
    if (outputCursor >= signature.size())
      return emitOpError("DPI signature is missing a formal copy-out");
    DPIABIAttr output = signature[outputCursor++];
    if (output.getDirection() != DPIArgumentDirection::Output ||
        output.getKind() != input.getKind() ||
        output.getWidth() != input.getWidth() ||
        output.getFourState() != input.getFourState() ||
        output.getIsSigned() != input.getIsSigned())
      return emitOpError("DPI formal copy-out must match its input ABI entry");
  }
  if (outputCursor != signature.size())
    return emitOpError("DPI signature has excess result entries");

  auto verifyLogicalType = [&](Type type, DPIABIAttr abi) -> LogicalResult {
    std::optional<unsigned> width = getPackedWidth(type);
    bool fourState = isa<LogicType>(getPackedScalarType(type));
    if (!width || *width != abi.getWidth() || fourState != abi.getFourState())
      return emitOpError(
          "logical operand or result type disagrees with its DPI ABI entry");
    return success();
  };
  if (!physicalCount) {
    if (!isa<runtime::ContextType, ContextType>(getRuntimeContext().getType()))
      return emitOpError(
          "runtime context must have simulation or runtime context type");
    uint64_t logicalOutputs = signature.size() - logicalInputs;
    if (getArguments().size() != logicalInputs ||
        getNumResults() - 1 != logicalOutputs)
      return emitOpError(
          "ABI signature must describe every data operand and result");
    for (auto [value, abi] : llvm::zip_equal(
             getArguments(),
             ArrayRef<DPIABIAttr>(signature).take_front(logicalInputs)))
      if (failed(verifyLogicalType(value.getType(), abi)))
        return failure();
    for (auto [value, abi] : llvm::zip_equal(
             getResults().drop_back(),
             ArrayRef<DPIABIAttr>(signature).drop_front(logicalInputs)))
      if (failed(verifyLogicalType(value.getType(), abi)))
        return failure();
    return success();
  }

  auto verifyPhysicalTypes = [&](TypeRange types, ArrayRef<DPIABIAttr> entries,
                                 StringRef role) -> LogicalResult {
    size_t physical = 0;
    for (DPIABIAttr abi : entries) {
      unsigned planes = abi.getFourState() ? 2 : 1;
      if (physical + planes > types.size())
        return emitOpError()
               << "is missing a physical DPI " << role << " plane";
      for (unsigned plane = 0; plane != planes; ++plane) {
        auto integer = dyn_cast<IntegerType>(types[physical++]);
        if (!integer || integer.getWidth() != abi.getWidth())
          return emitOpError()
                 << "has a malformed physical DPI " << role << " plane";
      }
    }
    if (physical != types.size())
      return emitOpError() << "has excess physical DPI " << role << " planes";
    return success();
  };
  if (failed(verifyPhysicalTypes(
          getArguments().getTypes(),
          ArrayRef<DPIABIAttr>(signature).take_front(logicalInputs), "input")))
    return failure();
  return verifyPhysicalTypes(
      getResults().drop_back().getTypes(),
      ArrayRef<DPIABIAttr>(signature).drop_front(logicalInputs), "result");
}

LogicalResult SimSpawnOp::verify() {
  if (!getOperation()->getParentOfType<SimFuncOp>())
    return emitOpError("must be nested in obelisk_sim.func");
  return success();
}

LogicalResult SimSpawnOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(getOperation(),
                                                               getCalleeAttr());
  if (!callee || callee.getEntryKind() == EntryKind::Function ||
      callee.getEntryKind() == EntryKind::Observer ||
      callee.getEntryKind() == EntryKind::Task ||
      callee.getEntryKind() == EntryKind::RootInitializer)
    return emitOpError("callee must name a sibling process entry");
  if (getOperandTypes() != callee.getFunctionType().getInputs() ||
      !callee.getFunctionType().getResults().empty())
    return emitOpError("operands must match the void callee signature");
  return success();
}

LogicalResult SimControlEnterOp::verify() {
  return verifyPositive(*this, getTargetIdAttr(), "control target ID");
}

LogicalResult SimControlDisableOp::verify() {
  if (failed(verifyPositive(*this, getTargetIdAttr(), "control target ID")))
    return failure();
  if (getActivation() &&
      getActivation().getType() != ControlType::get(getContext()))
    return emitOpError("activation must be a control token");
  if (getActivation() && getHierarchical())
    return emitOpError(
        "hierarchical disable must not name one activation token");
  return success();
}

LogicalResult SimStaticOnceOp::verify() {
  return verifyPositive(*this, getIdAttr(), "static initialization ID");
}

LogicalResult SimDeferredOnceOp::verify() {
  return verifyPositive(*this, getIdAttr(), "deferred assertion site ID");
}

LogicalResult SimContextStorageOp::verify() {
  return verifyNonnegative(*this, getIdAttr(), "storage ID");
}
LogicalResult SimContextNetOp::verify() {
  return verifyNonnegative(*this, getIdAttr(), "net ID");
}
LogicalResult SimContextDriverOp::verify() {
  return verifyNonnegative(*this, getIdAttr(), "driver ID");
}
LogicalResult SimContextEventOp::verify() {
  return verifyNonnegative(*this, getIdAttr(), "event ID");
}

static bool isUnionAggregate(Type type) {
  return isa<PackedUnionType, UnpackedUnionType>(type);
}

static bool isStructOrArrayAggregate(Type type) {
  return isAggregateType(type) && !isUnionAggregate(type);
}

LogicalResult SimPackedFlattenOp::verify() {
  if (!isAggregateType(getInput().getType()) ||
      !getPackedScalarType(getInput().getType()))
    return emitOpError("input must be a packed aggregate");
  if (getResult().getType() != getPackedScalarType(getInput().getType()))
    return emitOpError(
        "result must be the aggregate's width- and state-matched scalar");
  return success();
}

LogicalResult SimPackedUnflattenOp::verify() {
  if (!isAggregateType(getResult().getType()) ||
      !getPackedScalarType(getResult().getType()))
    return emitOpError("result must be a packed aggregate");
  if (getInput().getType() != getPackedScalarType(getResult().getType()))
    return emitOpError(
        "input must be the aggregate's width- and state-matched scalar");
  return success();
}

static LogicalResult verifyAggregateIndex(Operation *operation, Type aggregate,
                                          IntegerAttr index, Type result,
                                          bool requireUnion) {
  if (!isAggregateType(aggregate) ||
      requireUnion != isUnionAggregate(aggregate))
    return operation->emitOpError()
           << (requireUnion ? "input must be a union"
                            : "input must be a struct or fixed array");
  if (index.getValue().isNegative() || index.getValue().getActiveBits() > 32)
    return operation->emitOpError("aggregate index must be nonnegative");
  uint64_t ordinal = index.getValue().getZExtValue();
  Type expected = ordinal <= std::numeric_limits<unsigned>::max()
                      ? getAggregateElementType(aggregate, ordinal)
                      : Type{};
  if (!expected)
    return operation->emitOpError("aggregate index is out of range");
  if (result != expected)
    return operation->emitOpError()
           << "result type must match aggregate element type " << expected;
  return success();
}

LogicalResult SimAggregateDefaultOp::verify() {
  if (!isAggregateType(getResult().getType()))
    return emitOpError("result must be a fixed aggregate type");
  return success();
}

LogicalResult SimAggregateConstructOp::verify() {
  Type type = getResult().getType();
  if (!isStructOrArrayAggregate(type))
    return emitOpError("result must be a struct or fixed array");
  if (getElements().size() != getAggregateNumElements(type))
    return emitOpError("requires one operand per aggregate element");
  for (auto [index, element] : llvm::enumerate(getElements()))
    if (element.getType() != getAggregateElementType(type, index))
      return emitOpError() << "operand #" << index
                           << " does not match its aggregate element type";
  return success();
}

LogicalResult SimAggregateExtractOp::verify() {
  return verifyAggregateIndex(*this, getInput().getType(), getIndexAttr(),
                              getResult().getType(), false);
}

LogicalResult SimAggregateInsertOp::verify() {
  if (getInput().getType() != getResult().getType())
    return emitOpError("input and result aggregate types must match");
  return verifyAggregateIndex(*this, getInput().getType(), getIndexAttr(),
                              getReplacement().getType(), false);
}

LogicalResult SimArrayDynExtractOp::verify() {
  Type type = getInput().getType();
  if (!isa<PackedArrayType, UnpackedArrayType>(type))
    return emitOpError("input must be a fixed array");
  if (failed(verifyNormalizedIndex(*this, getIndex().getType())))
    return failure();
  if (getResult().getType() != getAggregateElementType(type, 0))
    return emitOpError("result must match the array element type");
  return success();
}

LogicalResult SimUnionConstructOp::verify() {
  return verifyAggregateIndex(*this, getResult().getType(), getIndexAttr(),
                              getValue().getType(), true);
}

LogicalResult SimUnionExtractOp::verify() {
  return verifyAggregateIndex(*this, getInput().getType(), getIndexAttr(),
                              getResult().getType(), true);
}

LogicalResult SimUnionIsActiveOp::verify() {
  Type type = getInput().getType();
  bool tagged = false;
  if (auto packed = dyn_cast<PackedUnionType>(type))
    tagged = packed.getIsTagged();
  else if (auto unpacked = dyn_cast<UnpackedUnionType>(type))
    tagged = unpacked.getIsTagged();
  else
    return emitOpError("input must be a tagged union");
  if (!tagged)
    return emitOpError("input union must be tagged");
  if (getIndexAttr().getValue().isNegative() ||
      getIndex() >= getAggregateNumElements(type))
    return emitOpError("tagged union member index is out of range");
  return success();
}

OpFoldResult SimUnionIsActiveOp::fold(FoldAdaptor) {
  if (auto packed = dyn_cast<PackedUnionType>(getInput().getType());
      packed && packed.getTagBits() == 0)
    return IntegerAttr::get(getResult().getType(), true);
  if (auto construct = getInput().getDefiningOp<SimUnionConstructOp>())
    return IntegerAttr::get(getResult().getType(),
                            construct.getIndex() == getIndex());
  return {};
}

static LogicalResult verifySubelementPath(Operation *operation, Type input,
                                          ArrayRef<int64_t> indices,
                                          Type result) {
  if (indices.empty())
    return operation->emitOpError("subelement path must not be empty");
  Type current = input;
  for (int64_t index : indices) {
    if (index < 0 ||
        static_cast<uint64_t>(index) > std::numeric_limits<unsigned>::max())
      return operation->emitOpError("subelement index must be nonnegative");
    current = getAggregateElementType(current, static_cast<unsigned>(index));
    if (!current)
      return operation->emitOpError("subelement path is out of range");
  }
  if (current != result)
    return operation->emitOpError()
           << "result element type must match selected subelement " << current;
  return success();
}

LogicalResult SimRefSubelementOp::verify() {
  return verifySubelementPath(*this, getInput().getType().getElementType(),
                              getIndices(),
                              getResult().getType().getElementType());
}

LogicalResult SimDriverSubelementOp::verify() {
  return verifySubelementPath(*this, getInput().getType().getElementType(),
                              getIndices(),
                              getResult().getType().getElementType());
}

static LogicalResult verifyArrayElementView(Operation *operation, Type input,
                                            Type index, Type result) {
  if (!isa<PackedArrayType, UnpackedArrayType>(input))
    return operation->emitOpError("input element must be a fixed array");
  if (failed(verifyNormalizedIndex(operation, index)))
    return failure();
  if (result != getAggregateElementType(input, 0))
    return operation->emitOpError("result must match the array element type");
  return success();
}

LogicalResult SimRefArrayElementOp::verify() {
  return verifyArrayElementView(*this, getInput().getType().getElementType(),
                                getIndex().getType(),
                                getResult().getType().getElementType());
}

LogicalResult SimDriverArrayElementOp::verify() {
  return verifyArrayElementView(*this, getInput().getType().getElementType(),
                                getIndex().getType(),
                                getResult().getType().getElementType());
}

LogicalResult SimRefAllocOp::verify() {
  if (getInitialValue().getType() != getResult().getType().getElementType())
    return emitOpError("initial value must match allocated element type");
  return success();
}

SmallVector<MemorySlot> SimRefAllocOp::getPromotableSlots() {
  return {{getResult(), getResult().getType().getElementType()}};
}

static std::optional<unsigned> getUnionSelectedInitializer(Value value) {
  if (auto construct = value.getDefiningOp<SimUnionConstructOp>())
    return static_cast<unsigned>(construct.getIndex());
  if (auto defaultValue = value.getDefiningOp<SimAggregateDefaultOp>()) {
    Type type = defaultValue.getResult().getType();
    if (auto packed = dyn_cast<PackedUnionType>(type);
        packed && packed.getIsTagged() && !containsFourStateLeaf(type))
      return 0;
    if (auto unpacked = dyn_cast<UnpackedUnionType>(type);
        unpacked && !unpacked.getIsTagged())
      return 0;
  }
  return std::nullopt;
}

SmallVector<DestructurableMemorySlot> SimRefAllocOp::getDestructurableSlots() {
  Type elementType = getResult().getType().getElementType();
  auto destructurable = dyn_cast<DestructurableTypeInterface>(elementType);
  if (!destructurable)
    return {};
  std::optional<DenseMap<Attribute, Type>> elements =
      destructurable.getSubelementIndexMap();
  if (!elements || elements->empty())
    return {};

  // A union can only lose its shared backing when every view and its
  // initializer agree on one active field. Whole accesses and mixed views
  // deliberately retain the allocation.
  if (isUnionAggregate(elementType)) {
    std::optional<unsigned> selected =
        getUnionSelectedInitializer(getInitialValue());
    if (!selected)
      return {};
    for (OpOperand &use : getResult().getUses()) {
      auto view = dyn_cast<SimRefSubelementOp>(use.getOwner());
      if (!view || use.get() != view.getInput() || view.getIndices().empty() ||
          static_cast<unsigned>(view.getIndices()[0]) != *selected)
        return {};
    }
  }
  return {DestructurableMemorySlot{{getResult(), elementType}, *elements}};
}

static Value materializeDefaultValue(OpBuilder &builder, Location location,
                                     Type type) {
  if (isAggregateType(type))
    return SimAggregateDefaultOp::create(builder, location, type);
  if (auto integer = dyn_cast<IntegerType>(type))
    return arith::ConstantOp::create(builder, location, integer,
                                     builder.getIntegerAttr(integer, 0));
  if (isa<FloatType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getFloatAttr(type, 0.0));
  if (auto logic = dyn_cast<LogicType>(type)) {
    auto plane = IntegerType::get(type.getContext(), logic.getWidth());
    return SimLogicConstantOp::create(
        builder, location, logic,
        builder.getIntegerAttr(plane, APInt::getZero(logic.getWidth())),
        builder.getIntegerAttr(plane, APInt::getAllOnes(logic.getWidth())));
  }
  if (isa<TimeType>(type))
    return SimTimeConstantOp::create(builder, location, type,
                                     builder.getI64IntegerAttr(0));
  return {};
}

DenseMap<Attribute, MemorySlot> SimRefAllocOp::destructure(
    const DestructurableMemorySlot &slot,
    const llvm::SmallPtrSetImpl<Attribute> &usedIndices, OpBuilder &builder,
    SmallVectorImpl<DestructurableAllocationOpInterface> &newAllocators) {
  assert(slot.ptr == getResult());
  builder.setInsertionPointAfter(*this);
  SmallVector<Attribute> sorted(usedIndices.begin(), usedIndices.end());
  llvm::sort(sorted, [](Attribute lhs, Attribute rhs) {
    return cast<IntegerAttr>(lhs).getInt() < cast<IntegerAttr>(rhs).getInt();
  });

  DenseMap<Attribute, MemorySlot> subslots;
  for (Attribute attribute : sorted) {
    unsigned index = cast<IntegerAttr>(attribute).getInt();
    Type type = slot.subelementTypes.lookup(attribute);
    Value initial;
    if (auto construct =
            getInitialValue().getDefiningOp<SimAggregateConstructOp>())
      initial = construct.getElements()[index];
    else if (auto construct =
                 getInitialValue().getDefiningOp<SimUnionConstructOp>();
             construct && construct.getIndex() == index)
      initial = construct.getValue();
    else if (isUnionAggregate(slot.elemType))
      initial = SimUnionExtractOp::create(builder, getLoc(), type,
                                          getInitialValue(), index);
    else
      initial = SimAggregateExtractOp::create(builder, getLoc(), type,
                                              getInitialValue(), index);
    auto allocation = SimRefAllocOp::create(
        builder, getLoc(), RefType::get(getContext(), type), initial);
    newAllocators.push_back(allocation);
    subslots.try_emplace(attribute, MemorySlot{allocation.getResult(), type});
  }
  return subslots;
}

std::optional<DestructurableAllocationOpInterface>
SimRefAllocOp::handleDestructuringComplete(const DestructurableMemorySlot &slot,
                                           OpBuilder &) {
  assert(slot.ptr == getResult());
  getOperation()->erase();
  return std::nullopt;
}

Value SimRefAllocOp::getDefaultValue(const MemorySlot &, OpBuilder &) {
  return getInitialValue();
}

void SimRefAllocOp::handleBlockArgument(const MemorySlot &, BlockArgument,
                                        OpBuilder &) {}

std::optional<PromotableAllocationOpInterface>
SimRefAllocOp::handlePromotionComplete(const MemorySlot &, Value, OpBuilder &) {
  getOperation()->erase();
  return std::nullopt;
}

bool SimRefLoadOp::loadsFrom(const MemorySlot &slot) {
  return getReference() == slot.ptr;
}
bool SimRefLoadOp::storesTo(const MemorySlot &) { return false; }
Value SimRefLoadOp::getStored(const MemorySlot &, OpBuilder &, Value,
                              const DataLayout &) {
  return {};
}
bool SimRefLoadOp::canUsesBeRemoved(
    const MemorySlot &slot,
    const llvm::SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &, const DataLayout &) {
  return getReference() == slot.ptr &&
         llvm::all_of(blockingUses, [&](OpOperand *use) {
           return use == &getReferenceMutable();
         });
}
DeletionKind SimRefLoadOp::removeBlockingUses(
    const MemorySlot &, const llvm::SmallPtrSetImpl<OpOperand *> &, OpBuilder &,
    Value reachingDefinition, const DataLayout &) {
  getResult().replaceAllUsesWith(reachingDefinition);
  return DeletionKind::Delete;
}

bool SimRefStoreOp::loadsFrom(const MemorySlot &) { return false; }
bool SimRefStoreOp::storesTo(const MemorySlot &slot) {
  return getReference() == slot.ptr;
}
Value SimRefStoreOp::getStored(const MemorySlot &slot, OpBuilder &, Value,
                               const DataLayout &) {
  return storesTo(slot) ? getValue() : Value{};
}
bool SimRefStoreOp::canUsesBeRemoved(
    const MemorySlot &slot,
    const llvm::SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &, const DataLayout &) {
  return getReference() == slot.ptr &&
         llvm::all_of(blockingUses, [&](OpOperand *use) {
           return use == &getReferenceMutable();
         });
}
DeletionKind
SimRefStoreOp::removeBlockingUses(const MemorySlot &,
                                  const llvm::SmallPtrSetImpl<OpOperand *> &,
                                  OpBuilder &, Value, const DataLayout &) {
  return DeletionKind::Delete;
}

static SmallVector<Attribute>
getSortedSubslotIndices(const DestructurableMemorySlot &slot) {
  SmallVector<Attribute> indices;
  indices.reserve(slot.subelementTypes.size());
  for (auto [index, type] : slot.subelementTypes)
    indices.push_back(index);
  llvm::sort(indices, [](Attribute lhs, Attribute rhs) {
    return cast<IntegerAttr>(lhs).getInt() < cast<IntegerAttr>(rhs).getInt();
  });
  return indices;
}

bool SimRefLoadOp::canRewire(const DestructurableMemorySlot &slot,
                             llvm::SmallPtrSetImpl<Attribute> &usedIndices,
                             SmallVectorImpl<MemorySlot> &,
                             const DataLayout &) {
  if (getReference() != slot.ptr || getResult().getType() != slot.elemType ||
      isUnionAggregate(slot.elemType))
    return false;
  if (isa<PackedArrayType, UnpackedArrayType>(slot.elemType) &&
      llvm::any_of(getResult().getUsers(), [](Operation *user) {
        return isa<SimArrayDynExtractOp>(user);
      }))
    return false;
  for (Attribute index : getSortedSubslotIndices(slot))
    usedIndices.insert(index);
  return true;
}

DeletionKind SimRefLoadOp::rewire(const DestructurableMemorySlot &slot,
                                  DenseMap<Attribute, MemorySlot> &subslots,
                                  OpBuilder &builder, const DataLayout &) {
  SmallVector<Value> elements;
  for (Attribute index : getSortedSubslotIndices(slot)) {
    MemorySlot subslot = subslots.at(index);
    elements.push_back(
        SimRefLoadOp::create(builder, getLoc(), subslot.elemType, subslot.ptr));
  }
  Value reconstructed = SimAggregateConstructOp::create(
      builder, getLoc(), slot.elemType, elements);
  getResult().replaceAllUsesWith(reconstructed);
  return DeletionKind::Delete;
}

LogicalResult SimRefLoadOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &, const DataLayout &) {
  return success(getReference() != slot.ptr ||
                 getResult().getType() == slot.elemType);
}

bool SimRefStoreOp::canRewire(const DestructurableMemorySlot &slot,
                              llvm::SmallPtrSetImpl<Attribute> &usedIndices,
                              SmallVectorImpl<MemorySlot> &,
                              const DataLayout &) {
  if (getReference() != slot.ptr || getValue() == slot.ptr ||
      getValue().getType() != slot.elemType || isUnionAggregate(slot.elemType))
    return false;
  for (Attribute index : getSortedSubslotIndices(slot))
    usedIndices.insert(index);
  return true;
}

DeletionKind SimRefStoreOp::rewire(const DestructurableMemorySlot &slot,
                                   DenseMap<Attribute, MemorySlot> &subslots,
                                   OpBuilder &builder, const DataLayout &) {
  for (Attribute attribute : getSortedSubslotIndices(slot)) {
    unsigned index = cast<IntegerAttr>(attribute).getInt();
    MemorySlot subslot = subslots.at(attribute);
    Value element = SimAggregateExtractOp::create(
        builder, getLoc(), subslot.elemType, getValue(), index);
    SimRefStoreOp::create(builder, getLoc(), element, subslot.ptr);
  }
  return DeletionKind::Delete;
}

LogicalResult SimRefStoreOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &, const DataLayout &) {
  return success(getReference() != slot.ptr ||
                 getValue().getType() == slot.elemType);
}

static Attribute getFirstSubelementIndex(DenseI64ArrayAttr indices,
                                         MLIRContext *context) {
  if (!indices || indices.empty() || indices[0] < 0 ||
      static_cast<uint64_t>(indices[0]) > std::numeric_limits<uint32_t>::max())
    return {};
  return getSubelementIndexAttr(context, static_cast<unsigned>(indices[0]));
}

bool SimRefSubelementOp::canRewire(
    const DestructurableMemorySlot &slot,
    llvm::SmallPtrSetImpl<Attribute> &usedIndices,
    SmallVectorImpl<MemorySlot> &mustBeSafelyUsed, const DataLayout &) {
  if (getInput() != slot.ptr)
    return false;
  Attribute index = getFirstSubelementIndex(getIndicesAttr(), getContext());
  if (!index || !slot.subelementTypes.contains(index))
    return false;
  usedIndices.insert(index);
  mustBeSafelyUsed.push_back(
      {getResult(), getResult().getType().getElementType()});
  return true;
}

DeletionKind
SimRefSubelementOp::rewire(const DestructurableMemorySlot &,
                           DenseMap<Attribute, MemorySlot> &subslots,
                           OpBuilder &builder, const DataLayout &) {
  Attribute index = getFirstSubelementIndex(getIndicesAttr(), getContext());
  MemorySlot subslot = subslots.at(index);
  Value replacement = subslot.ptr;
  ArrayRef<int64_t> path = getIndices();
  if (path.size() > 1) {
    auto remaining = builder.getDenseI64ArrayAttr(path.drop_front());
    replacement = SimRefSubelementOp::create(
        builder, getLoc(), getResult().getType(), subslot.ptr, remaining);
  }
  getResult().replaceAllUsesWith(replacement);
  return DeletionKind::Delete;
}

LogicalResult SimRefSubelementOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &mustBeSafelyUsed,
    const DataLayout &) {
  if (getInput() != slot.ptr)
    return success();
  Type result = getResult().getType().getElementType();
  if (failed(verifySubelementPath(getOperation(), slot.elemType, getIndices(),
                                  result)))
    return failure();
  mustBeSafelyUsed.push_back({getResult(), result});
  return success();
}

LogicalResult SimRefExtractOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &mustBeSafelyUsed,
    const DataLayout &) {
  if (getInput() != slot.ptr)
    return success();
  std::optional<unsigned> input = getPackedWidth(slot.elemType);
  Type resultType = getResult().getType().getElementType();
  std::optional<unsigned> result = getPackedWidth(resultType);
  if (!input || !result || getLowBit() > *input ||
      *result > *input - getLowBit())
    return failure();
  mustBeSafelyUsed.push_back({getResult(), resultType});
  return success();
}

LogicalResult SimRefDynExtractOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &mustBeSafelyUsed,
    const DataLayout &) {
  if (getInput() != slot.ptr)
    return success();
  Type resultType = getResult().getType().getElementType();
  if (!getPackedWidth(slot.elemType) || !getPackedWidth(resultType))
    return failure();
  mustBeSafelyUsed.push_back({getResult(), resultType});
  return success();
}

LogicalResult SimRefArrayElementOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &mustBeSafelyUsed,
    const DataLayout &) {
  if (getInput() != slot.ptr)
    return success();
  Type resultType = getResult().getType().getElementType();
  if (!isa<PackedArrayType, UnpackedArrayType>(slot.elemType) ||
      getAggregateElementType(slot.elemType, 0) != resultType)
    return failure();
  mustBeSafelyUsed.push_back({getResult(), resultType});
  return success();
}

LogicalResult SimRefExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() + *result > *input)
    return emitOpError("constant selection is outside the input element width");
  return success();
}

LogicalResult SimOverrideOp::verify() {
  Type elementType;
  if (auto reference = dyn_cast<RefType>(getTarget().getType()))
    elementType = reference.getElementType();
  else if (auto net = dyn_cast<NetType>(getTarget().getType()))
    elementType = net.getElementType();
  else
    return emitOpError("target must be a static reference or built-in net");
  if (getIsAssign() && !isa<RefType>(getTarget().getType()))
    return emitOpError("procedural assign requires a variable reference");
  if (elementType != getValue().getType())
    return emitOpError("target element type must match the override value");
  if (!getPackedWidth(elementType))
    return emitOpError("requires a fixed-width packed value");
  return success();
}

LogicalResult SimReleaseOverrideOp::verify() {
  Type elementType;
  if (auto reference = dyn_cast<RefType>(getTarget().getType()))
    elementType = reference.getElementType();
  else if (auto net = dyn_cast<NetType>(getTarget().getType()))
    elementType = net.getElementType();
  else
    return emitOpError("target must be a static reference or built-in net");
  if (getIsAssign() && !isa<RefType>(getTarget().getType()))
    return emitOpError("procedural deassign requires a variable reference");
  if (!getPackedWidth(elementType))
    return emitOpError("requires a fixed-width packed value");
  return success();
}

LogicalResult SimNetExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() + *result > *input)
    return emitOpError("constant selection is outside the input element width");
  return success();
}

LogicalResult SimRefDynExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())) ||
      failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || *result > *input)
    return emitOpError("result element width exceeds input element width");
  return success();
}

LogicalResult SimDriverExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() + *result > *input)
    return emitOpError("constant selection is outside the input element width");
  return success();
}

LogicalResult SimDriverDynExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())) ||
      failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || *result > *input)
    return emitOpError("result element width exceeds input element width");
  return success();
}

namespace {

struct LogicPlanes {
  APInt value;
  APInt unknown;
};

/// Decode the dialect's folded representation of a four-state value.
static std::optional<LogicPlanes> getLogicPlanes(Attribute attribute) {
  auto planes = dyn_cast_or_null<ArrayAttr>(attribute);
  if (!planes || planes.size() != 2)
    return std::nullopt;
  auto value = dyn_cast<IntegerAttr>(planes[0]);
  auto unknown = dyn_cast<IntegerAttr>(planes[1]);
  if (!value || !unknown ||
      value.getValue().getBitWidth() != unknown.getValue().getBitWidth())
    return std::nullopt;
  return LogicPlanes{value.getValue(), unknown.getValue()};
}

static ArrayAttr getLogicAttribute(MLIRContext *context, LogicPlanes planes) {
  auto type = IntegerType::get(context, planes.value.getBitWidth());
  return ArrayAttr::get(context, {IntegerAttr::get(type, planes.value),
                                  IntegerAttr::get(type, planes.unknown)});
}

static LogicPlanes getCanonicalUnknown(unsigned width) {
  return {APInt::getZero(width), APInt::getAllOnes(width)};
}

static LogicPlanes getLogicBoolean(bool value, bool unknown = false) {
  return {APInt(1, value), APInt(1, unknown)};
}

struct TruthState {
  bool value;
  bool unknown;
};

static TruthState getTruth(LogicPlanes input) {
  bool value = !(input.value & ~input.unknown).isZero();
  return {value, !value && !input.unknown.isZero()};
}

/// A present ConstantIndex is a constant operand. `value` is absent exactly
/// when that constant is a four-state value containing X or Z.
struct ConstantIndex {
  std::optional<APInt> value;
};

static std::optional<ConstantIndex> getConstantIndex(Attribute attribute) {
  if (auto integer = dyn_cast_or_null<IntegerAttr>(attribute))
    return ConstantIndex{integer.getValue()};
  auto planes = getLogicPlanes(attribute);
  if (!planes)
    return std::nullopt;
  if (!planes->unknown.isZero())
    return ConstantIndex{std::nullopt};
  return ConstantIndex{planes->value};
}

static std::optional<ConstantIndex> getConstantIndex(Value value) {
  Attribute attribute;
  if (!matchPattern(value, m_Constant(&attribute)))
    return std::nullopt;
  return getConstantIndex(attribute);
}

/// Interpret `index` as a signed two's-complement low bit and select one plane.
/// Arbitrary-width indices are classified against the input bounds before any
/// narrowing to a host integer.
static APInt dynamicExtractPlane(const APInt &input, const APInt &index,
                                 unsigned resultWidth, bool invalidOne) {
  APInt result(resultWidth, 0);
  if (invalidOne)
    result.setAllBits();

  if (index.isNegative()) {
    APInt magnitude = -index;
    if (magnitude.uge(resultWidth))
      return result;
    if (!magnitude.isIntN(64))
      return result;
    uint64_t skipped = magnitude.getZExtValue();
    for (uint64_t resultBit = skipped; resultBit < resultWidth; ++resultBit) {
      if (input[resultBit - skipped])
        result.setBit(resultBit);
      else
        result.clearBit(resultBit);
    }
    return result;
  }

  if (index.uge(input.getBitWidth()))
    return result;
  if (!index.isIntN(64))
    return result;
  uint64_t low = index.getZExtValue();
  for (uint64_t resultBit = 0; resultBit < resultWidth; ++resultBit) {
    uint64_t inputBit = low + resultBit;
    if (inputBit >= input.getBitWidth())
      break;
    if (input[inputBit])
      result.setBit(resultBit);
    else
      result.clearBit(resultBit);
  }
  return result;
}

static LogicPlanes dynamicExtract(LogicPlanes input, const APInt &index,
                                  unsigned resultWidth) {
  return {dynamicExtractPlane(input.value, index, resultWidth, false),
          dynamicExtractPlane(input.unknown, index, resultWidth, true)};
}

static bool isKnownInRangeIndex(const APInt &index, uint64_t inputWidth,
                                uint64_t resultWidth, uint64_t &low) {
  if (index.isNegative() || index.uge(inputWidth))
    return false;
  if (!index.isIntN(64))
    return false;
  low = index.getZExtValue();
  return resultWidth <= inputWidth - low;
}

} // namespace

LogicalResult SimLogicConstantOp::verify() {
  unsigned width = getResult().getType().getWidth();
  if (getValue().getBitWidth() != width || getUnknown().getBitWidth() != width)
    return emitOpError("value and unknown planes must match result width");
  return success();
}
OpFoldResult SimLogicConstantOp::fold(FoldAdaptor adaptor) {
  return ArrayAttr::get(getContext(),
                        {adaptor.getValueAttr(), adaptor.getUnknownAttr()});
}

OpFoldResult SimLogicFromBitsOp::fold(FoldAdaptor adaptor) {
  auto input = dyn_cast_or_null<IntegerAttr>(adaptor.getInput());
  if (!input)
    return {};
  return getLogicAttribute(
      getContext(),
      {input.getValue(), APInt::getZero(input.getValue().getBitWidth())});
}

LogicalResult SimLogicFromBitsOp::verify() {
  if (!getInput().getType().isSignless())
    return emitOpError("input must be a signless builtin integer");
  if (getInput().getType().getWidth() != getResult().getType().getWidth())
    return emitOpError("input and result widths must match");
  return success();
}
LogicalResult SimLogicToBitsOp::verify() {
  if (!getResult().getType().isSignless())
    return emitOpError("result must be a signless builtin integer");
  if (getInput().getType().getWidth() != getResult().getType().getWidth())
    return emitOpError("input and result widths must match");
  return success();
}

OpFoldResult SimLogicToBitsOp::fold(FoldAdaptor adaptor) {
  // to_bits(from_bits(x)) is x. The reverse is not an identity, because
  // from_bits discards the unknown plane it cannot represent.
  if (auto fromBits = getInput().getDefiningOp<SimLogicFromBitsOp>())
    return fromBits.getInput();
  auto planes = dyn_cast_or_null<ArrayAttr>(adaptor.getInput());
  if (!planes || planes.size() != 2)
    return {};
  auto value = dyn_cast<IntegerAttr>(planes[0]);
  auto unknown = dyn_cast<IntegerAttr>(planes[1]);
  if (!value || !unknown)
    return {};
  APInt converted = value.getValue() & ~unknown.getValue();
  return IntegerAttr::get(getResult().getType(), converted);
}

LogicalResult SimLogicCountBitsOp::verify() {
  if (getControls().empty())
    return emitOpError("requires at least one state control");
  std::optional<uint64_t> width;
  if (auto packed = getPackedWidth(getInput().getType()))
    width = *packed;
  else
    width = getProvenanceSpan(getInput().getType());
  if (!width || *width == 0)
    return emitOpError("input must be a nonempty fixed bitstream value");
  return success();
}

OpFoldResult SimLogicCountBitsOp::fold(FoldAdaptor adaptor) {
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};

  bool selected[4] = {};
  for (Attribute attribute : adaptor.getControls()) {
    auto control = getLogicPlanes(attribute);
    if (!control)
      return {};
    unsigned state =
        (control->unknown[0] ? 2u : 0u) | (control->value[0] ? 1u : 0u);
    selected[state] = true;
  }

  APInt value = input->value;
  APInt unknown = input->unknown;
  APInt known = ~unknown;
  APInt matches = APInt::getZero(value.getBitWidth());
  if (selected[0])
    matches |= ~value & known;
  if (selected[1])
    matches |= value & known;
  if (selected[2])
    matches |= ~value & unknown;
  if (selected[3])
    matches |= value & unknown;
  return IntegerAttr::get(IntegerType::get(getContext(), 32),
                          APInt(32, matches.popcount()));
}

OpFoldResult SimLogicClog2Op::fold(FoldAdaptor adaptor) {
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  APInt value = input->value & ~input->unknown;
  uint64_t result = value.isZero() ? 0 : (value - 1).getActiveBits();
  return IntegerAttr::get(IntegerType::get(getContext(), 32),
                          APInt(32, result));
}

OpFoldResult SimLogicIsTrueOp::fold(FoldAdaptor adaptor) {
  auto planes = dyn_cast_or_null<ArrayAttr>(adaptor.getInput());
  if (!planes || planes.size() != 2)
    return {};
  auto value = dyn_cast<IntegerAttr>(planes[0]);
  auto unknown = dyn_cast<IntegerAttr>(planes[1]);
  if (!value || !unknown)
    return {};
  bool isTrue = !(value.getValue() & ~unknown.getValue()).isZero();
  return IntegerAttr::get(getResult().getType(), isTrue ? 1 : 0);
}

OpFoldResult SimLogicMuxOp::fold(FoldAdaptor adaptor) {
  if (getTrueValue() == getFalseValue())
    return getTrueValue();
  auto condition = getLogicPlanes(adaptor.getCondition());
  if (!condition)
    return {};
  if (condition->unknown.isZero())
    return condition->value.isZero() ? OpFoldResult(getFalseValue())
                                     : OpFoldResult(getTrueValue());
  auto trueValue = getLogicPlanes(adaptor.getTrueValue());
  auto falseValue = getLogicPlanes(adaptor.getFalseValue());
  if (!trueValue || !falseValue)
    return {};
  APInt mismatch = (trueValue->value ^ falseValue->value) |
                   (trueValue->unknown ^ falseValue->unknown);
  LogicPlanes result{trueValue->value & ~mismatch,
                     trueValue->unknown | mismatch};
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicResizeOp::fold(FoldAdaptor adaptor) {
  if (getInput().getType() == getResult().getType())
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  unsigned width = getResult().getType().getWidth();
  LogicPlanes result = getIsSigned()
                           ? LogicPlanes{input->value.sextOrTrunc(width),
                                         input->unknown.sextOrTrunc(width)}
                           : LogicPlanes{input->value.zextOrTrunc(width),
                                         input->unknown.zextOrTrunc(width)};
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicUnaryOp::fold(FoldAdaptor adaptor) {
  if (getKind() == UnaryKind::Plus)
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};

  LogicPlanes result = *input;
  switch (getKind()) {
  case UnaryKind::Plus:
    llvm_unreachable("unary plus folded above");
  case UnaryKind::Negate:
    result = input->unknown.isZero()
                 ? LogicPlanes{-input->value,
                               APInt::getZero(input->value.getBitWidth())}
                 : getCanonicalUnknown(input->value.getBitWidth());
    break;
  case UnaryKind::BitNot:
    result = {~input->value & ~input->unknown, input->unknown};
    break;
  case UnaryKind::LogicalNot: {
    TruthState truth = getTruth(*input);
    result = getLogicBoolean(!truth.value && !truth.unknown, truth.unknown);
    break;
  }
  }
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicReductionOp::fold(FoldAdaptor adaptor) {
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};

  bool hasUnknown = !input->unknown.isZero();
  bool value = false;
  bool unknown = false;
  bool invert = getKind() == ReductionKind::Nand ||
                getKind() == ReductionKind::Nor ||
                getKind() == ReductionKind::Xnor;
  if (getKind() == ReductionKind::And || getKind() == ReductionKind::Nand) {
    bool hasKnownZero = !(~input->value & ~input->unknown).isZero();
    unknown = !hasKnownZero && hasUnknown;
    value = !hasKnownZero && !hasUnknown;
  } else if (getKind() == ReductionKind::Or ||
             getKind() == ReductionKind::Nor) {
    bool hasKnownOne = !(input->value & ~input->unknown).isZero();
    unknown = !hasKnownOne && hasUnknown;
    value = hasKnownOne;
  } else {
    unknown = hasUnknown;
    value = !hasUnknown && input->value.popcount() % 2;
  }
  if (invert && !unknown)
    value = !value;
  return getLogicAttribute(getContext(), getLogicBoolean(value, unknown));
}

LogicalResult SimLogicUnaryOp::verify() {
  if (getKind() == UnaryKind::LogicalNot) {
    if (getResult().getType().getWidth() != 1)
      return emitOpError("logical negation must produce !obelisk_sim.logic<1>");
  } else if (getInput().getType() != getResult().getType()) {
    return emitOpError("width-preserving unary operations require matching "
                       "input and result types");
  }
  return success();
}

OpFoldResult SimLogicBinaryOp::fold(FoldAdaptor adaptor) {
  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());

  // These are controlling values, not identities: unlike `x & all_ones` and
  // `x | zero`, they are insensitive to whether unknown bits encode X or Z.
  auto isKnownZero = [](const std::optional<LogicPlanes> &planes) {
    return planes && planes->unknown.isZero() && planes->value.isZero();
  };
  auto isKnownOnes = [](const std::optional<LogicPlanes> &planes) {
    return planes && planes->unknown.isZero() && planes->value.isAllOnes();
  };
  unsigned width = getResult().getType().getWidth();
  if (getKind() == BinaryKind::And && (isKnownZero(lhs) || isKnownZero(rhs)))
    return getLogicAttribute(getContext(),
                             {APInt::getZero(width), APInt::getZero(width)});
  if (getKind() == BinaryKind::Or && (isKnownOnes(lhs) || isKnownOnes(rhs)))
    return getLogicAttribute(getContext(),
                             {APInt::getAllOnes(width), APInt::getZero(width)});
  if (!lhs || !rhs)
    return {};

  LogicPlanes result{APInt::getZero(width), APInt::getZero(width)};
  if (getKind() == BinaryKind::And || getKind() == BinaryKind::Or ||
      getKind() == BinaryKind::Xor || getKind() == BinaryKind::Xnor) {
    APInt lhsKnown = ~lhs->unknown;
    APInt rhsKnown = ~rhs->unknown;
    if (getKind() == BinaryKind::And) {
      APInt knownZero = (~lhs->value & lhsKnown) | (~rhs->value & rhsKnown);
      APInt knownOne = (lhs->value & lhsKnown) & (rhs->value & rhsKnown);
      result = {knownOne, ~(knownZero | knownOne)};
    } else if (getKind() == BinaryKind::Or) {
      APInt knownOne = (lhs->value & lhsKnown) | (rhs->value & rhsKnown);
      APInt knownZero = (~lhs->value & lhsKnown) & (~rhs->value & rhsKnown);
      result = {knownOne, ~(knownZero | knownOne)};
    } else {
      result.unknown = lhs->unknown | rhs->unknown;
      APInt computed = lhs->value ^ rhs->value;
      if (getKind() == BinaryKind::Xnor)
        computed = ~computed;
      result.value = computed & ~result.unknown;
    }
    return getLogicAttribute(getContext(), std::move(result));
  }

  if (!lhs->unknown.isZero() || !rhs->unknown.isZero())
    return getLogicAttribute(getContext(), getCanonicalUnknown(width));

  bool invalid = false;
  switch (getKind()) {
  case BinaryKind::Add:
    result.value = lhs->value + rhs->value;
    break;
  case BinaryKind::Sub:
    result.value = lhs->value - rhs->value;
    break;
  case BinaryKind::Mul:
    result.value = lhs->value * rhs->value;
    break;
  case BinaryKind::UDiv:
  case BinaryKind::SDiv:
  case BinaryKind::UMod:
  case BinaryKind::SMod: {
    if (rhs->value.isZero()) {
      invalid = true;
      break;
    }
    bool isSigned =
        getKind() == BinaryKind::SDiv || getKind() == BinaryKind::SMod;
    bool isRemainder =
        getKind() == BinaryKind::UMod || getKind() == BinaryKind::SMod;
    bool overflow =
        isSigned && lhs->value.isMinSignedValue() && rhs->value.isAllOnes();
    if (overflow) {
      result.value = isRemainder ? APInt::getZero(width) : lhs->value;
    } else if (isSigned && isRemainder) {
      result.value = lhs->value.srem(rhs->value);
    } else if (isSigned) {
      result.value = lhs->value.sdiv(rhs->value);
    } else if (isRemainder) {
      result.value = lhs->value.urem(rhs->value);
    } else {
      result.value = lhs->value.udiv(rhs->value);
    }
    break;
  }
  default:
    llvm_unreachable("bitwise binary kinds folded above");
  }
  if (invalid)
    result = getCanonicalUnknown(width);
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicLogicalOp::fold(FoldAdaptor adaptor) {
  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());

  // Only controlling truth constants are identities in the presence of Z.
  if (lhs) {
    TruthState truth = getTruth(*lhs);
    if (getKind() == LogicalKind::And && !truth.value && !truth.unknown)
      return getLogicAttribute(getContext(), getLogicBoolean(false));
    if (getKind() == LogicalKind::Or && truth.value)
      return getLogicAttribute(getContext(), getLogicBoolean(true));
  }
  if (rhs) {
    TruthState truth = getTruth(*rhs);
    if (getKind() == LogicalKind::And && !truth.value && !truth.unknown)
      return getLogicAttribute(getContext(), getLogicBoolean(false));
    if (getKind() == LogicalKind::Or && truth.value)
      return getLogicAttribute(getContext(), getLogicBoolean(true));
  }
  if (!lhs || !rhs)
    return {};

  TruthState lhsTruth = getTruth(*lhs);
  TruthState rhsTruth = getTruth(*rhs);
  bool value;
  bool unknown;
  if (getKind() == LogicalKind::And) {
    value = lhsTruth.value && rhsTruth.value;
    bool knownFalse = (!lhsTruth.value && !lhsTruth.unknown) ||
                      (!rhsTruth.value && !rhsTruth.unknown);
    unknown = !knownFalse && !value;
  } else {
    value = lhsTruth.value || rhsTruth.value;
    bool knownFalse = !lhsTruth.value && !lhsTruth.unknown && !rhsTruth.value &&
                      !rhsTruth.unknown;
    unknown = !knownFalse && !value;
  }
  return getLogicAttribute(getContext(), getLogicBoolean(value, unknown));
}

OpFoldResult SimLogicShiftOp::fold(FoldAdaptor adaptor) {
  auto amount = getConstantIndex(adaptor.getAmount());
  if (!amount)
    return {};
  unsigned width = getInput().getType().getWidth();
  if (!amount->value)
    return getLogicAttribute(getContext(), getCanonicalUnknown(width));
  if (amount->value->isZero())
    return getInput();

  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  if (amount->value->uge(width)) {
    if (getKind() == ShiftKind::RightArith)
      return getLogicAttribute(getContext(), {input->value.ashr(width - 1),
                                              input->unknown.ashr(width - 1)});
    return getLogicAttribute(getContext(),
                             {APInt::getZero(width), APInt::getZero(width)});
  }

  uint64_t shift = amount->value->getZExtValue();
  LogicPlanes result = *input;
  if (getKind() == ShiftKind::Left)
    result = {input->value.shl(shift), input->unknown.shl(shift)};
  else if (getKind() == ShiftKind::Right)
    result = {input->value.lshr(shift), input->unknown.lshr(shift)};
  else
    result = {input->value.ashr(shift), input->unknown.ashr(shift)};
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicCompareOp::fold(FoldAdaptor adaptor) {
  bool integerResult =
      getKind() == CompareKind::CaseEq || getKind() == CompareKind::CaseNe ||
      getKind() == CompareKind::CaseZEq || getKind() == CompareKind::CaseXZEq;
  bool deterministic = integerResult;
  if (deterministic && getLhs() == getRhs()) {
    bool equal =
        getKind() != CompareKind::CaseNe && getKind() != CompareKind::WildNe;
    if (integerResult)
      return IntegerAttr::get(getResult().getType(), equal);
    return getLogicAttribute(getContext(), getLogicBoolean(equal));
  }

  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());
  if (!lhs || !rhs)
    return {};
  if (getKind() == CompareKind::CaseEq || getKind() == CompareKind::CaseNe) {
    bool equal = lhs->value == rhs->value && lhs->unknown == rhs->unknown;
    if (getKind() == CompareKind::CaseNe)
      equal = !equal;
    return IntegerAttr::get(getResult().getType(), equal);
  }
  if (getKind() == CompareKind::WildEq || getKind() == CompareKind::WildNe ||
      getKind() == CompareKind::CaseZEq || getKind() == CompareKind::CaseXZEq) {
    APInt wildcard = APInt::getZero(lhs->value.getBitWidth());
    if (getKind() == CompareKind::WildEq || getKind() == CompareKind::WildNe)
      wildcard = rhs->unknown;
    else if (getKind() == CompareKind::CaseZEq)
      wildcard = (lhs->unknown & lhs->value) | (rhs->unknown & rhs->value);
    else
      wildcard = lhs->unknown | rhs->unknown;
    APInt mismatch;
    APInt relevantUnknown = APInt::getZero(lhs->value.getBitWidth());
    if (getKind() == CompareKind::WildEq || getKind() == CompareKind::WildNe) {
      APInt compared = ~rhs->unknown;
      mismatch = (lhs->value ^ rhs->value) & ~lhs->unknown & compared;
      relevantUnknown = lhs->unknown & compared;
    } else {
      mismatch = ((lhs->value ^ rhs->value) | (lhs->unknown ^ rhs->unknown)) &
                 ~wildcard;
    }
    bool equal = mismatch.isZero() && relevantUnknown.isZero();
    bool unknown = mismatch.isZero() && !relevantUnknown.isZero();
    if (getKind() == CompareKind::WildNe && !unknown)
      equal = !equal;
    if (integerResult)
      return IntegerAttr::get(getResult().getType(), equal);
    return getLogicAttribute(getContext(), getLogicBoolean(equal, unknown));
  }
  if (getKind() == CompareKind::Eq || getKind() == CompareKind::Ne) {
    APInt knownMask = ~(lhs->unknown | rhs->unknown);
    bool knownMismatch =
        !((lhs->value ^ rhs->value) & knownMask).isZero();
    if (knownMismatch)
      return getLogicAttribute(
          getContext(), getLogicBoolean(getKind() == CompareKind::Ne));
    if (!lhs->unknown.isZero() || !rhs->unknown.isZero())
      return getLogicAttribute(getContext(), getLogicBoolean(false, true));
    return getLogicAttribute(
        getContext(), getLogicBoolean(getKind() == CompareKind::Eq));
  }
  if (!lhs->unknown.isZero() || !rhs->unknown.isZero())
    return getLogicAttribute(getContext(), getLogicBoolean(false, true));

  bool result;
  switch (getKind()) {
  case CompareKind::Eq:
    result = lhs->value == rhs->value;
    break;
  case CompareKind::Ne:
    result = lhs->value != rhs->value;
    break;
  case CompareKind::ULT:
    result = lhs->value.ult(rhs->value);
    break;
  case CompareKind::ULE:
    result = lhs->value.ule(rhs->value);
    break;
  case CompareKind::UGT:
    result = lhs->value.ugt(rhs->value);
    break;
  case CompareKind::UGE:
    result = lhs->value.uge(rhs->value);
    break;
  case CompareKind::SLT:
    result = lhs->value.slt(rhs->value);
    break;
  case CompareKind::SLE:
    result = lhs->value.sle(rhs->value);
    break;
  case CompareKind::SGT:
    result = lhs->value.sgt(rhs->value);
    break;
  case CompareKind::SGE:
    result = lhs->value.sge(rhs->value);
    break;
  default:
    llvm_unreachable("case comparisons folded above");
  }
  return getLogicAttribute(getContext(), getLogicBoolean(result));
}

LogicalResult SimLogicCompareOp::verify() {
  Type result = getResult().getType();
  bool caseComparison =
      getKind() == CompareKind::CaseEq || getKind() == CompareKind::CaseNe ||
      getKind() == CompareKind::CaseZEq || getKind() == CompareKind::CaseXZEq;
  if (caseComparison && !result.isSignlessInteger(1))
    return emitOpError("case comparisons must produce i1");
  if (!caseComparison && !isa<LogicType>(result))
    return emitOpError(
        "four-state comparisons must produce !obelisk_sim.logic<1>");
  if (auto logic = dyn_cast<LogicType>(result); logic && logic.getWidth() != 1)
    return emitOpError("comparison result logic width must be one");
  return success();
}
LogicalResult SimLogicShiftOp::verify() {
  if (!isa<IntegerType, LogicType>(getAmount().getType()))
    return emitOpError("shift amount must be an integer or four-state logic");
  return success();
}

OpFoldResult SimLogicConcatOp::fold(FoldAdaptor adaptor) {
  if (getInputs().size() == 1)
    return getInputs().front();
  unsigned resultWidth = getResult().getType().getWidth();
  LogicPlanes result{APInt::getZero(resultWidth), APInt::getZero(resultWidth)};
  uint64_t offset = resultWidth;
  for (Attribute attribute : adaptor.getInputs()) {
    auto input = getLogicPlanes(attribute);
    if (!input)
      return {};
    offset -= input->value.getBitWidth();
    result.value |= input->value.zextOrTrunc(resultWidth).shl(offset);
    result.unknown |= input->unknown.zextOrTrunc(resultWidth).shl(offset);
  }
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicReplicateOp::fold(FoldAdaptor adaptor) {
  if (getCount() == 1)
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  unsigned resultWidth = getResult().getType().getWidth();
  unsigned inputWidth = input->value.getBitWidth();
  LogicPlanes result{APInt::getZero(resultWidth), APInt::getZero(resultWidth)};
  LogicPlanes chunk{input->value.zextOrTrunc(resultWidth),
                    input->unknown.zextOrTrunc(resultWidth)};
  uint64_t remaining = static_cast<uint64_t>(getCount());
  uint64_t chunkCopies = 1;
  uint64_t placedCopies = 0;
  while (remaining != 0) {
    if (remaining & 1) {
      uint64_t offset = placedCopies * static_cast<uint64_t>(inputWidth);
      result.value |= chunk.value.shl(offset);
      result.unknown |= chunk.unknown.shl(offset);
      placedCopies += chunkCopies;
    }
    remaining >>= 1;
    if (remaining == 0)
      break;
    uint64_t chunkWidth = chunkCopies * static_cast<uint64_t>(inputWidth);
    chunk.value |= chunk.value.shl(chunkWidth);
    chunk.unknown |= chunk.unknown.shl(chunkWidth);
    chunkCopies <<= 1;
  }
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicExtractOp::fold(FoldAdaptor adaptor) {
  uint64_t low = getLowBit();
  unsigned resultWidth = getResult().getType().getWidth();
  if (low == 0 && resultWidth == getInput().getType().getWidth())
    return getInput();
  if (auto insert = getInput().getDefiningOp<SimLogicInsertOp>();
      insert && insert.getLowBit() == low &&
      insert.getReplacement().getType().getWidth() == resultWidth)
    return insert.getReplacement();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  return getLogicAttribute(getContext(),
                           {input->value.lshr(low).trunc(resultWidth),
                            input->unknown.lshr(low).trunc(resultWidth)});
}

OpFoldResult SimLogicDynExtractOp::fold(FoldAdaptor adaptor) {
  auto index = getConstantIndex(adaptor.getLowBit());
  if (!index)
    return {};
  unsigned resultWidth = getResult().getType().getWidth();
  if (!index->value)
    return getLogicAttribute(getContext(), getCanonicalUnknown(resultWidth));
  if (index->value->isZero() && resultWidth == getInput().getType().getWidth())
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  return getLogicAttribute(getContext(),
                           dynamicExtract(*input, *index->value, resultWidth));
}

OpFoldResult SimBitsDynExtractOp::fold(FoldAdaptor adaptor) {
  auto index = getConstantIndex(adaptor.getLowBit());
  if (!index)
    return {};
  unsigned resultWidth = getResult().getType().getWidth();
  if (!index->value)
    return IntegerAttr::get(getResult().getType(), APInt::getZero(resultWidth));
  if (index->value->isZero() && resultWidth == getInput().getType().getWidth())
    return getInput();
  auto input = dyn_cast_or_null<IntegerAttr>(adaptor.getInput());
  if (!input)
    return {};
  APInt result =
      dynamicExtractPlane(input.getValue(), *index->value, resultWidth, false);
  return IntegerAttr::get(getResult().getType(), result);
}

OpFoldResult SimLogicInsertOp::fold(FoldAdaptor adaptor) {
  uint64_t low = getLowBit();
  unsigned width = getResult().getType().getWidth();
  unsigned replacementWidth = getReplacement().getType().getWidth();
  if (low == 0 && replacementWidth == width)
    return getReplacement();
  if (auto extract = getReplacement().getDefiningOp<SimLogicExtractOp>();
      extract && extract.getInput() == getInput() &&
      extract.getLowBit() == low &&
      extract.getResult().getType().getWidth() == replacementWidth)
    return getInput();

  auto input = getLogicPlanes(adaptor.getInput());
  auto replacement = getLogicPlanes(adaptor.getReplacement());
  if (!input || !replacement)
    return {};
  APInt mask = APInt::getLowBitsSet(width, replacementWidth).shl(low);
  auto insertPlane = [&](const APInt &base, const APInt &piece) {
    return (base & ~mask) | piece.zextOrTrunc(width).shl(low);
  };
  return getLogicAttribute(getContext(),
                           {insertPlane(input->value, replacement->value),
                            insertPlane(input->unknown, replacement->unknown)});
}

LogicalResult SimLogicConcatOp::verify() {
  if (getInputs().empty())
    return emitOpError("requires at least one input");
  uint64_t width = 0;
  for (Value input : getInputs())
    width += cast<LogicType>(input.getType()).getWidth();
  if (width != getResult().getType().getWidth())
    return emitOpError("result width must equal the sum of input widths");
  return success();
}
LogicalResult SimLogicReplicateOp::verify() {
  if (getCount() <= 0)
    return emitOpError("replication count must be positive");
  uint64_t count = static_cast<uint64_t>(getCount());
  uint64_t inputWidth = getInput().getType().getWidth();
  if (count > std::numeric_limits<uint64_t>::max() / inputWidth)
    return emitOpError("replication width overflows uint64_t");
  uint64_t expected = count * inputWidth;
  if (expected > std::numeric_limits<unsigned>::max())
    return emitOpError("replication width exceeds the supported type width");
  if (expected != getResult().getType().getWidth())
    return emitOpError("result width must equal input width times count");
  return success();
}
LogicalResult SimLogicExtractOp::verify() {
  if (getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() +
              getResult().getType().getWidth() >
          getInput().getType().getWidth())
    return emitOpError("constant selection is outside the input width");
  return success();
}
LogicalResult SimLogicDynExtractOp::verify() {
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())))
    return failure();
  if (getResult().getType().getWidth() > getInput().getType().getWidth())
    return emitOpError("result width exceeds input width");
  return success();
}
LogicalResult SimBitsDynExtractOp::verify() {
  if (!getInput().getType().isSignless() || !getResult().getType().isSignless())
    return emitOpError("input and result must be signless builtin integers");
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())))
    return failure();
  if (getResult().getType().getWidth() > getInput().getType().getWidth())
    return emitOpError("result width exceeds input width");
  return success();
}
LogicalResult SimLogicInsertOp::verify() {
  if (getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() +
              getReplacement().getType().getWidth() >
          getInput().getType().getWidth())
    return emitOpError("replacement is outside the input width");
  return success();
}

namespace {

static std::optional<uint64_t> getSelectionWidth(Type type) {
  if (auto logic = dyn_cast<LogicType>(type))
    return logic.getWidth();
  if (auto reference = dyn_cast<RefType>(type))
    return getPackedWidth(reference.getElementType());
  if (auto driver = dyn_cast<DriverType>(type))
    return getPackedWidth(driver.getElementType());
  return std::nullopt;
}

template <typename OldOp, typename NewOp>
static void replaceWithNewOp(PatternRewriter &rewriter, OldOp oldOp,
                             NewOp newOp) {
  for (NamedAttribute attribute : oldOp->getDiscardableAttrDictionary())
    newOp->setAttr(attribute.getName(), attribute.getValue());
  rewriter.replaceOp(oldOp, newOp.getResult());
}

struct CollapseResizeChain final : OpRewritePattern<SimLogicResizeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicResizeOp op,
                                PatternRewriter &rewriter) const override {
    auto inner = op.getInput().getDefiningOp<SimLogicResizeOp>();
    if (!inner)
      return failure();

    unsigned sourceWidth = inner.getInput().getType().getWidth();
    unsigned innerWidth = inner.getResult().getType().getWidth();
    unsigned resultWidth = op.getResult().getType().getWidth();
    bool signedResult;
    if (resultWidth <= std::min(sourceWidth, innerWidth)) {
      // Both resizes only contribute a low-bit truncation.
      signedResult = false;
    } else if (innerWidth >= sourceWidth && resultWidth <= innerWidth) {
      // The result observes the extension performed by the inner resize.
      signedResult = inner.getIsSigned();
    } else if (innerWidth >= sourceWidth && resultWidth > innerWidth &&
               (!inner.getIsSigned() || op.getIsSigned())) {
      // Repeated zero-extension, or repeated sign-extension, composes.
      signedResult = inner.getIsSigned();
    } else {
      return failure();
    }

    auto replacement = SimLogicResizeOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), inner.getInput(),
        rewriter.getBoolAttr(signedResult));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

static bool isConstantValue(Value value) {
  Attribute attribute;
  return matchPattern(value, m_Constant(&attribute));
}

struct NormalizeBinaryConstant final : OpRewritePattern<SimLogicBinaryOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicBinaryOp op,
                                PatternRewriter &rewriter) const override {
    if (!isConstantValue(op.getLhs()) || isConstantValue(op.getRhs()))
      return failure();
    switch (op.getKind()) {
    case BinaryKind::Add:
    case BinaryKind::Mul:
    case BinaryKind::And:
    case BinaryKind::Or:
    case BinaryKind::Xor:
    case BinaryKind::Xnor:
      break;
    default:
      return failure();
    }
    rewriter.modifyOpInPlace(op, [&] {
      SmallVector<Value, 2> operands{op.getRhs(), op.getLhs()};
      op->setOperands(operands);
    });
    return success();
  }
};

struct NormalizeCompareConstant final : OpRewritePattern<SimLogicCompareOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicCompareOp op,
                                PatternRewriter &rewriter) const override {
    if (!isConstantValue(op.getLhs()) || isConstantValue(op.getRhs()))
      return failure();
    CompareKind kind = op.getKind();
    switch (op.getKind()) {
    case CompareKind::Eq:
    case CompareKind::Ne:
    case CompareKind::CaseEq:
    case CompareKind::CaseNe:
    case CompareKind::CaseZEq:
    case CompareKind::CaseXZEq:
      kind = op.getKind();
      break;
    case CompareKind::WildEq:
    case CompareKind::WildNe:
      return failure();
    case CompareKind::ULT:
      kind = CompareKind::UGT;
      break;
    case CompareKind::ULE:
      kind = CompareKind::UGE;
      break;
    case CompareKind::UGT:
      kind = CompareKind::ULT;
      break;
    case CompareKind::UGE:
      kind = CompareKind::ULE;
      break;
    case CompareKind::SLT:
      kind = CompareKind::SGT;
      break;
    case CompareKind::SLE:
      kind = CompareKind::SGE;
      break;
    case CompareKind::SGT:
      kind = CompareKind::SLT;
      break;
    case CompareKind::SGE:
      kind = CompareKind::SLE;
      break;
    }
    rewriter.modifyOpInPlace(op, [&] {
      SmallVector<Value, 2> operands{op.getRhs(), op.getLhs()};
      op->setOperands(operands);
      op.setKind(kind);
    });
    return success();
  }
};

struct FlattenConcat final : OpRewritePattern<SimLogicConcatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicConcatOp op,
                                PatternRewriter &rewriter) const override {
    SmallVector<Value> inputs;
    bool changed = false;
    for (Value input : op.getInputs()) {
      if (auto nested = input.getDefiningOp<SimLogicConcatOp>()) {
        inputs.append(nested.getInputs().begin(), nested.getInputs().end());
        changed = true;
      } else {
        inputs.push_back(input);
      }
    }
    if (!changed)
      return failure();
    auto replacement = SimLogicConcatOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), inputs);
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct ReplicateRepeatedConcatInput final : OpRewritePattern<SimLogicConcatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicConcatOp op,
                                PatternRewriter &rewriter) const override {
    if (op.getInputs().size() < 2)
      return failure();
    Value input = op.getInputs().front();
    if (!llvm::all_of(op.getInputs(),
                      [input](Value value) { return value == input; }))
      return failure();
    auto replacement = SimLogicReplicateOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), input,
        rewriter.getI64IntegerAttr(op.getInputs().size()));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct MergeAdjacentConcatExtracts final : OpRewritePattern<SimLogicConcatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicConcatOp op,
                                PatternRewriter &rewriter) const override {
    SmallVector<Value> inputs;
    bool changed = false;
    for (Value input : op.getInputs()) {
      auto right = input.getDefiningOp<SimLogicExtractOp>();
      auto left = inputs.empty()
                      ? SimLogicExtractOp{}
                      : inputs.back().getDefiningOp<SimLogicExtractOp>();
      if (!left || !right || left.getInput() != right.getInput() ||
          left.getLowBit() !=
              right.getLowBit() + right.getResult().getType().getWidth()) {
        inputs.push_back(input);
        continue;
      }
      uint64_t width = left.getResult().getType().getWidth() +
                       right.getResult().getType().getWidth();
      auto type = LogicType::get(op.getContext(), width);
      auto merged = SimLogicExtractOp::create(
          rewriter, op.getLoc(), type, right.getInput(),
          rewriter.getI64IntegerAttr(right.getLowBit()));
      inputs.back() = merged.getResult();
      changed = true;
    }
    if (!changed)
      return failure();
    if (inputs.size() == 1) {
      for (NamedAttribute attribute : op->getDiscardableAttrDictionary())
        inputs.front().getDefiningOp()->setAttr(attribute.getName(),
                                                attribute.getValue());
      rewriter.replaceOp(op, inputs.front());
      return success();
    }
    auto replacement = SimLogicConcatOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), inputs);
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct CombineReplication final : OpRewritePattern<SimLogicReplicateOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicReplicateOp op,
                                PatternRewriter &rewriter) const override {
    auto nested = op.getInput().getDefiningOp<SimLogicReplicateOp>();
    if (!nested)
      return failure();
    uint64_t innerCount = nested.getCount();
    uint64_t outerCount = op.getCount();
    if (outerCount != 0 &&
        innerCount > std::numeric_limits<uint64_t>::max() / outerCount)
      return failure();
    uint64_t count = innerCount * outerCount;
    if (count > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return failure();
    auto replacement = SimLogicReplicateOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), nested.getInput(),
        rewriter.getI64IntegerAttr(count));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

template <typename ExtractOp>
struct SimplifyStaticExtract final : OpRewritePattern<ExtractOp> {
  using OpRewritePattern<ExtractOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(ExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto inputWidth = getSelectionWidth(op.getInput().getType());
    auto resultWidth = getSelectionWidth(op.getResult().getType());
    if (!inputWidth || !resultWidth)
      return failure();
    if (op.getLowBit() == 0 && *inputWidth == *resultWidth) {
      rewriter.replaceOp(op, op.getInput());
      return success();
    }

    auto nested = op.getInput().template getDefiningOp<ExtractOp>();
    if (!nested)
      return failure();
    uint64_t innerLow = nested.getLowBit();
    uint64_t outerLow = op.getLowBit();
    if (innerLow > std::numeric_limits<uint64_t>::max() - outerLow)
      return failure();
    uint64_t low = innerLow + outerLow;
    if (low > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return failure();
    auto replacement =
        ExtractOp::create(rewriter, op.getLoc(), op.getResult().getType(),
                          nested.getInput(), rewriter.getI64IntegerAttr(low));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct SimplifyLogicExtractSource final : OpRewritePattern<SimLogicExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicExtractOp op,
                                PatternRewriter &rewriter) const override {
    uint64_t low = op.getLowBit();
    uint64_t width = op.getResult().getType().getWidth();
    uint64_t high = low + width;

    if (auto insert = op.getInput().getDefiningOp<SimLogicInsertOp>()) {
      uint64_t insertLow = insert.getLowBit();
      uint64_t insertWidth = insert.getReplacement().getType().getWidth();
      uint64_t insertHigh = insertLow + insertWidth;
      Value source;
      uint64_t sourceLow;
      if (high <= insertLow || low >= insertHigh) {
        source = insert.getInput();
        sourceLow = low;
      } else if (low >= insertLow && high <= insertHigh) {
        source = insert.getReplacement();
        sourceLow = low - insertLow;
      } else {
        return failure();
      }
      auto replacement = SimLogicExtractOp::create(
          rewriter, op.getLoc(), op.getResult().getType(), source,
          rewriter.getI64IntegerAttr(sourceLow));
      replaceWithNewOp(rewriter, op, replacement);
      return success();
    }

    if (auto replicate = op.getInput().getDefiningOp<SimLogicReplicateOp>()) {
      uint64_t inputWidth = replicate.getInput().getType().getWidth();
      if (low / inputWidth == (high - 1) / inputWidth) {
        auto replacement = SimLogicExtractOp::create(
            rewriter, op.getLoc(), op.getResult().getType(),
            replicate.getInput(), rewriter.getI64IntegerAttr(low % inputWidth));
        replaceWithNewOp(rewriter, op, replacement);
        return success();
      }
    }

    auto concat = op.getInput().getDefiningOp<SimLogicConcatOp>();
    if (!concat)
      return failure();
    uint64_t inputLow = 0;
    for (Value input : llvm::reverse(concat.getInputs())) {
      uint64_t inputWidth = cast<LogicType>(input.getType()).getWidth();
      uint64_t inputHigh = inputLow + inputWidth;
      if (low >= inputLow && high <= inputHigh) {
        auto replacement = SimLogicExtractOp::create(
            rewriter, op.getLoc(), op.getResult().getType(), input,
            rewriter.getI64IntegerAttr(low - inputLow));
        replaceWithNewOp(rewriter, op, replacement);
        return success();
      }
      inputLow = inputHigh;
    }
    return failure();
  }
};

template <typename DynamicOp, typename StaticOp>
struct ConstantDynamicExtract final : OpRewritePattern<DynamicOp> {
  using OpRewritePattern<DynamicOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(DynamicOp op,
                                PatternRewriter &rewriter) const override {
    auto index = getConstantIndex(op.getLowBit());
    if (!index || !index->value)
      return failure();
    auto inputWidth = getSelectionWidth(op.getInput().getType());
    auto resultWidth = getSelectionWidth(op.getResult().getType());
    if (!inputWidth || !resultWidth)
      return failure();
    uint64_t low;
    if (!isKnownInRangeIndex(*index->value, *inputWidth, *resultWidth, low) ||
        low > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return failure();
    auto replacement =
        StaticOp::create(rewriter, op.getLoc(), op.getResult().getType(),
                         op.getInput(), rewriter.getI64IntegerAttr(low));
    for (NamedAttribute attribute : op->getDiscardableAttrDictionary())
      replacement->setAttr(attribute.getName(), attribute.getValue());
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

struct RemoveOverwrittenInsert final : OpRewritePattern<SimLogicInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicInsertOp op,
                                PatternRewriter &rewriter) const override {
    auto nested = op.getInput().getDefiningOp<SimLogicInsertOp>();
    if (!nested)
      return failure();
    uint64_t nestedLow = nested.getLowBit();
    uint64_t nestedHigh =
        nestedLow + nested.getReplacement().getType().getWidth();
    uint64_t outerLow = op.getLowBit();
    uint64_t outerHigh = outerLow + op.getReplacement().getType().getWidth();
    if (outerLow > nestedLow || outerHigh < nestedHigh)
      return failure();
    auto replacement = SimLogicInsertOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), nested.getInput(),
        op.getReplacement(), op.getLowBitAttr());
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

static std::optional<int64_t> getConstantSourceIndex(Value value,
                                                     bool &unknown) {
  unknown = false;
  std::optional<ConstantIndex> constant = getConstantIndex(value);
  if (!constant)
    return std::nullopt;
  if (!constant->value) {
    unknown = true;
    return std::nullopt;
  }
  if (!constant->value->isSignedIntN(64))
    return std::nullopt;
  return constant->value->getSExtValue();
}

struct SimplifyAggregateExtract final
    : OpRewritePattern<SimAggregateExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateExtractOp op,
                                PatternRewriter &rewriter) const override {
    unsigned index = op.getIndex();
    if (auto construct =
            op.getInput().getDefiningOp<SimAggregateConstructOp>()) {
      rewriter.replaceOp(op, construct.getElements()[index]);
      return success();
    }
    if (op.getInput().getDefiningOp<SimAggregateDefaultOp>()) {
      Value value = materializeDefaultValue(rewriter, op.getLoc(),
                                            op.getResult().getType());
      if (!value)
        return failure();
      rewriter.replaceOp(op, value);
      return success();
    }
    if (auto insert = op.getInput().getDefiningOp<SimAggregateInsertOp>()) {
      if (insert.getIndex() == index) {
        rewriter.replaceOp(op, insert.getReplacement());
        return success();
      }
      auto replacement = SimAggregateExtractOp::create(
          rewriter, op.getLoc(), op.getResult().getType(), insert.getInput(),
          op.getIndexAttr());
      rewriter.replaceOp(op, replacement.getResult());
      return success();
    }
    if (auto load = op.getInput().getDefiningOp<SimRefLoadOp>()) {
      Value replacement;
      {
        OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPoint(load);
        Type refType = RefType::get(op.getContext(), op.getResult().getType());
        auto view = SimRefSubelementOp::create(
            rewriter, op.getLoc(), refType, load.getReference(),
            rewriter.getDenseI64ArrayAttr({static_cast<int64_t>(index)}));
        replacement = SimRefLoadOp::create(
            rewriter, op.getLoc(), op.getResult().getType(), view.getResult());
      }
      rewriter.replaceOp(op, replacement);
      return success();
    }
    return failure();
  }
};

struct SimplifyPackedFlatten final : OpRewritePattern<SimPackedFlattenOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimPackedFlattenOp op,
                                PatternRewriter &rewriter) const override {
    auto inverse = op.getInput().getDefiningOp<SimPackedUnflattenOp>();
    if (!inverse || inverse.getInput().getType() != op.getResult().getType())
      return failure();
    rewriter.replaceOp(op, inverse.getInput());
    return success();
  }
};

struct SimplifyPackedUnflatten final : OpRewritePattern<SimPackedUnflattenOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimPackedUnflattenOp op,
                                PatternRewriter &rewriter) const override {
    auto inverse = op.getInput().getDefiningOp<SimPackedFlattenOp>();
    if (!inverse || inverse.getInput().getType() != op.getResult().getType())
      return failure();
    rewriter.replaceOp(op, inverse.getInput());
    return success();
  }
};

struct SimplifyUnionExtract final : OpRewritePattern<SimUnionExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimUnionExtractOp op,
                                PatternRewriter &rewriter) const override {
    unsigned index = op.getIndex();
    if (auto construct = op.getInput().getDefiningOp<SimUnionConstructOp>();
        construct && construct.getIndex() == index) {
      rewriter.replaceOp(op, construct.getValue());
      return success();
    }
    if (op.getInput().getDefiningOp<SimAggregateDefaultOp>()) {
      Type unionType = op.getInput().getType();
      if (auto packed = dyn_cast<PackedUnionType>(unionType)) {
        if (packed.getIsTagged() &&
            (containsFourStateLeaf(unionType) || index != 0))
          return failure();
      } else if (auto unpacked = dyn_cast<UnpackedUnionType>(unionType)) {
        if (unpacked.getIsTagged() || index != 0)
          return failure();
      }
      Value value = materializeDefaultValue(rewriter, op.getLoc(),
                                            op.getResult().getType());
      if (!value)
        return failure();
      rewriter.replaceOp(op, value);
      return success();
    }
    if (auto load = op.getInput().getDefiningOp<SimRefLoadOp>()) {
      Value replacement;
      {
        OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPoint(load);
        Type refType = RefType::get(op.getContext(), op.getResult().getType());
        auto view = SimRefSubelementOp::create(
            rewriter, op.getLoc(), refType, load.getReference(),
            rewriter.getDenseI64ArrayAttr({static_cast<int64_t>(index)}));
        replacement = SimRefLoadOp::create(
            rewriter, op.getLoc(), op.getResult().getType(), view.getResult());
      }
      rewriter.replaceOp(op, replacement);
      return success();
    }
    return failure();
  }
};

struct SimplifyAggregateConstruct final
    : OpRewritePattern<SimAggregateConstructOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateConstructOp op,
                                PatternRewriter &rewriter) const override {
    Value source;
    for (auto [index, element] : llvm::enumerate(op.getElements())) {
      auto extract = element.getDefiningOp<SimAggregateExtractOp>();
      if (!extract || extract.getIndex() != index ||
          (source && source != extract.getInput()))
        return failure();
      source = extract.getInput();
    }
    if (!source || source.getType() != op.getResult().getType())
      return failure();
    rewriter.replaceOp(op, source);
    return success();
  }
};

struct SimplifyAggregateInsert final : OpRewritePattern<SimAggregateInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateInsertOp op,
                                PatternRewriter &rewriter) const override {
    if (auto extract =
            op.getReplacement().getDefiningOp<SimAggregateExtractOp>();
        extract && extract.getInput() == op.getInput() &&
        extract.getIndex() == op.getIndex()) {
      rewriter.replaceOp(op, op.getInput());
      return success();
    }
    auto nested = op.getInput().getDefiningOp<SimAggregateInsertOp>();
    if (!nested || nested.getIndex() != op.getIndex())
      return failure();
    auto replacement = SimAggregateInsertOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), nested.getInput(),
        op.getReplacement(), op.getIndexAttr());
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

struct ConstantArrayExtract final : OpRewritePattern<SimArrayDynExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimArrayDynExtractOp op,
                                PatternRewriter &rewriter) const override {
    bool unknown;
    std::optional<int64_t> sourceIndex =
        getConstantSourceIndex(op.getIndex(), unknown);
    if (!sourceIndex && !unknown)
      return failure();
    std::optional<unsigned> ordinal =
        sourceIndex
            ? getArrayElementOrdinal(op.getInput().getType(), *sourceIndex)
            : std::nullopt;
    if (!ordinal) {
      Value value = materializeDefaultValue(rewriter, op.getLoc(),
                                            op.getResult().getType());
      if (!value)
        return failure();
      rewriter.replaceOp(op, value);
      return success();
    }
    auto replacement = SimAggregateExtractOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), op.getInput(),
        rewriter.getI64IntegerAttr(*ordinal));
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

template <typename DynamicOp, typename StaticOp>
struct ConstantArrayView final : OpRewritePattern<DynamicOp> {
  using OpRewritePattern<DynamicOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(DynamicOp op,
                                PatternRewriter &rewriter) const override {
    bool unknown;
    std::optional<int64_t> sourceIndex =
        getConstantSourceIndex(op.getIndex(), unknown);
    if (!sourceIndex)
      return failure();
    Type arrayType = op.getInput().getType().getElementType();
    std::optional<unsigned> ordinal =
        getArrayElementOrdinal(arrayType, *sourceIndex);
    if (!ordinal)
      return failure();
    auto replacement = StaticOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), op.getInput(),
        rewriter.getDenseI64ArrayAttr({static_cast<int64_t>(*ordinal)}));
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

template <typename ViewOp>
struct FlattenSubelementPath final : OpRewritePattern<ViewOp> {
  using OpRewritePattern<ViewOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(ViewOp op,
                                PatternRewriter &rewriter) const override {
    auto nested = op.getInput().template getDefiningOp<ViewOp>();
    if (!nested)
      return failure();
    SmallVector<int64_t> indices(nested.getIndices());
    llvm::append_range(indices, op.getIndices());
    auto replacement = ViewOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), nested.getInput(),
        rewriter.getDenseI64ArrayAttr(indices));
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

} // namespace

void SimPackedFlattenOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<SimplifyPackedFlatten>(context);
}

void SimPackedUnflattenOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyPackedUnflatten>(context);
}

void SimAggregateConstructOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyAggregateConstruct>(context);
}

void SimAggregateExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyAggregateExtract>(context);
}

void SimAggregateInsertOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyAggregateInsert>(context);
}

void SimArrayDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<ConstantArrayExtract>(context);
}

void SimUnionConstructOp::getCanonicalizationPatterns(RewritePatternSet &,
                                                      MLIRContext *) {}

void SimUnionExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<SimplifyUnionExtract>(context);
}

void SimRefSubelementOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<FlattenSubelementPath<SimRefSubelementOp>>(context);
}

void SimRefArrayElementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<ConstantArrayView<SimRefArrayElementOp, SimRefSubelementOp>>(
      context);
}

void SimDriverSubelementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<FlattenSubelementPath<SimDriverSubelementOp>>(context);
}

void SimDriverArrayElementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results
      .add<ConstantArrayView<SimDriverArrayElementOp, SimDriverSubelementOp>>(
          context);
}

void SimLogicBinaryOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<NormalizeBinaryConstant>(context);
}

void SimLogicCompareOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<NormalizeCompareConstant>(context);
}

void SimLogicResizeOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<CollapseResizeChain>(context);
}

void SimLogicConcatOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<FlattenConcat, ReplicateRepeatedConcatInput,
              MergeAdjacentConcatExtracts>(context);
}

void SimLogicReplicateOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<CombineReplication>(context);
}

void SimLogicExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimLogicExtractOp>,
              SimplifyLogicExtractSource>(context);
}

void SimLogicDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<ConstantDynamicExtract<SimLogicDynExtractOp, SimLogicExtractOp>>(
      context);
}

void SimLogicInsertOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<RemoveOverwrittenInsert>(context);
}

void SimRefExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimRefExtractOp>>(context);
}

void SimNetExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimNetExtractOp>>(context);
}

void SimRefDynExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<ConstantDynamicExtract<SimRefDynExtractOp, SimRefExtractOp>>(
      context);
}

void SimDriverExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimDriverExtractOp>>(context);
}

void SimDriverDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results
      .add<ConstantDynamicExtract<SimDriverDynExtractOp, SimDriverExtractOp>>(
          context);
}

LogicalResult SimTimeConstantOp::verify() {
  if (getValueAttr().getValue().isNegative())
    return emitOpError("simulation time must be nonnegative");
  return success();
}

LogicalResult SimTimeScaleOp::verify() {
  if (!getInput().getType().isSignlessInteger(64))
    return emitOpError("input must be a normalized signless i64");
  if (getScaleAttr().getValue().isNegative() ||
      getScaleAttr().getValue().isZero())
    return emitOpError("tick scale must be positive");
  return success();
}

LogicalResult SimTimeToRealOp::verify() {
  if (!getScaleAttr().getValue().isStrictlyPositive())
    return emitOpError("tick scale must be positive");
  return success();
}

LogicalResult SimTimeFromRealOp::verify() {
  if (!getScaleAttr().getValue().isStrictlyPositive())
    return emitOpError("tick scale must be positive");
  if (!getQuantumAttr().getValue().isStrictlyPositive())
    return emitOpError("tick quantum must be positive");
  if (!getScaleAttr().getValue().urem(getQuantumAttr().getValue()).isZero())
    return emitOpError("tick quantum must divide the tick scale");
  return success();
}

LogicalResult SimEventTriggerOp::verify() {
  if (getDelay() && !getNonblocking())
    return emitOpError("a delayed named-event trigger must be nonblocking");
  return success();
}

OpFoldResult SimTimeConstantOp::fold(FoldAdaptor adaptor) {
  return adaptor.getValueAttr();
}

OpFoldResult SimTimeAddOp::fold(FoldAdaptor adaptor) {
  auto lhs = dyn_cast_or_null<IntegerAttr>(adaptor.getLhs());
  auto rhs = dyn_cast_or_null<IntegerAttr>(adaptor.getRhs());
  if (lhs && lhs.getValue().isZero())
    return getRhs();
  if (rhs && rhs.getValue().isZero())
    return getLhs();
  if (!lhs || !rhs)
    return {};
  bool overflow = false;
  APInt sum = lhs.getValue().sadd_ov(rhs.getValue(), overflow);
  if (overflow || sum.isNegative())
    return {};
  return IntegerAttr::get(lhs.getType(), sum);
}

static LogicalResult verifyContinuation(Operation *op,
                                        ValueRange continuationOperands,
                                        Block *continuation) {
  if (!continuation)
    return op->emitOpError("requires a continuation successor");
  if (continuationOperands.getTypes() != continuation->getArgumentTypes())
    return op->emitOpError(
        "continuation operand types must match successor block arguments");
  auto function = op->getParentOfType<SimFuncOp>();
  if (!function || continuation->getParent() != &function.getBody())
    return op->emitOpError("continuation must be a block in the same function");
  if (continuation == &function.getBody().front())
    return op->emitOpError("continuation must not target the entry block");
  if (auto resume =
          op->template getAttrOfType<EventRegionAttr>("resume_region")) {
    EventRegion region = resume.getValue();
    if (region != EventRegion::Active && region != EventRegion::Observed &&
        region != EventRegion::Reactive && region != EventRegion::Postponed)
      return op->emitOpError(
          "resume region must be an executable process home region");
  }
  return success();
}

template <typename SuspendOp>
static SuccessorOperands makeContinuationSuccessorOperands(SuspendOp op,
                                                           unsigned index) {
  assert(index == 0 && "suspension operations have one successor");
  return SuccessorOperands(op.getContinuationOperandsMutable());
}

SuccessorOperands SimSuspendDelayOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendChangeOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendEdgeOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendEdgeIffOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendLevelOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendAnyOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendEventOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendObserveOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendForeverOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendAwaitOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendJoinOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendChildrenOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimTaskCallOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}

LogicalResult SimSuspendDelayOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendChangeOp::verify() {
  if (!isa<RefType, NetType>(getWatched().getType()))
    return emitOpError("watched value must be a ref or net handle");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendEdgeOp::verify() {
  if (!isa<RefType, NetType>(getWatched().getType()))
    return emitOpError("watched value must be a ref or net handle");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendEdgeIffOp::verify() {
  if (!isa<RefType, NetType>(getWatched().getType()))
    return emitOpError("watched value must be a ref or net handle");
  if (!isa<RefType, NetType>(getCondition().getType()))
    return emitOpError("condition must be a ref or net handle");
  if (getEdge() == EdgeKind::Change)
    return emitOpError("primary event must request an edge");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendLevelOp::verify() {
  if (!isa<RefType, NetType>(getWatched().getType()))
    return emitOpError("watched value must be a ref or net handle");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendAnyOp::verify() {
  if (getEdges().size() > getNumOperands())
    return emitOpError("edge inventory exceeds the operand inventory");
  if (getWatched().empty())
    return emitOpError("requires at least one watched handle");
  if (getEdges().size() != getWatched().size())
    return emitOpError("requires one edge kind per watched handle");
  for (auto [watched, edge] : llvm::zip(getWatched(), getEdges())) {
    if (!isa<RefType, NetType>(watched.getType()))
      return emitOpError("watched values must be ref or net handles");
    if (edge < static_cast<int32_t>(EdgeKind::Change) ||
        edge > static_cast<int32_t>(EdgeKind::Both))
      return emitOpError("contains an invalid edge kind");
  }
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

Operation::operand_range SimSuspendAnyOp::getWatched() {
  return getValues().take_front(
      std::min<size_t>(getEdges().size(), getNumOperands()));
}

Operation::operand_range SimSuspendAnyOp::getContinuationOperands() {
  return getValues().drop_front(
      std::min<size_t>(getEdges().size(), getNumOperands()));
}

MutableOperandRange SimSuspendAnyOp::getContinuationOperandsMutable() {
  unsigned watchedCount = std::min<size_t>(getEdges().size(), getNumOperands());
  return MutableOperandRange(getOperation(), watchedCount,
                             getNumOperands() - watchedCount);
}
LogicalResult SimSuspendEventOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendObserveOp::verify() {
  if (getConditionCountAttr().getValue().isNegative())
    return emitOpError("condition count must be nonnegative");
  if (getPrimaries().empty())
    return emitOpError("requires at least one primary observer");
  if (getConditions().size() != static_cast<uint64_t>(getConditionCount()))
    return emitOpError("condition count exceeds the operand inventory");
  if (getPrimaries().size() != getInitialValues().size() ||
      getPrimaries().size() != getEdges().size() ||
      getPrimaries().size() != getConditionIndices().size())
    return emitOpError(
        "requires one initial value, edge, and condition index per primary");
  for (Value primary : getPrimaries())
    if (!isa<ObserverType>(primary.getType()))
      return emitOpError("primary operands must be observer handles");
  for (Value condition : getConditions())
    if (!isa<ObserverType>(condition.getType()))
      return emitOpError("condition operands must be observer handles");
  SmallVector<bool> usedConditions(getConditions().size(), false);
  for (auto [index, primary, initial, edge, conditionIndex] :
       llvm::enumerate(getPrimaries(), getInitialValues(), getEdges(),
                       getConditionIndices())) {
    auto observer = cast<ObserverType>(primary.getType());
    if (initial.getType() != observer.getResultType())
      return emitOpError() << "initial value #" << index
                           << " does not match its primary observer result";
    if (edge < static_cast<int32_t>(EdgeKind::Change) ||
        edge > static_cast<int32_t>(EdgeKind::Both))
      return emitOpError("contains an invalid edge kind");
    if (conditionIndex < -1 ||
        (conditionIndex >= 0 &&
         static_cast<uint64_t>(conditionIndex) >= getConditions().size()))
      return emitOpError("contains an invalid condition observer index");
    if (conditionIndex >= 0) {
      if (usedConditions[conditionIndex])
        return emitOpError(
            "a condition observer may belong to only one primary clause");
      usedConditions[conditionIndex] = true;
      Type result =
          cast<ObserverType>(getConditions()[conditionIndex].getType())
              .getResultType();
      auto integer = dyn_cast<IntegerType>(result);
      if (!integer || integer.getWidth() != 1)
        return emitOpError("condition observers must return i1");
    }
  }
  if (llvm::is_contained(usedConditions, false))
    return emitOpError("contains an unreferenced condition observer");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

Operation::operand_range SimSuspendObserveOp::getPrimaries() {
  size_t count = std::min<size_t>(getEdges().size(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimSuspendObserveOp::getInitialValues() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t remaining = getNumOperands() - primaryCount;
  return getValues().slice(primaryCount, std::min(primaryCount, remaining));
}

Operation::operand_range SimSuspendObserveOp::getConditions() {
  if (auto converted = (*this)->getAttrOfType<IntegerAttr>(
          "obelisk.coro.condition_operand_begin")) {
    size_t begin = std::min<uint64_t>(converted.getValue().getZExtValue(),
                                      getNumOperands());
    size_t count =
        getConditionCountAttr().getValue().isNegative()
            ? 0
            : std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
    return getValues().slice(begin, count);
  }
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t begin = std::min<size_t>(getNumOperands(), primaryCount * 2);
  size_t count =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return getValues().slice(begin, count);
}

Operation::operand_range SimSuspendObserveOp::getContinuationOperands() {
  if (auto converted = (*this)->getAttrOfType<IntegerAttr>(
          "obelisk.coro.continuation_operand_begin")) {
    size_t begin = std::min<uint64_t>(converted.getValue().getZExtValue(),
                                      getNumOperands());
    return getValues().drop_front(begin);
  }
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t begin = std::min<size_t>(getNumOperands(), primaryCount * 2);
  if (!getConditionCountAttr().getValue().isNegative())
    begin += std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return getValues().drop_front(begin);
}

MutableOperandRange SimSuspendObserveOp::getContinuationOperandsMutable() {
  if (auto converted = (*this)->getAttrOfType<IntegerAttr>(
          "obelisk.coro.continuation_operand_begin")) {
    size_t begin = std::min<uint64_t>(converted.getValue().getZExtValue(),
                                      getNumOperands());
    return MutableOperandRange(getOperation(), begin, getNumOperands() - begin);
  }
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t begin = std::min<size_t>(getNumOperands(), primaryCount * 2);
  if (!getConditionCountAttr().getValue().isNegative())
    begin += std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return MutableOperandRange(getOperation(), begin, getNumOperands() - begin);
}
LogicalResult SimSuspendForeverOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendAwaitOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendJoinOp::verify() {
  if (getProcessCountAttr().getValue().isNegative() || getProcessCount() == 0)
    return emitOpError("requires at least one child process");
  if (static_cast<uint64_t>(getProcessCount()) > getNumOperands())
    return emitOpError("process count exceeds the operand inventory");
  for (Value process : getProcesses())
    if (!isa<ProcessType>(process.getType()))
      return emitOpError("process prefix must contain only process handles");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

LogicalResult SimSuspendChildrenOp::verify() {
  auto function = getOperation()->getParentOfType<SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in obelisk_sim.func");
  if (function.getEntryKind() == EntryKind::Function)
    return emitOpError("is not permitted in a zero-time function entry");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

Operation::operand_range SimSuspendJoinOp::getProcesses() {
  size_t count = getProcessCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimSuspendJoinOp::getContinuationOperands() {
  size_t count = getProcessCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return getValues().drop_front(count);
}

MutableOperandRange SimSuspendJoinOp::getContinuationOperandsMutable() {
  unsigned count =
      getProcessCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return MutableOperandRange(getOperation(), count, getNumOperands() - count);
}

LogicalResult SimDisplayOp::verify() {
  int64_t radix = getDefaultRadix();
  if (radix != 2 && radix != 8 && radix != 10 && radix != 16)
    return emitOpError("default radix must be 2, 8, 10, or 16");
  if (IntegerAttr multiplier = getTimeMultiplierAttr())
    if (!multiplier.getValue().isStrictlyPositive())
      return emitOpError("time multiplier must be positive");
  unsigned itemIndex = 0;
  for (int32_t flags : getItemFlags()) {
    if ((flags & 2) != 0) {
      if (flags != 2)
        return emitOpError("omitted display items cannot carry other flags");
      continue;
    }
    if (itemIndex == getItems().size())
      return emitOpError("item flags require more display operands");
    Value item = getItems()[itemIndex++];
    if (!isa<BytesType, StringType, DynamicArrayType, QueueType,
             AssocArrayType, IntegerType, LogicType>(item.getType()) &&
        !item.getType().isF64())
      return emitOpError(
          "items must be literal bytes, packed integers, or f64 reals; "
          "managed strings and containers are also accepted");
    if ((flags & ~31) != 0)
      return emitOpError("display item flags contain an unknown bit");
    if ((flags & 16) != 0 &&
        !isa<DynamicArrayType, QueueType, AssocArrayType>(item.getType()))
      return emitOpError("container display flags require a container operand");
    if ((flags & 4) != 0 && !item.getType().isF64())
      return emitOpError("real display items must have f64 operands");
    if ((flags & 4) == 0 && item.getType().isF64())
      return emitOpError("f64 display operands must be marked real");
    if ((flags & 5) == 5)
      return emitOpError("real display items cannot be marked signed");
    if (isa<BytesType>(item.getType()) && flags != 0)
      return emitOpError("literal byte items cannot be signed");
    if (isa<StringType>(item.getType()) && flags != 8)
      return emitOpError("managed string display items require the string flag");
    if (isa<DynamicArrayType, QueueType, AssocArrayType>(item.getType()) &&
        flags != 16)
      return emitOpError(
          "managed container display items require the container flag");
  }
  if (itemIndex != getItems().size())
    return emitOpError("requires one flag entry per display item");
  return success();
}

static LogicalResult verifyPackedFileResult(Operation *operation, Type type) {
  auto integer = dyn_cast<IntegerType>(type);
  if (!integer || integer.getWidth() == 0)
    return operation->emitOpError(
        "packed data result must be a nonzero-width integer");
  return success();
}

LogicalResult SimFileGetlineOp::verify() {
  return verifyPackedFileResult(*this, getData().getType());
}

LogicalResult SimFileReadPackedOp::verify() {
  return verifyPackedFileResult(*this, getData().getType());
}

} // namespace obelisk::sim
