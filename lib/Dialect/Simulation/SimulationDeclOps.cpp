//===- SimulationDeclOps.cpp - Declaration op verifiers -------===//
//
// Verifiers for scope, storage, net, driver, covergroup, class, and reference
// declarations, and the design-level structural verification.
//
//===----------------------------------------------------------------------===//

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

namespace obelisk::sim {

static constexpr uint64_t interfaceDispatchSlot =
    std::numeric_limits<uint32_t>::max();

LogicalResult SimScopeDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "scope ID")))
    return failure();
  if (getParentAttr() &&
      failed(verifyNonnegative(*this, getParentAttr(), "parent scope ID")))
    return failure();
  if (getParentAttr() && getParentAttr() == getIdAttr())
    return emitOpError("scope cannot be its own parent");
  if (StringAttr interfaceType = getInterfaceTypeAttr()) {
    if (interfaceType.getValue().empty())
      return emitOpError("interface specialization key cannot be empty");
    if (getId() == 0)
      return emitOpError("root scope cannot be an interface instance");
  }
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

static SimCovergroupDeclOp lookupCovergroup(Operation *operation,
                                            SymbolRefAttr symbol) {
  return symbol ? SymbolTable::lookupNearestSymbolFrom<SimCovergroupDeclOp>(
                      operation, symbol)
                : SimCovergroupDeclOp{};
}

static LogicalResult verifyCovergroupHandle(Operation *operation,
                                            CovergroupHandleType handle) {
  if (!lookupCovergroup(operation, handle.getCovergroupName()))
    return operation->emitOpError(
        "handle type references an unknown covergroup declaration");
  return success();
}

LogicalResult SimCovergroupDeclOp::verify() {
  if (failed(verifyPositive(*this, getIdAttr(), "covergroup ID")))
    return failure();
  if (getCoverpointBins().empty())
    return emitOpError("requires at least one coverpoint");
  for (int64_t bins : getCoverpointBins())
    if (bins <= 0 || static_cast<uint64_t>(bins) > UINT32_MAX)
      return emitOpError(
          "every coverpoint requires a positive 32-bit named-bin count");
  return success();
}

LogicalResult SimCovergroupNullOp::verify() {
  return verifyCovergroupHandle(*this, getResult().getType());
}

LogicalResult SimVirtualInterfaceBindOp::verify() {
  if (failed(verifyPositive(*this, getScopeIdAttr(), "interface scope ID")))
    return failure();
  SimDesignOp design = (*this)->getParentOfType<SimDesignOp>();
  if (!design)
    return emitOpError("requires an enclosing simulation design");
  SimScopeDeclOp found;
  for (SimScopeDeclOp scope : design.getBody().front().getOps<SimScopeDeclOp>())
    if (scope.getId() == getScopeId()) {
      found = scope;
      break;
    }
  if (!found)
    return emitOpError("references an unknown interface scope ID ")
           << getScopeId();
  if (!found.getInterfaceTypeAttr())
    return emitOpError("scope ID does not identify an interface instance");
  if (found.getInterfaceTypeAttr() != getResult().getType().getInterfaceName())
    return emitOpError("scope interface specialization does not match result type");
  return success();
}

LogicalResult SimVirtualInterfaceCastOp::verify() {
  if (getInput().getType().getInterfaceName() !=
      getResult().getType().getInterfaceName())
    return emitOpError("cannot change the interface specialization");
  StringRef source = getInput().getType().getModport().getValue();
  StringRef target = getResult().getType().getModport().getValue();
  if (!source.empty() && source != target)
    return emitOpError("cannot remove or change a selected modport");
  return success();
}

LogicalResult SimVirtualInterfaceEqualOp::verify() {
  if (getLhs().getType().getInterfaceName() !=
      getRhs().getType().getInterfaceName())
    return emitOpError("cannot compare different interface specializations");
  return success();
}

LogicalResult SimCovergroupCreateOp::verify() {
  SimCovergroupDeclOp declaration =
      lookupCovergroup(*this, getDeclarationAttr());
  if (!declaration)
    return emitOpError("references an unknown covergroup declaration");
  auto expected = FlatSymbolRefAttr::get(declaration.getOperation());
  if (getResult().getType().getCovergroupName() != expected)
    return emitOpError("result type must name the selected declaration");
  return success();
}

LogicalResult SimCovergroupSampleEnabledOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupBinHitOp::verify() {
  SimCovergroupDeclOp declaration =
      lookupCovergroup(*this, getHandle().getType().getCovergroupName());
  if (!declaration)
    return emitOpError("handle type references an unknown declaration");
  uint64_t coverpoint = getCoverpoint();
  if (coverpoint >= declaration.getCoverpointBins().size())
    return emitOpError("coverpoint index is outside the declaration");
  uint64_t bin = getBin();
  if (bin >= static_cast<uint64_t>(declaration.getCoverpointBins()[coverpoint]))
    return emitOpError("bin index is outside the selected coverpoint");
  return success();
}

