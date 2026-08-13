//===- LowerUnit.cpp - Lower one frozen code unit to SSA and CF ---------===//
//
// Rewrites the semantic statement and expression tree cloned into one
// `obelisk_sim.func` into an SSA CFG. The unit is isolated and every non-local
// resource is already an entry argument, so this pass never consults the
// design or any sibling unit and can run on all units concurrently.
//
//===----------------------------------------------------------------------===//

#include "LowerUnit.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <functional>
#include <limits>
#include <vector>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMLOWERUNITPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

constexpr bool sameEventRegionEncoding(ir::EventRegion source,
                                       sim::EventRegion target) {
  return static_cast<uint32_t>(source) == static_cast<uint32_t>(target);
}

static_assert(
    sameEventRegionEncoding(ir::EventRegion::Preponed,
                            sim::EventRegion::Preponed) &&
        sameEventRegionEncoding(ir::EventRegion::PreActive,
                                sim::EventRegion::PreActive) &&
        sameEventRegionEncoding(ir::EventRegion::Active,
                                sim::EventRegion::Active) &&
        sameEventRegionEncoding(ir::EventRegion::Inactive,
                                sim::EventRegion::Inactive) &&
        sameEventRegionEncoding(ir::EventRegion::PreNBA,
                                sim::EventRegion::PreNBA) &&
        sameEventRegionEncoding(ir::EventRegion::NBA, sim::EventRegion::NBA) &&
        sameEventRegionEncoding(ir::EventRegion::PostNBA,
                                sim::EventRegion::PostNBA) &&
        sameEventRegionEncoding(ir::EventRegion::PreObserved,
                                sim::EventRegion::PreObserved) &&
        sameEventRegionEncoding(ir::EventRegion::Observed,
                                sim::EventRegion::Observed) &&
        sameEventRegionEncoding(ir::EventRegion::PostObserved,
                                sim::EventRegion::PostObserved) &&
        sameEventRegionEncoding(ir::EventRegion::Reactive,
                                sim::EventRegion::Reactive) &&
        sameEventRegionEncoding(ir::EventRegion::ReInactive,
                                sim::EventRegion::ReInactive) &&
        sameEventRegionEncoding(ir::EventRegion::PreReNBA,
                                sim::EventRegion::PreReNBA) &&
        sameEventRegionEncoding(ir::EventRegion::ReNBA,
                                sim::EventRegion::ReNBA) &&
        sameEventRegionEncoding(ir::EventRegion::PostReNBA,
                                sim::EventRegion::PostReNBA) &&
        sameEventRegionEncoding(ir::EventRegion::PrePostponed,
                                sim::EventRegion::PrePostponed) &&
        sameEventRegionEncoding(ir::EventRegion::Postponed,
                                sim::EventRegion::Postponed),
    "Obelisk and simulation event-region enums must stay in lockstep");

static uint64_t stableTypeID(Type type) {
  std::string spelling;
  llvm::raw_string_ostream stream(spelling);
  type.print(stream);
  stream.flush();
  uint64_t hash = obelisk_stable_hash(spelling.data(), spelling.size());
  return hash ? hash : 1;
}

FailureOr<simlowering::ContainerElementDescriptor>
describeContainerElementImpl(Type type, Location location) {
  ContainerElementDescriptor result{stableTypeID(type), 0, 0, 0, 1, 0, {}, {}};
  if (auto integer = dyn_cast<IntegerType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_BITS;
    result.valueSize = (integer.getWidth() + 7) / 8;
    result.bitWidth = integer.getWidth();
    return result;
  }
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_LOGIC;
    result.flags = OBELISK_RT_ELEMENT_FOUR_STATE;
    result.valueSize = (logic.getWidth() + 7) / 8;
    result.bitWidth = logic.getWidth();
    return result;
  }
  if (auto real = dyn_cast<FloatType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_REAL;
    result.valueSize = real.getWidth() / 8;
    result.bitWidth = real.getWidth();
    return result;
  }
  if (isa<sim::ClassHandleType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_CLASS_HANDLE;
    result.valueSize = sizeof(void *);
    return result;
  }
  if (isa<sim::StringType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_STRING;
    result.valueSize = sizeof(void *);
    return result;
  }
  if (isa<sim::EventType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_EVENT;
    result.valueSize = sizeof(uint64_t);
    return result;
  }
  if (isa<sim::ProcessType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_BITS;
    result.valueSize = sizeof(uint64_t);
    result.bitWidth = 64;
    return result;
  }
  // Virtual-interface handles are nullable 64-bit elaborated scope IDs. They
  // contain no managed pointer and therefore use the ordinary bits container
  // ABI, just like process IDs.
  if (isa<sim::VirtualInterfaceType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_BITS;
    result.valueSize = sizeof(uint64_t);
    result.bitWidth = 64;
    return result;
  }
  if (isa<sim::ChandleType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_BITS;
    result.valueSize = sizeof(void *);
    result.bitWidth = sizeof(void *) * 8;
    return result;
  }
  if (isa<sim::DynamicArrayType, sim::QueueType, sim::MailboxType,
          sim::SemaphoreType, sim::AssocArrayType>(type)) {
    result.kind = OBELISK_RT_ELEMENT_CONTAINER_HANDLE;
    result.valueSize = sizeof(void *);
    return result;
  }
  if (Type scalar = sim::getPackedScalarType(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || *width == 0)
      return failure();
    bool fourState = isa<sim::LogicType>(scalar);
    result.kind =
        fourState ? OBELISK_RT_ELEMENT_LOGIC : OBELISK_RT_ELEMENT_BITS;
    result.flags = fourState ? OBELISK_RT_ELEMENT_FOUR_STATE : 0;
    result.valueSize = (*width + 7) / 8;
    result.bitWidth = *width;
    return result;
  }
  if (sim::isAggregateType(type)) {
    std::optional<uint64_t> width = sim::getProvenanceSpan(type);
    if (!width || *width == 0) {
      emitError(location)
          << "dynamic-array aggregate element has no canonical layout: "
          << type;
      return failure();
    }
    SmallVector<sim::ManagedHandleSlot, 2> traceSlots;
    if (!sim::getManagedHandleSlots(type, traceSlots)) {
      emitError(location)
          << "dynamic-array aggregate element has no canonical trace layout: "
          << type;
      return failure();
    }
    for (const sim::ManagedHandleSlot &slot : traceSlots) {
      std::optional<uint32_t> kind = sim::getManagedHandleTraceKind(slot);
      if (!kind || (slot.bitOffset & 7) != 0 ||
          slot.bitOffset / 8 > uint64_t{INT64_MAX}) {
        emitError(location)
            << "dynamic-array aggregate element has no canonical trace layout: "
            << type;
        return failure();
      }
      result.traceOffsets.push_back(static_cast<int64_t>(slot.bitOffset / 8));
      result.traceKinds.push_back(static_cast<int32_t>(*kind));
    }
    bool fourState = false;
    type.walk([&](sim::LogicType) { fourState = true; });
    result.kind = OBELISK_RT_ELEMENT_AGGREGATE;
    result.flags = fourState ? OBELISK_RT_ELEMENT_FOUR_STATE : 0;
    result.valueSize = (*width + 7) / 8;
    result.bitWidth = result.valueSize * 8;
    return result;
  }
  emitError(location)
      << "dynamic-array element type has no canonical container ABI: " << type;
  return failure();
}

} // namespace

namespace simlowering {

FailureOr<ContainerElementDescriptor>
describeContainerElement(Type type, Location location) {
  return describeContainerElementImpl(type, location);
}

UnitLowering::UnitLowering(sim::SimFuncOp function)
    : function(function), builder(function.getContext()),
      current(&function.getBody().front()) {
  builder.setInsertionPointToStart(current);
  ModuleOp module = function->getParentOfType<ModuleOp>();
  sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
  DenseMap<uint64_t, StringAttr> interfaceScopes;
  // Unit-lowering passes may run concurrently for sibling functions.  The
  // design declarations are immutable here, but recursively walking the whole
  // design would also traverse function bodies while sibling passes rewrite
  // them.  Inventory only the declaration operations in the design body.
  for (Operation &operation : design.getBody().front())
    if (auto scope = dyn_cast<sim::SimScopeDeclOp>(operation)) {
      if (std::optional<StringRef> hierarchy = scope.getHierarchicalName())
        scopeIDs[*hierarchy] = scope.getId();
      if (StringAttr identity = scope.getInterfaceTypeAttr())
        interfaceScopes[scope.getId()] = identity;
    }
  auto memberKey = [](StringRef identity, StringRef member) {
    return (Twine(identity) + "\n" + member).str();
  };
  for (Operation &operation : design.getBody().front())
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      virtualInterfaceStorageTypes[storage.getId()] = storage.getType();
      auto scope = interfaceScopes.find(storage.getScopeId());
      StringAttr member = storage->getAttrOfType<StringAttr>(
          "obelisk_sim.virtual_interface_member");
      if (scope != interfaceScopes.end() && member)
        virtualInterfaceStorageMembers[memberKey(scope->second.getValue(),
                                                 member.getValue())]
            .push_back({storage.getScopeId(), storage.getId()});
    }
  for (Operation &operation : design.getBody().front())
    if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
      virtualInterfaceNetTypes[net.getId()] = net.getType();
      auto scope = interfaceScopes.find(net.getScopeId());
      StringAttr member = net->getAttrOfType<StringAttr>(
          "obelisk_sim.virtual_interface_member");
      if (scope != interfaceScopes.end() && member)
        virtualInterfaceNetMembers[memberKey(scope->second.getValue(),
                                             member.getValue())]
            .push_back({net.getScopeId(), net.getId()});
    }
  for (Operation &topLevel : module.getBody()->getOperations()) {
    if (auto definition = dyn_cast<semantic::SVDefinitionSymbolOp>(topLevel))
      if (auto name = definition.getName())
        coverageDefinitionNames.insert(*name);
    if (auto root = dyn_cast<semantic::SVRootSymbolOp>(topLevel)) {
      root->walk([&](semantic::SVCovergroupTypeOp covergroup) {
        semanticCovergroups[covergroup.getSymName()] = covergroup;
      });
    }
  }
  if (auto argument =
          function->getAttrOfType<IntegerAttr>("obelisk_sim.this_argument")) {
    uint64_t index = argument.getValue().getZExtValue();
    if (index < function.getNumArguments())
      thisObject = function.getBody().front().getArgument(index);
    else
      invalidBindings = true;
  }
  if (function.getEntryKind() == sim::EntryKind::Task)
    if (auto targetID = function->getAttrOfType<IntegerAttr>(
            "obelisk_sim.control_target_id"))
      taskControlActivation =
          sim::SimControlEnterOp::create(builder, function.getLoc(), targetID);
  auto bindings = function->getAttrOfType<ArrayAttr>(bindingsAttrName);
  if (auto inherited = function->getAttrOfType<ArrayAttr>("inherited_controls"))
    for (Attribute attribute : inherited) {
      auto entry = dyn_cast<DictionaryAttr>(attribute);
      auto path = entry ? entry.getAs<StringAttr>("path") : StringAttr{};
      auto id = entry ? entry.getAs<IntegerAttr>("id") : IntegerAttr{};
      if (path && id)
        inheritedControlIDs[path.getValue()] = id.getValue().getZExtValue();
    }
  if (!bindings)
    return;
  for (Attribute attr : bindings) {
    if (auto argument = dyn_cast<sim::ArgumentBindingAttr>(attr)) {
      StringRef path = argument.getPath().getValue();
      Value value =
          function.getBody().front().getArgument(argument.getArgument());
      if (argument.getKind() == sim::UnitArgumentKind::CopyOutDestination) {
        copyOutDestinations[path] = value;
        continue;
      }
      if (argument.getKind() == sim::UnitArgumentKind::FormalLocal) {
        Value local = sim::SimRefAllocOp::create(
            builder, function.getLoc(),
            sim::RefType::get(function.getContext(), value.getType()), value);
        values[path] = local;
        lvalues[path] = local;
        if (argument.getCopyOut())
          copyOutPaths.push_back(path.str());
        continue;
      }
      if (argument.getKind() == sim::UnitArgumentKind::LValueOnly) {
        lvalues[path] = value;
        if (IntegerAttr node = argument.getLvalueNode())
          nodeLvalues[node.getValue().getZExtValue()] = value;
        continue;
      }
      if (function.getEntryKind() == sim::EntryKind::Task) {
        Value local = values.lookup(path);
        if (local && local != value && isa<sim::RefType>(local.getType()) &&
            local.getType() == value.getType()) {
          Value initial = sim::SimRefLoadOp::create(
              builder, function.getLoc(),
              cast<sim::RefType>(local.getType()).getElementType(), local);
          sim::SimRefStoreOp::create(builder, function.getLoc(), initial,
                                     value);
          // A static task formal is backed by its descriptor after copy-in.
          // Keep reads and writes on that same storage; otherwise reads use
          // the descriptor while assignments continue updating the discarded
          // activation-local reference.
          lvalues[path] = value;
        }
      }
      values[path] = value;
      if (isa<sim::RefType, sim::ArgumentRefType, sim::NetType,
              sim::DriverType>(value.getType()))
        lvalues.try_emplace(path, value);
      continue;
    }
    if (auto constant = dyn_cast<sim::ConstantBindingAttr>(attr)) {
      FailureOr<Value> value = sim::materializeFrozenConstant(
          builder, function.getLoc(), constant.getValue());
      if (failed(value)) {
        function.emitError() << "cannot materialize frozen constant binding '"
                             << constant.getPath().getValue() << "'";
        invalidBindings = true;
        continue;
      }
      values[constant.getPath().getValue()] = *value;
      continue;
    }
    auto localBinding = dyn_cast<sim::LocalBindingAttr>(attr);
    if (!localBinding) {
      invalidBindings = true;
      continue;
    }
    StringRef path = localBinding.getPath().getValue();
    Type type = localBinding.getType();
    Value initial = createDefaultValue(builder, function.getLoc(), type);
    if (!initial) {
      function.emitError() << "cannot initialize local binding '" << path
                           << "' of type " << type;
      invalidBindings = true;
      continue;
    }
    localDefaults[path] = initial;
    bool isReturn = localBinding.getIsReturn();
    if (isReturn)
      returnPath = path.str();
    if (localBinding.getAutomatic()) {
      automaticLocals.insert(path);
      // Pattern variables have statement-execution lifetime rather than
      // function-activation lifetime. Their references are allocated by
      // lowerPattern at the point where the capture occurs so overlapping
      // loop iterations and detached forks retain independent snapshots.
      if (localBinding.getPatternVariable())
        continue;
      // Compiler-generated function return variables have no declaration
      // statement at which to allocate their activation-local storage.
      if (isReturn) {
        Value local = sim::SimRefAllocOp::create(
            builder, function.getLoc(),
            sim::RefType::get(function.getContext(), type), initial);
        values[path] = local;
        lvalues[path] = local;
      }
      continue;
    }
    Value local = sim::SimRefAllocOp::create(
        builder, function.getLoc(),
        sim::RefType::get(function.getContext(), type), initial);
    values[path] = local;
    lvalues[path] = local;
  }
}

