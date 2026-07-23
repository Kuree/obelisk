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
    if (name == "obelisk_sim.capture_kind" ||
        name == "obelisk_sim.descriptor_id" || name == "obelisk_sim.bindings" ||
        name == "obelisk_sim.delay_scale" ||
        name == "obelisk_sim.hierarchical_name")
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
      if (name.starts_with("obelisk_sim.") &&
          name != "obelisk_sim.capture_kind" &&
          name != "obelisk_sim.descriptor_id")
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

bool isSuspension(Operation *operation) {
  return isa<SimSuspendDelayOp, SimSuspendChangeOp, SimSuspendEdgeOp,
             SimSuspendAnyOp, SimSuspendEventOp, SimSuspendAwaitOp,
             SimSuspendJoinOp>(operation);
}

} // namespace

InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee) {
  SimFuncOp caller = call ? call->getParentOfType<SimFuncOp>() : SimFuncOp{};
  auto design = caller ? caller->getParentOfType<SimDesignOp>() : SimDesignOp{};
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
    if (isSuspension(operation))
      legality = InlineLegality::Suspension;
    else if (auto display = dyn_cast<SimDisplayOp>(operation);
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
  case InlineLegality::Suspension:
    return "callee contains a suspension";
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
  return getAggregateFields(type).size();
}

Type getAggregateElementType(Type type, unsigned index) {
  if (auto array = dyn_cast<PackedArrayType>(type))
    return index < getAggregateNumElements(type) ? array.getElementType()
                                                 : Type{};
  if (auto array = dyn_cast<UnpackedArrayType>(type))
    return index < getAggregateNumElements(type) ? array.getElementType()
                                                 : Type{};
  ArrayAttr fields = getAggregateFields(type);
  if (index >= fields.size())
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
  if (isa<TimeType>(type))
    return uint64_t{64};
  auto checkedAdd = [](uint64_t &total, uint64_t amount) {
    if (amount > std::numeric_limits<uint64_t>::max() - total)
      return false;
    total += amount;
    return true;
  };
  if (isa<UnpackedArrayType>(type)) {
    uint64_t count = getAggregateNumElements(type);
    std::optional<uint64_t> element =
        getProvenanceSpan(getAggregateElementType(type, 0));
    if (!element ||
        (count && *element > std::numeric_limits<uint64_t>::max() / count))
      return std::nullopt;
    return count * *element;
  }
  if (isa<UnpackedStructType>(type)) {
    uint64_t total = 0;
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      std::optional<uint64_t> child =
          getProvenanceSpan(getAggregateElementType(type, index));
      if (!child || !checkedAdd(total, *child))
        return std::nullopt;
    }
    return total;
  }
  if (isa<UnpackedUnionType>(type)) {
    uint64_t maximum = 0;
    for (unsigned index = 0; index < getAggregateNumElements(type); ++index) {
      std::optional<uint64_t> child =
          getProvenanceSpan(getAggregateElementType(type, index));
      if (!child)
        return std::nullopt;
      maximum = std::max(maximum, *child);
    }
    return maximum;
  }
  return std::nullopt;
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
    if (*span && index > std::numeric_limits<uint64_t>::max() / *span)
      return std::nullopt;
    offset = index * *span;
  } else if (isa<UnpackedStructType>(type)) {
    for (unsigned previous = 0; previous < index; ++previous) {
      std::optional<uint64_t> previousSpan =
          getProvenanceSpan(getAggregateElementType(type, previous));
      if (!previousSpan ||
          *previousSpan > std::numeric_limits<uint64_t>::max() - offset)
        return std::nullopt;
      offset += *previousSpan;
    }
  } else if (!isa<UnpackedUnionType>(type)) {
    return std::nullopt;
  }
  return std::pair<uint64_t, uint64_t>{offset, *span};
}

static bool isNormalizedValueType(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.isSignless();
  return isa<LogicType>(type) || isAggregateType(type);
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
  return verifyElementType(emitError, elementType);
}

LogicalResult
DriverType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                   Type elementType) {
  return verifyElementType(emitError, elementType);
}

static LogicalResult verifyNonnegative(Operation *op, IntegerAttr attr,
                                       StringRef name) {
  if (attr.getValue().isNegative())
    return op->emitOpError() << name << " must be nonnegative";
  return success();
}