LogicalResult SimCovergroupSampleOp::verify() {
  SimCovergroupDeclOp declaration =
      lookupCovergroup(*this, getHandle().getType().getCovergroupName());
  if (!declaration)
    return emitOpError(
        "handle type references an unknown covergroup declaration");
  uint64_t expected = 0;
  for (int64_t bins : declaration.getCoverpointBins()) {
    if (static_cast<uint64_t>(bins) > UINT64_MAX - expected)
      return emitOpError("declaration bin inventory is too large");
    expected += static_cast<uint64_t>(bins);
  }
  if (getHits().size() != expected)
    return emitOpError() << "requires exactly " << expected
                         << " flattened bin-hit operands";
  return success();
}

LogicalResult SimCovergroupStartOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupStopOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupInstanceQueryOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupTypeQueryOp::verify() {
  if (!lookupCovergroup(*this, getDeclarationAttr()))
    return emitOpError("references an unknown covergroup declaration");
  return success();
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
  if (Attribute attribute = (*this)->getAttr(metadata::randomModeField)) {
    auto reference = dyn_cast<FlatSymbolRefAttr>(attribute);
    auto field =
        reference ? SymbolTable::lookupNearestSymbolFrom<SimClassFieldDeclOp>(
                        *this, reference)
                  : SimClassFieldDeclOp{};
    if (getBaseAttr() || !field || field.getOwner() != getSymName() ||
        field.getIsStatic() || !field.getType().isInteger(64))
      return emitOpError(
          "random mode field must name an instance i64 field owned by the "
          "root class");
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
  Attribute modeAttribute = (*this)->getAttr(metadata::randomModeIndex);
  auto modeIndex = dyn_cast_or_null<IntegerAttr>(modeAttribute);
  if (modeAttribute && !modeIndex)
    return emitOpError("random mode index must be an integer attribute");
  if (modeIndex && (modeIndex.getValue().isNegative() ||
                    modeIndex.getValue().getActiveBits() > 6))
    return emitOpError(
        "random mode index exceeds the 64-property executable boundary");
  if (Attribute edge = (*this)->getAttr(metadata::randomObjectEdge)) {
    if (!isa<UnitAttr>(edge))
      return emitOpError("random object edge must be a unit attribute");
    if (!modeIndex || getIsStatic() || getIsWeak() ||
        !isa<ClassHandleType>(getType()))
      return emitOpError(
          "random object edge requires an indexed, strong instance "
          "class-handle field");
  }
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
  if (getSlotAttr()) {
    if (getSlotAttr().getValue().isNegative() ||
        getSlot() > interfaceDispatchSlot)
      return emitOpError("virtual-method slot exceeds the 32-bit dispatch ABI");
    if (owner.getIsInterface() && getSlot() != interfaceDispatchSlot)
      return emitOpError(
          "interface virtual methods require the interface dispatch slot");
    if (!owner.getIsInterface() && getSlot() == interfaceDispatchSlot)
      return emitOpError(
          "non-interface virtual methods cannot use the interface dispatch "
          "slot");
  }
  if (owner.getIsInterface() && getIsVirtual()) {
    if (!getInterfaceOrdinalAttr() ||
        getInterfaceOrdinalAttr().getValue().isNegative() ||
        getInterfaceOrdinal() > UINT32_MAX)
      return emitOpError(
          "interface virtual methods require a 32-bit interface ordinal");
  } else if (getInterfaceOrdinalAttr()) {
    return emitOpError(
        "only interface virtual methods may have an interface ordinal");
  }
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

LogicalResult SimManagedWatchOp::verify() {
  Type input = getInput().getType();
  switch (getKind()) {
  case ManagedWatchKind::Field:
    if (!isa<ManagedRefType>(input))
      return emitOpError("field watches require a managed reference");
    break;
  case ManagedWatchKind::ContainerSize:
    if (!isa<DynamicArrayType, QueueType, AssocArrayType>(input))
      return emitOpError(
          "container-size watches require a dynamic, queue, or associative "
          "array handle");
    break;
  }
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
  SmallVector<ManagedHandleSlot, 2> slots;
  if (isa<ManagedRefType>(type))
    slots.push_back(
        {0, static_cast<uint32_t>(ManagedHandleKind::Class), false});
  else if (isa<ArgumentRefType>(type))
    slots.push_back(
        {0,
         static_cast<uint32_t>(ManagedHandleKind::Class) |
             static_cast<uint32_t>(ManagedHandleKind::ReferencePath),
         false});
  else if (!getManagedHandleSlots(type, slots))
    return emitOpError("rooted value has no fixed managed layout");
  auto selected = llvm::find_if(slots, [&](const ManagedHandleSlot &slot) {
    return slot.bitOffset == getBitOffset();
  });
  if (selected == slots.end())
    return emitOpError("bit offset does not select a managed handle");
  ManagedRootMode expectedMode = selected->conditional
                                     ? ManagedRootMode::Candidate
                                     : ManagedRootMode::Exact;
  if (getMode() != expectedMode || getKindMask() != selected->kindMask)
    return emitOpError("root mode or managed-kind mask disagrees with type");
  return success();
}

LogicalResult SimClassDirectCallOp::verify() {
  auto callee =
      SymbolTable::lookupNearestSymbolFrom<SimFuncOp>(*this, getCalleeAttr());
  if (!callee)
    return emitOpError("references an unknown method implementation");
  if (callee.getEntryKind() != EntryKind::Function)
    return emitOpError(
        "must reference a zero-time function implementation");
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
      connectionIds, covergroupIds, classIds;
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
    } else if (auto covergroup = dyn_cast<SimCovergroupDeclOp>(op)) {
      if (failed(addId(covergroup.getIdAttr(), covergroupIds, "covergroup")))
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
    ElementShape shape{type,
                       kind,
                       flags,
                       valueSize,
                       alignment,
                       bitWidth,
                       SmallVector<int64_t, 2>(traceOffsets),
                       SmallVector<int32_t, 2>(traceKinds)};
    auto [found, inserted] = elementShapes.try_emplace(typeId, shape);
    if (!inserted &&
        (found->second.type != shape.type || found->second.kind != shape.kind ||
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
      return recordElementShape(operation, create.getTypeId(),
                                create.getResult().getType().getElementType(),
                                static_cast<uint32_t>(create.getElementKind()),
                                static_cast<uint32_t>(create.getElementFlags()),
                                create.getValueSize(), create.getAlignment(),
                                create.getBitWidth(), create.getTraceOffsets(),
                                create.getTraceKinds());
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
  llvm::StringMap<llvm::DenseSet<uint64_t>> fieldOrdinals, methodSlots,
      interfaceMethodOrdinals;
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
      if (classDecl.getIsInterface()) {
        llvm::SmallPtrSet<Operation *, 8> reached;
        SmallVector<SimClassDeclOp> pending;
        auto appendInterfaces = [&](SimClassDeclOp declaration) {
          if (ArrayAttr interfaces = declaration.getInterfacesAttr())
            for (Attribute attribute : interfaces) {
              auto reference = cast<FlatSymbolRefAttr>(attribute);
              auto found = classes.find(reference.getValue());
              if (found != classes.end() && found->second.getIsInterface())
                pending.push_back(found->second);
            }
        };
        appendInterfaces(classDecl);
        while (!pending.empty()) {
          SimClassDeclOp current = pending.pop_back_val();
          if (current == classDecl)
            return classDecl.emitOpError(
                "interface inheritance contains a cycle");
          if (reached.insert(current).second)
            appendInterfaces(current);
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
          *method.getSlot() != interfaceDispatchSlot &&
          !methodSlots[method.getOwner()].insert(*method.getSlot()).second)
        return method.emitOpError(
            "owner class contains a duplicate virtual-method slot");
      if (method.getInterfaceOrdinalAttr() &&
          !interfaceMethodOrdinals[method.getOwner()]
               .insert(*method.getInterfaceOrdinal())
               .second)
        return method.emitOpError(
            "owner interface contains a duplicate method ordinal");
      if (auto implementation = method.getImplementation()) {
        auto found = functionsByName.find(*implementation);
        if (found == functionsByName.end() ||
            found->second.getFunctionType() != method.getFunctionType())
          return method.emitOpError(
              "implementation is missing or has an incompatible signature");
        EntryKind expected =
            method.getIsTask() ? EntryKind::Task : EntryKind::Function;
        if (found->second.getEntryKind() != expected)
          return method.emitOpError(
              "implementation entry kind does not match the method kind");
      }
    }
  }
  for (auto &entry : interfaceMethodOrdinals)
    for (uint64_t ordinal = 0; ordinal != entry.second.size(); ++ordinal)
      if (!entry.second.count(ordinal))
        return emitOpError() << "interface " << entry.first()
                             << " contains a non-dense method ordinal set";
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
    bool pendingClockedSamplePlan = static_cast<bool>(
        function->getAttrOfType<DictionaryAttr>(
            "obelisk_sim.clocked_sample_plan"));
    if (!function.isExternal() &&
        function.getEntryKind() != EntryKind::RootInitializer &&
        !function.getCodeUnitIdAttr() && !pendingClockedSamplePlan)
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


} // namespace obelisk::sim