Block *UnitLowering::addBlock() {
  Block *block = new Block();
  function.getBody().push_back(block);
  return block;
}

bool UnitLowering::isCurrentClockingOccurrence(Block *block) const {
  DenseSet<Block *> visiting;
  std::function<bool(Block *)> reachesOccurrence = [&](Block *candidate) {
    if (clockingEventContinuations.contains(candidate))
      return true;
    if (timingBoundaryContinuations.contains(candidate))
      return false;
    // A backedge within the currently inspected predecessor SCC does not
    // introduce a new entry path. Its external predecessors decide whether
    // the whole zero-time loop remains in the clocking occurrence.
    if (!visiting.insert(candidate).second)
      return true;
    auto predecessors = candidate->getPredecessors();
    if (predecessors.empty()) {
      visiting.erase(candidate);
      return false;
    }
    bool all = llvm::all_of(predecessors, reachesOccurrence);
    visiting.erase(candidate);
    return all;
  };
  return reachesOccurrence(block);
}

void UnitLowering::setCurrent(Block *block) {
  current = block;
  builder.setInsertionPointToEnd(block);
}

Type UnitLowering::getReferenceElementType(Value reference) const {
  if (auto type = dyn_cast<sim::RefType>(reference.getType()))
    return type.getElementType();
  if (auto type = dyn_cast<sim::ManagedRefType>(reference.getType()))
    return type.getElementType();
  if (auto type = dyn_cast<sim::ArgumentRefType>(reference.getType()))
    return type.getElementType();
  if (auto type = dyn_cast<sim::ReferencePathType>(reference.getType()))
    return type.getElementType();
  return {};
}

Value UnitLowering::cloneSequentialValue(Value value, Location location) {
  Type type = value.getType();
  if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(type))
    return sim::SimContainerCloneOp::create(builder, location, type, value);
  if (!isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type))
    return value;

  SmallVector<Value> elements;
  unsigned count = sim::getAggregateNumElements(type);
  elements.reserve(count);
  for (unsigned index = 0; index < count; ++index) {
    Type elementType = sim::getAggregateElementType(type, index);
    Value element = sim::SimAggregateExtractOp::create(
        builder, location, elementType, value, index);
    elements.push_back(cloneSequentialValue(element, location));
  }
  return sim::SimAggregateConstructOp::create(builder, location, type,
                                              elements);
}

FailureOr<Value> UnitLowering::ensureSequentialContainer(Value value,
                                                         Location location) {
  Type type = value.getType();
  Type elementType;
  uint32_t containerKind;
  uint64_t bound = 0;
  if (auto array = dyn_cast<sim::DynamicArrayType>(type)) {
    elementType = array.getElementType();
    containerKind = OBELISK_RT_CONTAINER_DYNAMIC_ARRAY;
  } else if (auto queue = dyn_cast<sim::QueueType>(type)) {
    elementType = queue.getElementType();
    containerKind = OBELISK_RT_CONTAINER_QUEUE;
    bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
  } else {
    return failure();
  }
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(elementType, location);
  if (failed(descriptor))
    return failure();
  Value isNull = sim::SimManagedIsNullOp::create(builder, location,
                                                 builder.getI1Type(), value);
  Block *create = addBlock();
  Block *resume = addBlock();
  resume->addArgument(type, location);
  cf::CondBranchOp::create(builder, location, isNull, create, ValueRange{},
                           resume, ValueRange{value});
  setCurrent(create);
  Value size = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
  Value allocated = sim::SimContainerCreateOp::create(
      builder, location, type, size, descriptor->typeID, descriptor->kind,
      descriptor->flags, descriptor->valueSize, descriptor->alignment,
      descriptor->bitWidth,
      builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
      builder.getDenseI32ArrayAttr(descriptor->traceKinds), containerKind,
      bound);
  cf::BranchOp::create(builder, location, resume, ValueRange{allocated});
  setCurrent(resume);
  return resume->getArgument(0);
}

FailureOr<Value> UnitLowering::createAssocArray(sim::AssocArrayType type,
                                                Location location) {
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(type.getElementType(), location);
  if (failed(descriptor))
    return failure();
  bool stringKey = isa<sim::StringType>(type.getKeyType());
  bool classKey = isa<sim::ClassHandleType>(type.getKeyType());
  bool processKey = isa<sim::ProcessType>(type.getKeyType());
  std::optional<unsigned> width = stringKey || classKey || processKey
                                      ? std::optional<unsigned>(0)
                                      : sim::getPackedWidth(type.getKeyType());
  if (!width || (!stringKey && !classKey && !processKey && *width == 0)) {
    emitError(location)
        << "associative array key must be string, class, process, or integral";
    return failure();
  }
  uint32_t keyKind =
      stringKey ? OBELISK_RT_ASSOC_KEY_STRING
      : classKey ? OBELISK_RT_ASSOC_KEY_CLASS
      : processKey ? OBELISK_RT_ASSOC_KEY_PROCESS
                 : (type.getSignedKey() ? OBELISK_RT_ASSOC_KEY_SIGNED
                                        : OBELISK_RT_ASSOC_KEY_UNSIGNED);
  return sim::SimAssocCreateOp::create(
             builder, location, type, descriptor->typeID, descriptor->kind,
             descriptor->flags, descriptor->valueSize, descriptor->alignment,
             descriptor->bitWidth,
             builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
             builder.getDenseI32ArrayAttr(descriptor->traceKinds), keyKind,
             *width)
      .getResult();
}

FailureOr<Value> UnitLowering::ensureAssocArray(Value value,
                                                Location location) {
  auto type = dyn_cast<sim::AssocArrayType>(value.getType());
  if (!type)
    return failure();
  Value isNull = sim::SimManagedIsNullOp::create(builder, location,
                                                 builder.getI1Type(), value);
  Block *create = addBlock();
  Block *resume = addBlock();
  resume->addArgument(type, location);
  cf::CondBranchOp::create(builder, location, isNull, create, ValueRange{},
                           resume, ValueRange{value});
  setCurrent(create);
  FailureOr<Value> allocated = createAssocArray(type, location);
  if (failed(allocated))
    return failure();
  cf::BranchOp::create(builder, location, resume, ValueRange{*allocated});
  setCurrent(resume);
  return resume->getArgument(0);
}

FailureOr<std::pair<Value, Value>>
UnitLowering::traverseAssoc(Value array, Value key, int32_t direction,
                            bool endpoint, Location location) {
  auto type = dyn_cast<sim::AssocArrayType>(array.getType());
  if (!type || key.getType() != type.getKeyType() ||
      (direction != -1 && direction != 1))
    return failure();
  Value isNull = sim::SimManagedIsNullOp::create(builder, location,
                                                 builder.getI1Type(), array);
  Block *empty = addBlock();
  Block *present = addBlock();
  Block *resume = addBlock();
  resume->addArgument(type.getKeyType(), location);
  resume->addArgument(builder.getI1Type(), location);
  cf::CondBranchOp::create(builder, location, isNull, empty, ValueRange{},
                           present, ValueRange{});
  setCurrent(empty);
  Value falseValue = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(false));
  cf::BranchOp::create(builder, location, resume, ValueRange{key, falseValue});
  setCurrent(present);
  auto traversed = sim::SimAssocTraverseOp::create(
      builder, location, type.getKeyType(), builder.getI1Type(), array, key,
      static_cast<uint32_t>(direction), endpoint);
  cf::BranchOp::create(
      builder, location, resume,
      ValueRange{traversed.getResultKey(), traversed.getSuccess()});
  setCurrent(resume);
  return std::pair<Value, Value>{resume->getArgument(0),
                                 resume->getArgument(1)};
}

FailureOr<Value> UnitLowering::loadReference(Value reference,
                                             Location location) {
  if (auto type = dyn_cast<sim::RefType>(reference.getType())) {
    if (sampleAssertionValues && sim::getPackedWidth(type.getElementType()))
      return sim::SimSampledReadOp::create(
                 builder, location, type.getElementType(),
                 function.getBody().front().getArgument(0), reference)
          .getResult();
    return sim::SimRefLoadOp::create(builder, location, type.getElementType(),
                                     reference)
        .getResult();
  }
  if (auto type = dyn_cast<sim::ManagedRefType>(reference.getType())) {
    recordManagedRead(reference, location);
    return sim::SimManagedLoadOp::create(builder, location,
                                         type.getElementType(), reference)
        .getResult();
  }
  if (auto type = dyn_cast<sim::ArgumentRefType>(reference.getType()))
    return sim::SimArgumentRefLoadOp::create(builder, location,
                                             type.getElementType(), reference)
        .getResult();
  if (auto type = dyn_cast<sim::ReferencePathType>(reference.getType())) {
    Type argumentType =
        sim::ArgumentRefType::get(function.getContext(), type.getElementType());
    Value argument = sim::SimArgumentRefFromPathOp::create(
        builder, location, argumentType, reference);
    return sim::SimArgumentRefLoadOp::create(builder, location,
                                             type.getElementType(), argument)
        .getResult();
  }
  return failure();
}