static std::optional<CaptureKind> getCaptureKind(DictionaryAttr attrs) {
  if (!attrs)
    return std::nullopt;
  auto value =
      dyn_cast_or_null<CaptureKindAttr>(attrs.get("obelisk_sim.capture_kind"));
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
        failed(
            verifyNonnegative(*this, getDrivenWidthAttr(), "driven width")))
      return failure();
    uint64_t low = getDrivenLowAttr().getValue().getZExtValue();
    uint64_t width = getDrivenWidthAttr().getValue().getZExtValue();
    std::optional<unsigned> typeWidth = getPackedWidth(getType());
    if (width == 0)
      return emitOpError("driven width must be positive");
    if (!typeWidth || low > *typeWidth || width > *typeWidth - low)
      return emitOpError("driven range exceeds the driver type");
  }
  return verifyElementType([&] { return emitOpError(); }, getType());
}

LogicalResult SimDesignOp::verifyRegions() {
  if (auto precision = getTimePrecisionFsAttr();
      precision &&
      (precision.getValue().isNegative() || precision.getValue().isZero()))
    return emitOpError("time precision must be a positive femtosecond value");
  llvm::DenseSet<uint64_t> scopeIds, codeUnitIds, storageIds, netIds, driverIds,
      connectionIds;
  llvm::DenseMap<uint64_t, SimCodeUnitDeclOp> codeUnits;
  llvm::DenseMap<uint64_t, Type> storageTypes, netTypes, driverTypes;
  llvm::DenseMap<uint64_t, NetResolutionKind> netResolutions;
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
    } else if (auto function = dyn_cast<SimFuncOp>(op)) {
      functions.push_back(function);
    }
  }
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
      auto descriptor = function.getArgAttrOfType<IntegerAttr>(
          index, "obelisk_sim.descriptor_id");
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
              index, "obelisk_sim.descriptor_root_type");
          auto low = function.getArgAttrOfType<IntegerAttr>(
              index, "obelisk_sim.descriptor_low");
          if (!rootType) {
            if (low || function.getArgAttr(
                           index, "obelisk_sim.descriptor_indices") ||
                function.getArgAttr(
                    index, "obelisk_sim.descriptor_aggregate_type") ||
                function.getArgAttr(index,
                                    "obelisk_sim.descriptor_packed_low"))
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
              index, "obelisk_sim.descriptor_indices");
          bool validView = true;
          if (indices) {
            for (int64_t rawIndex : indices.asArrayRef()) {
              if (rawIndex < 0 ||
                  static_cast<uint64_t>(rawIndex) >
                      std::numeric_limits<unsigned>::max()) {
                validView = false;
                break;
              }
              auto subelement = getAggregateProvenanceSubelement(
                  selected, static_cast<unsigned>(rawIndex));
              if (!subelement ||
                  subelement->first > UINT64_MAX - computedLow) {
                validView = false;
                break;
              }
              computedLow += subelement->first;
              selected = getAggregateElementType(
                  selected, static_cast<unsigned>(rawIndex));
            }
          }
          auto aggregateType = function.getArgAttrOfType<TypeAttr>(
              index, "obelisk_sim.descriptor_aggregate_type");
          if ((indices && !aggregateType) ||
              (aggregateType && aggregateType.getValue() != selected))
            validView = false;

          auto packedLow = function.getArgAttrOfType<IntegerAttr>(
              index, "obelisk_sim.descriptor_packed_low");
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
          } else if (packedLow &&
                     (packedLow.getValue().isNegative() ||
                      packedLow.getValue().getActiveBits() > 64 ||
                      packedLow.getValue().getZExtValue() != 0)) {
            validView = false;
          }

          uint64_t encodedLow = low.getValue().getZExtValue();
          if (validView && encodedLow == computedLow &&
              encodedLow <= *rootSpan &&
              *viewSpan <= *rootSpan - encodedLow)
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

