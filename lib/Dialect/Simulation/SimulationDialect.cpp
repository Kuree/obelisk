//===- SimulationDialect.cpp - Executable simulation dialect ------------===//

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/TypeSwitch.h"

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

void ObeliskSimulationDialect::initialize() {
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
    return integer.getWidth();
  if (auto logic = dyn_cast<LogicType>(type))
    return logic.getWidth();
  return std::nullopt;
}

static bool isNormalizedValueType(Type type) {
  return isa<IntegerType, LogicType>(type);
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
  if (isa<LogicType>(input) != isa<LogicType>(result))
    return op->emitOpError(
        "input and result element types must use the same state domain");
  return success();
}

static LogicalResult
verifyElementType(llvm::function_ref<InFlightDiagnostic()> emitError,
                  Type elementType) {
  if (!isNormalizedValueType(elementType))
    return emitError() << "element type must be a signless builtin integer or "
                          "!obelisk_sim.logic, got "
                       << elementType;
  if (auto integer = dyn_cast<IntegerType>(elementType);
      integer && !integer.isSignless())
    return emitError() << "builtin integer element types must be signless";
  return success();
}

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

LogicalResult SimDriverDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "driver ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getNetIdAttr(), "net ID")))
    return failure();
  return verifyElementType([&] { return emitOpError(); }, getType());
}

LogicalResult SimDesignOp::verifyRegions() {
  if (auto precision = getTimePrecisionFsAttr();
      precision &&
      (precision.getValue().isNegative() || precision.getValue().isZero()))
    return emitOpError("time precision must be a positive femtosecond value");
  llvm::DenseSet<uint64_t> scopeIds, storageIds, netIds, driverIds;
  llvm::DenseMap<uint64_t, Type> storageTypes, netTypes, driverTypes;
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
    } else if (auto storage = dyn_cast<SimStorageDeclOp>(op)) {
      if (failed(addId(storage.getIdAttr(), storageIds, "storage")))
        return failure();
      storageTypes[storage.getId()] = storage.getType();
    } else if (auto net = dyn_cast<SimNetDeclOp>(op)) {
      if (failed(addId(net.getIdAttr(), netIds, "net")))
        return failure();
      netTypes[net.getId()] = net.getType();
    } else if (auto driver = dyn_cast<SimDriverDeclOp>(op)) {
      if (failed(addId(driver.getIdAttr(), driverIds, "driver")))
        return failure();
      driverTypes[driver.getId()] = driver.getType();
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
      failed(verifyDense(driverIds, "driver")))
    return failure();
  for (Operation &op : getBody().front()) {
    if (auto scope = dyn_cast<SimScopeDeclOp>(op)) {
      if (scope.getParentAttr() && !scopeIds.count(*scope.getParent()))
        return scope.emitOpError("references an unknown parent scope ID");
      if (scope.getParentAttr() && *scope.getParent() >= scope.getId())
        return scope.emitOpError(
            "parent scope ID must precede the child scope ID");
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
    }
  }

  // Descriptor tables live on this operation, so descriptor references are
  // resolved here rather than in a function-local verifier: an operation pass
  // on one function may run concurrently with passes on its siblings, and a
  // nested verifier must not reach into shared parent state. Callee symbols
  // instead use SymbolUserOpInterface, which the framework verifies against
  // this symbol table with a cached SymbolTableCollection.
  for (SimFuncOp function : functions) {
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
        if (descriptorId && storageTypes.count(*descriptorId))
          expected =
              RefType::get(getContext(), storageTypes.lookup(*descriptorId));
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
  if (type.getNumInputs() == 0 || !isa<ContextType>(type.getInput(0)))
    return emitOpError("first argument must be !obelisk_sim.context");
  for (Type input : type.getInputs()) {
    if (!isa<ContextType, RefType, NetType, DriverType, EventType, ProcessType,
             IntegerType, LogicType, TimeType>(input))
      return emitOpError() << "contains non-normalized argument type " << input;
    if (auto integer = dyn_cast<IntegerType>(input);
        integer && !integer.isSignless())
      return emitOpError("builtin integer arguments must be signless");
  }
  for (Type result : type.getResults()) {
    if (!isa<IntegerType, LogicType, TimeType, EventType, ProcessType>(result))
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

LogicalResult SimRefAllocOp::verify() {
  if (getInitialValue().getType() != getResult().getType().getElementType())
    return emitOpError("initial value must match allocated element type");
  return success();
}

SmallVector<MemorySlot> SimRefAllocOp::getPromotableSlots() {
  return {{getResult(), getResult().getType().getElementType()}};
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

OpFoldResult SimLogicResizeOp::fold(FoldAdaptor) {
  if (getInput().getType() == getResult().getType())
    return getInput();
  return {};
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
LogicalResult SimTimeConstantOp::verify() {
  if (getValueAttr().getValue().isNegative())
    return emitOpError("simulation time must be nonnegative");
  return success();
}

LogicalResult SimTimeScaleOp::verify() {
  if (!isNormalizedValueType(getInput().getType()))
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

} // namespace obelisk::sim