LogicalResult UnitLowering::storeReference(Value reference, Value value,
                                           Location location) {
  value = cloneSequentialValue(value, location);
  if (isa<sim::RefType>(reference.getType()))
    sim::SimRefStoreOp::create(builder, location, value, reference);
  else if (isa<sim::ManagedRefType>(reference.getType()))
    sim::SimManagedStoreOp::create(builder, location, value, reference);
  else if (isa<sim::ArgumentRefType>(reference.getType()))
    sim::SimArgumentRefStoreOp::create(builder, location, value, reference);
  else if (auto type = dyn_cast<sim::ReferencePathType>(reference.getType())) {
    Type argumentType =
        sim::ArgumentRefType::get(function.getContext(), type.getElementType());
    Value argument = sim::SimArgumentRefFromPathOp::create(
        builder, location, argumentType, reference);
    sim::SimArgumentRefStoreOp::create(builder, location, value, argument);
  } else
    return failure();
  return success();
}

FailureOr<Value> UnitLowering::toArgumentReference(Value reference,
                                                   Type elementType,
                                                   Location location) {
  if (getReferenceElementType(reference) != elementType)
    return failure();
  Type resultType =
      sim::ArgumentRefType::get(function.getContext(), elementType);
  if (isa<sim::ArgumentRefType>(reference.getType()))
    return reference;
  if (isa<sim::RefType>(reference.getType())) {
    recordSensitivity(reference);
    return sim::SimArgumentRefFromRefOp::create(builder, location, resultType,
                                                reference)
        .getResult();
  }
  if (isa<sim::ManagedRefType>(reference.getType()))
    return sim::SimArgumentRefFromManagedOp::create(builder, location,
                                                    resultType, reference)
        .getResult();
  if (isa<sim::ReferencePathType>(reference.getType()))
    return sim::SimArgumentRefFromPathOp::create(builder, location, resultType,
                                                 reference)
        .getResult();
  return failure();
}

void UnitLowering::emitBranch(Block *destination) {
  if (current->empty() || !current->back().hasTrait<OpTrait::IsTerminator>())
    cf::BranchOp::create(builder, function.getLoc(), destination);
}

void UnitLowering::emitControlLeaves(size_t first, Location location) {
  for (const ControlScope &scope :
       llvm::reverse(ArrayRef(controlScopes).drop_front(first)))
    sim::SimControlLeaveOp::create(builder, location, scope.activation);
}

InFlightDiagnostic UnitLowering::unsupported(Operation *op) {
  return emitError(getSemanticLocation(op))
         << "unsupported semantic node in the first simulation slice: "
         << op->getName();
}

void UnitLowering::recordSensitivity(Value value) {
  if (isa<sim::EventType, sim::ManagedWatchType>(value.getType())) {
    if (observedDependencies)
      observedDependencies->insert(value);
    return;
  }
  if (!isa<sim::RefType, sim::NetType>(value.getType()))
    return;
  if (observedDependencies)
    observedDependencies->insert(value);
  if (auto argument = dyn_cast<BlockArgument>(value);
      argument && argument.getOwner() == &function.getBody().front())
    sensitivity.insert(value);
}

void UnitLowering::recordManagedRead(Value reference, Location location) {
  if (!observedDependencies ||
      !isa<sim::ManagedRefType>(reference.getType()))
    return;
  Value watch = sim::SimManagedWatchOp::create(
      builder, location, sim::ManagedWatchType::get(function.getContext()),
      reference, sim::ManagedWatchKind::Field);
  recordSensitivity(watch);
}

void UnitLowering::recordContainerSizeRead(Value container,
                                           Location location) {
  if (!observedDependencies ||
      !isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
          container.getType()))
    return;
  Value watch = sim::SimManagedWatchOp::create(
      builder, location, sim::ManagedWatchType::get(function.getContext()),
      container, sim::ManagedWatchKind::ContainerSize);
  recordSensitivity(watch);
}

void UnitLowering::recordImplicitWrite(Value value) {
  if (!observedWrites)
    return;
  // Match a write through a constant or dynamic view with reads of the
  // captured declaration-level reference.
  while (value) {
    if (auto extract = value.getDefiningOp<sim::SimRefExtractOp>()) {
      value = extract.getInput();
      continue;
    }
    if (auto extract = value.getDefiningOp<sim::SimRefDynExtractOp>()) {
      value = extract.getInput();
      continue;
    }
    if (auto extract = value.getDefiningOp<sim::SimRefSubelementOp>()) {
      value = extract.getInput();
      continue;
    }
    if (auto extract = value.getDefiningOp<sim::SimRefArrayElementOp>()) {
      value = extract.getInput();
      continue;
    }
    break;
  }
  if (value && isa<sim::RefType>(value.getType()))
    observedWrites->insert(value);
}

FailureOr<Value>
UnitLowering::bindObserver(Operation *expression,
                          ValueRange dynamicDependencies) {
  Location location = getSemanticLocation(expression);
  auto evaluator =
      expression->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.observer");
  auto capturePaths =
      expression->getAttrOfType<ArrayAttr>("obelisk_sim.observer_captures");
  auto dependencyPaths =
      expression->getAttrOfType<ArrayAttr>("obelisk_sim.observer_dependencies");
  auto resultKind =
      expression->getAttrOfType<IntegerAttr>(observerResultAttrName);
  if (!evaluator || !capturePaths || !dependencyPaths || !resultKind) {
    emitError(location) << "computed timing expression has no observer binding";
    return failure();
  }
  std::optional<ObserverResult> parsedResult = parseObserverResult(resultKind);
  if (!parsedResult) {
    emitError(location) << "unknown observer result kind "
                        << resultKind.getValue().getZExtValue();
    return failure();
  }
  SmallVector<Value> captures;
  SmallVector<Value> dependencies;
  auto resolve = [&](Attribute pathAttr) -> FailureOr<Value> {
    auto path = dyn_cast<StringAttr>(pathAttr);
    if (!path)
      return emitError(location) << "observer path is not a string", failure();
    Value value = values.lookup(path.getValue());
    if (!value)
      value = lvalues.lookup(path.getValue());
    if (!value)
      return emitError(location)
                 << "observer capture has no frozen local binding: "
                 << path.getValue(),
             failure();
    return value;
  };
  for (Attribute path : capturePaths) {
    FailureOr<Value> value = resolve(path);
    if (failed(value))
      return failure();
    captures.push_back(*value);
  }
  for (Attribute path : dependencyPaths) {
    FailureOr<Value> value = resolve(path);
    if (failed(value))
      return failure();
    if (!isa<sim::RefType, sim::NetType, sim::EventType>((*value).getType())) {
      emitError(location) << "observer dependency is not a watchable handle: "
                          << (*value).getType();
      return failure();
    }
    dependencies.push_back(*value);
  }
  for (Value dependency : dynamicDependencies) {
    if (!isa<sim::ManagedWatchType>(dependency.getType())) {
      emitError(location)
          << "dynamic observer dependency is not a managed-watch handle: "
          << dependency.getType();
      return failure();
    }
    if (!llvm::is_contained(dependencies, dependency))
      dependencies.push_back(dependency);
  }
  Type resultType;
  if (*parsedResult == ObserverResult::Truth ||
      *parsedResult == ObserverResult::Event) {
    resultType = builder.getI1Type();
  } else {
    FailureOr<Type> normalized = getNormalizedSemanticType(expression);
    if (failed(normalized))
      return failure();
    resultType = isa<FloatType>(*normalized)
                     ? *normalized
                     : sim::getPackedScalarType(*normalized);
    if (!resultType) {
      emitError(location)
          << "observer expression does not have a packed scalar result";
      return failure();
    }
  }
  SmallVector<Value> operands(captures);
  llvm::append_range(operands, dependencies);
  auto binding = sim::SimObserverBindOp::create(
      builder, location,
      sim::ObserverType::get(function.getContext(), resultType), evaluator,
      operands,
      builder.getI32IntegerAttr(static_cast<uint32_t>(captures.size())));
  if (*parsedResult == ObserverResult::Event)
    binding->setAttr(observerEventPrimaryAttrName, builder.getUnitAttr());
  return binding.getResult();
}

//===----------------------------------------------------------------------===//
// Normalized value conversions
//===----------------------------------------------------------------------===//