LogicalResult SimFuncOp::verify() {
  FunctionType type = getFunctionType();
  if (getCodeUnitIdAttr() &&
      failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")))
    return failure();
  if (type.getNumInputs() == 0 || !isa<ContextType>(type.getInput(0)))
    return emitOpError("first argument must be !obelisk_sim.context");
  for (Type input : type.getInputs()) {
    if (!isa<ContextType, RefType, NetType, DriverType, EventType, ProcessType,
             IntegerType, LogicType, TimeType>(input) &&
        !isAggregateType(input))
      return emitOpError() << "contains non-normalized argument type " << input;
    if (auto integer = dyn_cast<IntegerType>(input);
        integer && !integer.isSignless())
      return emitOpError("builtin integer arguments must be signless");
  }
  for (Type result : type.getResults()) {
    if (!isa<IntegerType, LogicType, TimeType, EventType, ProcessType>(
            result) &&
        !isAggregateType(result))
      return emitOpError() << "contains non-normalized result type " << result;
    if (auto integer = dyn_cast<IntegerType>(result);
        integer && !integer.isSignless())
      return emitOpError("builtin integer results must be signless");
  }

  if (getEntryKind() != EntryKind::Function && !type.getResults().empty())
    return emitOpError("process and root entries must not return values");
  if (getEntryKind() == EntryKind::RootInitializer && type.getNumInputs() != 1)
    return emitOpError("root initializer accepts only the context argument");
  if (getEntryKind() == EntryKind::Function) {
    // Only the time-controlled statements are illegal in a SystemVerilog
    // function. Nonblocking assignment, nonblocking event trigger, and
    // `fork ... join_none` are all legal there and consume no simulation
    // time, so they stay representable and are handled by the schedule.
    WalkResult blocking = getBody().walk([&](Operation *op) {
      if (isa<SimSuspendDelayOp, SimSuspendChangeOp, SimSuspendEdgeOp,
              SimSuspendAnyOp, SimSuspendEventOp, SimSuspendAwaitOp,
              SimSuspendJoinOp>(op)) {
        op->emitOpError("is not permitted in a zero-time function entry");
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
    auto descriptor =
        dictionary.getAs<IntegerAttr>("obelisk_sim.descriptor_id");
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
  if (!getOperation()->getParentOfType<SimFuncOp>())
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

LogicalResult SimSpawnOp::verify() {
  if (!getOperation()->getParentOfType<SimFuncOp>())
    return emitOpError("must be nested in obelisk_sim.func");
  return success();
}

LogicalResult SimSpawnOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(getOperation(),
                                                               getCalleeAttr());
  if (!callee || callee.getEntryKind() == EntryKind::Function ||
      callee.getEntryKind() == EntryKind::RootInitializer)
    return emitOpError("callee must name a sibling process entry");
  if (getOperandTypes() != callee.getFunctionType().getInputs() ||
      !callee.getFunctionType().getResults().empty())
    return emitOpError("operands must match the void callee signature");
  return success();
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
  bool caseComparison =
      getKind() == CompareKind::CaseEq || getKind() == CompareKind::CaseNe;
  if (caseComparison && getLhs() == getRhs()) {
    bool equal = getKind() == CompareKind::CaseEq;
    return IntegerAttr::get(getResult().getType(), equal);
  }

  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());
  if (!lhs || !rhs)
    return {};
  if (caseComparison) {
    bool equal = lhs->value == rhs->value && lhs->unknown == rhs->unknown;
    if (getKind() == CompareKind::CaseNe)
      equal = !equal;
    return IntegerAttr::get(getResult().getType(), equal);
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
      getKind() == CompareKind::CaseEq || getKind() == CompareKind::CaseNe;
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
      kind = op.getKind();
      break;
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
  if (!isa<IntegerType, LogicType>(getInput().getType()))
    return emitOpError(
        "input must be a signless builtin integer or four-state logic");
  if (auto integer = dyn_cast<IntegerType>(getInput().getType());
      integer && !integer.isSignless())
    return emitOpError("builtin integer input must be signless");
  if (getScaleAttr().getValue().isNegative() ||
      getScaleAttr().getValue().isZero())
    return emitOpError("tick scale must be positive");
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
SuccessorOperands SimSuspendAnyOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendEventOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendAwaitOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendJoinOp::getSuccessorOperands(unsigned index) {
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
    if (!isa<BytesType, IntegerType, LogicType>(item.getType()))
      return emitOpError("items must be literal bytes or packed integers");
    if ((flags & ~3) != 0)
      return emitOpError("display item flags contain an unknown bit");
    if (isa<BytesType>(item.getType()) && flags != 0)
      return emitOpError("literal byte items cannot be signed");
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