FailureOr<Value> UnitLowering::convert(Value value, Type targetType,
                                       bool sourceSigned, Location location,
                                       bool targetSigned) {
  if (value.getType() == targetType)
    return value;
  if (isa<sim::StringType>(targetType)) {
    FailureOr<Value> packed = toPackedScalar(value, location);
    if (failed(packed))
      return failure();
    return sim::SimStringFromPackedOp::create(builder, location, targetType,
                                              *packed)
        .getResult();
  }
  if (isa<sim::StringType>(value.getType())) {
    Type scalarType = sim::getPackedScalarType(targetType);
    std::optional<unsigned> width =
        scalarType ? sim::getPackedWidth(scalarType) : std::nullopt;
    if (!scalarType || !width) {
      emitError(location) << "cannot convert string to " << targetType;
      return failure();
    }
    Type bitsType = IntegerType::get(value.getContext(), *width);
    Value bits =
        sim::SimStringToPackedOp::create(builder, location, bitsType, value);
    Value scalar = bits;
    if (isa<sim::LogicType>(scalarType))
      scalar =
          sim::SimLogicFromBitsOp::create(builder, location, scalarType, bits);
    if (scalarType == targetType)
      return scalar;
    return sim::SimPackedUnflattenOp::create(builder, location, targetType,
                                             scalar)
        .getResult();
  }
  if (isa<sim::ClassHandleType>(value.getType()) &&
      isa<sim::ClassHandleType>(targetType))
    return sim::SimClassCastOp::create(builder, location, targetType, value)
        .getResult();
  if (isa<sim::VirtualInterfaceType>(value.getType()) &&
      isa<sim::VirtualInterfaceType>(targetType)) {
    auto source = cast<sim::VirtualInterfaceType>(value.getType());
    auto target = cast<sim::VirtualInterfaceType>(targetType);
    if (source.getInterfaceName() != target.getInterfaceName()) {
      emitError(location)
          << "cannot convert between different virtual-interface "
             "specializations";
      return failure();
    }
    StringRef sourceModport = source.getModport().getValue();
    StringRef targetModport = target.getModport().getValue();
    if (!sourceModport.empty() && sourceModport != targetModport) {
      emitError(location)
          << "cannot remove or change a virtual-interface modport view";
      return failure();
    }
    return sim::SimVirtualInterfaceCastOp::create(builder, location, targetType,
                                                  value)
        .getResult();
  }
  if (isa<sim::DynamicArrayType, sim::QueueType>(value.getType()) &&
      isa<sim::DynamicArrayType, sim::QueueType>(targetType)) {
    Type sourceElement =
        isa<sim::DynamicArrayType>(value.getType())
            ? cast<sim::DynamicArrayType>(value.getType()).getElementType()
            : cast<sim::QueueType>(value.getType()).getElementType();
    Type targetElement =
        isa<sim::DynamicArrayType>(targetType)
            ? cast<sim::DynamicArrayType>(targetType).getElementType()
            : cast<sim::QueueType>(targetType).getElementType();
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(targetElement, location);
    if (failed(descriptor))
      return failure();
    Value size = sim::SimContainerSizeOp::create(builder, location,
                                                 builder.getI64Type(), value);
    uint32_t containerKind = isa<sim::DynamicArrayType>(targetType)
                                 ? OBELISK_RT_CONTAINER_DYNAMIC_ARRAY
                                 : OBELISK_RT_CONTAINER_QUEUE;
    uint64_t bound = 0;
    if (auto queue = dyn_cast<sim::QueueType>(targetType))
      bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
    Value allocationSize = containerKind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY
                               ? size
                               : Value(arith::ConstantOp::create(
                                     builder, location, builder.getI64Type(),
                                     builder.getI64IntegerAttr(0)));
    Value result = sim::SimContainerCreateOp::create(
        builder, location, targetType, allocationSize, descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds), containerKind,
        bound);
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *exit = addBlock();
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    cf::BranchOp::create(builder, location, header, ValueRange{zero});
    setCurrent(header);
    Value index = header->getArgument(0);
    Value more = arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::ult, index, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{});
    setCurrent(body);
    Value source = sim::SimContainerReadOp::create(builder, location,
                                                   sourceElement, value, index);
    FailureOr<Value> converted =
        convert(source, targetElement, sourceSigned, location, targetSigned);
    if (failed(converted))
      return failure();
    sim::SimContainerWriteOp::create(builder, location, result, index,
                                     *converted);
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    setCurrent(exit);
    return result;
  }
  if (isa<FloatType>(value.getType()) && isa<FloatType>(targetType)) {
    auto source = cast<FloatType>(value.getType());
    auto target = cast<FloatType>(targetType);
    if (source.getWidth() < target.getWidth())
      return arith::ExtFOp::create(builder, location, target, value)
          .getResult();
    return arith::TruncFOp::create(builder, location, target, value)
        .getResult();
  }
  if (targetType.isF32()) {
    if (isa<IntegerType>(value.getType()))
      return sim::SimRealFromIntegerOp::create(builder, location, targetType,
                                               value, sourceSigned)
          .getResult();
  }
  if (targetType.isF64()) {
    if (auto sourceInt = dyn_cast<IntegerType>(value.getType()))
      return sim::SimRealFromIntegerOp::create(
                 builder, location, targetType, value,
                 builder.getBoolAttr(sourceSigned))
          .getResult();
    if (auto sourceLogic = dyn_cast<sim::LogicType>(value.getType())) {
      Type bitsType =
          IntegerType::get(value.getContext(), sourceLogic.getWidth());
      Value bits =
          sim::SimLogicToBitsOp::create(builder, location, bitsType, value);
      Value roundTrip =
          sim::SimLogicFromBitsOp::create(builder, location, sourceLogic, bits);
      Value known = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
          value, roundTrip);
      Value zero = arith::ConstantOp::create(
          builder, location, bitsType, builder.getIntegerAttr(bitsType, 0));
      Value normalized =
          arith::SelectOp::create(builder, location, known, bits, zero);
      return sim::SimRealFromIntegerOp::create(
                 builder, location, targetType, normalized,
                 builder.getBoolAttr(sourceSigned))
          .getResult();
    }
  }
  if (value.getType().isF64()) {
    if (auto targetInt = dyn_cast<IntegerType>(targetType))
      return sim::SimRealToIntegerOp::create(builder, location, targetInt,
                                             value,
                                             builder.getBoolAttr(targetSigned))
          .getResult();
    if (auto targetLogic = dyn_cast<sim::LogicType>(targetType)) {
      Type bitsType =
          IntegerType::get(value.getContext(), targetLogic.getWidth());
      Value bits =
          sim::SimRealToIntegerOp::create(builder, location, bitsType, value,
                                          builder.getBoolAttr(targetSigned));
      return sim::SimLogicFromBitsOp::create(builder, location, targetLogic,
                                             bits)
          .getResult();
    }
  }
  if (value.getType().isF32()) {
    Value wide =
        arith::ExtFOp::create(builder, location, builder.getF64Type(), value);
    return convert(wide, targetType, sourceSigned, location, targetSigned);
  }
  if (auto sourceArray = dyn_cast<sim::UnpackedArrayType>(value.getType())) {
    if (auto targetArray = dyn_cast<sim::UnpackedArrayType>(targetType)) {
      unsigned sourceCount = sim::getAggregateNumElements(sourceArray);
      unsigned targetCount = sim::getAggregateNumElements(targetArray);
      if (sourceCount != targetCount) {
        emitError(location)
            << "cannot convert unpacked array " << value.getType() << " to "
            << targetType << " because their sizes differ";
        return failure();
      }
      SmallVector<Value> elements;
      elements.reserve(sourceCount);
      for (unsigned ordinal = 0; ordinal < sourceCount; ++ordinal) {
        Type sourceElement = sim::getAggregateElementType(sourceArray, ordinal);
        Type targetElement = sim::getAggregateElementType(targetArray, ordinal);
        Value element = sim::SimAggregateExtractOp::create(
            builder, location, sourceElement, value, ordinal);
        FailureOr<Value> converted = convert(
            element, targetElement, sourceSigned, location, targetSigned);
        if (failed(converted))
          return failure();
        elements.push_back(*converted);
      }
      return sim::SimAggregateConstructOp::create(builder, location, targetType,
                                                  elements)
          .getResult();
    }
  }
  if (sim::isAggregateType(value.getType())) {
    Type scalarType = sim::getPackedScalarType(value.getType());
    if (!scalarType) {
      emitError(location) << "cannot convert unpacked aggregate "
                          << value.getType() << " to " << targetType;
      return failure();
    }
    Value flattened =
        sim::SimPackedFlattenOp::create(builder, location, scalarType, value);
    return convert(flattened, targetType, sourceSigned, location, targetSigned);
  }
  if (sim::isAggregateType(targetType)) {
    Type scalarType = sim::getPackedScalarType(targetType);
    if (!scalarType) {
      emitError(location) << "cannot convert " << value.getType()
                          << " to unpacked aggregate " << targetType;
      return failure();
    }
    FailureOr<Value> converted =
        convert(value, scalarType, sourceSigned, location, targetSigned);
    if (failed(converted))
      return failure();
    return sim::SimPackedUnflattenOp::create(builder, location, targetType,
                                             *converted)
        .getResult();
  }
  if (auto sourceInt = dyn_cast<IntegerType>(value.getType())) {
    if (auto targetInt = dyn_cast<IntegerType>(targetType)) {
      if (sourceInt.getWidth() > targetInt.getWidth())
        return arith::TruncIOp::create(builder, location, targetInt, value)
            .getResult();
      if (sourceInt.getWidth() < targetInt.getWidth()) {
        if (sourceSigned)
          return arith::ExtSIOp::create(builder, location, targetInt, value)
              .getResult();
        return arith::ExtUIOp::create(builder, location, targetInt, value)
            .getResult();
      }
    }
    if (auto targetLogic = dyn_cast<sim::LogicType>(targetType)) {
      Value resized = value;
      auto intermediate =
          IntegerType::get(value.getContext(), targetLogic.getWidth());
      if (sourceInt != intermediate) {
        FailureOr<Value> converted =
            convert(value, intermediate, sourceSigned, location);
        if (failed(converted))
          return failure();
        resized = *converted;
      }
      return sim::SimLogicFromBitsOp::create(builder, location, targetLogic,
                                             resized)
          .getResult();
    }
  }
  if (auto sourceLogic = dyn_cast<sim::LogicType>(value.getType())) {
    if (auto targetLogic = dyn_cast<sim::LogicType>(targetType))
      return sim::SimLogicResizeOp::create(builder, location, targetLogic,
                                           value,
                                           builder.getBoolAttr(sourceSigned))
          .getResult();
    if (auto targetInt = dyn_cast<IntegerType>(targetType)) {
      Value resized = value;
      if (sourceLogic.getWidth() != targetInt.getWidth())
        resized =
            sim::SimLogicResizeOp::create(
                builder, location,
                sim::LogicType::get(value.getContext(), targetInt.getWidth()),
                value, builder.getBoolAttr(sourceSigned))
                .getResult();
      return sim::SimLogicToBitsOp::create(builder, location, targetInt,
                                           resized)
          .getResult();
    }
  }
  emitError(location) << "unsupported normalized conversion from "
                      << value.getType() << " to " << targetType;
  return failure();
}

FailureOr<Value> UnitLowering::toPackedScalar(Value value, Location location) {
  Type scalarType = sim::getPackedScalarType(value.getType());
  if (!scalarType) {
    emitError(location) << "operand is not a packed value: " << value.getType();
    return failure();
  }
  if (scalarType == value.getType())
    return value;
  return sim::SimPackedFlattenOp::create(builder, location, scalarType, value)
      .getResult();
}

FailureOr<Value> UnitLowering::formatTaggedUnionPattern(Value value,
                                                        Type semanticType,
                                                        Location location) {
  auto unionType = dyn_cast<sim::UnpackedUnionType>(value.getType());
  auto sourceType = dyn_cast<semantic::SourceAggregateType>(semanticType);
  if (!unionType || !unionType.getIsTagged() || !sourceType ||
      !sourceType.getIsUnion() || !sourceType.getIsTagged() ||
      sourceType.getFields().size() != unionType.getFields().size())
    return failure();
  if (sourceType.getIsFourState()) {
    emitError(location)
        << "four-state tagged-union pattern formatting is unsupported";
    return failure();
  }

  Type stringType = sim::StringType::get(function.getContext());
  auto literal = [&](StringRef text) {
    return sim::SimStringLiteralOp::create(builder, location, stringType, text)
        .getResult();
  };
  Value pattern = literal("'{}");
  for (auto [index, attribute] : llvm::enumerate(unionType.getFields())) {
    auto field = cast<sim::FieldAttr>(attribute);
    auto sourceField = cast<DictionaryAttr>(sourceType.getFields()[index]);
    Type sourceFieldType = cast<TypeAttr>(sourceField.get("type")).getValue();
    Type fieldType = field.getType();
    Value candidate;
    if (isa<semantic::VoidType>(sourceFieldType)) {
      candidate =
          literal((Twine("'{") + field.getName().getValue() + "}").str());
    } else {
      Value member = sim::SimUnionExtractOp::create(builder, location,
                                                    fieldType, value, index);
      FailureOr<Value> scalar = toPackedScalar(member, location);
      if (failed(scalar))
        return failure();
      std::optional<unsigned> width = sim::getPackedWidth((*scalar).getType());
      if (!width || *width > 64) {
        emitError(location)
            << "tagged-union pattern member is not a scalar of at most 64 bits";
        return failure();
      }
      bool isSigned = isSignedSemanticType(sourceFieldType);
      FailureOr<Value> integer =
          convert(*scalar, builder.getI64Type(), isSigned, location);
      if (failed(integer))
        return failure();
      Value formatted = sim::SimStringFormatIntegerOp::create(
          builder, location, stringType, *integer,
          builder.getI32IntegerAttr(10), builder.getBoolAttr(isSigned));
      SmallVector<Value> parts{
          literal((Twine("'{") + field.getName().getValue() + ":").str()),
          formatted, literal("}")};
      candidate =
          sim::SimStringConcatOp::create(builder, location, stringType, parts);
    }
    Value active = sim::SimUnionIsActiveOp::create(
        builder, location, builder.getI1Type(), value, index);
    pattern =
        arith::SelectOp::create(builder, location, active, candidate, pattern);
  }
  return pattern;
}

FailureOr<Value> UnitLowering::truthValue(Value value, Location location) {
  if (isa<sim::ChandleType>(value.getType())) {
    Value null = sim::SimChandleNullOp::create(builder, location);
    Value isNull =
        sim::SimChandleEqualOp::create(builder, location, value, null);
    return arith::XOrIOp::create(
               builder, location, isNull,
               arith::ConstantOp::create(builder, location,
                                         builder.getI1Type(),
                                         builder.getBoolAttr(true)))
        .getResult();
  }
  if (isa<sim::StringType>(value.getType())) {
    Value length = sim::SimStringLengthOp::create(builder, location,
                                                  builder.getI64Type(), value);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                 length, zero)
        .getResult();
  }
  if (isa<FloatType>(value.getType())) {
    Value zero =
        arith::ConstantOp::create(builder, location, value.getType(),
                                  builder.getFloatAttr(value.getType(), 0.0));
    return arith::CmpFOp::create(builder, location, arith::CmpFPredicate::UNE,
                                 value, zero)
        .getResult();
  }
  FailureOr<Value> scalar = toPackedScalar(value, location);
  if (failed(scalar))
    return failure();
  value = *scalar;
  if (isa<sim::LogicType>(value.getType()))
    return sim::SimLogicIsTrueOp::create(builder, location, builder.getI1Type(),
                                         value)
        .getResult();
  auto integer = dyn_cast<IntegerType>(value.getType());
  if (!integer) {
    emitError(location) << "condition is not a packed value: "
                        << value.getType();
    return failure();
  }
  Value zero = arith::ConstantOp::create(builder, location, integer,
                                         builder.getIntegerAttr(integer, 0));
  return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                               value, zero)
      .getResult();
}

FailureOr<Value> UnitLowering::toLogic(Value value, Location location) {
  FailureOr<Value> scalar = toPackedScalar(value, location);
  if (failed(scalar))
    return failure();
  value = *scalar;
  if (isa<sim::LogicType>(value.getType()))
    return value;
  auto integer = dyn_cast<IntegerType>(value.getType());
  if (!integer) {
    emitError(location) << "operand is not a packed value: " << value.getType();
    return failure();
  }
  return sim::SimLogicFromBitsOp::create(
             builder, location,
             sim::LogicType::get(function.getContext(), integer.getWidth()),
             value)
      .getResult();
}

LogicalResult UnitLowering::emitFunctionReturn(
    Location location, std::optional<Value> explicitResult, bool resultSigned) {
  if (function.getEntryKind() == sim::EntryKind::Task) {
    if (explicitResult) {
      emitError(location) << "task return cannot carry a value";
      return failure();
    }
    for (StringRef path : copyOutPaths) {
      Value storage = lvalues.lookup(path);
      Value destination = copyOutDestinations.lookup(path);
      if (!storage || !destination || !isa<sim::RefType>(storage.getType()) ||
          storage.getType() != destination.getType()) {
        function.emitError()
            << "task copy-out formal has inconsistent activation storage: "
            << path;
        return failure();
      }
      Value value = sim::SimRefLoadOp::create(
          builder, location,
          cast<sim::RefType>(storage.getType()).getElementType(), storage);
      if (failed(storeReference(destination, value, location)))
        return failure();
    }
    if (taskControlActivation)
      sim::SimControlLeaveOp::create(builder, location, taskControlActivation);
    sim::SimReturnOp::create(builder, location, ValueRange{});
    return success();
  }
  if (function.getEntryKind() != sim::EntryKind::Function) {
    if (explicitResult) {
      emitError(location) << "non-function entry cannot return a value";
      return failure();
    }
    sim::SimReturnOp::create(builder, location, ValueRange{});
    return success();
  }

  TypeRange resultTypes = function.getFunctionType().getResults();
  bool hasPrimaryResult =
      !function->hasAttr("obelisk_sim.constructor") &&
      !function->hasAttr("obelisk_sim.static_initializer") &&
      !function->hasAttr("obelisk_sim.void_function");
  if (!hasPrimaryResult && explicitResult) {
    emitError(location) << "constructor or initializer cannot return a value";
    return failure();
  }
  if (hasPrimaryResult && resultTypes.empty()) {
    function.emitError("function signature has no primary result");
    return failure();
  }
  SmallVector<Value> results;
  if (hasPrimaryResult) {
    if (explicitResult) {
      FailureOr<Value> converted =
          convert(*explicitResult, resultTypes.front(), resultSigned, location);
      if (failed(converted))
        return failure();
      results.push_back(cloneSequentialValue(*converted, location));
    } else {
      Value returnStorage = values.lookup(returnPath);
      if (returnStorage && isa<sim::RefType>(returnStorage.getType()))
        results.push_back(cloneSequentialValue(
            sim::SimRefLoadOp::create(
                builder, location,
                cast<sim::RefType>(returnStorage.getType()).getElementType(),
                returnStorage),
            location));
      else {
        Value defaultResult =
            createDefaultValue(builder, location, resultTypes.front());
        if (!defaultResult) {
          function.emitError("cannot materialize the default function result");
          return failure();
        }
        results.push_back(defaultResult);
      }
    }
  }

  unsigned copyOutResultOffset = hasPrimaryResult ? 1 : 0;
  if (resultTypes.size() != copyOutPaths.size() + copyOutResultOffset) {
    function.emitError()
        << "function copy-out result inventory is inconsistent (signature has "
        << resultTypes.size() << ", expected "
        << copyOutPaths.size() + copyOutResultOffset << ")";
    return failure();
  }
  for (auto [index, path] : llvm::enumerate(copyOutPaths)) {
    Value storage = lvalues.lookup(path);
    if (!storage || !isa<sim::RefType>(storage.getType())) {
      function.emitError() << "copy-out formal has no local storage: " << path;
      return failure();
    }
    Value value = sim::SimRefLoadOp::create(
        builder, location,
        cast<sim::RefType>(storage.getType()).getElementType(), storage);
    if (value.getType() != resultTypes[index + copyOutResultOffset]) {
      function.emitError("copy-out formal type does not match its result");
      return failure();
    }
    results.push_back(cloneSequentialValue(value, location));
  }
  sim::SimReturnOp::create(builder, location, results);
  return success();
}

LogicalResult UnitLowering::emitRuntimeFatal(Location location,
                                             StringRef detail) {
  std::string file = "<unknown>";
  unsigned line = 0;
  if (auto source = location->findInstanceOf<FileLineColLoc>()) {
    file = source.getFilename().str();
    line = source.getLine();
  }
  std::string message =
      (Twine("FATAL: ") + file + ":" + Twine(line) + ": " + detail).str();
  for (size_t position = 0;
       (position = message.find('%', position)) != std::string::npos;
       position += 2)
    message.insert(position, 1, '%');

  Value context = function.getBody().front().getArgument(0);
  Value descriptor = arith::ConstantOp::create(
      builder, location, builder.getI32Type(),
      builder.getI32IntegerAttr(static_cast<int32_t>(0x80000002u)));
  Value item =
      sim::SimBytesConstantOp::create(builder, location, message).getResult();
  auto timeMultiplier =
      function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
  StringAttr scope =
      function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
  sim::SimDisplayOp::create(builder, location, context, descriptor,
                            ValueRange{item}, true, 10,
                            builder.getDenseI32ArrayAttr({0}), scope,
                            StringAttr{}, timeMultiplier, IntegerAttr{});
  Value verbosity = arith::ConstantOp::create(
      builder, location, builder.getI32Type(), builder.getI32IntegerAttr(1));
  sim::SimFatalOp::create(builder, location, context, verbosity);
  return emitFunctionReturn(location, std::nullopt, false);
}

// Unsized numeric literals whose most-significant four-state bit is unknown
// fill that bit through a wider context. Slang retains the declared-unsized
// fact separately from the normalized 32-bit constant value.
static bool fillsWidenedUnknown(Operation *source, Type targetType) {
  auto literal = dyn_cast<semantic::SVIntegerLiteralOp>(source);
  auto declaredUnsized = source->getAttrOfType<BoolAttr>("is_declared_unsized");
  if (!literal || !declaredUnsized || !declaredUnsized.getValue())
    return false;
  FailureOr<Type> sourceType = getNormalizedSemanticType(source);
  Type sourceScalar =
      succeeded(sourceType) ? sim::getPackedScalarType(*sourceType) : Type{};
  Type targetScalar = sim::getPackedScalarType(targetType);
  std::optional<unsigned> sourceWidth =
      sourceScalar ? sim::getPackedWidth(sourceScalar) : std::nullopt;
  std::optional<unsigned> targetWidth =
      targetScalar ? sim::getPackedWidth(targetScalar) : std::nullopt;
  std::optional<StringRef> spelling = getConstantSpelling(source);
  if (!sourceWidth || !targetWidth || *targetWidth <= *sourceWidth || !spelling)
    return false;
  FailureOr<ParsedConstant> parsed =
      parseSVInteger(*spelling, *sourceWidth, getSemanticLocation(source));
  return succeeded(parsed) && parsed->unknown.isSignBitSet();
}

// Slang represents context-determined integral conversions explicitly.  Their
// target signedness controls widening (for example, a signed operand in a
// common unsigned binary or case context must be zero-extended), unlike an
// ordinary assignment conversion, which extends according to the source.
FailureOr<Value> UnitLowering::lowerContextDeterminedExpression(Operation *op) {
  auto conversion = dyn_cast<semantic::SVConversionExpressionOp>(op);
  SmallVector<Operation *> children =
      conversion ? getChildren(conversion) : SmallVector<Operation *>{};
  if (!conversion || children.size() != 1)
    return lowerExpression(op);
  FailureOr<Type> target = getNormalizedSemanticType(op);
  if (failed(target) || !sim::getPackedScalarType(*target))
    return lowerExpression(op);
  if (auto streaming = dyn_cast<
          semantic::SVStreamingConcatenationExpressionOp>(children.front()))
    return lowerStreaming(streaming, *target);
  FailureOr<Value> input = lowerExpression(children.front());
  if (failed(input))
    return failure();
  bool signedConversion = sim::getPackedScalarType((*input).getType())
                              ? isSignedNode(op)
                              : isSignedNode(children.front());
  signedConversion |= fillsWidenedUnknown(children.front(), *target);
  return convert(*input, *target, signedConversion, getSemanticLocation(op),
                 isSignedNode(op));
}

FailureOr<Value> UnitLowering::lowerExpression(Operation *op, bool lvalue) {
  if (auto variable = op->getAttrOfType<IntegerAttr>(randomVariableAttrName)) {
    const APInt &indexValue = variable.getValue();
    if (lvalue || indexValue.isNegative() || indexValue.getActiveBits() > 64 ||
        indexValue.getZExtValue() >= randomizeCandidateValues.size()) {
      emitError(getSemanticLocation(op))
          << "randomization candidate binding is invalid";
      return failure();
    }
    uint64_t index = indexValue.getZExtValue();
    return randomizeCandidateValues[index];
  }
  if (isa<semantic::SVEmptyArgumentExpressionOp>(op)) {
    if (expressionPlaceholder)
      return expressionPlaceholder;
    emitError(getSemanticLocation(op))
        << "empty expression placeholder has no resolved value";
    return failure();
  }
  if (isa<semantic::SVUnboundedLiteralOp>(op)) {
    if (unboundedPlaceholder)
      return unboundedPlaceholder;
    emitError(getSemanticLocation(op))
        << "unbounded literal has no resolved container bound";
    return failure();
  }
  if (isa<semantic::SVLValueReferenceExpressionOp>(op)) {
    if (lvalueReferencePlaceholder)
      return lvalueReferencePlaceholder;
    emitError(getSemanticLocation(op))
        << "lvalue-reference placeholder has no resolved value";
    return failure();
  }
  if (op->hasAttr(staticNetConstantAttrName) &&
      isa<semantic::SVNamedValueExpressionOp,
          semantic::SVHierarchicalValueExpressionOp>(op)) {
    if (lvalue) {
      emitError(getSemanticLocation(op))
          << "constant named value is not an lvalue";
      return failure();
    }
    return lowerLiteral(op);
  }
  if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(op))
    return lowerNamedValue(named, lvalue);
  if (auto interface = dyn_cast<semantic::SVArbitrarySymbolExpressionOp>(op)) {
    if (lvalue) {
      emitError(getSemanticLocation(op))
          << "an interface instance is not an assignable value";
      return failure();
    }
    FailureOr<Type> type = getNormalizedSemanticType(op);
    auto virtualType = succeeded(type)
                           ? dyn_cast<sim::VirtualInterfaceType>(*type)
                           : sim::VirtualInterfaceType{};
    auto scope = scopeIDs.find(interface.getReferencedPath());
    if (!virtualType || scope == scopeIDs.end() || scope->second == 0) {
      emitError(getSemanticLocation(op))
          << "interface reference has no executable elaborated scope: "
          << interface.getReferencedPath();
      return failure();
    }
    return sim::SimVirtualInterfaceBindOp::create(
               builder, getSemanticLocation(op), virtualType,
               builder.getI64IntegerAttr(scope->second))
        .getResult();
  }
  if (auto hierarchical =
          dyn_cast<semantic::SVHierarchicalValueExpressionOp>(op))
    return lowerReferencedValue(op, hierarchical.getReferencedPath(), lvalue);
  if (isa<semantic::SVIntegerLiteralOp,
          semantic::SVUnbasedUnsizedIntegerLiteralOp>(op))
    return lowerLiteral(op);
  if (isa<semantic::SVStringLiteralOp>(op)) {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return lowerStringLiteralValue(builder, op, *type, getSemanticLocation(op));
  }
  if (isa<semantic::SVRealLiteralOp, semantic::SVTimeLiteralOp>(op)) {
    Location location = getSemanticLocation(op);
    FailureOr<Type> type = getNormalizedSemanticType(op);
    auto spelling = op->getAttrOfType<StringAttr>("constant_value");
    if (failed(type) || !spelling || !isa<FloatType>(*type)) {
      emitError(location) << "malformed floating-point literal";
      return failure();
    }
    double value = 0.0;
    if (spelling.getValue().getAsDouble(value)) {
      emitError(location) << "floating-point literal is not representable";
      return failure();
    }
    return arith::ConstantOp::create(builder, location, *type,
                                     builder.getFloatAttr(*type, value))
        .getResult();
  }
  if (isa<semantic::SVConversionExpressionOp>(op)) {
    SmallVector<Operation *> children = getChildren(op);
    if (children.size() != 1) {
      unsupported(op) << " (conversion arity)";
      return failure();
    }
    FailureOr<Type> target = getNormalizedSemanticType(op);
    if (failed(target))
      return failure();
    // A string literal's packed representation is only an intermediate
    // expression type. Preserve the literal byte payload when an explicit
    // semantic conversion gives it string context. In particular, Slang
    // represents the empty literal as an 8-bit zero value; converting that
    // packed value would create a one-byte NUL string instead of an empty
    // string.
    if (isa<sim::StringType>(*target) &&
        isa<semantic::SVStringLiteralOp>(children.front()))
      return lowerStringLiteralValue(builder, children.front(), *target,
                                     getSemanticLocation(op));
    if (isa<semantic::SVNullLiteralOp>(children.front())) {
      if (isa<sim::ProcessType>(*target))
        return sim::SimProcessNullOp::create(builder, getSemanticLocation(op))
            .getResult();
      if (isa<sim::ClassHandleType>(*target))
        return sim::SimClassNullOp::create(builder, getSemanticLocation(op),
                                           *target)
            .getResult();
      if (isa<sim::CovergroupHandleType>(*target))
        return sim::SimCovergroupNullOp::create(
                   builder, getSemanticLocation(op), *target)
            .getResult();
      if (isa<sim::VirtualInterfaceType>(*target))
        return sim::SimVirtualInterfaceNullOp::create(
                   builder, getSemanticLocation(op), *target)
            .getResult();
      if (isa<sim::ChandleType>(*target))
        return sim::SimChandleNullOp::create(builder, getSemanticLocation(op))
            .getResult();
    }
    if (auto streaming = dyn_cast<
            semantic::SVStreamingConcatenationExpressionOp>(children.front()))
      return lowerStreaming(streaming, *target);
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    bool sourceSigned = isSignedNode(children.front()) ||
                        fillsWidenedUnknown(children.front(), *target);
    return convert(*input, *target, sourceSigned, getSemanticLocation(op),
                   isSignedNode(op));
  }
  if (isa<semantic::SVCopyClassExpressionOp>(op)) {
    SmallVector<Operation *> children = getChildren(op);
    if (children.size() != 1) {
      unsupported(op) << " (class copy arity)";
      return failure();
    }
    FailureOr<Value> source = lowerExpression(children.front());
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(source) || failed(resultType) ||
        !isa<sim::ClassHandleType>(*resultType))
      return failure();
    FailureOr<Value> converted =
        convert(*source, *resultType, false, getSemanticLocation(op));
    if (failed(converted))
      return failure();
    Value copy = sim::SimClassCopyOp::create(
                     builder, getSemanticLocation(op), *resultType,
                     function.getBody().front().getArgument(0), *converted)
                     .getResult();
    if (failed(initializeObjectRandomStream(copy, getSemanticLocation(op))))
      return failure();
    return copy;
  }
  if (isa<semantic::SVConcatenationExpressionOp>(op))
    return lowerConcatenation(op);
  if (isa<semantic::SVReplicationExpressionOp>(op))
    return lowerReplication(op);
  if (auto streaming =
          dyn_cast<semantic::SVStreamingConcatenationExpressionOp>(op))
    return lowerStreaming(streaming);
  if (auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(op))
    return lowerMember(member, lvalue);
  if (auto tagged = dyn_cast<semantic::SVTaggedUnionExpressionOp>(op))
    return lowerTaggedUnion(tagged);
  if (isa<semantic::SVSimpleAssignmentPatternExpressionOp,
          semantic::SVStructuredAssignmentPatternExpressionOp,
          semantic::SVReplicatedAssignmentPatternExpressionOp>(op))
    return lowerAssignmentPattern(op);
  if (isa<semantic::SVNewArrayExpressionOp>(op))
    return lowerNewArray(op);
  if (isa<semantic::SVRangeSelectExpressionOp,
          semantic::SVElementSelectExpressionOp>(op))
    return lowerSelection(op, lvalue);
  if (auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(op))
    return lowerAssignment(assignment);
  if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(op))
    return lowerUnary(unary);
  if (auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(op))
    return lowerBinary(binary);
  if (auto conditional = dyn_cast<semantic::SVConditionalExpressionOp>(op))
    return lowerConditionalExpression(conditional);
  if (auto inside = dyn_cast<semantic::SVInsideExpressionOp>(op))
    return lowerInside(inside);
  if (auto call = dyn_cast<semantic::SVCallExpressionOp>(op))
    return lowerCall(call);
  if (auto construct = dyn_cast<semantic::SVNewCovergroupExpressionOp>(op))
    return lowerNewCovergroup(construct);
  if (auto construct = dyn_cast<semantic::SVNewClassExpressionOp>(op))
    return lowerNewClass(construct);

  unsupported(op);
  return failure();
}

//===----------------------------------------------------------------------===//
// Statements
//===----------------------------------------------------------------------===//

LogicalResult UnitLowering::lowerSequence(ArrayRef<Operation *> operations) {
  for (Operation *op : operations)
    if (failed(lowerStatement(op)))
      return failure();
  return success();
}

LogicalResult UnitLowering::lowerPrimitive(StringRef name,
                                           ArrayRef<Operation *> operations) {
  Location location = function.getLoc();
  SmallVector<Operation *> outputs;
  size_t inputStart = 0;
  while (inputStart < operations.size()) {
    auto assignment =
        dyn_cast<semantic::SVAssignmentExpressionOp>(operations[inputStart]);
    if (!assignment)
      break;
    SmallVector<Operation *> children = getChildren(assignment);
    if (children.empty())
      return emitError(getSemanticLocation(assignment))
             << "primitive output has no assignment lvalue";
    outputs.push_back(children.front());
    ++inputStart;
  }
  if (outputs.empty())
    return emitError(location) << "primitive has no output";

  bool multipleOutputs = name == "buf" || name == "not";
  if (!multipleOutputs && outputs.size() != 1)
    return emitError(location)
           << "primitive '" << name << "' requires exactly one output";
  ArrayRef<Operation *> inputs = operations.drop_front(inputStart);
  FailureOr<Type> outputType = getNormalizedSemanticType(outputs.front());
  if (failed(outputType))
    return failure();
  Type scalarType = sim::getPackedScalarType(*outputType);
  auto logicType = dyn_cast_or_null<sim::LogicType>(scalarType);
  if (!logicType)
    return emitError(location)
           << "primitive output is not four-state packed data";

  auto lowerInput = [&](Operation *input,
                        Type target = Type{}) -> FailureOr<Value> {
    FailureOr<Value> lowered = lowerExpression(input);
    if (failed(lowered))
      return failure();
    FailureOr<Value> converted =
        convert(*lowered, target ? target : *outputType, isSignedNode(input),
                getSemanticLocation(input));
    if (failed(converted))
      return failure();
    FailureOr<Value> scalar =
        toPackedScalar(*converted, getSemanticLocation(input));
    if (failed(scalar))
      return failure();
    return toLogic(*scalar, getSemanticLocation(input));
  };

  Value result;
  if (name == "and" || name == "nand" || name == "or" || name == "nor" ||
      name == "xor" || name == "xnor") {
    if (inputs.empty())
      return emitError(location)
             << "primitive '" << name << "' requires at least one input";
    FailureOr<Value> first = lowerInput(inputs.front());
    if (failed(first))
      return failure();
    result = *first;
    sim::BinaryKind kind =
        (name == "and" || name == "nand") ? sim::BinaryKind::And
        : (name == "or" || name == "nor") ? sim::BinaryKind::Or
                                          : sim::BinaryKind::Xor;
    for (Operation *input : inputs.drop_front()) {
      FailureOr<Value> next = lowerInput(input);
      if (failed(next))
        return failure();
      result = sim::SimLogicBinaryOp::create(builder, location, logicType, kind,
                                             result, *next);
    }
    if (name == "nand" || name == "nor" || name == "xnor")
      result = sim::SimLogicUnaryOp::create(builder, location, logicType,
                                            sim::UnaryKind::BitNot, result);
  } else if (name == "buf" || name == "not") {
    if (inputs.size() != 1)
      return emitError(location)
             << "primitive '" << name << "' requires exactly one input";
    FailureOr<Value> input = lowerInput(inputs.front());
    if (failed(input))
      return failure();
    result = *input;
    if (name == "not")
      result = sim::SimLogicUnaryOp::create(builder, location, logicType,
                                            sim::UnaryKind::BitNot, result);
  } else if (name == "bufif0" || name == "bufif1" || name == "notif0" ||
             name == "notif1") {
    if (inputs.size() != 2)
      return emitError(location)
             << "primitive '" << name << "' requires data and control inputs";
    FailureOr<Value> data = lowerInput(inputs[0]);
    FailureOr<Value> control =
        lowerInput(inputs[1], sim::LogicType::get(function.getContext(), 1));
    if (failed(data) || failed(control))
      return failure();
    Value driven = *data;
    if (name.starts_with("not"))
      driven = sim::SimLogicUnaryOp::create(builder, location, logicType,
                                            sim::UnaryKind::BitNot, driven);
    auto planeType =
        IntegerType::get(function.getContext(), logicType.getWidth());
    APInt highZ = APInt::getAllOnes(logicType.getWidth());
    Value disabled = sim::SimLogicConstantOp::create(
        builder, location, logicType, builder.getIntegerAttr(planeType, highZ),
        builder.getIntegerAttr(planeType, highZ));
    bool activeHigh = name.ends_with("1");
    result =
        activeHigh
            ? Value(sim::SimLogicMuxOp::create(builder, location, logicType,
                                               *control, driven, disabled))
            : Value(sim::SimLogicMuxOp::create(builder, location, logicType,
                                               *control, disabled, driven));
  } else {
    return emitError(location)
           << "unsupported built-in primitive '" << name << "'";
  }

  FailureOr<Value> converted = convert(result, *outputType, false, location);
  if (failed(converted))
    return failure();
  for (Operation *output : outputs)
    if (failed(writeLValue(output, *converted, false, false,
                           getSemanticLocation(output))))
      return failure();
  return success();
}
LogicalResult UnitLowering::lowerStatement(Operation *op) {
  SmallVector<Operation *> children = getChildren(op);
  Location location = getSemanticLocation(op);
  builder.setInsertionPointToEnd(current);

  if (auto path =
          op->getAttrOfType<StringAttr>("obelisk_sim.initialize_static")) {
    Value destination = lvalues.lookup(path.getValue());
    auto referenceType = destination
                             ? dyn_cast<sim::RefType>(destination.getType())
                             : sim::RefType{};
    if (!referenceType) {
      emitError(location)
          << "static class property initializer has no reference binding: "
          << path.getValue();
      return failure();
    }
    if (auto ordinalAttr = op->getAttrOfType<IntegerAttr>(
            "obelisk_sim.initialize_subelement")) {
      int64_t ordinal = ordinalAttr.getInt();
      Type aggregateType = referenceType.getElementType();
      if (ordinal < 0 || static_cast<uint64_t>(ordinal) >=
                             sim::getAggregateNumElements(aggregateType)) {
        emitError(location)
            << "aggregate member initializer ordinal " << ordinal
            << " is out of range for " << aggregateType;
        return failure();
      }
      Type fieldType =
          sim::getAggregateElementType(aggregateType, unsigned(ordinal));
      destination = sim::SimRefSubelementOp::create(
                        builder, location,
                        sim::RefType::get(function.getContext(), fieldType),
                        destination, builder.getDenseI64ArrayAttr({ordinal}))
                        .getResult();
      referenceType = cast<sim::RefType>(destination.getType());
    }
    FailureOr<Value> value = lowerExpression(op);
    if (failed(value))
      return failure();
    FailureOr<Value> converted = convert(*value, referenceType.getElementType(),
                                         isSignedNode(op), location);
    if (failed(converted))
      return failure();
    sim::SimRefStoreOp::create(builder, location, *converted, destination);
    return success();
  }
  if (auto path = op->getAttrOfType<StringAttr>("obelisk_sim.initialize_net")) {
    Value destination = lvalues.lookup(path.getValue());
    auto driverType = destination
                          ? dyn_cast<sim::DriverType>(destination.getType())
                          : sim::DriverType{};
    if (!driverType) {
      emitError(location) << "net initializer has no driver binding: "
                          << path.getValue();
      return failure();
    }
    FailureOr<Value> value = lowerExpression(op);
    if (failed(value))
      return failure();
    FailureOr<Value> converted = convert(*value, driverType.getElementType(),
                                         isSignedNode(op), location);
    if (failed(converted))
      return failure();
    sim::SimDriverDriveOp::create(builder, location, destination, *converted);
    return success();
  }
  if (auto field = op->getAttrOfType<FlatSymbolRefAttr>(
          "obelisk_sim.initialize_field")) {
    if (!thisObject) {
      emitError(location) << "class property initializer has no this object";
      return failure();
    }
    FailureOr<Value> value = lowerExpression(op);
    if (failed(value))
      return failure();
    auto objectType = cast<sim::ClassHandleType>(thisObject.getType());
    Type referenceType = sim::ManagedRefType::get(
        function.getContext(), (*value).getType(), objectType.getClassName());
    Value reference = sim::SimClassFieldRefOp::create(
        builder, location, referenceType, thisObject, field);
    sim::SimManagedStoreOp::create(builder, location, *value, reference);
    return success();
  }
  if (isa<semantic::SVEmptyStatementOp>(op))
    return success();
  if (auto assertion = dyn_cast<semantic::SVImmediateAssertionStatementOp>(op))
    return lowerImmediateAssertion(assertion);
  if (auto assertion = dyn_cast<semantic::SVConcurrentAssertionStatementOp>(op))
    return lowerConcurrentAssertion(assertion);
  if (isa<semantic::SVExpressionStatementOp>(op)) {
    if (children.size() != 1) {
      unsupported(op) << " (expression statement arity)";
      return failure();
    }
    return success(succeeded(lowerExpression(children.front())));
  }
  if (auto override = dyn_cast<semantic::SVProceduralAssignStatementOp>(op)) {
    if (children.size() != 1) {
      unsupported(op) << " (procedural force/assign arity)";
      return failure();
    }
    auto assignment =
        dyn_cast<semantic::SVAssignmentExpressionOp>(children.front());
    SmallVector<Operation *> assignmentChildren =
        assignment ? getChildren(assignment) : SmallVector<Operation *>{};
    if (!assignment || assignmentChildren.size() != 2) {
      unsupported(op) << " (procedural force/assign expression)";
      return failure();
    }

    Operation *lhs = assignmentChildren[0];
    bool selected = isa<semantic::SVElementSelectExpressionOp,
                        semantic::SVRangeSelectExpressionOp>(lhs);
    if (!isa<semantic::SVNamedValueExpressionOp,
             semantic::SVHierarchicalValueExpressionOp>(lhs) &&
        !selected) {
      emitError(getSemanticLocation(lhs))
          << "force and procedural assign currently require a whole "
             "statically allocated packed variable or whole built-in net";
      return failure();
    }
    FailureOr<Value> target = lowerExpression(lhs, true);
    if (failed(target))
      return failure();
    auto referenceType = dyn_cast<sim::RefType>((*target).getType());
    auto netType = dyn_cast<sim::NetType>((*target).getType());
    if ((!referenceType && !netType) ||
        !isStaticallyAllocatedOverrideTarget(*target)) {
      emitError(getSemanticLocation(lhs))
          << "force and procedural assign require statically allocated "
             "packed storage";
      return failure();
    }
    bool isAssign = !override.getIsForce();
    if (selected && (!netType || isAssign)) {
      emitError(getSemanticLocation(lhs))
          << "only constant built-in net bit and part selects are supported "
             "for force";
      return failure();
    }
    if (isAssign && netType) {
      emitError(getSemanticLocation(lhs))
          << "procedural assign requires a packed variable";
      return failure();
    }

    Operation *rhs = assignmentChildren[1];
    std::function<bool(Operation *)> isConstantRHS =
        [&](Operation *expression) -> bool {
      if (getConstantSpelling(expression) ||
          expression->hasAttr("constant_value"))
        return true;
      StringRef path;
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression))
        path = named.getReferencedPath();
      else if (auto hierarchical =
                   dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                       expression))
        path = hierarchical.getReferencedPath();
      if (!path.empty()) {
        Value bound = values.lookup(path);
        return bound && foldConstantValue(bound);
      }
      if (isa<semantic::SVCallExpressionOp>(expression))
        return false;
      SmallVector<Operation *> operands = getChildren(expression);
      return !operands.empty() &&
             llvm::all_of(operands, [&](Operation *operand) {
               return isConstantRHS(operand);
             });
    };
    if (!isConstantRHS(rhs)) {
      emitError(getSemanticLocation(rhs))
          << "signal-dependent force and procedural assign right-hand sides "
             "are not yet supported";
      return failure();
    }
    FailureOr<Value> value = lowerExpression(rhs);
    if (failed(value))
      return failure();
    Type elementType = referenceType ? referenceType.getElementType()
                                     : netType.getElementType();
    if (!sim::getPackedWidth(elementType)) {
      emitError(getSemanticLocation(lhs))
          << "force and procedural assign require packed integral storage";
      return failure();
    }
    FailureOr<Value> converted =
        convert(*value, elementType, isSignedNode(rhs), location);
    if (failed(converted))
      return failure();
    sim::SimOverrideOp::create(builder, location, *target, *converted,
                               builder.getBoolAttr(isAssign));
    return success();
  }
  if (auto release = dyn_cast<semantic::SVProceduralDeassignStatementOp>(op)) {
    if (children.size() != 1) {
      unsupported(op) << " (procedural release/deassign arity)";
      return failure();
    }
    Operation *lhs = children.front();
    bool selected = isa<semantic::SVElementSelectExpressionOp,
                        semantic::SVRangeSelectExpressionOp>(lhs);
    if (!isa<semantic::SVNamedValueExpressionOp,
             semantic::SVHierarchicalValueExpressionOp>(lhs) &&
        !selected) {
      emitError(getSemanticLocation(lhs))
          << "release and deassign currently require a whole statically "
             "allocated packed variable or whole built-in net";
      return failure();
    }
    FailureOr<Value> target = lowerExpression(lhs, true);
    if (failed(target))
      return failure();
    auto referenceType = dyn_cast<sim::RefType>((*target).getType());
    auto netType = dyn_cast<sim::NetType>((*target).getType());
    if ((!referenceType && !netType) ||
        !isStaticallyAllocatedOverrideTarget(*target)) {
      emitError(getSemanticLocation(lhs))
          << "release and deassign require statically allocated packed "
             "storage";
      return failure();
    }
    bool isAssign = !release.getIsRelease();
    if (selected && (!netType || isAssign)) {
      emitError(getSemanticLocation(lhs))
          << "only constant built-in net bit and part selects are supported "
             "for release";
      return failure();
    }
    if (isAssign && netType) {
      emitError(getSemanticLocation(lhs))
          << "deassign requires a packed variable";
      return failure();
    }
    Type elementType = referenceType ? referenceType.getElementType()
                                     : netType.getElementType();
    if (!sim::getPackedWidth(elementType)) {
      emitError(getSemanticLocation(lhs))
          << "release and deassign require packed integral storage";
      return failure();
    }
    sim::SimReleaseOverrideOp::create(builder, location, *target,
                                      builder.getBoolAttr(isAssign));
    return success();
  }
  if (auto block = dyn_cast<semantic::SVBlockStatementOp>(op)) {
    return lowerBlock(block);
  }
  if (isa<semantic::SVStatementListOp>(op))
    return lowerSequence(children);
  if (isa<semantic::SVTimedStatementOp>(op)) {
    if (children.size() != 2) {
      unsupported(op) << " (timed statement arity)";
      return failure();
    }
    return lowerTiming(children[0], children[1]);
  }
  if (auto wait = dyn_cast<semantic::SVWaitStatementOp>(op))
    return lowerWait(wait);
  if (isa<semantic::SVWaitOrderStatementOp>(op)) {
    unsupported(op) << " (wait_order occurrence sequencing)";
    return failure();
  }
  if (isa<semantic::SVWaitForkStatementOp>(op)) {
    Block *continuation = addBlock();
    sim::SimSuspendChildrenOp::create(builder, location, ValueRange{},
                                      sim::ContinuationSiteAttr{},
                                      sim::EventRegionAttr{}, continuation);
    setCurrent(continuation);
    return success();
  }
  if (isa<semantic::SVDisableForkStatementOp>(op)) {
    sim::SimDisableChildrenOp::create(builder, location);
    return success();
  }
  if (auto disable = dyn_cast<semantic::SVDisableStatementOp>(op))
    return lowerDisable(disable);
  if (auto trigger = dyn_cast<semantic::SVEventTriggerStatementOp>(op))
    return lowerEventTrigger(trigger);
  if (auto conditional = dyn_cast<semantic::SVConditionalStatementOp>(op))
    return lowerConditional(conditional);
  if (auto caseStatement = dyn_cast<semantic::SVCaseStatementOp>(op))
    return lowerCase(caseStatement);
  if (auto patternCase = dyn_cast<semantic::SVPatternCaseStatementOp>(op))
    return lowerPatternCase(patternCase);
  if (auto randCase = dyn_cast<semantic::SVRandCaseStatementOp>(op))
    return lowerRandCase(randCase);
  if (auto randSequence = dyn_cast<semantic::SVRandSequenceStatementOp>(op))
    return lowerRandSequence(randSequence);
  if (isa<semantic::SVWhileLoopStatementOp>(op))
    return lowerWhile(op);
  if (isa<semantic::SVDoWhileLoopStatementOp>(op))
    return lowerDoWhile(op);
  if (auto forLoop = dyn_cast<semantic::SVForLoopStatementOp>(op))
    return lowerFor(forLoop);
  if (isa<semantic::SVForeverLoopStatementOp>(op))
    return lowerForever(op);
  if (auto foreach = dyn_cast<semantic::SVForeachLoopStatementOp>(op))
    return lowerForeach(foreach);
  if (isa<semantic::SVRepeatLoopStatementOp>(op))
    return lowerRepeat(op);
  if (isa<semantic::SVBreakStatementOp>(op)) {
    if (loopTargets.empty()) {
      if (randSequenceContexts.empty()) {
        emitError(location) << "break is not nested in a loop or randsequence";
        return failure();
      }
      emitControlLeaves(randSequenceContexts.back().controlDepth, location);
      cf::BranchOp::create(builder, location,
                           randSequenceContexts.back().breakTarget);
      setCurrent(addBlock());
      return success();
    }
    emitControlLeaves(loopTargets.back().controlDepth, location);
    cf::BranchOp::create(builder, location, loopTargets.back().breakTarget);
    setCurrent(addBlock());
    return success();
  }
  if (isa<semantic::SVContinueStatementOp>(op)) {
    if (loopTargets.empty()) {
      emitError(location) << "continue is not nested in a loop";
      return failure();
    }
    emitControlLeaves(loopTargets.back().controlDepth, location);
    cf::BranchOp::create(builder, location, loopTargets.back().continueTarget,
                         loopTargets.back().continueOperands);
    setCurrent(addBlock());
    return success();
  }
  if (isa<semantic::SVReturnStatementOp>(op)) {
    if (!randSequenceProductionReturns.empty()) {
      if (!children.empty()) {
        emitError(location)
            << "value-returning randsequence productions are outside the "
               "current executable boundary";
        return failure();
      }
      emitControlLeaves(randSequenceProductionReturns.back().controlDepth,
                        location);
      cf::BranchOp::create(builder, location,
                           randSequenceProductionReturns.back().target);
      setCurrent(addBlock());
      return success();
    }
    std::optional<Value> result;
    bool resultSigned = false;
    if (!children.empty()) {
      FailureOr<Value> value = lowerExpression(children.front());
      if (failed(value))
        return failure();
      result = *value;
      resultSigned = isSignedNode(children.front());
    }
    emitControlLeaves(0, location);
    if (failed(emitFunctionReturn(location, result, resultSigned)))
      return failure();
    setCurrent(addBlock());
    return success();
  }
  // Formal declarations are represented solely by entry block arguments;
  // automatic variable declarations were materialized from frozen bindings.
  if (auto declaration = dyn_cast<semantic::SVVariableDeclStatementOp>(op))
    return lowerVariableDeclaration(declaration);
  if (isa<semantic::SVFormalArgumentSymbolOp, semantic::SVVariableSymbolOp>(op))
    return success();
  if (auto connection = dyn_cast<semantic::SVPortConnectionOp>(op))
    return lowerPortConnection(connection);

  // An expression used directly as a statement, or an unrecognized node, for
  // which lowerExpression emits the same diagnostic.
  return success(succeeded(lowerExpression(op)));
}

LogicalResult UnitLowering::lower(ArrayRef<Operation *> roots) {
  if (invalidBindings)
    return failure();
  for (Operation *root : roots)
    root->walk([&](semantic::SVBlockStatementOp block) {
      auto path = block.getBlockPathAttr();
      auto targetID =
          block->getAttrOfType<IntegerAttr>("obelisk_sim.control_target_id");
      SmallVector<Operation *> contents = getChildren(block);
      if (path && targetID && contents.size() == 1 &&
          isa<semantic::SVImmediateAssertionStatementOp>(contents.front()))
        assertionControlIDs[path.getValue()] =
            targetID.getValue().getZExtValue();
    });
  setCurrent(&function.getBody().front());
  if (function->hasAttr(sequenceEndpointMonitorAttrName))
    return lowerSequenceEndpointMonitor(roots);
  sim::EntryKind entryKind = function.getEntryKind();
  continuousStore = entryKind == sim::EntryKind::Continuous ||
                    entryKind == sim::EntryKind::AlwaysComb ||
                    entryKind == sim::EntryKind::AlwaysLatch ||
                    entryKind == sim::EntryKind::PortInput ||
                    entryKind == sim::EntryKind::PortOutput;
  if (entryKind == sim::EntryKind::Observer) {
    if (roots.size() != 1) {
      function.emitError("observer entry requires one expression root");
      return failure();
    }
    FailureOr<Value> result = lowerExpression(roots.front());
    if (failed(result))
      return failure();
    auto resultKind =
        function->getAttrOfType<IntegerAttr>(observerResultAttrName);
    if (!resultKind) {
      function.emitError("observer entry has no result-kind metadata");
      return failure();
    }
    std::optional<ObserverResult> parsedResult =
        parseObserverResult(resultKind);
    if (!parsedResult) {
      function.emitError() << "unknown observer result kind "
                           << resultKind.getValue().getZExtValue();
      return failure();
    }
    if (*parsedResult == ObserverResult::Event) {
      if (!isa<sim::EventType>((*result).getType())) {
        function.emitError("named-event observer did not produce an event");
        return failure();
      }
      result = sim::SimEventTriggeredOp::create(builder, function.getLoc(),
                                                builder.getI1Type(), *result)
                   .getResult();
    } else if (*parsedResult == ObserverResult::Truth) {
      result = truthValue(*result, function.getLoc());
      if (failed(result))
        return failure();
    } else if (!isa<FloatType>((*result).getType())) {
      result = toPackedScalar(*result, function.getLoc());
      if (failed(result))
        return failure();
    }
    if (function.getFunctionType().getNumResults() != 1 ||
        function.getFunctionType().getResult(0) != (*result).getType()) {
      function.emitError("observer result does not match its signature");
      return failure();
    }
    sim::SimReturnOp::create(builder, function.getLoc(), ValueRange{*result});
    return success();
  }
  bool loopsForever = entryKind == sim::EntryKind::Always ||
                      entryKind == sim::EntryKind::AlwaysComb ||
                      entryKind == sim::EntryKind::AlwaysFF ||
                      entryKind == sim::EntryKind::AlwaysLatch ||
                      entryKind == sim::EntryKind::Continuous ||
                      entryKind == sim::EntryKind::PortInput ||
                      entryKind == sim::EntryKind::PortOutput;

  // Keep track of the outer event control so graph construction can
  // distinguish its process-local writes from external activations. Nested
  // implicit event controls remain ordinary procedural waits.
  if (entryKind == sim::EntryKind::Always && roots.size() == 1) {
    if (auto timed = dyn_cast<semantic::SVTimedStatementOp>(roots.front())) {
      SmallVector<Operation *> children = getChildren(timed);
      if (children.size() == 2 &&
          isa<semantic::SVImplicitEventControlOp>(children.front()))
        topLevelWildcardControl = children.front();
    }
  }

  Block *loopHeader = nullptr;
  if (loopsForever) {
    loopHeader = addBlock();
    emitBranch(loopHeader);
    setCurrent(loopHeader);
  }
  auto primitive =
      function->getAttrOfType<StringAttr>("obelisk_sim.primitive_name");
  llvm::SetVector<Value> implicitProcessWrites;
  llvm::SetVector<Value> *savedWrites = observedWrites;
  bool excludesWrittenSensitivity = entryKind == sim::EntryKind::AlwaysComb ||
                                    entryKind == sim::EntryKind::AlwaysLatch;
  if (excludesWrittenSensitivity) {
    observedWrites = &implicitProcessWrites;
    observeNonblockingWrites = true;
  }
  LogicalResult lowered = success();
  if (primitive) {
    lowered = lowerPrimitive(primitive.getValue(), roots);
  } else {
    lowered = lowerSequence(roots);
  }
  observedWrites = savedWrites;
  if (failed(lowered))
    return failure();
  if (!current->empty() && current->back().hasTrait<OpTrait::IsTerminator>())
    return success();

  // A trailing block created by `break`, `continue`, or `return` may be
  // unreachable; terminate it with defaults rather than an implicit loop.
  llvm::DenseSet<Block *> reachable;
  SmallVector<Block *> worklist{&function.getBody().front()};
  while (!worklist.empty()) {
    Block *block = worklist.pop_back_val();
    if (!reachable.insert(block).second || block->empty())
      continue;
    for (Block *successor : block->back().getSuccessors())
      worklist.push_back(successor);
  }
  if (!reachable.contains(current)) {
    SmallVector<Value> defaults;
    for (Type type : function.getFunctionType().getResults()) {
      Value value = createDefaultValue(builder, function.getLoc(), type);
      if (!value) {
        function.emitError()
            << "cannot terminate unreachable block for result type " << type;
        return failure();
      }
      defaults.push_back(value);
    }
    sim::SimReturnOp::create(builder, function.getLoc(), defaults);
    return success();
  }

  if (!loopsForever) {
    if (entryKind == sim::EntryKind::Function ||
        entryKind == sim::EntryKind::Task)
      return emitFunctionReturn(function.getLoc(), std::nullopt);
    sim::SimReturnOp::create(builder, function.getLoc(), ValueRange{});
    return success();
  }

  // An implicitly sensitive process waits on everything it read; an explicitly
  // timed `always` block re-enters its own timing control instead.
  if (entryKind != sim::EntryKind::AlwaysComb &&
      entryKind != sim::EntryKind::AlwaysLatch &&
      entryKind != sim::EntryKind::Continuous &&
      entryKind != sim::EntryKind::PortInput &&
      entryKind != sim::EntryKind::PortOutput) {
    cf::BranchOp::create(builder, function.getLoc(), loopHeader);
    return success();
  }
  // IEEE 1800-2017 9.2.2.2.1 excludes any expression also written by an
  // always_comb (and, by 9.2.2.3, always_latch) from implicit sensitivity.
  for (Value read : virtualInterfaceReadSensitivity)
    sensitivity.insert(read);
  for (Value written : virtualInterfaceWrittenSensitivity)
    implicitProcessWrites.insert(written);
  for (Value written : implicitProcessWrites)
    sensitivity.remove(written);
  if (sensitivity.empty()) {
    if (entryKind == sim::EntryKind::Continuous ||
        entryKind == sim::EntryKind::AlwaysComb ||
        entryKind == sim::EntryKind::AlwaysLatch) {
      // These units execute once when spawned. If every read is an elaborated
      // constant, there is no source transition that can trigger another
      // evaluation. always_comb and always_latch still retain their required
      // time-zero execution.
      sim::SimReturnOp::create(builder, function.getLoc(), ValueRange{});
      return success();
    }
    function.emitError("combinational process has no sensitivity capture");
    return failure();
  }
  if (sensitivity.size() == 1) {
    sim::SimSuspendChangeOp::create(
        builder, function.getLoc(), sensitivity.front(), ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, loopHeader);
    return success();
  }
  SmallVector<int32_t> edges(sensitivity.size(),
                             static_cast<int32_t>(sim::EdgeKind::Change));
  sim::SimSuspendAnyOp::create(
      builder, function.getLoc(), sensitivity.getArrayRef(),
      builder.getDenseI32ArrayAttr(edges), sim::ContinuationSiteAttr{},
      sim::EventRegionAttr{}, loopHeader);
  return success();
}

} // namespace simlowering

namespace {

class ObeliskSimLowerUnitPass
    : public impl::ObeliskSimLowerUnitPassBase<ObeliskSimLowerUnitPass> {
public:
  void runOnOperation() override {
    sim::SimFuncOp function = getOperation();
    if (function->hasAttr(sim::metadata::lowered))
      return;
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      return;
    // Imported DPI declarations deliberately have no executable body. Their
    // call sites lower to obelisk_sim.dpi.call in the caller instead.
    if (function.getBody().empty())
      return;
    if (failed(sim::verifyUnitBindings(function))) {
      signalPassFailure();
      return;
    }

    Block &entry = function.getBody().front();
    SmallVector<Operation *> sourceRoots;
    for (Operation &op : entry)
      if (isSemanticOp(&op))
        sourceRoots.push_back(&op);

    // Drop the placeholder terminator before its producer so no operation is
    // erased while it still has a live SSA use.
    for (Operation &op : llvm::make_early_inc_range(entry))
      if (isa<sim::SimReturnOp>(op))
        op.erase();
    for (Operation &op : llvm::make_early_inc_range(entry))
      if (op.hasAttr(placeholderAttrName))
        op.erase();

    simlowering::UnitLowering lowering(function);
    LogicalResult result = lowering.lower(sourceRoots);
    for (Operation *source : sourceRoots)
      source->erase();
    if (failed(result))
      signalPassFailure();
    else
      function->setAttr(sim::metadata::lowered,
                        UnitAttr::get(function.getContext()));
  }
};

} // namespace
} // namespace obelisk
