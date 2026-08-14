//===- LowerUnitRandomize.cpp - Lower randomize() calls ---------------===//
//
// Lowering of SystemVerilog randomize() calls: the constraint program the
// solver executes, its proposal tables and finite domains, and the inline
// sampling fast paths emitted when a class needs no solver call.
//
//===----------------------------------------------------------------------===//

#include "LowerUnit.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Solver/ConstraintSolver.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringSet.h"

#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <tuple>

using namespace mlir;

namespace obelisk::simlowering {
FailureOr<Value>
UnitLowering::lowerRandomize(semantic::SVCallExpressionOp op,
                             Value receiverOverride) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op->hasAttr(randomizeNestedDispatchAttrName)) {
    auto receiverIndexAttr =
        op->getAttrOfType<IntegerAttr>(randomReceiverIndexAttrName);
    auto field = op->getAttrOfType<FlatSymbolRefAttr>(
        randomizeNestedDispatchFieldAttrName);
    auto storageTypeAttr = op->getAttrOfType<TypeAttr>(
        randomizeNestedDispatchStorageAttrName);
    auto dispatchPath = op->getAttrOfType<ArrayAttr>(
        randomizeNestedDispatchPathAttrName);
    auto dispatchSelectionPath = op->getAttrOfType<ArrayAttr>(
        randomizeNestedDispatchSelectionPathAttrName);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (!receiverIndexAttr || !field || !storageTypeAttr ||
        failed(resultType) || receiverIndexAttr.getValue().isNegative() ||
        receiverIndexAttr.getValue().getActiveBits() > 64 ||
        receiverIndexAttr.getValue().getZExtValue() >= children.size() ||
        !isa<sim::ClassHandleType>(storageTypeAttr.getValue()) ||
        (static_cast<bool>(dispatchPath) !=
         static_cast<bool>(dispatchSelectionPath)) ||
        (dispatchPath &&
         (dispatchPath.empty() || dispatchSelectionPath.empty()))) {
      emitError(location)
          << "nested randomize dispatch has malformed metadata";
      return failure();
    }
    unsigned receiverIndex =
        static_cast<unsigned>(receiverIndexAttr.getValue().getZExtValue());
    FailureOr<Value> loweredReceiver =
        receiverOverride ? FailureOr<Value>(receiverOverride)
                         : lowerExpression(children[receiverIndex]);
    if (failed(loweredReceiver) ||
        !isa<sim::ClassHandleType>((*loweredReceiver).getType())) {
      emitError(location)
          << "nested randomize dispatch receiver is not a class object";
      return failure();
    }
    if (auto planClass = op->getAttrOfType<FlatSymbolRefAttr>(
            randomizePlanClassAttrName)) {
      Type targetType =
          sim::ClassHandleType::get(function.getContext(), planClass);
      if ((*loweredReceiver).getType() != targetType)
        loweredReceiver = sim::SimClassCastOp::create(
                              builder, location, targetType, *loweredReceiver)
                              .getResult();
    }
    auto receiverType =
        dyn_cast<sim::ClassHandleType>((*loweredReceiver).getType());
    if (!receiverType)
      return failure();

    SmallVector<semantic::SVCallExpressionOp> alternatives;
    semantic::SVCallExpressionOp nullAlternative;
    auto getSelectedPlan = [&](semantic::SVCallExpressionOp alternative) {
      if (auto selectedPlans = alternative->getAttrOfType<ArrayAttr>(
              randomizeNestedPlansAttrName))
        for (Attribute selectedAttr : selectedPlans)
          if (auto selected = dyn_cast<DictionaryAttr>(selectedAttr)) {
            if (dispatchSelectionPath) {
              if (selected.getAs<ArrayAttr>("path") ==
                  dispatchSelectionPath)
                return selected;
            } else if (!selected.get("path") &&
                       selected.getAs<FlatSymbolRefAttr>("field") == field) {
              return selected;
            }
          }
      return DictionaryAttr{};
    };
    for (Operation *child : children) {
      auto alternative = dyn_cast<semantic::SVCallExpressionOp>(child);
      if (!alternative ||
          (!alternative->hasAttr(randomizeAttrName) &&
           !alternative->hasAttr(randomizeNestedDispatchAttrName)))
        continue;
      DictionaryAttr selectedPlan = getSelectedPlan(alternative);
      if (!selectedPlan)
        continue;
      if (selectedPlan.get("null"))
        nullAlternative = alternative;
      else
        alternatives.push_back(alternative);
    }
    if (!nullAlternative) {
      emitError(location)
          << "nested randomize dispatch has no null alternative";
      return failure();
    }
    Block *done = addBlock();
    Value doneResult = done->addArgument(*resultType, location);
    Block *nullBlock = addBlock();
    Block *nonNullBlock = addBlock();
    Value object;
    if (dispatchPath) {
      Value owner = *loweredReceiver;
      auto ownerType = receiverType;
      for (auto [index, elementAttr] : llvm::enumerate(dispatchPath)) {
        auto element = dyn_cast<DictionaryAttr>(elementAttr);
        auto pathField =
            element ? element.getAs<FlatSymbolRefAttr>("field")
                    : FlatSymbolRefAttr{};
        auto pathStorageType =
            element ? element.getAs<TypeAttr>("storage_type") : TypeAttr{};
        auto pathConcreteType =
            element ? element.getAs<TypeAttr>("concrete_type") : TypeAttr{};
        bool isTarget = index + 1 == dispatchPath.size();
        if (!pathField || !pathStorageType ||
            !isa<sim::ClassHandleType>(pathStorageType.getValue()) ||
            (!isTarget &&
             (!pathConcreteType ||
              !isa<sim::ClassHandleType>(pathConcreteType.getValue()))) ||
            (isTarget && pathConcreteType) ||
            (index == 0 &&
             (pathField != field ||
              pathStorageType.getValue() != storageTypeAttr.getValue()))) {
          emitError(location)
              << "nested randomize dispatch path is malformed";
          return failure();
        }
        Type referenceType = sim::ManagedRefType::get(
            function.getContext(), pathStorageType.getValue(),
            ownerType.getClassName());
        Value reference = sim::SimClassFieldRefOp::create(
            builder, location, referenceType, owner, pathField);
        FailureOr<Value> loaded = loadReference(reference, location);
        if (failed(loaded))
          return failure();
        if (isTarget) {
          object = *loaded;
          break;
        }
        Value ancestorNull = sim::SimManagedIsNullOp::create(
            builder, location, builder.getI1Type(), *loaded);
        Block *pathBlock = addBlock();
        cf::CondBranchOp::create(builder, location, ancestorNull, nullBlock,
                                 ValueRange{}, pathBlock, ValueRange{});
        setCurrent(pathBlock);
        owner = *loaded;
        if (owner.getType() != pathConcreteType.getValue())
          owner = sim::SimClassCastOp::create(
              builder, location, pathConcreteType.getValue(), owner);
        ownerType = cast<sim::ClassHandleType>(pathConcreteType.getValue());
      }
    } else {
      Type referenceType = sim::ManagedRefType::get(
          function.getContext(), storageTypeAttr.getValue(),
          receiverType.getClassName());
      Value reference = sim::SimClassFieldRefOp::create(
          builder, location, referenceType, *loweredReceiver, field);
      FailureOr<Value> loaded = loadReference(reference, location);
      if (failed(loaded))
        return failure();
      object = *loaded;
    }
    if (!object) {
      emitError(location) << "nested randomize dispatch path has no target";
      return failure();
    }
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), object);
    cf::CondBranchOp::create(builder, location, isNull, nullBlock,
                             ValueRange{}, nonNullBlock, ValueRange{});
    setCurrent(nullBlock);
    FailureOr<Value> nullResult =
        lowerRandomize(nullAlternative, *loweredReceiver);
    if (failed(nullResult))
      return failure();
    cf::BranchOp::create(builder, location, done, ValueRange{*nullResult});
    setCurrent(nonNullBlock);
    for (semantic::SVCallExpressionOp alternative : alternatives) {
      DictionaryAttr selectedPlan = getSelectedPlan(alternative);
      FlatSymbolRefAttr planClass =
          selectedPlan ? selectedPlan.getAs<FlatSymbolRefAttr>("class")
                       : FlatSymbolRefAttr{};
      if (!planClass) {
        emitError(location)
            << "nested randomize alternative has no target class";
        return failure();
      }
      Value matches = sim::SimClassIsInstanceOp::create(
          builder, location, object, planClass);
      Block *selected = addBlock();
      Block *next = addBlock();
      cf::CondBranchOp::create(builder, location, matches, selected,
                               ValueRange{}, next, ValueRange{});
      setCurrent(selected);
      FailureOr<Value> result =
          lowerRandomize(alternative, *loweredReceiver);
      if (failed(result))
        return failure();
      cf::BranchOp::create(builder, location, done, ValueRange{*result});
      setCurrent(next);
    }
    FailureOr<Value> noPlan = convert(
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(false)),
        *resultType, false, location);
    if (failed(noPlan))
      return failure();
    cf::BranchOp::create(builder, location, done, ValueRange{*noPlan});
    setCurrent(done);
    return doneResult;
  }
  if (op->hasAttr(randomizeDispatchAttrName)) {
    auto receiverIndexAttr =
        op->getAttrOfType<IntegerAttr>(randomReceiverIndexAttrName);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (!receiverIndexAttr || failed(resultType) ||
        receiverIndexAttr.getValue().isNegative() ||
        receiverIndexAttr.getValue().getActiveBits() > 64 ||
        receiverIndexAttr.getValue().getZExtValue() >= children.size()) {
      emitError(location) << "randomize dispatch has malformed metadata";
      return failure();
    }
    unsigned receiverIndex =
        static_cast<unsigned>(receiverIndexAttr.getValue().getZExtValue());
    FailureOr<Value> loweredReceiver = receiverOverride
                                           ? FailureOr<Value>(receiverOverride)
                                           : lowerExpression(
                                                 children[receiverIndex]);
    if (failed(loweredReceiver) ||
        !isa<sim::ClassHandleType>((*loweredReceiver).getType())) {
      emitError(location) << "randomize dispatch receiver is not a class object";
      return failure();
    }

    SmallVector<semantic::SVCallExpressionOp> alternatives;
    for (Operation *child : children)
      if (auto alternative = dyn_cast<semantic::SVCallExpressionOp>(child);
          alternative &&
          alternative->hasAttr(randomizePlanClassAttrName) &&
          (alternative->hasAttr(randomizeAttrName) ||
           alternative->hasAttr(randomizeNestedDispatchAttrName)))
        alternatives.push_back(alternative);

    Block *done = addBlock();
    Value doneResult = done->addArgument(*resultType, location);
    for (semantic::SVCallExpressionOp alternative : alternatives) {
      auto planClass = alternative->getAttrOfType<FlatSymbolRefAttr>(
          randomizePlanClassAttrName);
      if (!planClass) {
        emitError(location) << "randomize alternative has no target class";
        return failure();
      }
      Value matches = sim::SimClassIsInstanceOp::create(
          builder, location, *loweredReceiver, planClass);
      Block *selected = addBlock();
      Block *next = addBlock();
      cf::CondBranchOp::create(builder, location, matches, selected,
                               ValueRange{}, next, ValueRange{});
      setCurrent(selected);
      FailureOr<Value> result = lowerRandomize(alternative, *loweredReceiver);
      if (failed(result))
        return failure();
      cf::BranchOp::create(builder, location, done, ValueRange{*result});
      setCurrent(next);
    }
    FailureOr<Value> noObject = convert(
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(false)),
        *resultType, false, location);
    if (failed(noObject))
      return failure();
    cf::BranchOp::create(builder, location, done, ValueRange{*noObject});
    setCurrent(done);
    return doneResult;
  }

  auto properties = op->getAttrOfType<ArrayAttr>(randomPropertiesAttrName);
  auto containerProperties =
      op->getAttrOfType<ArrayAttr>(randomContainerPropertiesAttrName);
  auto nestedConstraintModes =
      op->getAttrOfType<ArrayAttr>(randomNestedConstraintModesAttrName);
  auto nestedHooks = op->getAttrOfType<ArrayAttr>(randomNestedHooksAttrName);
  auto recursiveAliasGuards =
      op->getAttrOfType<ArrayAttr>(randomRecursiveAliasGuardsAttrName);
  auto totalWidthAttr =
      op->getAttrOfType<IntegerAttr>(randomTotalWidthAttrName);
  auto receiverIndexAttr =
      op->getAttrOfType<IntegerAttr>(randomReceiverIndexAttrName);
  auto constraintCountAttr =
      op->getAttrOfType<IntegerAttr>(randomConstraintCountAttrName);
  auto staticConstraintStorages = op->getAttrOfType<DenseI64ArrayAttr>(
      constraintModeStaticStoragesAttrName);
  if (children.empty() || !properties || !containerProperties ||
      !nestedConstraintModes ||
      !nestedHooks ||
      !recursiveAliasGuards ||
      !totalWidthAttr ||
      !receiverIndexAttr || !constraintCountAttr) {
    emitError(location) << "randomize call has no frozen constraint plan";
    return failure();
  }
  APInt receiverIndexValue = receiverIndexAttr.getValue();
  APInt totalWidthValue = totalWidthAttr.getValue();
  APInt constraintCountValue = constraintCountAttr.getValue();
  if (receiverIndexValue.isNegative() ||
      receiverIndexValue.getActiveBits() > 64 || totalWidthValue.isNegative() ||
      totalWidthValue.getActiveBits() > 64 ||
      constraintCountValue.isNegative() ||
      constraintCountValue.getActiveBits() > 64 ||
      receiverIndexValue.getZExtValue() >= children.size()) {
    emitError(location) << "randomize call has malformed constraint metadata";
    return failure();
  }
  unsigned receiverIndex =
      static_cast<unsigned>(receiverIndexValue.getZExtValue());
  uint64_t totalWidth = totalWidthValue.getZExtValue();
  uint64_t constraintCount = constraintCountValue.getZExtValue();
  bool checkerOnly = op->hasAttr(randomizeCheckerOnlyAttrName);
  if (totalWidth > UINT32_MAX || constraintCount > 64 ||
      (staticConstraintStorages &&
       static_cast<uint64_t>(staticConstraintStorages.size()) !=
           constraintCount) ||
      (staticConstraintStorages &&
       llvm::any_of(staticConstraintStorages.asArrayRef(),
                    [](int64_t storage) { return storage < -1; }))) {
    emitError(location)
        << "randomize plan exceeds its bit-offset or constraint-mode boundary";
    return failure();
  }

  FailureOr<Value> loweredReceiver =
      receiverOverride ? FailureOr<Value>(receiverOverride)
                       : lowerExpression(children[receiverIndex]);
  if (receiverOverride) {
    auto planClass = op->getAttrOfType<FlatSymbolRefAttr>(
        randomizePlanClassAttrName);
    if (!planClass) {
      emitError(location) << "randomize receiver override has no target class";
      return failure();
    }
    Type targetType =
        sim::ClassHandleType::get(function.getContext(), planClass);
    if ((*loweredReceiver).getType() != targetType)
      loweredReceiver = sim::SimClassCastOp::create(
                            builder, location, targetType, *loweredReceiver)
                            .getResult();
  }
  auto objectType =
      succeeded(loweredReceiver)
          ? dyn_cast<sim::ClassHandleType>((*loweredReceiver).getType())
          : sim::ClassHandleType{};
  if (failed(loweredReceiver) || !objectType)
    return failure();
  Value receiver = *loweredReceiver;

  auto callLifecycleHook = [&](StringRef calleeAttr,
                               StringRef ownerAttr, StringRef capturesAttr,
                               StringRef readsAttr) -> LogicalResult {
    auto callee = op->getAttrOfType<FlatSymbolRefAttr>(calleeAttr);
    auto owner = op->getAttrOfType<FlatSymbolRefAttr>(ownerAttr);
    if (static_cast<bool>(callee) != static_cast<bool>(owner)) {
      emitError(location) << "randomize lifecycle hook metadata is incomplete";
      return failure();
    }
    if (!callee)
      return success();
    Type ownerType = sim::ClassHandleType::get(function.getContext(), owner);
    Value hookReceiver = receiver;
    if (hookReceiver.getType() != ownerType)
      hookReceiver = sim::SimClassCastOp::create(
          builder, location, ownerType, hookReceiver);
    llvm::StringSet<> readCaptures;
    if (auto reads = op->getAttrOfType<ArrayAttr>(readsAttr))
      for (Attribute read : reads)
        readCaptures.insert(cast<StringAttr>(read).getValue());
    SmallVector<Value> arguments;
    if (auto captures = op->getAttrOfType<ArrayAttr>(capturesAttr))
      for (Attribute captureAttr : captures) {
        StringRef path = cast<StringAttr>(captureAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture) {
          emitError(location)
              << "randomization hook capture has no local binding: " << path;
          return failure();
        }
        if (readCaptures.contains(path))
          recordSensitivity(capture);
        arguments.push_back(capture);
      }
    sim::SimClassDirectCallOp::create(builder, location, TypeRange{}, callee,
                                      hookReceiver, arguments);
    return success();
  };
  if (failed(callLifecycleHook(
          randomPreHookAttrName, randomPreHookOwnerAttrName,
          randomPreHookCapturesAttrName, randomPreHookReadCapturesAttrName)))
    return failure();

  struct DomainPattern {
    uint64_t mask;
    uint64_t value;
    uint64_t freeMask;
    uint64_t cardinality;
  };
  struct PropertyDomain {
    uint32_t offset;
    unsigned width;
    SmallVector<DomainPattern> patterns;
    uint64_t cardinality;
  };
  struct ObjectPathElement {
    FlatSymbolRefAttr field;
    Type concreteType;
    Type storageType;
    unsigned modeIndex;
    FlatSymbolRefAttr modeField;
  };
  struct Property {
    Type type;
    unsigned width;
    unsigned modeIndex;
    bool isContainerSize;
    Type containerType;
    uint64_t sizeConstraintMask;
    bool hasUnconditionalSizeConstraint;
    Value nestedObjectReference;
    Type nestedObjectType;
    FlatSymbolRefAttr nestedField;
    unsigned nestedModeIndex;
    FlatSymbolRefAttr nestedModeField;
    bool isSigned;
    Value reference;
    bool isRandC;
    Value randcKeyReference;
    Value randcPositionReference;
    Value nextRandcKey;
    Value nextRandcPosition;
    std::optional<uint64_t> randomModeStorage;
    SmallVector<PropertyDomain> domains;
    SmallVector<ObjectPathElement> nestedObjectPath;
    FlatSymbolRefAttr nestedObjectRootField;
    Value nestedObjectID;
  };
  SmallVector<Property, 0> planned;
  uint64_t plannedWidth = 0;
  Type i64 = builder.getI64Type();
  auto parseNestedObjectPath =
      [&](ArrayAttr pathAttr,
          SmallVectorImpl<ObjectPathElement> &path) -> LogicalResult {
    if (!pathAttr)
      return success();
    for (Attribute elementAttr : pathAttr) {
      auto element = dyn_cast<DictionaryAttr>(elementAttr);
      auto field = element ? element.getAs<FlatSymbolRefAttr>("field")
                           : FlatSymbolRefAttr{};
      auto concreteTypeAttr =
          element ? element.getAs<TypeAttr>("concrete_type") : TypeAttr{};
      auto storageTypeAttr =
          element ? element.getAs<TypeAttr>("storage_type") : TypeAttr{};
      auto modeIndexAttr =
          element ? element.getAs<IntegerAttr>("rand_mode_index")
                  : IntegerAttr{};
      if (!field || !concreteTypeAttr || !storageTypeAttr || !modeIndexAttr ||
          !isa<sim::ClassHandleType>(concreteTypeAttr.getValue()) ||
          !isa<sim::ClassHandleType>(storageTypeAttr.getValue()) ||
          modeIndexAttr.getValue().isNegative() ||
          modeIndexAttr.getValue().getActiveBits() > 32 ||
          modeIndexAttr.getValue().getZExtValue() >= 64) {
        emitError(location) << "nested random-object path is malformed";
        return failure();
      }
      auto concreteType =
          cast<sim::ClassHandleType>(concreteTypeAttr.getValue());
      auto declaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, concreteType.getClassName());
      while (declaration &&
             !declaration->hasAttr("obelisk_sim.random_mode_field")) {
        if (!declaration.getBaseAttr())
          break;
        declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, declaration.getBaseAttr());
      }
      auto modeField =
          declaration
              ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                    "obelisk_sim.random_mode_field")
              : FlatSymbolRefAttr{};
      if (!modeField) {
        emitError(location)
            << "nested random-object path has no rand_mode field";
        return failure();
      }
      path.push_back(
          {field, concreteTypeAttr.getValue(), storageTypeAttr.getValue(),
           static_cast<unsigned>(modeIndexAttr.getValue().getZExtValue()),
           modeField});
    }
    return success();
  };
  for (Attribute propertyAttr : properties) {
    auto property = dyn_cast<DictionaryAttr>(propertyAttr);
    auto field = property ? property.getAs<FlatSymbolRefAttr>("field")
                          : FlatSymbolRefAttr{};
    auto referencePath =
        property ? property.getAs<StringAttr>(randomPropertyPathAttrName)
                 : StringAttr{};
    auto typeAttr = property ? property.getAs<TypeAttr>("type") : TypeAttr{};
    auto widthAttr =
        property ? property.getAs<IntegerAttr>("width") : IntegerAttr{};
    auto modeIndexAttr = property
                             ? property.getAs<IntegerAttr>(
                                   randomPropertyModeIndexAttrName)
                             : IntegerAttr{};
    auto signedAttr =
        property ? property.getAs<BoolAttr>("is_signed") : BoolAttr{};
    auto randcAttr =
        property ? property.getAs<BoolAttr>("is_randc") : BoolAttr{};
    auto modeStorageAttr =
        property
            ? property.getAs<IntegerAttr>(randomPropertyModeStorageAttrName)
            : IntegerAttr{};
    bool isContainerSize = property &&
                           property.contains(randomContainerSizeAttrName);
    auto containerTypeAttr =
        property ? property.getAs<TypeAttr>(randomContainerTypeAttrName)
                 : TypeAttr{};
    auto sizeConstraintMaskAttr =
        property ? property.getAs<IntegerAttr>("size_constraint_mask")
                 : IntegerAttr{};
    auto unconditionalSizeConstraintAttr =
        property ? property.getAs<BoolAttr>("unconditional_size_constraint")
                 : BoolAttr{};
    auto nestedObjectField =
        property ? property.getAs<FlatSymbolRefAttr>(
                       randomNestedObjectFieldAttrName)
                 : FlatSymbolRefAttr{};
    auto nestedObjectTypeAttr =
        property ? property.getAs<TypeAttr>(randomNestedObjectTypeAttrName)
                 : TypeAttr{};
    auto nestedObjectStorageTypeAttr =
        property
            ? property.getAs<TypeAttr>(randomNestedObjectStorageTypeAttrName)
            : TypeAttr{};
    auto nestedModeIndexAttr =
        property ? property.getAs<IntegerAttr>(randomNestedModeIndexAttrName)
                 : IntegerAttr{};
    auto nestedObjectPathAttr =
        property ? property.getAs<ArrayAttr>(randomNestedObjectPathAttrName)
                 : ArrayAttr{};
    bool isNestedObject = static_cast<bool>(nestedObjectField);
    if (static_cast<bool>(field) == static_cast<bool>(referencePath) ||
        !typeAttr || !widthAttr || !modeIndexAttr || !signedAttr ||
        !randcAttr || modeIndexAttr.getValue().isNegative() ||
        modeIndexAttr.getValue().getActiveBits() > 32 ||
        modeIndexAttr.getValue().getZExtValue() >= 64 ||
        widthAttr.getValue().isZero() || widthAttr.getValue().isNegative() ||
        widthAttr.getValue().getActiveBits() > 64 ||
        (isContainerSize != static_cast<bool>(containerTypeAttr)) ||
        (isContainerSize &&
         (!field || referencePath || !sizeConstraintMaskAttr ||
          !unconditionalSizeConstraintAttr ||
          sizeConstraintMaskAttr.getValue().getBitWidth() > 64)) ||
        (isNestedObject &&
         (!field || referencePath || !nestedObjectTypeAttr ||
          !nestedObjectStorageTypeAttr || !nestedModeIndexAttr ||
          nestedModeIndexAttr.getValue().isNegative() ||
          nestedModeIndexAttr.getValue().getActiveBits() > 32 ||
          nestedModeIndexAttr.getValue().getZExtValue() >= 64)) ||
        (!isNestedObject && nestedObjectPathAttr)) {
      emitError(location) << "randomize property plan is malformed";
      return failure();
    }
    uint64_t width = widthAttr.getValue().getZExtValue();
    if (width > UINT32_MAX - plannedWidth) {
      emitError(location)
          << "randomize property plan exceeds its 32-bit bit-offset space";
      return failure();
    }
    Type type = typeAttr.getValue();
    std::optional<unsigned> typeWidth = sim::getPackedWidth(type);
    if (!typeWidth || *typeWidth != width) {
      emitError(location) << "randomize property type width is inconsistent";
      return failure();
    }
    Value reference;
    Value nestedObjectReference;
    FlatSymbolRefAttr nestedModeField;
    if (isNestedObject) {
      Type objectReferenceType = sim::ManagedRefType::get(
          function.getContext(), nestedObjectStorageTypeAttr.getValue(),
          objectType.getClassName());
      nestedObjectReference = sim::SimClassFieldRefOp::create(
          builder, location, objectReferenceType, receiver, nestedObjectField);
      auto nestedDeclaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function,
              cast<sim::ClassHandleType>(nestedObjectTypeAttr.getValue())
                  .getClassName());
      while (nestedDeclaration &&
             !nestedDeclaration->hasAttr("obelisk_sim.random_mode_field")) {
        if (!nestedDeclaration.getBaseAttr())
          break;
        nestedDeclaration =
            SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                function, nestedDeclaration.getBaseAttr());
      }
      nestedModeField =
          nestedDeclaration
              ? nestedDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                    "obelisk_sim.random_mode_field")
              : FlatSymbolRefAttr{};
      if (!nestedModeField) {
        emitError(location) << "nested rand object has no rand_mode field";
        return failure();
      }
    } else if (field) {
      Type referencedType = isContainerSize ? containerTypeAttr.getValue()
                                            : type;
      Type referenceType = sim::ManagedRefType::get(
          function.getContext(), referencedType, objectType.getClassName());
      reference = sim::SimClassFieldRefOp::create(
          builder, location, referenceType, receiver, field);
    } else {
      reference = lvalues.lookup(referencePath.getValue());
      if (!reference || getReferenceElementType(reference) != type) {
        emitError(location)
            << "static random property has no typed unit-local reference: "
            << referencePath.getValue();
        return failure();
      }
    }
    std::optional<uint64_t> randomModeStorage;
    if (modeStorageAttr) {
      APInt storage = modeStorageAttr.getValue();
      if (field || storage.isNegative() || storage.getActiveBits() > 63) {
        emitError(location) << "static random property mode is malformed";
        return failure();
      }
      randomModeStorage = storage.getZExtValue();
    }
    if (referencePath && !op->hasAttr(randomizeExplicitPropertiesAttrName) &&
        !randomModeStorage) {
      emitError(location)
          << "ordinary static random property has no shared rand_mode state";
      return failure();
    }
    Value randcKeyReference;
    Value randcPositionReference;
    if (randcAttr.getValue()) {
      auto keyField = property.getAs<FlatSymbolRefAttr>("randc_key_field");
      auto positionField =
          property.getAs<FlatSymbolRefAttr>("randc_position_field");
      auto keyPath = property.getAs<StringAttr>(randomRandCKeyPathAttrName);
      auto positionPath =
          property.getAs<StringAttr>(randomRandCPositionPathAttrName);
      bool hasKeyField = static_cast<bool>(keyField);
      bool hasPositionField = static_cast<bool>(positionField);
      bool hasKeyPath = static_cast<bool>(keyPath);
      bool hasPositionPath = static_cast<bool>(positionPath);
      if (width > 32 || hasKeyField != hasPositionField ||
          hasKeyPath != hasPositionPath || hasKeyField == hasKeyPath ||
          hasKeyField != static_cast<bool>(field)) {
        emitError(location) << "randc property plan is malformed";
        return failure();
      }
      if (hasKeyField) {
        Type stateReferenceType = sim::ManagedRefType::get(
            function.getContext(), i64, objectType.getClassName());
        randcKeyReference = sim::SimClassFieldRefOp::create(
            builder, location, stateReferenceType, receiver, keyField);
        randcPositionReference = sim::SimClassFieldRefOp::create(
            builder, location, stateReferenceType, receiver, positionField);
      } else {
        randcKeyReference = lvalues.lookup(keyPath.getValue());
        randcPositionReference = lvalues.lookup(positionPath.getValue());
        if (!randcKeyReference || !randcPositionReference ||
            getReferenceElementType(randcKeyReference) != i64 ||
            getReferenceElementType(randcPositionReference) != i64) {
          emitError(location)
              << "static randc state has no typed unit-local references";
          return failure();
        }
      }
    }
    SmallVector<PropertyDomain> propertyDomains;
    if (auto domains = property.getAs<ArrayAttr>("domains")) {
      if (width > 64) {
        emitError(location)
            << "finite semantic domains in random properties wider than 64 "
               "bits are not executable yet";
        return failure();
      }
      uint64_t coveredMask = 0;
      for (Attribute domainAttr : domains) {
        auto domain = dyn_cast<DictionaryAttr>(domainAttr);
        auto offsetAttr =
            domain ? domain.getAs<IntegerAttr>("offset") : IntegerAttr{};
        auto domainWidthAttr =
            domain ? domain.getAs<IntegerAttr>("width") : IntegerAttr{};
        auto patternsAttr =
            domain ? domain.getAs<ArrayAttr>("patterns") : ArrayAttr{};
        if (!offsetAttr || !domainWidthAttr || !patternsAttr ||
            patternsAttr.empty() || offsetAttr.getValue().isNegative() ||
            domainWidthAttr.getValue().isNegative() ||
            offsetAttr.getValue().getActiveBits() > 64 ||
            domainWidthAttr.getValue().getActiveBits() > 64) {
          emitError(location) << "randomize property domain is malformed";
          return failure();
        }
        uint64_t domainOffset = offsetAttr.getValue().getZExtValue();
        uint64_t domainWidth = domainWidthAttr.getValue().getZExtValue();
        if (domainWidth == 0 || domainWidth > 64 || domainOffset > width ||
            domainWidth > width - domainOffset) {
          emitError(location) << "randomize property domain is out of range";
          return failure();
        }
        uint64_t localMask =
            domainWidth == 64 ? UINT64_MAX : (uint64_t{1} << domainWidth) - 1;
        uint64_t shiftedMask = localMask << domainOffset;
        if ((coveredMask & shiftedMask) != 0) {
          emitError(location) << "randomize property domains overlap";
          return failure();
        }
        coveredMask |= shiftedMask;
        PropertyDomain propertyDomain{static_cast<uint32_t>(domainOffset),
                                      static_cast<unsigned>(domainWidth),
                                      {},
                                      0};
        bool fullCardinality = false;
        for (Attribute patternAttr : patternsAttr) {
          auto pattern = dyn_cast<DictionaryAttr>(patternAttr);
          auto maskAttr =
              pattern ? pattern.getAs<IntegerAttr>("mask") : IntegerAttr{};
          auto valueAttr =
              pattern ? pattern.getAs<IntegerAttr>("value") : IntegerAttr{};
          if (!maskAttr || !valueAttr ||
              maskAttr.getValue().getBitWidth() > 64 ||
              valueAttr.getValue().getBitWidth() > 64) {
            emitError(location)
                << "randomize property domain pattern is malformed";
            return failure();
          }
          uint64_t patternMask = maskAttr.getValue().getZExtValue();
          uint64_t patternValue = valueAttr.getValue().getZExtValue();
          if ((patternMask & ~localMask) != 0 ||
              (patternValue & ~patternMask) != 0) {
            emitError(location)
                << "randomize property domain pattern is inconsistent";
            return failure();
          }
          for (const DomainPattern &other : propertyDomain.patterns)
            if (((patternValue ^ other.value) & (patternMask & other.mask)) ==
                0) {
              emitError(location)
                  << "randomize property domain patterns overlap";
              return failure();
            }
          uint64_t freeMask = localMask & ~patternMask;
          unsigned freeBits = 0;
          for (uint64_t bits = freeMask; bits != 0; bits &= bits - 1)
            ++freeBits;
          uint64_t patternCardinality =
              freeBits == 64 ? 0 : uint64_t{1} << freeBits;
          uint64_t updated = 0;
          bool overflow = __builtin_add_overflow(propertyDomain.cardinality,
                                                 patternCardinality, &updated);
          if (fullCardinality || freeBits == 64 || (overflow && updated != 0)) {
            emitError(location)
                << "randomize property domain cardinality is malformed";
            return failure();
          }
          if (overflow)
            fullCardinality = true;
          else
            propertyDomain.cardinality = updated;
          propertyDomain.patterns.push_back(
              {patternMask, patternValue, freeMask, patternCardinality});
        }
        bool coversFullDomain =
            fullCardinality ||
            (domainWidth != 64 &&
             propertyDomain.cardinality == (uint64_t{1} << domainWidth));
        // A domain that covers every field encoding carries no semantic
        // restriction. Treat its bits as ordinary packed bits instead of
        // retaining a zero-cardinality radix that would later require an
        // invalid division or remainder operation.
        if (coversFullDomain)
          continue;
        propertyDomains.push_back(std::move(propertyDomain));
      }
    }
    SmallVector<ObjectPathElement> nestedObjectPath;
    if (failed(parseNestedObjectPath(nestedObjectPathAttr,
                                     nestedObjectPath)))
      return failure();
    planned.push_back({type,
                       static_cast<unsigned>(width),
                       static_cast<unsigned>(
                           modeIndexAttr.getValue().getZExtValue()),
                       isContainerSize,
                       containerTypeAttr ? containerTypeAttr.getValue() : Type{},
                       sizeConstraintMaskAttr
                           ? sizeConstraintMaskAttr.getValue().getZExtValue()
                           : 0,
                       unconditionalSizeConstraintAttr &&
                           unconditionalSizeConstraintAttr.getValue(),
                       nestedObjectReference,
                       nestedObjectTypeAttr ? nestedObjectTypeAttr.getValue()
                                            : Type{},
                       field,
                       nestedModeIndexAttr
                           ? static_cast<unsigned>(
                                 nestedModeIndexAttr.getValue().getZExtValue())
                           : 0,
                       nestedModeField,
                       signedAttr.getValue(),
                       reference,
                       randcAttr.getValue(),
                       randcKeyReference,
                       randcPositionReference,
                       {},
                       {},
                       randomModeStorage,
                       std::move(propertyDomains),
                       std::move(nestedObjectPath),
                       nestedObjectField,
                       {}});
    plannedWidth += width;
  }
  if (plannedWidth != totalWidth) {
    emitError(location) << "randomize property plan width is inconsistent";
    return failure();
  }
  struct ContainerProperty {
    Type type;
    Type elementType;
    unsigned elementWidth;
    unsigned modeIndex;
    Value reference;
    Value nestedObjectReference;
    Type nestedObjectType;
    FlatSymbolRefAttr nestedField;
    unsigned nestedModeIndex;
    FlatSymbolRefAttr nestedModeField;
    SmallVector<ObjectPathElement> nestedObjectPath;
    bool inertClassHandles;
    FlatSymbolRefAttr nestedObjectRootField;
  };
  SmallVector<ContainerProperty> plannedContainers;
  for (Attribute propertyAttr : containerProperties) {
    auto property = dyn_cast<DictionaryAttr>(propertyAttr);
    auto field = property ? property.getAs<FlatSymbolRefAttr>("field")
                          : FlatSymbolRefAttr{};
    auto typeAttr = property ? property.getAs<TypeAttr>("type") : TypeAttr{};
    auto elementTypeAttr =
        property ? property.getAs<TypeAttr>("element_type") : TypeAttr{};
    auto elementWidthAttr =
        property ? property.getAs<IntegerAttr>("element_width")
                 : IntegerAttr{};
    auto modeIndexAttr = property
                             ? property.getAs<IntegerAttr>(
                                   randomPropertyModeIndexAttrName)
                             : IntegerAttr{};
    auto nestedObjectField =
        property ? property.getAs<FlatSymbolRefAttr>(
                       randomNestedObjectFieldAttrName)
                 : FlatSymbolRefAttr{};
    auto nestedObjectTypeAttr =
        property ? property.getAs<TypeAttr>(randomNestedObjectTypeAttrName)
                 : TypeAttr{};
    auto nestedObjectStorageTypeAttr =
        property
            ? property.getAs<TypeAttr>(randomNestedObjectStorageTypeAttrName)
            : TypeAttr{};
    auto nestedModeIndexAttr =
        property ? property.getAs<IntegerAttr>(randomNestedModeIndexAttrName)
                 : IntegerAttr{};
    auto nestedObjectPathAttr =
        property ? property.getAs<ArrayAttr>(randomNestedObjectPathAttrName)
                 : ArrayAttr{};
    bool inertClassHandles =
        property && property.contains("inert_class_handles");
    bool isNestedObject = static_cast<bool>(nestedObjectField);
    bool hasCompleteNestedObject =
        nestedObjectField && nestedObjectTypeAttr &&
        nestedObjectStorageTypeAttr && nestedModeIndexAttr;
    bool hasAnyNestedObject =
        nestedObjectField || nestedObjectTypeAttr ||
        nestedObjectStorageTypeAttr || nestedModeIndexAttr;
    if (!field || !typeAttr || !elementTypeAttr || !elementWidthAttr ||
        !modeIndexAttr || elementWidthAttr.getValue().isNegative() ||
        (!inertClassHandles && elementWidthAttr.getValue().isZero()) ||
        elementWidthAttr.getValue().getActiveBits() > 32 ||
        elementWidthAttr.getValue().getZExtValue() > 64 ||
        modeIndexAttr.getValue().isNegative() ||
        modeIndexAttr.getValue().getActiveBits() > 32 ||
        modeIndexAttr.getValue().getZExtValue() >= 64 ||
        (hasAnyNestedObject != hasCompleteNestedObject) ||
        (isNestedObject &&
         (!isa<sim::ClassHandleType>(nestedObjectTypeAttr.getValue()) ||
          !isa<sim::ClassHandleType>(nestedObjectStorageTypeAttr.getValue()) ||
          nestedModeIndexAttr.getValue().isNegative() ||
          nestedModeIndexAttr.getValue().getActiveBits() > 32 ||
          nestedModeIndexAttr.getValue().getZExtValue() >= 64))) {
      emitError(location) << "random dynamic-container plan is malformed";
      return failure();
    }
    Type containerElementType;
    if (auto array = dyn_cast<sim::DynamicArrayType>(typeAttr.getValue()))
      containerElementType = array.getElementType();
    else if (auto queue = dyn_cast<sim::QueueType>(typeAttr.getValue()))
      containerElementType = queue.getElementType();
    uint64_t elementWidth = elementWidthAttr.getValue().getZExtValue();
    if (!containerElementType ||
        containerElementType != elementTypeAttr.getValue() ||
        ((!inertClassHandles &&
          sim::getPackedWidth(elementTypeAttr.getValue()) != elementWidth) ||
         (inertClassHandles &&
          !isa<sim::ClassHandleType>(elementTypeAttr.getValue())))) {
      emitError(location) << "random dynamic-container type is inconsistent";
      return failure();
    }
    Value reference;
    Value nestedObjectReference;
    FlatSymbolRefAttr nestedModeField;
    if (isNestedObject) {
      Type objectReferenceType = sim::ManagedRefType::get(
          function.getContext(), nestedObjectStorageTypeAttr.getValue(),
          objectType.getClassName());
      nestedObjectReference = sim::SimClassFieldRefOp::create(
          builder, location, objectReferenceType, receiver, nestedObjectField);
      auto nestedDeclaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function,
              cast<sim::ClassHandleType>(nestedObjectTypeAttr.getValue())
                  .getClassName());
      while (nestedDeclaration &&
             !nestedDeclaration->hasAttr("obelisk_sim.random_mode_field")) {
        if (!nestedDeclaration.getBaseAttr())
          break;
        nestedDeclaration =
            SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                function, nestedDeclaration.getBaseAttr());
      }
      nestedModeField =
          nestedDeclaration
              ? nestedDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                    "obelisk_sim.random_mode_field")
              : FlatSymbolRefAttr{};
      if (!nestedModeField) {
        emitError(location) << "nested rand object has no rand_mode field";
        return failure();
      }
    } else {
      Type referenceType = sim::ManagedRefType::get(
          function.getContext(), typeAttr.getValue(),
          objectType.getClassName());
      reference = sim::SimClassFieldRefOp::create(
          builder, location, referenceType, receiver, field);
    }
    SmallVector<ObjectPathElement> nestedObjectPath;
    if ((!isNestedObject && nestedObjectPathAttr) ||
        failed(parseNestedObjectPath(nestedObjectPathAttr,
                                     nestedObjectPath)))
      return failure();
    plannedContainers.push_back(
        {typeAttr.getValue(), elementTypeAttr.getValue(),
         static_cast<unsigned>(elementWidth),
         static_cast<unsigned>(modeIndexAttr.getValue().getZExtValue()),
         reference,
         nestedObjectReference,
         nestedObjectTypeAttr ? nestedObjectTypeAttr.getValue() : Type{},
         field,
         nestedModeIndexAttr
             ? static_cast<unsigned>(
                   nestedModeIndexAttr.getValue().getZExtValue())
             : 0,
         nestedModeField,
         std::move(nestedObjectPath),
         inertClassHandles,
         nestedObjectField});
  }
  SmallVector<bool> containerNeedsIdentity(plannedContainers.size(), false);
  auto sameContainerOccurrence = [](const ContainerProperty &left,
                                    const ContainerProperty &right) {
    if (left.nestedObjectRootField != right.nestedObjectRootField ||
        left.nestedObjectPath.size() != right.nestedObjectPath.size())
      return false;
    return llvm::equal(
        left.nestedObjectPath, right.nestedObjectPath,
        [](const ObjectPathElement &leftElement,
           const ObjectPathElement &rightElement) {
          return leftElement.field == rightElement.field;
        });
  };
  auto containerOwnerType = [](const ContainerProperty &property) {
    return property.nestedObjectPath.empty()
               ? property.nestedObjectType
               : property.nestedObjectPath.back().concreteType;
  };
  for (auto [leftIndex, left] : llvm::enumerate(plannedContainers)) {
    if (!left.nestedObjectReference)
      continue;
    for (auto [relativeRightIndex, right] : llvm::enumerate(
             ArrayRef(plannedContainers).drop_front(leftIndex + 1))) {
      unsigned rightIndex = leftIndex + 1 + relativeRightIndex;
      if (!right.nestedObjectReference ||
          left.nestedField != right.nestedField ||
          containerOwnerType(left) != containerOwnerType(right) ||
          sameContainerOccurrence(left, right))
        continue;
      containerNeedsIdentity[leftIndex] = true;
      containerNeedsIdentity[rightIndex] = true;
    }
  }
  bool hasRandC = llvm::any_of(
      planned, [](const Property &property) { return property.isRandC; });

  sim::SimClassDeclOp declaration =
      SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, objectType.getClassName());
  while (declaration &&
         !declaration->hasAttr("obelisk_sim.random_state_field")) {
    if (!declaration.getBaseAttr())
      break;
    declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
        function, declaration.getBaseAttr());
  }
  auto stateField = declaration ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                      "obelisk_sim.random_state_field")
                                : FlatSymbolRefAttr{};
  auto incrementField = declaration
                            ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                  "obelisk_sim.random_increment_field")
                            : FlatSymbolRefAttr{};
  auto modeField = declaration ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                     "obelisk_sim.random_mode_field")
                               : FlatSymbolRefAttr{};
  auto constraintModeField =
      declaration ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                        "obelisk_sim.constraint_mode_field")
                  : FlatSymbolRefAttr{};
  if (!declaration || !stateField || !incrementField || !modeField ||
      !constraintModeField) {
    emitError(location)
        << "randomize receiver has no object-local stream or mode state";
    return failure();
  }
  Type randomReferenceType = sim::ManagedRefType::get(
      function.getContext(), i64, objectType.getClassName());
  Value stateReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, stateField);
  Value incrementReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, incrementField);
  Value modeReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, modeField);
  Value constraintModeReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, constraintModeField);
  Value state =
      sim::SimManagedLoadOp::create(builder, location, i64, stateReference);
  Value increment =
      sim::SimManagedLoadOp::create(builder, location, i64, incrementReference);
  Value mode =
      sim::SimManagedLoadOp::create(builder, location, i64, modeReference);
  Value constraintMode = sim::SimManagedLoadOp::create(
      builder, location, i64, constraintModeReference);

  auto constant64 = [&](uint64_t value) -> Value {
    return arith::ConstantOp::create(
        builder, location, i64, builder.getIntegerAttr(i64, APInt(64, value)));
  };
  auto constantLike = [&](Value exemplar, uint64_t value) -> Value {
    auto type = cast<IntegerType>(exemplar.getType());
    return arith::ConstantOp::create(
        builder, location, type,
        builder.getIntegerAttr(type, APInt(type.getWidth(), value)));
  };
  for (Attribute guardAttr : recursiveAliasGuards) {
    auto guard = dyn_cast<DictionaryAttr>(guardAttr);
    auto field = guard ? guard.getAs<FlatSymbolRefAttr>("field")
                       : FlatSymbolRefAttr{};
    auto concreteTypeAttr =
        guard ? guard.getAs<TypeAttr>("concrete_type") : TypeAttr{};
    auto storageTypeAttr =
        guard ? guard.getAs<TypeAttr>("storage_type") : TypeAttr{};
    auto outerModeIndexAttr =
        guard ? guard.getAs<IntegerAttr>("outer_mode_index") : IntegerAttr{};
    auto pathAttr = guard ? guard.getAs<ArrayAttr>("path") : ArrayAttr{};
    auto aliasDepthAttr =
        guard ? guard.getAs<IntegerAttr>("alias_depth") : IntegerAttr{};
    SmallVector<ObjectPathElement> path;
    if (!field || !concreteTypeAttr || !storageTypeAttr ||
        !outerModeIndexAttr || !pathAttr || pathAttr.empty() ||
        !aliasDepthAttr ||
        !isa<sim::ClassHandleType>(concreteTypeAttr.getValue()) ||
        !isa<sim::ClassHandleType>(storageTypeAttr.getValue()) ||
        outerModeIndexAttr.getValue().isNegative() ||
        outerModeIndexAttr.getValue().getActiveBits() > 32 ||
        outerModeIndexAttr.getValue().getZExtValue() >= 64 ||
        aliasDepthAttr.getValue().isNegative() ||
        aliasDepthAttr.getValue().getActiveBits() > 32 ||
        aliasDepthAttr.getValue().getZExtValue() >= pathAttr.size() ||
        failed(parseNestedObjectPath(pathAttr, path))) {
      emitError(location) << "recursive random-object alias guard is malformed";
      return failure();
    }
    auto rootConcreteType =
        cast<sim::ClassHandleType>(concreteTypeAttr.getValue());
    auto rootDeclaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, rootConcreteType.getClassName());
    while (rootDeclaration &&
           !rootDeclaration->hasAttr("obelisk_sim.random_mode_field")) {
      if (!rootDeclaration.getBaseAttr())
        break;
      rootDeclaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, rootDeclaration.getBaseAttr());
    }
    auto rootModeField =
        rootDeclaration
            ? rootDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                  "obelisk_sim.random_mode_field")
            : FlatSymbolRefAttr{};
    if (!rootModeField) {
      emitError(location)
          << "recursive random-object root has no rand_mode field";
      return failure();
    }

    Type rootReferenceType = sim::ManagedRefType::get(
        function.getContext(), storageTypeAttr.getValue(),
        objectType.getClassName());
    Value rootReference = sim::SimClassFieldRefOp::create(
        builder, location, rootReferenceType, receiver, field);
    FailureOr<Value> loadedRoot = loadReference(rootReference, location);
    if (failed(loadedRoot))
      return failure();
    Value rootNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), *loadedRoot);
    uint64_t outerModeIndex =
        outerModeIndexAttr.getValue().getZExtValue();
    Value outerModeBit = arith::AndIOp::create(
        builder, location, mode, constant64(uint64_t{1} << outerModeIndex));
    Value outerEnabled = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, outerModeBit,
        constant64(0));
    Value rootNonNull = arith::XOrIOp::create(
        builder, location, rootNull,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    Value rootActive = arith::AndIOp::create(builder, location, outerEnabled,
                                             rootNonNull);
    Block *continueBlock = addBlock();
    Block *rootPresentBlock = addBlock();
    cf::CondBranchOp::create(builder, location, rootActive, rootPresentBlock,
                             ValueRange{}, continueBlock, ValueRange{});
    setCurrent(rootPresentBlock);

    Value currentObject = *loadedRoot;
    if (currentObject.getType() != concreteTypeAttr.getValue())
      currentObject = sim::SimClassCastOp::create(
          builder, location, concreteTypeAttr.getValue(), currentObject);
    auto currentType = rootConcreteType;
    FlatSymbolRefAttr currentModeField = rootModeField;
    SmallVector<Value> activeObjects{currentObject};
    for (const ObjectPathElement &element : path) {
      Type modeReferenceType = sim::ManagedRefType::get(
          function.getContext(), i64, currentType.getClassName());
      Value currentModeReference = sim::SimClassFieldRefOp::create(
          builder, location, modeReferenceType, currentObject,
          currentModeField);
      Value currentMode = sim::SimManagedLoadOp::create(
          builder, location, i64, currentModeReference);
      Value edgeModeBit = arith::AndIOp::create(
          builder, location, currentMode,
          constant64(uint64_t{1} << element.modeIndex));
      Value edgeEnabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, edgeModeBit,
          constant64(0));
      Type edgeReferenceType = sim::ManagedRefType::get(
          function.getContext(), element.storageType,
          currentType.getClassName());
      Value edgeReference = sim::SimClassFieldRefOp::create(
          builder, location, edgeReferenceType, currentObject, element.field);
      FailureOr<Value> loadedEdge = loadReference(edgeReference, location);
      if (failed(loadedEdge))
        return failure();
      Value edgeNull = sim::SimManagedIsNullOp::create(
          builder, location, builder.getI1Type(), *loadedEdge);
      Value edgeNonNull = arith::XOrIOp::create(
          builder, location, edgeNull,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
      Value edgeActive = arith::AndIOp::create(builder, location, edgeEnabled,
                                               edgeNonNull);
      Block *edgePresentBlock = addBlock();
      cf::CondBranchOp::create(builder, location, edgeActive, edgePresentBlock,
                               ValueRange{}, continueBlock, ValueRange{});
      setCurrent(edgePresentBlock);
      currentObject = *loadedEdge;
      if (currentObject.getType() != element.concreteType)
        currentObject = sim::SimClassCastOp::create(
            builder, location, element.concreteType, currentObject);
      activeObjects.push_back(currentObject);
      currentType = cast<sim::ClassHandleType>(element.concreteType);
      currentModeField = element.modeField;
    }

    unsigned aliasDepth =
        static_cast<unsigned>(aliasDepthAttr.getValue().getZExtValue());
    Value targetID =
        sim::SimClassIdOp::create(builder, location, activeObjects.back());
    Value ancestorID = sim::SimClassIdOp::create(
        builder, location, activeObjects[aliasDepth]);
    Value closesCycle = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, targetID, ancestorID);
    Block *unsupportedBlock = addBlock();
    cf::CondBranchOp::create(builder, location, closesCycle, continueBlock,
                             ValueRange{}, unsupportedBlock, ValueRange{});
    setCurrent(unsupportedBlock);
    if (failed(emitRuntimeFatal(
            location,
            "randomize encountered a distinct object beyond the static "
            "recursive graph plan")))
      return failure();
    setCurrent(continueBlock);
  }
  struct NestedHookRuntime {
    DictionaryAttr plan;
    Value object;
    Value enabled;
    Value objectID;
  };
  SmallVector<NestedHookRuntime> nestedHookRuntimes;
  auto callNestedHook = [&](DictionaryAttr plan, StringRef prefix,
                            Value object) -> LogicalResult {
    auto callee = plan.getAs<FlatSymbolRefAttr>((prefix + "_callee").str());
    if (!callee)
      return success();
    auto owner = plan.getAs<FlatSymbolRefAttr>((prefix + "_owner").str());
    auto captures = plan.getAs<ArrayAttr>((prefix + "_captures").str());
    auto reads = plan.getAs<ArrayAttr>((prefix + "_reads").str());
    if (!owner || !captures || !reads) {
      emitError(location) << "nested randomization hook plan is malformed";
      return failure();
    }
    Type ownerType = sim::ClassHandleType::get(function.getContext(), owner);
    Value hookReceiver = object;
    if (hookReceiver.getType() != ownerType)
      hookReceiver = sim::SimClassCastOp::create(builder, location, ownerType,
                                                 hookReceiver);
    llvm::StringSet<> readCaptures;
    for (Attribute read : reads)
      readCaptures.insert(cast<StringAttr>(read).getValue());
    SmallVector<Value> arguments;
    for (Attribute captureAttr : captures) {
      StringRef path = cast<StringAttr>(captureAttr).getValue();
      Value capture = values.lookup(path);
      if (!capture) {
        emitError(location)
            << "nested randomization hook capture has no local binding: "
            << path;
        return failure();
      }
      if (readCaptures.contains(path))
        recordSensitivity(capture);
      arguments.push_back(capture);
    }
    sim::SimClassDirectCallOp::create(builder, location, TypeRange{}, callee,
                                      hookReceiver, arguments);
    return success();
  };
  for (Attribute hookAttr : nestedHooks) {
    auto hook = dyn_cast<DictionaryAttr>(hookAttr);
    auto field = hook ? hook.getAs<FlatSymbolRefAttr>("field")
                      : FlatSymbolRefAttr{};
    auto concreteTypeAttr =
        hook ? hook.getAs<TypeAttr>("concrete_type") : TypeAttr{};
    auto storageTypeAttr =
        hook ? hook.getAs<TypeAttr>("storage_type") : TypeAttr{};
    auto outerModeIndexAttr =
        hook ? hook.getAs<IntegerAttr>("outer_mode_index") : IntegerAttr{};
    auto pathAttr = hook ? hook.getAs<ArrayAttr>("path") : ArrayAttr{};
    if (!hook || !field || !concreteTypeAttr || !storageTypeAttr ||
        !outerModeIndexAttr || outerModeIndexAttr.getValue().isNegative() ||
        outerModeIndexAttr.getValue().getActiveBits() > 32 ||
        outerModeIndexAttr.getValue().getZExtValue() >= 64 ||
        !isa<sim::ClassHandleType>(concreteTypeAttr.getValue()) ||
        !isa<sim::ClassHandleType>(storageTypeAttr.getValue())) {
      emitError(location) << "nested randomization hook plan is malformed";
      return failure();
    }
    SmallVector<ObjectPathElement> path;
    if (failed(parseNestedObjectPath(pathAttr, path)))
      return failure();
    Type objectReferenceType = sim::ManagedRefType::get(
        function.getContext(), storageTypeAttr.getValue(),
        objectType.getClassName());
    Value objectReference = sim::SimClassFieldRefOp::create(
        builder, location, objectReferenceType, receiver, field);
    FailureOr<Value> loadedObject = loadReference(objectReference, location);
    if (failed(loadedObject))
      return failure();
    Value object = *loadedObject;
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), object);
    uint64_t outerModeIndex = outerModeIndexAttr.getValue().getZExtValue();
    Value outerModeBit = arith::AndIOp::create(
        builder, location, mode, constant64(uint64_t{1} << outerModeIndex));
    Value outerEnabled = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, outerModeBit,
        constant64(0));
    Value nonNull = arith::XOrIOp::create(
        builder, location, isNull,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    Value enabled = arith::AndIOp::create(builder, location, outerEnabled,
                                          nonNull);
    if (object.getType() != concreteTypeAttr.getValue())
      object = sim::SimClassCastOp::create(
          builder, location, concreteTypeAttr.getValue(), object);
    if (!path.empty()) {
      Type targetType = path.back().concreteType;
      Block *missingBlock = addBlock();
      Block *rootPresentBlock = addBlock();
      Block *pathMergeBlock = addBlock();
      pathMergeBlock->addArgument(targetType, location);
      pathMergeBlock->addArgument(builder.getI1Type(), location);
      cf::CondBranchOp::create(builder, location, enabled, rootPresentBlock,
                               ValueRange{}, missingBlock, ValueRange{});

      setCurrent(missingBlock);
      Value missingObject = createDefaultValue(builder, location, targetType);
      if (!missingObject)
        return failure();
      Value falseValue = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(false));
      cf::BranchOp::create(builder, location, pathMergeBlock,
                           ValueRange{missingObject, falseValue});

      setCurrent(rootPresentBlock);
      auto currentType = cast<sim::ClassHandleType>(concreteTypeAttr.getValue());
      for (const ObjectPathElement &element : path) {
        sim::SimClassDeclOp currentDeclaration =
            SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                function, currentType.getClassName());
        while (currentDeclaration &&
               !currentDeclaration->hasAttr("obelisk_sim.random_mode_field")) {
          if (!currentDeclaration.getBaseAttr())
            break;
          currentDeclaration =
              SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                  function, currentDeclaration.getBaseAttr());
        }
        auto currentModeField =
            currentDeclaration
                ? currentDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                      "obelisk_sim.random_mode_field")
                : FlatSymbolRefAttr{};
        if (!currentModeField) {
          emitError(location)
              << "nested hook path has no rand_mode field";
          return failure();
        }
        Type modeReferenceType = sim::ManagedRefType::get(
            function.getContext(), i64, currentType.getClassName());
        Value currentModeReference = sim::SimClassFieldRefOp::create(
            builder, location, modeReferenceType, object, currentModeField);
        Value currentMode = sim::SimManagedLoadOp::create(
            builder, location, i64, currentModeReference);
        Value edgeModeBit = arith::AndIOp::create(
            builder, location, currentMode,
            constant64(uint64_t{1} << element.modeIndex));
        Value edgeEnabled = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, edgeModeBit,
            constant64(0));
        Type edgeReferenceType = sim::ManagedRefType::get(
            function.getContext(), element.storageType,
            currentType.getClassName());
        Value edgeReference = sim::SimClassFieldRefOp::create(
            builder, location, edgeReferenceType, object, element.field);
        FailureOr<Value> loadedEdge = loadReference(edgeReference, location);
        if (failed(loadedEdge))
          return failure();
        Value edgeNull = sim::SimManagedIsNullOp::create(
            builder, location, builder.getI1Type(), *loadedEdge);
        Value edgeNonNull = arith::XOrIOp::create(
            builder, location, edgeNull,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true)));
        Value edgeActive = arith::AndIOp::create(
            builder, location, edgeEnabled, edgeNonNull);
        Block *edgePresentBlock = addBlock();
        cf::CondBranchOp::create(builder, location, edgeActive,
                                 edgePresentBlock, ValueRange{}, missingBlock,
                                 ValueRange{});
        setCurrent(edgePresentBlock);
        object = *loadedEdge;
        if (object.getType() != element.concreteType)
          object = sim::SimClassCastOp::create(
              builder, location, element.concreteType, object);
        currentType = cast<sim::ClassHandleType>(element.concreteType);
      }
      Value trueValue = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      cf::BranchOp::create(builder, location, pathMergeBlock,
                           ValueRange{object, trueValue});
      setCurrent(pathMergeBlock);
      object = pathMergeBlock->getArgument(0);
      enabled = pathMergeBlock->getArgument(1);
    }
    Value objectID = sim::SimClassIdOp::create(builder, location, object);
    Value alreadyVisited = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    for (const NestedHookRuntime &previous : nestedHookRuntimes) {
      Value sameID = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, objectID,
          previous.objectID);
      Value previousMatch = arith::AndIOp::create(
          builder, location, previous.enabled, sameID);
      alreadyVisited = arith::OrIOp::create(builder, location, alreadyVisited,
                                            previousMatch);
    }
    Value firstVisit = arith::XOrIOp::create(
        builder, location, alreadyVisited,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    Value callEnabled =
        arith::AndIOp::create(builder, location, enabled, firstVisit);
    Block *callBlock = addBlock();
    Block *mergeBlock = addBlock();
    cf::CondBranchOp::create(builder, location, callEnabled, callBlock,
                             ValueRange{}, mergeBlock, ValueRange{});
    setCurrent(callBlock);
    if (failed(callNestedHook(hook, "pre", object)))
      return failure();
    cf::BranchOp::create(builder, location, mergeBlock);
    setCurrent(mergeBlock);
    nestedHookRuntimes.push_back({hook, object, callEnabled, objectID});
  }
  Value nullNestedConstraintMask = constant64(0);
  for (Attribute nestedAttr : nestedConstraintModes) {
    auto nested = dyn_cast<DictionaryAttr>(nestedAttr);
    auto field = nested ? nested.getAs<FlatSymbolRefAttr>("field")
                        : FlatSymbolRefAttr{};
    auto concreteTypeAttr =
        nested ? nested.getAs<TypeAttr>("concrete_type") : TypeAttr{};
    auto storageTypeAttr =
        nested ? nested.getAs<TypeAttr>("storage_type") : TypeAttr{};
    auto globalIndices =
        nested ? nested.getAs<DenseI64ArrayAttr>("global_indices")
               : DenseI64ArrayAttr{};
    auto outerModeIndexAttr =
        nested ? nested.getAs<IntegerAttr>("outer_mode_index") : IntegerAttr{};
    auto pathAttr = nested ? nested.getAs<ArrayAttr>("path") : ArrayAttr{};
    if (!field || !concreteTypeAttr || !storageTypeAttr || !globalIndices ||
        !outerModeIndexAttr || outerModeIndexAttr.getValue().isNegative() ||
        outerModeIndexAttr.getValue().getActiveBits() > 32 ||
        outerModeIndexAttr.getValue().getZExtValue() >= 64 ||
        globalIndices.empty() || globalIndices.size() > 64 ||
        !isa<sim::ClassHandleType>(concreteTypeAttr.getValue()) ||
        !isa<sim::ClassHandleType>(storageTypeAttr.getValue())) {
      emitError(location) << "nested constraint-mode plan is malformed";
      return failure();
    }
    uint64_t globalMask = 0;
    for (int64_t index : globalIndices.asArrayRef()) {
      if (index < 0 || static_cast<uint64_t>(index) >= constraintCount ||
          (globalMask & (uint64_t{1} << index)) != 0) {
        emitError(location)
            << "nested constraint-mode index is malformed";
        return failure();
      }
      globalMask |= uint64_t{1} << index;
    }
    SmallVector<ObjectPathElement> path;
    if (failed(parseNestedObjectPath(pathAttr, path)))
      return failure();
    Type objectReferenceType = sim::ManagedRefType::get(
        function.getContext(), storageTypeAttr.getValue(),
        objectType.getClassName());
    Value objectReference = sim::SimClassFieldRefOp::create(
        builder, location, objectReferenceType, receiver, field);
    FailureOr<Value> loadedObject = loadReference(objectReference, location);
    if (failed(loadedObject))
      return failure();
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), *loadedObject);
    uint64_t outerModeIndex = outerModeIndexAttr.getValue().getZExtValue();
    Value outerModeBit = arith::AndIOp::create(
        builder, location, mode, constant64(uint64_t{1} << outerModeIndex));
    Value outerDisabled = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, outerModeBit,
        constant64(0));
    Value rootInactive = arith::OrIOp::create(builder, location, isNull,
                                              outerDisabled);
    Block *disabledBlock = addBlock();
    Block *objectBlock = addBlock();
    Block *mergeBlock = addBlock();
    mergeBlock->addArgument(i64, location);
    mergeBlock->addArgument(builder.getI1Type(), location);
    cf::CondBranchOp::create(builder, location, rootInactive, disabledBlock,
                             ValueRange{}, objectBlock, ValueRange{});

    setCurrent(disabledBlock);
    Value disabledMode = arith::OrIOp::create(
        builder, location, constraintMode, constant64(globalMask));
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    cf::BranchOp::create(builder, location, mergeBlock,
                         ValueRange{disabledMode, trueValue});

    setCurrent(objectBlock);
    Value object = *loadedObject;
    if (object.getType() != concreteTypeAttr.getValue())
      object = sim::SimClassCastOp::create(
          builder, location, concreteTypeAttr.getValue(), object);
    auto concreteType =
        cast<sim::ClassHandleType>(concreteTypeAttr.getValue());
    for (const ObjectPathElement &element : path) {
      sim::SimClassDeclOp currentDeclaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, concreteType.getClassName());
      while (currentDeclaration &&
             !currentDeclaration->hasAttr("obelisk_sim.random_mode_field")) {
        if (!currentDeclaration.getBaseAttr())
          break;
        currentDeclaration =
            SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                function, currentDeclaration.getBaseAttr());
      }
      auto currentModeField =
          currentDeclaration
              ? currentDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                    "obelisk_sim.random_mode_field")
              : FlatSymbolRefAttr{};
      if (!currentModeField) {
        emitError(location)
            << "nested constraint path has no rand_mode field";
        return failure();
      }
      Type modeReferenceType = sim::ManagedRefType::get(
          function.getContext(), i64, concreteType.getClassName());
      Value modeReference = sim::SimClassFieldRefOp::create(
          builder, location, modeReferenceType, object, currentModeField);
      Value objectMode = sim::SimManagedLoadOp::create(
          builder, location, i64, modeReference);
      Value edgeModeBit = arith::AndIOp::create(
          builder, location, objectMode,
          constant64(uint64_t{1} << element.modeIndex));
      Value edgeDisabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, edgeModeBit,
          constant64(0));
      Type edgeReferenceType = sim::ManagedRefType::get(
          function.getContext(), element.storageType,
          concreteType.getClassName());
      Value edgeReference = sim::SimClassFieldRefOp::create(
          builder, location, edgeReferenceType, object, element.field);
      FailureOr<Value> loadedEdge = loadReference(edgeReference, location);
      if (failed(loadedEdge))
        return failure();
      Value edgeNull = sim::SimManagedIsNullOp::create(
          builder, location, builder.getI1Type(), *loadedEdge);
      Value edgeInactive = arith::OrIOp::create(
          builder, location, edgeDisabled, edgeNull);
      Block *nextObjectBlock = addBlock();
      cf::CondBranchOp::create(builder, location, edgeInactive, disabledBlock,
                               ValueRange{}, nextObjectBlock, ValueRange{});
      setCurrent(nextObjectBlock);
      object = *loadedEdge;
      if (object.getType() != element.concreteType)
        object = sim::SimClassCastOp::create(
            builder, location, element.concreteType, object);
      concreteType = cast<sim::ClassHandleType>(element.concreteType);
    }
    sim::SimClassDeclOp nestedDeclaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, concreteType.getClassName());
    while (nestedDeclaration &&
           !nestedDeclaration->hasAttr("obelisk_sim.constraint_mode_field")) {
      if (!nestedDeclaration.getBaseAttr())
        break;
      nestedDeclaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, nestedDeclaration.getBaseAttr());
    }
    auto nestedModeField =
        nestedDeclaration
            ? nestedDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                  "obelisk_sim.constraint_mode_field")
            : FlatSymbolRefAttr{};
    if (!nestedModeField) {
      emitError(location)
          << "nested rand object has no constraint_mode field";
      return failure();
    }
    Type nestedModeReferenceType = sim::ManagedRefType::get(
        function.getContext(), i64, concreteType.getClassName());
    Value nestedModeReference = sim::SimClassFieldRefOp::create(
        builder, location, nestedModeReferenceType, object, nestedModeField);
    Value nestedMode = sim::SimManagedLoadOp::create(
        builder, location, i64, nestedModeReference);
    Value mappedMode = arith::AndIOp::create(
        builder, location, constraintMode, constant64(~globalMask));
    for (auto [localIndex, globalIndex] :
         llvm::enumerate(globalIndices.asArrayRef())) {
      Value localBit = arith::AndIOp::create(
          builder, location, nestedMode, constant64(uint64_t{1} << localIndex));
      if (static_cast<uint64_t>(globalIndex) > localIndex)
        localBit = arith::ShLIOp::create(
            builder, location, localBit,
            constant64(static_cast<uint64_t>(globalIndex) - localIndex));
      else if (static_cast<uint64_t>(globalIndex) < localIndex)
        localBit = arith::ShRUIOp::create(
            builder, location, localBit,
            constant64(localIndex - static_cast<uint64_t>(globalIndex)));
      mappedMode =
          arith::OrIOp::create(builder, location, mappedMode, localBit);
    }
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    cf::BranchOp::create(builder, location, mergeBlock,
                         ValueRange{mappedMode, falseValue});
    setCurrent(mergeBlock);
    constraintMode = mergeBlock->getArgument(0);
    Value inactiveMask = arith::SelectOp::create(
        builder, location, mergeBlock->getArgument(1),
        constant64(globalMask), constant64(0));
    nullNestedConstraintMask = arith::OrIOp::create(
        builder, location, nullNestedConstraintMask, inactiveMask);
  }
  if (staticConstraintStorages) {
    Value context = function.getBody().front().getArgument(0);
    Type referenceType = sim::RefType::get(function.getContext(), i64);
    for (auto [index, storage] :
         llvm::enumerate(staticConstraintStorages.asArrayRef())) {
      if (storage < 0)
        continue;
      uint64_t bit = uint64_t{1} << index;
      constraintMode = arith::AndIOp::create(
          builder, location, constraintMode, constant64(~bit));
      Value reference = sim::SimContextStorageOp::create(
          builder, location, referenceType, context,
          builder.getI64IntegerAttr(storage));
      Value staticMode =
          sim::SimRefLoadOp::create(builder, location, i64, reference);
      Value disabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, staticMode,
          constant64(0));
      Value selected = arith::SelectOp::create(builder, location, disabled,
                                                constant64(bit), constant64(0));
      constraintMode =
          arith::OrIOp::create(builder, location, constraintMode, selected);
    }
  }
  // Static constraint_mode is class-wide, but a null random object member has
  // no variables or constraints in this randomize call. Apply null gating
  // after shared static modes so a globally enabled block cannot resurrect a
  // constraint belonging to an absent child.
  constraintMode = arith::OrIOp::create(
      builder, location, constraintMode, nullNestedConstraintMask);
  unsigned assignmentWidth = std::max<uint64_t>(64, totalWidth);
  auto assignmentType = IntegerType::get(function.getContext(), assignmentWidth);
  auto constantAssignment = [&](const APInt &value) -> Value {
    APInt resized = value.zextOrTrunc(assignmentWidth);
    return arith::ConstantOp::create(
        builder, location, assignmentType,
        builder.getIntegerAttr(assignmentType, resized));
  };
  auto constantAssignment64 = [&](uint64_t value) -> Value {
    return constantAssignment(APInt(64, value));
  };
  APInt domainMask = APInt::getLowBitsSet(assignmentWidth, totalWidth);
  Value mask = arith::ConstantOp::create(
      builder, location, assignmentType,
      builder.getIntegerAttr(assignmentType, domainMask));
  uint64_t propertyModeMask = 0;
  for (const Property &property : planned)
    propertyModeMask |= uint64_t{1} << property.modeIndex;
  for (const ContainerProperty &property : plannedContainers)
    if (!property.inertClassHandles)
      propertyModeMask |= uint64_t{1} << property.modeIndex;
  Value relevantMode;
  if (checkerOnly)
    relevantMode = constant64(propertyModeMask);
  else if (op->hasAttr(randomizeExplicitPropertiesAttrName))
    relevantMode = constant64(0);
  else
    relevantMode = arith::AndIOp::create(builder, location, mode,
                                         constant64(propertyModeMask));
  if (!checkerOnly && !op->hasAttr(randomizeExplicitPropertiesAttrName)) {
    Value context = function.getBody().front().getArgument(0);
    Type referenceType = sim::RefType::get(function.getContext(), i64);
    for (const Property &property : planned) {
      if (!property.randomModeStorage)
        continue;
      uint64_t bit = uint64_t{1} << property.modeIndex;
      relevantMode = arith::AndIOp::create(builder, location, relevantMode,
                                           constant64(~bit));
      Value reference = sim::SimContextStorageOp::create(
          builder, location, referenceType, context,
          builder.getI64IntegerAttr(*property.randomModeStorage));
      Value staticMode =
          sim::SimRefLoadOp::create(builder, location, i64, reference);
      Value disabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, staticMode,
          constant64(0));
      Value selected = arith::SelectOp::create(builder, location, disabled,
                                                constant64(bit), constant64(0));
      relevantMode =
          arith::OrIOp::create(builder, location, relevantMode, selected);
    }
  }
  Value randomizationEnabled = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, relevantMode, constant64(0));
  Value allPropertiesDisabled =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                            relevantMode, constant64(propertyModeMask));
  uint64_t constraintModeMask =
      constraintCount == 64 ? UINT64_MAX
                            : (uint64_t{1} << constraintCount) - 1;
  Value relevantConstraintMode = arith::AndIOp::create(
      builder, location, constraintMode, constant64(constraintModeMask));
  Value allConstraintsEnabled = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, relevantConstraintMode,
      constant64(0));

  auto next32 = [&](Value &streamState) -> Value {
    Value old = streamState;
    streamState = arith::AddIOp::create(
        builder, location,
        arith::MulIOp::create(builder, location, old,
                              constant64(UINT64_C(6364136223846793005))),
        increment);
    Value xored = arith::XOrIOp::create(
        builder, location,
        arith::ShRUIOp::create(builder, location, old, constant64(18)), old);
    Value shifted =
        arith::ShRUIOp::create(builder, location, xored, constant64(27));
    Type i32 = builder.getI32Type();
    Value bits = arith::TruncIOp::create(builder, location, i32, shifted);
    Value rotation = arith::TruncIOp::create(
        builder, location, i32,
        arith::ShRUIOp::create(builder, location, old, constant64(59)));
    Value zero32 = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(0));
    Value thirtyOne = arith::ConstantOp::create(builder, location, i32,
                                                builder.getI32IntegerAttr(31));
    Value leftAmount = arith::AndIOp::create(
        builder, location,
        arith::SubIOp::create(builder, location, zero32, rotation), thirtyOne);
    return arith::OrIOp::create(
        builder, location,
        arith::ShRUIOp::create(builder, location, bits, rotation),
        arith::ShLIOp::create(builder, location, bits, leftAmount));
  };
  auto next64 = [&](Value &streamState) -> Value {
    Value high = next32(streamState);
    Value low = next32(streamState);
    return arith::OrIOp::create(
        builder, location,
        arith::ShLIOp::create(
            builder, location,
            arith::ExtUIOp::create(builder, location, i64, high),
            constant64(32)),
        arith::ExtUIOp::create(builder, location, i64, low));
  };
  auto nextAssignment = [&](Value &streamState) -> Value {
    if (totalWidth <= 64)
      return next64(streamState);
    Value result = constantAssignment64(0);
    uint64_t propertyOffset = 0;
    for (const Property &property : planned) {
      for (uint64_t fieldOffset = 0; fieldOffset < property.width;) {
        unsigned chunkWidth = static_cast<unsigned>(
            std::min<uint64_t>(64, property.width - fieldOffset));
        Value chunk = next64(streamState);
        if (chunkWidth != 64)
          chunk = arith::AndIOp::create(
              builder, location, chunk,
              constant64(APInt::getLowBitsSet(64, chunkWidth).getZExtValue()));
        chunk = arith::ExtUIOp::create(builder, location, assignmentType, chunk);
        uint64_t offset = propertyOffset + fieldOffset;
        if (offset != 0)
          chunk = arith::ShLIOp::create(builder, location, chunk,
                                        constantAssignment64(offset));
        result = arith::OrIOp::create(builder, location, result, chunk);
        fieldOffset += chunkWidth;
      }
      propertyOffset += property.width;
    }
    return arith::AndIOp::create(builder, location, result, mask);
  };
  auto countBits = [](uint64_t bits) {
    unsigned count = 0;
    for (; bits != 0; bits &= bits - 1)
      ++count;
    return count;
  };
  auto propertyDomainMask = [&](const Property &property) {
    uint64_t result = 0;
    for (const PropertyDomain &domain : property.domains) {
      uint64_t mask =
          domain.width == 64 ? UINT64_MAX : (uint64_t{1} << domain.width) - 1;
      result |= mask << domain.offset;
    }
    return result;
  };
  auto propertyDomainCardinality = [&](const Property &property) {
    if (property.width > 64)
      return uint64_t{0};
    uint64_t valueMask =
        property.width == 64 ? UINT64_MAX : (uint64_t{1} << property.width) - 1;
    unsigned ordinaryBits =
        countBits(valueMask & ~propertyDomainMask(property));
    uint64_t cardinality = ordinaryBits == 64 ? 0 : uint64_t{1} << ordinaryBits;
    for (const PropertyDomain &domain : property.domains)
      cardinality *= domain.cardinality;
    return cardinality;
  };
  auto expandConstantBits = [](uint64_t index, uint64_t outputMask) {
    uint64_t result = 0;
    unsigned inputBit = 0;
    for (unsigned outputBit = 0; outputBit != 64; ++outputBit) {
      uint64_t output = uint64_t{1} << outputBit;
      if ((outputMask & output) == 0)
        continue;
      if ((index & (uint64_t{1} << inputBit)) != 0)
        result |= output;
      ++inputBit;
    }
    return result;
  };
  auto materializeConstantPropertyDomainIndex = [&](const Property &property,
                                                    uint64_t index) {
    uint64_t valueMask =
        property.width == 64 ? UINT64_MAX : (uint64_t{1} << property.width) - 1;
    uint64_t ordinaryMask = valueMask & ~propertyDomainMask(property);
    unsigned ordinaryBits = countBits(ordinaryMask);
    uint64_t field = expandConstantBits(index, ordinaryMask);
    uint64_t remaining = ordinaryBits == 0 ? index : index >> ordinaryBits;
    for (const PropertyDomain &domain : property.domains) {
      uint64_t groupIndex = remaining % domain.cardinality;
      remaining /= domain.cardinality;
      uint64_t base = 0;
      for (const DomainPattern &pattern : domain.patterns) {
        if (groupIndex - base < pattern.cardinality) {
          uint64_t selected =
              pattern.value |
              expandConstantBits(groupIndex - base, pattern.freeMask);
          field |= selected << domain.offset;
          break;
        }
        base += pattern.cardinality;
      }
    }
    return field;
  };
  auto expandStaticBits = [&](Value index, uint64_t outputMask) {
    Value result = constant64(0);
    unsigned inputBit = 0;
    for (unsigned outputBit = 0; outputBit != 64; ++outputBit) {
      if ((outputMask & (uint64_t{1} << outputBit)) == 0)
        continue;
      Value bit =
          arith::AndIOp::create(builder, location,
                                arith::ShRUIOp::create(builder, location, index,
                                                       constant64(inputBit)),
                                constant64(1));
      if (outputBit != 0)
        bit = arith::ShLIOp::create(builder, location, bit,
                                    constant64(outputBit));
      result = arith::OrIOp::create(builder, location, result, bit);
      ++inputBit;
    }
    return result;
  };
  auto materializePropertyDomainIndex = [&](const Property &property,
                                            Value index) {
    uint64_t valueMask =
        property.width == 64 ? UINT64_MAX : (uint64_t{1} << property.width) - 1;
    uint64_t ordinaryMask = valueMask & ~propertyDomainMask(property);
    unsigned ordinaryBits = countBits(ordinaryMask);
    Value field = expandStaticBits(index, ordinaryMask);
    Value remaining = index;
    if (ordinaryBits != 0)
      remaining = arith::ShRUIOp::create(builder, location, remaining,
                                         constant64(ordinaryBits));
    for (const PropertyDomain &domain : property.domains) {
      Value groupIndex = arith::RemUIOp::create(builder, location, remaining,
                                                constant64(domain.cardinality));
      remaining = arith::DivUIOp::create(builder, location, remaining,
                                         constant64(domain.cardinality));
      uint64_t lastBase = 0;
      for (const DomainPattern &pattern : llvm::drop_end(domain.patterns))
        lastBase += pattern.cardinality;
      const DomainPattern &last = domain.patterns.back();
      Value selected = arith::OrIOp::create(
          builder, location, constant64(last.value),
          expandStaticBits(arith::SubIOp::create(builder, location, groupIndex,
                                                 constant64(lastBase)),
                           last.freeMask));
      uint64_t end = lastBase;
      for (const DomainPattern &pattern :
           llvm::reverse(ArrayRef(domain.patterns).drop_back())) {
        end -= pattern.cardinality;
        Value candidate = arith::OrIOp::create(
            builder, location, constant64(pattern.value),
            expandStaticBits(arith::SubIOp::create(builder, location,
                                                   groupIndex, constant64(end)),
                             pattern.freeMask));
        Value use = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ult, groupIndex,
            constant64(end + pattern.cardinality));
        selected = arith::SelectOp::create(builder, location, use, candidate,
                                           selected);
      }
      if (domain.offset != 0)
        selected = arith::ShLIOp::create(builder, location, selected,
                                         constant64(domain.offset));
      field = arith::OrIOp::create(builder, location, field, selected);
    }
    return field;
  };
  Value currentAssignment = constantAssignment64(0);
  Value mutableMask = constantAssignment64(0);
  SmallVector<Value> propertyEnabled;
  SmallVector<uint32_t> propertyOffsets;
  uint64_t currentOffset = 0;
  for (Property &property : planned) {
    propertyOffsets.push_back(static_cast<uint32_t>(currentOffset));
    Value nestedEnabled;
    FailureOr<Value> current = [&]() -> FailureOr<Value> {
      if (property.nestedObjectReference) {
        FailureOr<Value> loadedObject =
            loadReference(property.nestedObjectReference, location);
        if (failed(loadedObject))
          return failure();
        Value isNull = sim::SimManagedIsNullOp::create(
            builder, location, builder.getI1Type(), *loadedObject);
        Block *nullBlock = addBlock();
        Block *objectBlock = addBlock();
        Block *mergeBlock = addBlock();
        mergeBlock->addArgument(property.type, location);
        mergeBlock->addArgument(builder.getI1Type(), location);
        mergeBlock->addArgument(i64, location);
        cf::CondBranchOp::create(builder, location, isNull, nullBlock,
                                 ValueRange{}, objectBlock, ValueRange{});

        setCurrent(nullBlock);
        cf::BranchOp::create(
            builder, location, mergeBlock,
            ValueRange{createDefaultValue(builder, location, property.type),
                       arith::ConstantOp::create(
                           builder, location, builder.getI1Type(),
                           builder.getBoolAttr(false)),
                       constant64(0)});

        setCurrent(objectBlock);
        Value object = *loadedObject;
        if (object.getType() != property.nestedObjectType)
          object = sim::SimClassCastOp::create(
              builder, location, property.nestedObjectType, object);
        auto concreteType = cast<sim::ClassHandleType>(property.nestedObjectType);
        FlatSymbolRefAttr objectModeField = property.nestedModeField;
        for (const ObjectPathElement &element : property.nestedObjectPath) {
          Type modeReferenceType = sim::ManagedRefType::get(
              function.getContext(), i64, concreteType.getClassName());
          Value modeReference = sim::SimClassFieldRefOp::create(
              builder, location, modeReferenceType, object, objectModeField);
          Value objectMode = sim::SimManagedLoadOp::create(
              builder, location, i64, modeReference);
          Value edgeModeBit = arith::AndIOp::create(
              builder, location, objectMode,
              constant64(uint64_t{1} << element.modeIndex));
          Value edgeEnabled = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, edgeModeBit,
              constant64(0));
          Type edgeReferenceType = sim::ManagedRefType::get(
              function.getContext(), element.storageType,
              concreteType.getClassName());
          Value edgeReference = sim::SimClassFieldRefOp::create(
              builder, location, edgeReferenceType, object, element.field);
          FailureOr<Value> loadedEdge = loadReference(edgeReference, location);
          if (failed(loadedEdge))
            return failure();
          Value edgeNull = sim::SimManagedIsNullOp::create(
              builder, location, builder.getI1Type(), *loadedEdge);
          Value edgeNonNull = arith::XOrIOp::create(
              builder, location, edgeNull,
              arith::ConstantOp::create(builder, location,
                                        builder.getI1Type(),
                                        builder.getBoolAttr(true)));
          Value edgeActive = arith::AndIOp::create(
              builder, location, edgeEnabled, edgeNonNull);
          Block *edgeBlock = addBlock();
          cf::CondBranchOp::create(builder, location, edgeActive, edgeBlock,
                                   ValueRange{}, nullBlock, ValueRange{});
          setCurrent(edgeBlock);
          object = *loadedEdge;
          if (object.getType() != element.concreteType)
            object = sim::SimClassCastOp::create(
                builder, location, element.concreteType, object);
          concreteType = cast<sim::ClassHandleType>(element.concreteType);
          objectModeField = element.modeField;
        }
        Type nestedFieldType = property.isContainerSize
                                   ? property.containerType
                                   : property.type;
        Type fieldReferenceType = sim::ManagedRefType::get(
            function.getContext(), nestedFieldType,
            concreteType.getClassName());
        Value fieldReference = sim::SimClassFieldRefOp::create(
            builder, location, fieldReferenceType, object,
            property.nestedField);
        FailureOr<Value> value = loadReference(fieldReference, location);
        Type modeReferenceType = sim::ManagedRefType::get(
            function.getContext(), i64, concreteType.getClassName());
        Value childModeReference = sim::SimClassFieldRefOp::create(
            builder, location, modeReferenceType, object,
            objectModeField);
        Value childMode = sim::SimManagedLoadOp::create(
            builder, location, i64, childModeReference);
        Value childModeBit = arith::AndIOp::create(
            builder, location, childMode,
            constant64(uint64_t{1} << property.nestedModeIndex));
        Value childEnabled = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, childModeBit,
            constant64(0));
        if (failed(value))
          return failure();
        if (property.isContainerSize) {
          Value size = sim::SimContainerSizeOp::create(builder, location, i64,
                                                       *value);
          value = arith::TruncIOp::create(builder, location, property.type,
                                          size)
                      .getResult();
        }
        cf::BranchOp::create(builder, location, mergeBlock,
                             ValueRange{*value, childEnabled,
                                        sim::SimClassIdOp::create(
                                            builder, location, object)});
        setCurrent(mergeBlock);
        nestedEnabled = mergeBlock->getArgument(1);
        property.nestedObjectID = mergeBlock->getArgument(2);
        return mergeBlock->getArgument(0);
      }
      if (!property.isContainerSize)
        return loadReference(property.reference, location);
      FailureOr<Value> container = loadReference(property.reference, location);
      if (failed(container))
        return failure();
      Value size = sim::SimContainerSizeOp::create(builder, location, i64,
                                                   *container);
      return arith::TruncIOp::create(builder, location, property.type, size)
          .getResult();
    }();
    if (failed(current))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*current, location);
    FailureOr<Value> extended =
        succeeded(scalar)
            ? convert(*scalar, assignmentType, property.isSigned, location,
                      false)
            : FailureOr<Value>(failure());
    if (failed(extended))
      return failure();
    APInt valueMask =
        APInt::getLowBitsSet(assignmentWidth, property.width);
    Value bits = arith::AndIOp::create(builder, location, *extended,
                                       constantAssignment(valueMask));

    Value propertyMode = arith::AndIOp::create(
        builder, location, relevantMode,
        constant64(uint64_t{1} << property.modeIndex));
    Value enabled =
        checkerOnly
            ? arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false))
                  .getResult()
            : arith::CmpIOp::create(builder, location,
                                    arith::CmpIPredicate::eq, propertyMode,
                                    constant64(0))
                  .getResult();
    if (nestedEnabled)
      enabled = arith::AndIOp::create(builder, location, enabled,
                                      nestedEnabled);
    if (property.isContainerSize &&
        !property.hasUnconditionalSizeConstraint) {
      Value disabledSizeConstraints = arith::AndIOp::create(
          builder, location, relevantConstraintMode,
          constant64(property.sizeConstraintMask));
      Value allSizeConstraintsDisabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq,
          disabledSizeConstraints, constant64(property.sizeConstraintMask));
      enabled = arith::AndIOp::create(
          builder, location, enabled,
          arith::XOrIOp::create(
              builder, location, allSizeConstraintsDisabled,
              arith::ConstantOp::create(builder, location,
                                        builder.getI1Type(),
                                        builder.getBoolAttr(true))));
    }
    propertyEnabled.push_back(enabled);
    if (property.isRandC) {
      Block *enabledBlock = addBlock();
      Block *disabledBlock = addBlock();
      Block *mergeBlock = addBlock();
      Value mergedState = mergeBlock->addArgument(i64, location);
      Value mergedBits = mergeBlock->addArgument(assignmentType, location);
      Value mergedKey = mergeBlock->addArgument(i64, location);
      Value mergedPosition = mergeBlock->addArgument(i64, location);
      cf::CondBranchOp::create(builder, location, enabled, enabledBlock,
                               ValueRange{}, disabledBlock, ValueRange{});

      setCurrent(disabledBlock);
      cf::BranchOp::create(
          builder, location, mergeBlock,
          ValueRange{state, bits, constant64(0), constant64(0)});

      setCurrent(enabledBlock);
      FailureOr<Value> key =
          loadReference(property.randcKeyReference, location);
      FailureOr<Value> position =
          loadReference(property.randcPositionReference, location);
      if (failed(key) || failed(position))
        return failure();
      uint64_t semanticCardinality = propertyDomainCardinality(property);
      if (semanticCardinality == 0 ||
          semanticCardinality > (uint64_t{1} << 32)) {
        emitError(location)
            << "randc semantic domains must contain at most 2^32 values";
        return failure();
      }
      if (semanticCardinality == 1) {
        Value value =
            property.domains.empty()
                ? constant64(0)
                : materializePropertyDomainIndex(property, constant64(0));
        if (assignmentType != i64)
          value = arith::ExtUIOp::create(builder, location, assignmentType,
                                         value);
        cf::BranchOp::create(builder, location, mergeBlock,
                             ValueRange{state, value, *key, *position});
      } else {
        unsigned cycleWidth = llvm::Log2_64_Ceil(semanticCardinality);
        Block *cycleEntry = addBlock();
        Value cycleState = cycleEntry->addArgument(i64, location);
        Value cycleKey = cycleEntry->addArgument(i64, location);
        Value cyclePosition = cycleEntry->addArgument(i64, location);
        Block *rekeyBlock = addBlock();
        Block *cycleBlock = addBlock();
        Value activeState = cycleBlock->addArgument(i64, location);
        Value activeKey = cycleBlock->addArgument(i64, location);
        Value activePosition = cycleBlock->addArgument(i64, location);
        cf::BranchOp::create(builder, location, cycleEntry,
                             ValueRange{state, *key, *position});

        setCurrent(cycleEntry);
        Value needsRekey =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  cyclePosition, constant64(0));
        cf::CondBranchOp::create(
            builder, location, needsRekey, rekeyBlock,
            ValueRange{cycleState, cycleKey, cyclePosition}, cycleBlock,
            ValueRange{cycleState, cycleKey, cyclePosition});

        Value rekeyState = rekeyBlock->addArgument(i64, location);
        rekeyBlock->addArgument(i64, location);
        Value rekeyPosition = rekeyBlock->addArgument(i64, location);
        setCurrent(rekeyBlock);
        Value newKey = next64(rekeyState);
        cf::BranchOp::create(builder, location, cycleBlock,
                             ValueRange{rekeyState, newKey, rekeyPosition});

        setCurrent(cycleBlock);
        auto cycle = sim::SimRandomCycleNextOp::create(
            builder, location, activeKey, activePosition,
            builder.getI32IntegerAttr(cycleWidth));
        Value valid = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ult, cycle.getValue(),
            constant64(semanticCardinality));
        Value semanticValue =
            property.domains.empty()
                ? cycle.getValue()
                : materializePropertyDomainIndex(property, cycle.getValue());
        if (assignmentType != i64)
          semanticValue = arith::ExtUIOp::create(
              builder, location, assignmentType, semanticValue);
        cf::CondBranchOp::create(
            builder, location, valid, mergeBlock,
            ValueRange{activeState, semanticValue, activeKey,
                       cycle.getNextPosition()},
            cycleEntry,
            ValueRange{activeState, activeKey, cycle.getNextPosition()});
      }

      setCurrent(mergeBlock);
      state = mergedState;
      bits = mergedBits;
      property.nextRandcKey = mergedKey;
      property.nextRandcPosition = mergedPosition;
    }
    APInt aggregateMask = valueMask;
    if (currentOffset != 0) {
      bits = arith::ShLIOp::create(builder, location, bits,
                                   constantAssignment64(currentOffset));
      aggregateMask = aggregateMask.shl(currentOffset);
    }
    currentAssignment =
        arith::OrIOp::create(builder, location, currentAssignment, bits);

    Value enabledMask =
        property.isRandC
            ? constantAssignment64(0)
            : arith::SelectOp::create(builder, location, enabled,
                                      constantAssignment(aggregateMask),
                                      constantAssignment64(0))
                  .getResult();
    mutableMask =
        arith::OrIOp::create(builder, location, mutableMask, enabledMask);
    currentOffset += property.width;
  }
  struct AliasEquality {
    uint32_t leftOffset;
    uint32_t rightOffset;
    unsigned width;
    Value active;
  };
  SmallVector<AliasEquality> aliasEqualities;
  auto sameObjectOccurrence = [](const Property &left,
                                 const Property &right) {
    if (left.nestedObjectRootField != right.nestedObjectRootField ||
        left.nestedObjectPath.size() != right.nestedObjectPath.size())
      return false;
    return llvm::equal(
        left.nestedObjectPath, right.nestedObjectPath,
        [](const ObjectPathElement &leftElement,
           const ObjectPathElement &rightElement) {
          return leftElement.field == rightElement.field;
        });
  };
  auto objectOwnerType = [](const Property &property) {
    return property.nestedObjectPath.empty()
               ? property.nestedObjectType
               : property.nestedObjectPath.back().concreteType;
  };
  for (auto [leftIndex, left] : llvm::enumerate(planned)) {
    if (!left.nestedObjectReference)
      continue;
    for (auto [relativeRightIndex, right] :
         llvm::enumerate(ArrayRef(planned).drop_front(leftIndex + 1))) {
      unsigned rightIndex = leftIndex + 1 + relativeRightIndex;
      if (!right.nestedObjectReference ||
          left.nestedField != right.nestedField ||
          objectOwnerType(left) != objectOwnerType(right) ||
          sameObjectOccurrence(left, right))
        continue;
      if (left.type != right.type || left.width != right.width) {
        emitError(location)
            << "aliased random-object field plans have inconsistent types";
        return failure();
      }
      Value bothEnabled = arith::AndIOp::create(
          builder, location, propertyEnabled[leftIndex],
          propertyEnabled[rightIndex]);
      Value sameID = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, left.nestedObjectID,
          right.nestedObjectID);
      aliasEqualities.push_back(
          {propertyOffsets[leftIndex], propertyOffsets[rightIndex], left.width,
           arith::AndIOp::create(builder, location, bothEnabled, sameID)});
    }
  }
  Value fixedAssignment = arith::AndIOp::create(
      builder, location, currentAssignment,
      arith::XOrIOp::create(builder, location, mutableMask, mask));

  bool hasSoftConstraint = false;
  bool hasRuntimeForeachConstraint = false;
  for (auto [index, child] : llvm::enumerate(children)) {
    if (index == receiverIndex)
      continue;
    child->walk([&](semantic::SVExpressionConstraintOp expression) {
      hasSoftConstraint |= expression.getIsSoft();
    });
    child->walk([&](semantic::SVForeachConstraintOp) {
      hasRuntimeForeachConstraint = true;
    });
  }

  struct SolveBeforeEdge {
    uint64_t beforeMask;
    uint64_t afterMask;
    uint32_t constraintBlock;

    bool operator==(const SolveBeforeEdge &other) const {
      return beforeMask == other.beforeMask &&
             afterMask == other.afterMask &&
             constraintBlock == other.constraintBlock;
    }
  };
  SmallVector<uint64_t> propertyMasks;
  uint64_t solvePropertyOffset = 0;
  for (const Property &property : planned) {
    if (totalWidth <= 64) {
      uint64_t valueMask = property.width == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << property.width) - 1;
      propertyMasks.push_back(valueMask << solvePropertyOffset);
    } else {
      propertyMasks.push_back(0);
    }
    solvePropertyOffset += property.width;
  }
  SmallVector<SolveBeforeEdge> solveBeforeEdges;
  SmallVector<uint64_t> solveBeforeLayerMasks;
  uint64_t randomAssignmentMask =
      totalWidth == 64 ? UINT64_MAX
                       : totalWidth < 64 ? (uint64_t{1} << totalWidth) - 1 : 0;
  bool hasSolveBefore = false;
  auto randomPropertyMask = [&](Operation *expression) -> FailureOr<uint64_t> {
    if (totalWidth > 64) {
      emitError(getSemanticLocation(expression))
          << "solve before on a wide randomization plan requires plan "
             "decomposition";
      return failure();
    }
    auto indexAttr =
        expression->getAttrOfType<IntegerAttr>(randomVariableAttrName);
    if (!indexAttr || indexAttr.getValue().isNegative() ||
        indexAttr.getValue().getActiveBits() > 64 ||
        indexAttr.getValue().getZExtValue() >= planned.size()) {
      emitError(getSemanticLocation(expression))
          << "solve before currently requires direct rand properties";
      return failure();
    }
    return propertyMasks[indexAttr.getValue().getZExtValue()];
  };
  for (auto [index, root] : llvm::enumerate(children)) {
    if (index == receiverIndex)
      continue;
    uint32_t solveConstraintBlock = OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
    if (auto block =
            root->getAttrOfType<IntegerAttr>(randomConstraintBlockAttrName)) {
      APInt value = block.getValue();
      if (value.isNegative() || value.getActiveBits() > 64 ||
          value.getZExtValue() >= constraintCount) {
        emitError(getSemanticLocation(root))
            << "random constraint block index is malformed";
        return failure();
      }
      solveConstraintBlock = static_cast<uint32_t>(value.getZExtValue());
    }
    if (auto order =
            root->getAttrOfType<DenseI64ArrayAttr>(randomFunctionOrderAttrName)) {
      ArrayRef<int64_t> pairs = order.asArrayRef();
      if (pairs.size() % 2 != 0) {
        emitError(getSemanticLocation(root))
            << "constraint function ordering metadata is malformed";
        return failure();
      }
      for (size_t pair = 0; pair != pairs.size(); pair += 2) {
        uint64_t beforeMask = static_cast<uint64_t>(pairs[pair]);
        uint64_t afterMask = static_cast<uint64_t>(pairs[pair + 1]);
        if (beforeMask == 0 || afterMask == 0 ||
            (beforeMask & ~randomAssignmentMask) != 0 ||
            (afterMask & ~randomAssignmentMask) != 0 ||
            (beforeMask & afterMask) != 0) {
          emitError(getSemanticLocation(root))
              << "constraint function ordering metadata is out of range";
          return failure();
        }
        hasSolveBefore = true;
        SolveBeforeEdge edge{beforeMask, afterMask, solveConstraintBlock};
        if (!llvm::is_contained(solveBeforeEdges, edge))
          solveBeforeEdges.push_back(edge);
      }
    }
    WalkResult result =
        root->walk([&](semantic::SVSolveBeforeConstraintOp solve) {
          hasSolveBefore = true;
          auto solveCountAttr =
              solve->getAttrOfType<IntegerAttr>("solve_count");
          auto afterCountAttr =
              solve->getAttrOfType<IntegerAttr>("after_count");
          SmallVector<Operation *> operands = getChildren(solve);
          if (!solveCountAttr || !afterCountAttr ||
              solveCountAttr.getValue().isNegative() ||
              afterCountAttr.getValue().isNegative() ||
              solveCountAttr.getValue().getActiveBits() > 64 ||
              afterCountAttr.getValue().getActiveBits() > 64) {
            emitError(getSemanticLocation(solve))
                << "solve before has malformed operand counts";
            return WalkResult::interrupt();
          }
          uint64_t solveCount = solveCountAttr.getValue().getZExtValue();
          uint64_t afterCount = afterCountAttr.getValue().getZExtValue();
          if (solveCount == 0 || afterCount == 0 ||
              solveCount > operands.size() ||
              afterCount != operands.size() - solveCount) {
            emitError(getSemanticLocation(solve))
                << "solve before has inconsistent operand counts";
            return WalkResult::interrupt();
          }
          SmallVector<uint64_t> before;
          SmallVector<uint64_t> after;
          for (auto [operandIndex, operand] : llvm::enumerate(operands)) {
            FailureOr<uint64_t> property = randomPropertyMask(operand);
            if (failed(property))
              return WalkResult::interrupt();
            (operandIndex < solveCount ? before : after).push_back(*property);
          }
          for (uint64_t lhs : before)
            for (uint64_t rhs : after) {
              if ((lhs & rhs) != 0) {
                emitError(getSemanticLocation(solve))
                    << "solve before cannot order a property before itself";
                return WalkResult::interrupt();
              }
              SolveBeforeEdge edge{lhs, rhs, solveConstraintBlock};
              if (!llvm::is_contained(solveBeforeEdges, edge))
                solveBeforeEdges.push_back(edge);
            }
          return WalkResult::advance();
        });
    if (result.wasInterrupted())
      return failure();
  }
  if (hasSolveBefore) {
    // Slang's ValuePath graph treats overlapping paths as the same node. Keep
    // each transitive overlap class together so, for example, an edge to one
    // field of a packed slice cannot cause the other bits of that slice to be
    // sampled in an earlier layer.
    SmallVector<uint64_t> nodes;
    auto addNode = [&](uint64_t node) {
      for (size_t index = 0; index != nodes.size();) {
        if ((nodes[index] & node) == 0) {
          ++index;
          continue;
        }
        node |= nodes[index];
        nodes.erase(nodes.begin() + index);
      }
      nodes.push_back(node);
    };
    for (const SolveBeforeEdge &edge : solveBeforeEdges) {
      addNode(edge.beforeMask);
      addNode(edge.afterMask);
    }
    while (!nodes.empty()) {
      uint64_t layer = 0;
      uint64_t remaining = 0;
      for (uint64_t node : nodes)
        remaining |= node;
      for (uint64_t node : nodes) {
        bool hasPredecessor =
            llvm::any_of(solveBeforeEdges, [&](const auto &edge) {
              return (edge.afterMask & node) != 0 &&
                     (edge.beforeMask & remaining) != 0;
            });
        if (!hasPredecessor)
          layer |= node;
      }
      if (layer == 0) {
        emitError(location) << "solve before ordering contains a cycle";
        return failure();
      }
      solveBeforeLayerMasks.push_back(layer);
      llvm::erase_if(nodes,
                     [&](uint64_t node) { return (node & layer) != 0; });
    }
  }
  if (hasRuntimeForeachConstraint && hasSolveBefore) {
    emitError(location)
        << "foreach constraints combined with solve before require dynamic "
           "ordered residual solving";
    return failure();
  }

  struct EncodedInstruction {
    uint8_t opcode;
    uint32_t width;
    uint8_t flags = 0;
    uint32_t operand = 0;
    uint64_t immediate = 0;
    std::optional<APInt> literal;
  };
  SmallVector<EncodedInstruction> programInstructions;
  SmallVector<Value> programCaptures;
  auto instruction = [&](uint8_t opcode, unsigned width, bool isSigned = false,
                         uint32_t operand = 0, uint64_t immediate = 0) {
    programInstructions.push_back(
        {opcode, static_cast<uint32_t>(width),
         static_cast<uint8_t>(isSigned ? OBELISK_RT_RANDOM_INSTRUCTION_SIGNED
                                       : 0),
         operand, immediate, std::nullopt});
  };
  auto literalInstruction = [&](unsigned width, bool isSigned,
                                const APInt &value) {
    programInstructions.push_back(
        {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, static_cast<uint32_t>(width),
         static_cast<uint8_t>(isSigned ? OBELISK_RT_RANDOM_INSTRUCTION_SIGNED
                                       : 0),
         0, 0, value.zextOrTrunc(width)});
  };
  auto expressionWidth = [&](Operation *expression) -> FailureOr<unsigned> {
    FailureOr<Type> type = getNormalizedSemanticType(expression);
    std::optional<unsigned> width =
        succeeded(type) ? sim::getPackedWidth(*type) : std::nullopt;
    if (!width || *width == 0) {
      emitError(getSemanticLocation(expression))
          << "runtime random constraint values must be packed";
      return failure();
    }
    return *width;
  };
  auto dependsOnCandidate = [&](Operation *expression) {
    bool dependent = false;
    expression->walk([&](Operation *nested) {
      dependent |= nested->hasAttr(randomVariableAttrName);
    });
    return dependent;
  };
  struct DistRangePlan {
    uint64_t lower;
    uint64_t cardinality;
    uint64_t coefficient;
    uint64_t selectionCoefficient;
    uint32_t weightCapture;
    bool weightSigned;
    Value weight;
  };
  struct DistPlan {
    Operation *source;
    unsigned propertyIndex;
    uint32_t propertyOffset;
    uint32_t constraintBlock;
    SmallVector<DistRangePlan> ranges;
  };
  SmallVector<DistPlan> distPlans;
  uint32_t activeProgramConstraintBlock =
      OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
  auto emitLiteral = [&](bool value) {
    instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, false, 0, value);
  };
  std::function<LogicalResult(Operation *)> emitProgramExpression;
  emitProgramExpression = [&](Operation *expression) -> LogicalResult {
    if (auto dist = dyn_cast<semantic::SVDistExpressionOp>(expression)) {
      if (llvm::is_contained(llvm::map_range(
                                 distPlans,
                                 [](const DistPlan &plan) { return plan.source; }),
                             expression)) {
        emitError(getSemanticLocation(expression))
            << "distribution expression was encoded more than once";
        return failure();
      }
      SmallVector<Operation *> nested = getChildren(expression);
      auto itemCountAttr =
          expression->getAttrOfType<IntegerAttr>("item_count");
      auto itemHasWeight =
          expression->getAttrOfType<DenseI64ArrayAttr>("item_has_weight");
      auto itemWeightKinds =
          expression->getAttrOfType<DenseI64ArrayAttr>("item_weight_kinds");
      auto hasDefaultWeight =
          expression->getAttrOfType<BoolAttr>("has_default_weight");
      auto defaultWeightKind =
          expression->getAttrOfType<IntegerAttr>("default_weight_kind");
      if (!itemCountAttr || itemCountAttr.getValue().isNegative() ||
          itemCountAttr.getValue().getActiveBits() > 64 || !itemHasWeight ||
          !itemWeightKinds || !hasDefaultWeight || !defaultWeightKind) {
        emitError(getSemanticLocation(expression))
            << "distribution expression has malformed metadata";
        return failure();
      }
      uint64_t itemCount = itemCountAttr.getValue().getZExtValue();
      if (itemCount == 0 || itemCount > static_cast<uint64_t>(INT64_MAX) ||
          itemHasWeight.size() != static_cast<int64_t>(itemCount) ||
          itemWeightKinds.size() != static_cast<int64_t>(itemCount) ||
          nested.empty() ||
          (defaultWeightKind.getInt() != 0 &&
           defaultWeightKind.getInt() != 1)) {
        emitError(getSemanticLocation(expression))
            << "distribution expression has inconsistent metadata";
        return failure();
      }

      Operation *target = nested.front();
      while (isa<semantic::SVConversionExpressionOp>(target)) {
        SmallVector<Operation *> converted = getChildren(target);
        if (converted.size() != 1)
          break;
        target = converted.front();
      }
      auto variable = target->getAttrOfType<IntegerAttr>(randomVariableAttrName);
      if (!variable || variable.getValue().isNegative() ||
          variable.getValue().getActiveBits() > 64 ||
          variable.getValue().getZExtValue() >= planned.size()) {
        emitError(getSemanticLocation(expression))
            << "dist currently requires a direct rand property on its left "
               "hand side";
        return failure();
      }
      unsigned propertyIndex =
          static_cast<unsigned>(variable.getValue().getZExtValue());
      const Property &property = planned[propertyIndex];
      FailureOr<unsigned> comparisonWidth = expressionWidth(nested.front());
      if (property.width > 64) {
        emitError(getSemanticLocation(expression))
            << "dist on a random property wider than 64 bits is not "
               "executable yet";
        return failure();
      }
      if (property.isRandC) {
        emitError(getSemanticLocation(expression))
            << "dist cannot weight a randc property";
        return failure();
      }
      if (failed(comparisonWidth) || *comparisonWidth < property.width ||
          isSignedNode(nested.front()) != property.isSigned) {
        emitError(getSemanticLocation(expression))
            << "dist requires a widening conversion that preserves the rand "
               "property signedness";
        return failure();
      }

      struct RawDistItem {
        Operation *value;
        Operation *weight;
        bool perRange;
      };
      SmallVector<RawDistItem> rawItems;
      size_t childIndex = 1;
      for (uint64_t index = 0; index != itemCount; ++index) {
        if (childIndex >= nested.size() ||
            (itemHasWeight[index] != 0 && itemHasWeight[index] != 1) ||
            (itemWeightKinds[index] != 0 && itemWeightKinds[index] != 1)) {
          emitError(getSemanticLocation(expression))
              << "distribution item metadata is malformed";
          return failure();
        }
        Operation *value = nested[childIndex++];
        Operation *weight = nullptr;
        if (itemHasWeight[index] != 0) {
          if (childIndex >= nested.size())
            return failure();
          weight = nested[childIndex++];
        }
        rawItems.push_back(
            {value, weight, itemWeightKinds[index] != 0});
      }
      Operation *defaultWeight = nullptr;
      if (hasDefaultWeight.getValue()) {
        if (childIndex >= nested.size()) {
          emitError(getSemanticLocation(expression))
              << "distribution default weight is missing";
          return failure();
        }
        defaultWeight = nested[childIndex++];
      }
      if (childIndex != nested.size()) {
        emitError(getSemanticLocation(expression))
            << "distribution expression has an unexpected child inventory";
        return failure();
      }

      std::function<FailureOr<ParsedConstant>(Operation *)> constantValue;
      constantValue = [&](Operation *constant) -> FailureOr<ParsedConstant> {
        if (isa<semantic::SVConversionExpressionOp>(constant)) {
          SmallVector<Operation *> converted = getChildren(constant);
          if (converted.size() != 1)
            return failure();
          return constantValue(converted.front());
        }
        if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(constant)) {
          SmallVector<Operation *> operand = getChildren(unary);
          if (operand.size() != 1)
            return failure();
          FailureOr<ParsedConstant> value = constantValue(operand.front());
          if (failed(value))
            return failure();
          switch (unary.getOperatorKind()) {
          case semantic::SVUnaryOperator::Plus:
            return *value;
          case semantic::SVUnaryOperator::Minus:
            value->value = -value->value;
            return *value;
          case semantic::SVUnaryOperator::BitwiseNot:
            value->value = ~value->value;
            return *value;
          default:
            return failure();
          }
        }
        std::optional<StringRef> spelling = getConstantSpelling(constant);
        if (!spelling)
          return failure();
        return parseSVInteger(*spelling, *comparisonWidth,
                              getSemanticLocation(constant));
      };
      auto constantBits = [&](Operation *value) -> FailureOr<uint64_t> {
        FailureOr<ParsedConstant> parsed = constantValue(value);
        if (failed(parsed)) {
          emitError(getSemanticLocation(value))
              << "dist range endpoints must be compile-time integral "
                 "constants";
          return failure();
        }
        if (!parsed->unknown.isZero()) {
          emitError(getSemanticLocation(value))
              << "dist range endpoints must be two-state constants";
          return failure();
        }
        APInt bits = parsed->value;
        if (property.isSigned) {
          APInt minimum =
              APInt::getSignedMinValue(property.width).sext(*comparisonWidth);
          APInt maximum =
              APInt::getSignedMaxValue(property.width).sext(*comparisonWidth);
          if (bits.slt(minimum) || bits.sgt(maximum)) {
            emitError(getSemanticLocation(value))
                << "dist endpoint is outside the rand property domain";
            return failure();
          }
        } else {
          APInt maximum = APInt::getLowBitsSet(*comparisonWidth, property.width);
          if (bits.ugt(maximum)) {
            emitError(getSemanticLocation(value))
                << "dist endpoint is outside the rand property domain";
            return failure();
          }
        }
        uint64_t propertyBits = bits.trunc(property.width).getZExtValue();
        if (property.isSigned)
          propertyBits ^= uint64_t{1} << (property.width - 1);
        return propertyBits;
      };

      struct PendingRange {
        uint64_t lower;
        uint64_t cardinality;
        Operation *weight;
        bool perRange;
        uint64_t denominator;
        uint64_t selectionCardinality;
        bool isDefault;
      };
      SmallVector<PendingRange> pending;
      SmallVector<std::pair<uint64_t, uint64_t>> explicitIntervals;
      auto appendItemRange = [&](const RawDistItem &item) -> LogicalResult {
        uint64_t lower = 0;
        uint64_t upper = 0;
        if (auto range =
                dyn_cast<semantic::SVValueRangeExpressionOp>(item.value)) {
          SmallVector<Operation *> endpoints = getChildren(range);
          if (endpoints.size() != 2) {
            emitError(getSemanticLocation(item.value))
                << "dist range has malformed endpoints";
            return failure();
          }
          FailureOr<uint64_t> first = constantBits(endpoints[0]);
          FailureOr<uint64_t> second = constantBits(endpoints[1]);
          if (failed(first) || failed(second))
            return failure();
          lower = std::min(*first, *second);
          upper = std::max(*first, *second);
        } else {
          FailureOr<uint64_t> singleton = constantBits(item.value);
          if (failed(singleton))
            return failure();
          lower = upper = *singleton;
        }
        uint64_t cardinality = upper - lower + 1;
        if (cardinality == 0 && item.perRange && property.domains.empty()) {
          emitError(getSemanticLocation(item.value))
              << "a per-range dist weight cannot span the complete 64-bit "
                 "domain";
          return failure();
        }
        pending.push_back({lower, cardinality, item.weight, item.perRange,
                           cardinality, cardinality, false});
        explicitIntervals.emplace_back(lower, upper);
        return success();
      };
      for (const RawDistItem &item : rawItems)
        if (failed(appendItemRange(item)))
          return failure();

      if (defaultWeight) {
        llvm::sort(explicitIntervals);
        SmallVector<std::pair<uint64_t, uint64_t>> merged;
        for (auto interval : explicitIntervals) {
          if (merged.empty() ||
              (merged.back().second != UINT64_MAX &&
               interval.first > merged.back().second + 1)) {
            merged.push_back(interval);
          } else {
            merged.back().second =
                std::max(merged.back().second, interval.second);
          }
        }
        uint64_t domainMaximum = property.width == 64
                                     ? UINT64_MAX
                                     : (uint64_t{1} << property.width) - 1;
        SmallVector<std::pair<uint64_t, uint64_t>> complement;
        uint64_t next = 0;
        bool exhausted = false;
        for (auto interval : merged) {
          if (next < interval.first)
            complement.emplace_back(next, interval.first - 1);
          if (interval.second == UINT64_MAX) {
            exhausted = true;
            break;
          }
          next = interval.second + 1;
        }
        if (!exhausted && next <= domainMaximum)
          complement.emplace_back(next, domainMaximum);
        uint64_t defaultCardinality = 0;
        for (auto interval : complement) {
          uint64_t cardinality = interval.second - interval.first + 1;
          uint64_t updated = defaultCardinality + cardinality;
          if (updated < defaultCardinality)
            defaultCardinality = 0;
          else
            defaultCardinality = updated;
        }
        bool defaultPerRange = defaultWeightKind.getInt() != 0;
        if (defaultPerRange && property.domains.empty() &&
            !complement.empty() && defaultCardinality == 0) {
          emitError(getSemanticLocation(defaultWeight))
              << "a per-range default dist weight cannot cover the complete "
                 "64-bit domain";
          return failure();
        }
        for (auto interval : complement)
          pending.push_back({interval.first,
                             interval.second - interval.first + 1,
                             defaultWeight, defaultPerRange, defaultCardinality,
                             interval.second - interval.first + 1, true});
      }

      if (!property.domains.empty()) {
        constexpr uint64_t maxFiniteDistDomain = uint64_t{1} << 20;
        uint64_t semanticCardinality = propertyDomainCardinality(property);
        if (semanticCardinality == 0 ||
            semanticCardinality > maxFiniteDistDomain) {
          emitError(getSemanticLocation(expression))
              << "dist over a finite semantic domain currently requires at "
                 "most 2^20 property values";
          return failure();
        }
        uint64_t defaultCardinality = 0;
        for (PendingRange &range : pending) {
          range.selectionCardinality = 0;
          for (uint64_t index = 0; index != semanticCardinality; ++index) {
            uint64_t candidate =
                materializeConstantPropertyDomainIndex(property, index);
            if (property.isSigned)
              candidate ^= uint64_t{1} << (property.width - 1);
            bool matches = range.cardinality == 0 ||
                           (candidate >= range.lower &&
                            candidate - range.lower < range.cardinality);
            if (matches)
              ++range.selectionCardinality;
          }
          if (range.isDefault)
            defaultCardinality += range.selectionCardinality;
        }
        for (PendingRange &range : pending) {
          if (!range.perRange)
            continue;
          range.denominator =
              range.isDefault ? defaultCardinality : range.selectionCardinality;
        }
        llvm::erase_if(pending, [](const PendingRange &range) {
          return range.selectionCardinality == 0;
        });
      }

      uint64_t normalization = 1;
      for (const PendingRange &range : pending) {
        if (!range.perRange)
          continue;
        uint64_t divisor = std::gcd(normalization, range.denominator);
        uint64_t factor = range.denominator / divisor;
        if (normalization > UINT64_MAX / factor) {
          emitError(getSemanticLocation(expression))
              << "dist per-range normalization exceeds 64 bits";
          return failure();
        }
        normalization *= factor;
      }

      llvm::DenseMap<Operation *, std::pair<uint32_t, Value>> weights;
      std::optional<std::pair<uint32_t, Value>> implicitWeight;
      auto materializeWeight = [&](Operation *weight,
                                   bool &isSigned) -> FailureOr<std::pair<uint32_t, Value>> {
        if (weight) {
          if (dependsOnCandidate(weight)) {
            emitError(getSemanticLocation(weight))
                << "dist weights cannot depend on randomized properties";
            return failure();
          }
          if (auto found = weights.find(weight); found != weights.end()) {
            isSigned = isSignedNode(weight);
            return found->second;
          }
          FailureOr<Value> lowered = lowerExpression(weight);
          FailureOr<Value> scalar =
              succeeded(lowered)
                  ? toPackedScalar(*lowered, getSemanticLocation(weight))
                  : FailureOr<Value>(failure());
          if (failed(scalar) || !isa<IntegerType>((*scalar).getType())) {
            emitError(getSemanticLocation(weight))
                << "dist weights must be packed integral values";
            return failure();
          }
          isSigned = isSignedNode(weight);
          FailureOr<Value> extended = convert(
              *scalar, i64, isSigned, getSemanticLocation(weight), false);
          if (failed(extended))
            return failure();
          uint32_t capture = static_cast<uint32_t>(programCaptures.size());
          programCaptures.push_back(*extended);
          auto result = std::make_pair(capture, *extended);
          weights[weight] = result;
          return result;
        }
        isSigned = false;
        if (!implicitWeight) {
          uint32_t capture = static_cast<uint32_t>(programCaptures.size());
          Value one = constant64(1);
          programCaptures.push_back(one);
          implicitWeight = std::make_pair(capture, one);
        }
        return *implicitWeight;
      };

      DistPlan plan{expression, propertyIndex, 0,
                    activeProgramConstraintBlock, {}};
      for (unsigned index = 0; index != propertyIndex; ++index)
        plan.propertyOffset += planned[index].width;
      for (const PendingRange &range : pending) {
        bool weightSigned = false;
        FailureOr<std::pair<uint32_t, Value>> weight =
            materializeWeight(range.weight, weightSigned);
        if (failed(weight))
          return failure();
        uint64_t coefficient =
            range.perRange ? normalization / range.denominator : normalization;
        if (range.selectionCardinality == 0 ||
            coefficient > UINT64_MAX / range.selectionCardinality) {
          emitError(getSemanticLocation(expression))
              << "dist range selection mass exceeds 64 bits";
          return failure();
        }
        uint64_t selectionCoefficient =
            coefficient * range.selectionCardinality;
        plan.ranges.push_back({range.lower, range.cardinality, coefficient,
                               selectionCoefficient, weight->first,
                               weightSigned, weight->second});
      }
      if (plan.ranges.empty()) {
        emitError(getSemanticLocation(expression))
            << "distribution has no supported values";
        return failure();
      }
      distPlans.push_back(std::move(plan));

      auto emitWeightPositive = [&](Operation *weight) -> LogicalResult {
        bool weightSigned = false;
        FailureOr<std::pair<uint32_t, Value>> materialized =
            materializeWeight(weight, weightSigned);
        if (failed(materialized))
          return failure();
        instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 64, weightSigned,
                    materialized->first);
        instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 64, false, 0, 0);
        instruction(OBELISK_RT_RANDOM_GT_V1, 1, weightSigned);
        return success();
      };
      auto emitMatch = [&](Operation *value) -> LogicalResult {
        if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(value)) {
          SmallVector<Operation *> endpoints = getChildren(range);
          if (endpoints.size() != 2)
            return failure();
          FailureOr<uint64_t> first = constantBits(endpoints[0]);
          FailureOr<uint64_t> second = constantBits(endpoints[1]);
          if (failed(first) || failed(second))
            return failure();
          if (*first > *second)
            std::swap(endpoints[0], endpoints[1]);
          if (
              failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[0])))
            return failure();
          instruction(OBELISK_RT_RANDOM_GE_V1, 1,
                      isSignedNode(nested.front()));
          if (failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[1])))
            return failure();
          instruction(OBELISK_RT_RANDOM_LE_V1, 1,
                      isSignedNode(nested.front()));
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          return success();
        }
        if (failed(emitProgramExpression(nested.front())) ||
            failed(emitProgramExpression(value)))
          return failure();
        instruction(OBELISK_RT_RANDOM_EQ_V1, 1,
                    isSignedNode(nested.front()));
        return success();
      };

      bool firstSupport = true;
      for (const RawDistItem &item : rawItems) {
        if (failed(emitMatch(item.value)) ||
            failed(emitWeightPositive(item.weight)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        if (!firstSupport)
          instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
        firstSupport = false;
      }
      if (defaultWeight) {
        bool firstExplicit = true;
        for (const RawDistItem &item : rawItems) {
          if (failed(emitMatch(item.value)))
            return failure();
          if (!firstExplicit)
            instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
          firstExplicit = false;
        }
        instruction(OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1);
        if (failed(emitWeightPositive(defaultWeight)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        if (!firstSupport)
          instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
        firstSupport = false;
      }
      if (firstSupport)
        emitLiteral(false);
      return success();
    }
    FailureOr<unsigned> width = expressionWidth(expression);
    if (failed(width))
      return failure();
    if (auto bitOffset = expression->getAttrOfType<IntegerAttr>(
            randomVariableBitOffsetAttrName)) {
      const APInt &offsetValue = bitOffset.getValue();
      if (offsetValue.isNegative() || offsetValue.getActiveBits() > 64 ||
          offsetValue.getZExtValue() >= totalWidth ||
          *width > totalWidth - offsetValue.getZExtValue()) {
        emitError(getSemanticLocation(expression))
            << "random constraint value-path binding is invalid";
        return failure();
      }
      instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, *width,
                  isSignedNode(expression),
                  static_cast<uint32_t>(offsetValue.getZExtValue()));
      return success();
    }
    if (auto variable =
            expression->getAttrOfType<IntegerAttr>(randomVariableAttrName)) {
      APInt indexValue = variable.getValue();
      if (indexValue.isNegative() || indexValue.getActiveBits() > 64 ||
          indexValue.getZExtValue() >= planned.size()) {
        emitError(getSemanticLocation(expression))
            << "random constraint variable index is malformed";
        return failure();
      }
      unsigned index = static_cast<unsigned>(indexValue.getZExtValue());
      uint32_t offset = 0;
      for (unsigned current = 0; current != index; ++current)
        offset += planned[current].width;
      instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, planned[index].width,
                  planned[index].isSigned, offset);
      return success();
    }
    if (std::optional<StringRef> spelling = getConstantSpelling(expression)) {
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(*spelling, *width, getSemanticLocation(expression));
      if (failed(parsed) || !parsed->unknown.isZero()) {
        emitError(getSemanticLocation(expression))
            << "four-state constants are not executable in the runtime "
               "random solver";
        return failure();
      }
      literalInstruction(*width, isSignedNode(expression), parsed->value);
      return success();
    }
    SmallVector<Operation *> nested = getChildren(expression);
    auto captureExpression = [&]() -> LogicalResult {
      FailureOr<Value> value = lowerExpression(expression);
      FailureOr<Value> scalar =
          succeeded(value)
              ? toPackedScalar(*value, getSemanticLocation(expression))
              : FailureOr<Value>(failure());
      if (failed(scalar) || !isa<IntegerType>((*scalar).getType())) {
        emitError(getSemanticLocation(expression))
            << "four-state and non-integral runtime constraint captures are "
               "not executable yet";
        return failure();
      }
      // Preserve the scalar runtime ABI for captures that fit in one word.
      // Wider v2 captures retain their source width for compile-time-assisted
      // materialization (and for the wide runtime ABI added separately).
      Type captureType = *width <= 64
                             ? Type(i64)
                             : Type(IntegerType::get(function.getContext(),
                                                     *width));
      FailureOr<Value> extended =
          convert(*scalar, captureType, false, getSemanticLocation(expression),
                  false);
      if (failed(extended))
        return failure();
      uint32_t capture = static_cast<uint32_t>(programCaptures.size());
      programCaptures.push_back(*extended);
      instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, *width,
                  isSignedNode(expression), capture);
      return success();
    };
    if (isa<semantic::SVConversionExpressionOp>(expression)) {
      // Keep constant conversions structural so Z3 can fold them, but evaluate
      // a conversion of runtime state once as a capture. This preserves the
      // conversion's exact signed extension/truncation while exposing a direct
      // capture endpoint to interval planning.
      bool hasRuntimeInput = false;
      expression->walk([&](Operation *nestedExpression) {
        if (getConstantSpelling(nestedExpression) ||
            nestedExpression->hasAttr(randomVariableAttrName) ||
            nestedExpression->hasAttr(randomVariableBitOffsetAttrName))
          return;
        if (isa<semantic::SVNamedValueExpressionOp,
                semantic::SVHierarchicalValueExpressionOp,
                semantic::SVMemberAccessExpressionOp,
                semantic::SVCallExpressionOp>(nestedExpression))
          hasRuntimeInput = true;
      });
      if (!dependsOnCandidate(expression) && hasRuntimeInput)
        return captureExpression();
      if (nested.size() != 1 || failed(emitProgramExpression(nested.front())))
        return failure();
      instruction(OBELISK_RT_RANDOM_CAST_V1, *width,
                  isSignedNode(nested.front()));
      return success();
    }
    if (!dependsOnCandidate(expression))
      return captureExpression();

    if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(expression)) {
      if (nested.size() != 1 || failed(emitProgramExpression(nested.front())))
        return failure();
      uint8_t opcode = 0;
      using Unary = semantic::SVUnaryOperator;
      switch (unary.getOperatorKind()) {
      case Unary::Plus:
        opcode = OBELISK_RT_RANDOM_POS_V1;
        break;
      case Unary::Minus:
        opcode = OBELISK_RT_RANDOM_NEG_V1;
        break;
      case Unary::BitwiseNot:
        opcode = OBELISK_RT_RANDOM_BIT_NOT_V1;
        break;
      case Unary::BitwiseAnd:
        opcode = OBELISK_RT_RANDOM_REDUCE_AND_V1;
        break;
      case Unary::BitwiseOr:
        opcode = OBELISK_RT_RANDOM_REDUCE_OR_V1;
        break;
      case Unary::BitwiseXor:
        opcode = OBELISK_RT_RANDOM_REDUCE_XOR_V1;
        break;
      case Unary::BitwiseNand:
        opcode = OBELISK_RT_RANDOM_REDUCE_NAND_V1;
        break;
      case Unary::BitwiseNor:
        opcode = OBELISK_RT_RANDOM_REDUCE_NOR_V1;
        break;
      case Unary::BitwiseXnor:
        opcode = OBELISK_RT_RANDOM_REDUCE_XNOR_V1;
        break;
      case Unary::LogicalNot:
        opcode = OBELISK_RT_RANDOM_LOGICAL_NOT_V1;
        break;
      default:
        return failure();
      }
      instruction(opcode, *width, isSignedNode(expression));
      return success();
    }
    if (auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(expression)) {
      if (nested.size() != 2 || failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramExpression(nested[1])))
        return failure();
      uint8_t opcode = 0;
      bool signedOperation = isSignedNode(nested.front());
      using Binary = semantic::SVBinaryOperator;
      switch (binary.getOperatorKind()) {
      case Binary::Add:
        opcode = OBELISK_RT_RANDOM_ADD_V1;
        break;
      case Binary::Subtract:
        opcode = OBELISK_RT_RANDOM_SUB_V1;
        break;
      case Binary::Multiply:
        opcode = OBELISK_RT_RANDOM_MUL_V1;
        break;
      case Binary::Divide:
      case Binary::Mod: {
        FailureOr<unsigned> divisorWidth = expressionWidth(nested[1]);
        std::optional<StringRef> spelling = getConstantSpelling(nested[1]);
        FailureOr<ParsedConstant> divisor =
            succeeded(divisorWidth) && spelling
                ? parseSVInteger(*spelling, *divisorWidth,
                                 getSemanticLocation(nested[1]))
                : FailureOr<ParsedConstant>(failure());
        if (failed(divisor) || !divisor->unknown.isZero() ||
            divisor->value.isZero()) {
          emitError(getSemanticLocation(expression))
              << "runtime random division and modulo require a statically "
                 "nonzero divisor";
          return failure();
        }
        opcode = binary.getOperatorKind() == Binary::Divide
                     ? OBELISK_RT_RANDOM_DIV_V1
                     : OBELISK_RT_RANDOM_MOD_V1;
        break;
      }
      case Binary::BinaryAnd:
        opcode = OBELISK_RT_RANDOM_BIT_AND_V1;
        break;
      case Binary::BinaryOr:
        opcode = OBELISK_RT_RANDOM_BIT_OR_V1;
        break;
      case Binary::BinaryXor:
        opcode = OBELISK_RT_RANDOM_BIT_XOR_V1;
        break;
      case Binary::BinaryXnor:
        opcode = OBELISK_RT_RANDOM_BIT_XNOR_V1;
        break;
      case Binary::Equality:
      case Binary::CaseEquality:
      case Binary::WildcardEquality:
        opcode = OBELISK_RT_RANDOM_EQ_V1;
        break;
      case Binary::Inequality:
      case Binary::CaseInequality:
      case Binary::WildcardInequality:
        opcode = OBELISK_RT_RANDOM_NE_V1;
        break;
      case Binary::GreaterThanEqual:
        opcode = OBELISK_RT_RANDOM_GE_V1;
        break;
      case Binary::GreaterThan:
        opcode = OBELISK_RT_RANDOM_GT_V1;
        break;
      case Binary::LessThanEqual:
        opcode = OBELISK_RT_RANDOM_LE_V1;
        break;
      case Binary::LessThan:
        opcode = OBELISK_RT_RANDOM_LT_V1;
        break;
      case Binary::LogicalAnd:
        opcode = OBELISK_RT_RANDOM_LOGICAL_AND_V1;
        break;
      case Binary::LogicalOr:
        opcode = OBELISK_RT_RANDOM_LOGICAL_OR_V1;
        break;
      case Binary::LogicalImplication:
        opcode = OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1;
        break;
      case Binary::LogicalEquivalence:
        opcode = OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1;
        break;
      case Binary::LogicalShiftLeft:
      case Binary::ArithmeticShiftLeft:
        opcode = OBELISK_RT_RANDOM_SHIFT_LEFT_V1;
        break;
      case Binary::LogicalShiftRight:
        opcode = OBELISK_RT_RANDOM_SHIFT_RIGHT_V1;
        break;
      case Binary::ArithmeticShiftRight:
        opcode = isSignedNode(nested[0])
                     ? OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1
                     : OBELISK_RT_RANDOM_SHIFT_RIGHT_V1;
        break;
      case Binary::Power:
        if (isSignedNode(nested[1])) {
          FailureOr<unsigned> exponentWidth = expressionWidth(nested[1]);
          std::optional<StringRef> spelling = getConstantSpelling(nested[1]);
          FailureOr<ParsedConstant> exponent =
              succeeded(exponentWidth) && spelling
                  ? parseSVInteger(*spelling, *exponentWidth,
                                   getSemanticLocation(nested[1]))
                  : FailureOr<ParsedConstant>(failure());
          if (failed(exponent) || !exponent->unknown.isZero() ||
              exponent->value.isNegative()) {
            emitError(getSemanticLocation(expression))
                << "runtime random integral power requires an unsigned or "
                   "statically nonnegative exponent";
            return failure();
          }
        }
        opcode = OBELISK_RT_RANDOM_POWER_V1;
        break;
      default:
        emitError(getSemanticLocation(expression))
            << "operator is not encoded by the runtime random solver";
        return failure();
      }
      instruction(opcode, *width, signedOperation);
      return success();
    }
    if (isa<semantic::SVConcatenationExpressionOp>(expression)) {
      if (nested.empty()) {
        emitError(getSemanticLocation(expression))
            << "runtime random concatenation cannot be empty";
        return failure();
      }
      SmallVector<unsigned> inputWidths;
      uint64_t combinedWidth = 0;
      for (Operation *input : nested) {
        FailureOr<unsigned> inputWidth = expressionWidth(input);
        if (failed(inputWidth) ||
            combinedWidth > std::numeric_limits<unsigned>::max() -
                                *inputWidth)
          return failure();
        combinedWidth += *inputWidth;
        inputWidths.push_back(*inputWidth);
      }
      if (combinedWidth != *width) {
        emitError(getSemanticLocation(expression))
            << "runtime random concatenation width is inconsistent";
        return failure();
      }
      if (failed(emitProgramExpression(nested.front())))
        return failure();
      unsigned materializedWidth = inputWidths.front();
      for (auto [input, inputWidth] :
           llvm::zip_equal(ArrayRef(nested).drop_front(),
                           ArrayRef(inputWidths).drop_front())) {
        materializedWidth += inputWidth;
        instruction(OBELISK_RT_RANDOM_CAST_V1, materializedWidth,
                    /*isSigned=*/false);
        literalInstruction(materializedWidth, /*isSigned=*/false,
                           APInt(materializedWidth, inputWidth));
        instruction(OBELISK_RT_RANDOM_SHIFT_LEFT_V1, materializedWidth,
                    /*isSigned=*/false);
        if (failed(emitProgramExpression(input)))
          return failure();
        instruction(OBELISK_RT_RANDOM_BIT_OR_V1, materializedWidth,
                    /*isSigned=*/false);
      }
      if (nested.size() == 1)
        instruction(OBELISK_RT_RANDOM_CAST_V1, *width,
                    /*isSigned=*/false);
      return success();
    }
    if (isa<semantic::SVReplicationExpressionOp>(expression)) {
      if (nested.size() != 2) {
        emitError(getSemanticLocation(expression))
            << "runtime random replication has malformed operands";
        return failure();
      }
      FailureOr<unsigned> countWidth = expressionWidth(nested[0]);
      FailureOr<unsigned> inputWidth = expressionWidth(nested[1]);
      std::optional<StringRef> spelling = getConstantSpelling(nested[0]);
      FailureOr<ParsedConstant> count =
          succeeded(countWidth) && spelling
              ? parseSVInteger(*spelling, *countWidth,
                               getSemanticLocation(nested[0]))
              : FailureOr<ParsedConstant>(failure());
      if (failed(count) || !count->unknown.isZero() || count->value.isZero() ||
          count->value.isNegative() || failed(inputWidth)) {
        emitError(getSemanticLocation(expression))
            << "runtime random replication requires a known positive count";
        return failure();
      }
      uint64_t repetitions = count->value.getZExtValue();
      if (repetitions > std::numeric_limits<unsigned>::max() / *inputWidth ||
          repetitions * *inputWidth != *width) {
        emitError(getSemanticLocation(expression))
            << "runtime random replication width is inconsistent";
        return failure();
      }
      if (failed(emitProgramExpression(nested[1])))
        return failure();
      APInt multiplier = APInt::getZero(*width);
      for (uint64_t index = 0; index != repetitions; ++index)
        multiplier.setBit(static_cast<unsigned>(index * *inputWidth));
      literalInstruction(*width, /*isSigned=*/false, multiplier);
      instruction(OBELISK_RT_RANDOM_MUL_V1, *width, /*isSigned=*/false);
      return success();
    }
    if (auto conditional =
            dyn_cast<semantic::SVConditionalExpressionOp>(expression)) {
      ArrayRef<int64_t> patternFlags = conditional.getConditionPatternFlags();
      uint64_t conditionCount = conditional.getConditionCount();
      if (conditionCount == 0 || patternFlags.size() != conditionCount ||
          llvm::any_of(patternFlags, [](int64_t flag) { return flag != 0; }) ||
          nested.size() != conditionCount + 2) {
        emitError(getSemanticLocation(expression))
            << "runtime random conditional requires one or more "
               "non-pattern conditions";
        return failure();
      }
      if (failed(emitProgramExpression(nested.front())))
        return failure();
      for (Operation *condition :
           ArrayRef(nested).slice(1, conditionCount - 1)) {
        if (failed(emitProgramExpression(condition)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
      }
      if (failed(emitProgramExpression(nested[conditionCount])) ||
          failed(emitProgramExpression(nested[conditionCount + 1])))
        return failure();
      instruction(OBELISK_RT_RANDOM_SELECT_V1, *width,
                  isSignedNode(expression));
      return success();
    }
    if (isa<semantic::SVInsideExpressionOp>(expression)) {
      if (nested.size() < 2)
        return failure();
      bool first = true;
      for (Operation *item : ArrayRef(nested).drop_front()) {
        if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item)) {
          SmallVector<Operation *> endpoints = getChildren(range);
          if (endpoints.size() != 2 ||
              failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[0])))
            return failure();
          instruction(OBELISK_RT_RANDOM_GE_V1, 1, isSignedNode(nested.front()));
          if (failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[1])))
            return failure();
          instruction(OBELISK_RT_RANDOM_LE_V1, 1, isSignedNode(nested.front()));
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        } else {
          if (failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(item)))
            return failure();
          instruction(OBELISK_RT_RANDOM_EQ_V1, 1, isSignedNode(nested.front()));
        }
        if (!first)
          instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
        first = false;
      }
      return success();
    }
    emitError(getSemanticLocation(expression))
        << "candidate-dependent expression is not encoded by the runtime "
           "random solver: "
        << expression->getName();
    return failure();
  };

  std::function<LogicalResult(Operation *, Operation *)> emitProgramConstraint;
  emitProgramConstraint = [&](Operation *constraint,
                              Operation *softTarget) -> LogicalResult {
    SmallVector<Operation *> nested = getChildren(constraint);
    if (isa<semantic::SVConstraintListOp>(constraint)) {
      if (nested.empty()) {
        emitLiteral(true);
        return success();
      }
      if (failed(emitProgramConstraint(nested.front(), softTarget)))
        return failure();
      for (Operation *item : ArrayRef(nested).drop_front()) {
        if (failed(emitProgramConstraint(item, softTarget)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
      }
      return success();
    }
    if (isa<semantic::SVSolveBeforeConstraintOp>(constraint)) {
      emitLiteral(true);
      return success();
    }
    if (auto expression =
            dyn_cast<semantic::SVExpressionConstraintOp>(constraint)) {
      bool selected = softTarget ? softTarget == constraint
                                 : !expression.getIsSoft();
      if (!selected) {
        emitLiteral(true);
        return success();
      }
      return nested.size() == 1 ? emitProgramExpression(nested.front())
                                : failure();
    }
    if (isa<semantic::SVImplicationConstraintOp>(constraint)) {
      if (nested.size() != 2 || failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramConstraint(nested[1], softTarget)))
        return failure();
      instruction(OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1, 1);
      return success();
    }
    if (auto conditional =
            dyn_cast<semantic::SVConditionalConstraintOp>(constraint)) {
      if (nested.size() != (conditional.getHasElse() ? 3u : 2u) ||
          failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramConstraint(nested[1], softTarget)))
        return failure();
      if (conditional.getHasElse()) {
        if (failed(emitProgramConstraint(nested[2], softTarget)))
          return failure();
      } else {
        emitLiteral(true);
      }
      instruction(OBELISK_RT_RANDOM_SELECT_V1, 1);
      return success();
    }
    if (isa<semantic::SVUniquenessConstraintOp>(constraint)) {
      if (softTarget) {
        emitLiteral(true);
        return success();
      }
      bool first = true;
      for (size_t left = 0; left != nested.size(); ++left)
        for (size_t right = left + 1; right != nested.size(); ++right) {
          if (failed(emitProgramExpression(nested[left])) ||
              failed(emitProgramExpression(nested[right])))
            return failure();
          instruction(OBELISK_RT_RANDOM_NE_V1, 1, isSignedNode(nested[left]));
          if (!first)
            instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          first = false;
        }
      if (first)
        emitLiteral(true);
      return success();
    }
    if (isa<semantic::SVForeachConstraintOp>(constraint)) {
      // Runtime-sized foreach predicates are evaluated by the generated
      // candidate checker below.  The scalar residual program deliberately
      // contributes the identity here; its fixed capture ABI cannot encode a
      // runtime-varying number of array elements.
      emitLiteral(true);
      return success();
    }
    emitError(getSemanticLocation(constraint))
        << "constraint is not encoded by the runtime random solver: "
        << constraint->getName();
    return failure();
  };

  // Encode and analyze the fallback program before synthesizing the tier-0
  // CFG. Candidate-independent captures are materialized once in the dispatch
  // block and shared with the runtime fallback. Keeping the analysis here also
  // lets the generated proposal consume conservative compiler-side domains.
  Value programSavedThis = thisObject;
  SmallVector<Value> programSavedCandidates =
      std::move(randomizeCandidateValues);
  llvm::scope_exit restoreProgramBindings([&] {
    thisObject = programSavedThis;
    randomizeCandidateValues = std::move(programSavedCandidates);
  });
  thisObject = receiver;
  bool emittedHard = false;
  bool emittedSoft = false;
  llvm::DenseMap<Operation *, uint64_t> softPriorities;
  uint64_t nextSoftPriority = 0;
  auto assignSoftPriorities = [&](bool inlineConstraints) {
    for (auto [index, root] : llvm::enumerate(children)) {
      if (index == receiverIndex ||
          root->hasAttr(randomConstraintBlockAttrName) == inlineConstraints)
        continue;
      SmallVector<Operation *> items = isa<semantic::SVConstraintListOp>(root)
                                           ? getChildren(root)
                                           : SmallVector<Operation *>{root};
      for (Operation *item : items)
        item->walk([&](semantic::SVExpressionConstraintOp expression) {
          if (expression.getIsSoft())
            softPriorities[expression] = nextSoftPriority++;
        });
    }
  };
  // Later declarations have higher priority. Class constraints are frozen in
  // base-to-derived declaration order, and every inline constraint has higher
  // priority than the class constraints it augments.
  assignSoftPriorities(/*inlineConstraints=*/false);
  assignSoftPriorities(/*inlineConstraints=*/true);
  if (nextSoftPriority > 64) {
    emitError(location)
        << "random fallback supports at most 64 soft constraint priorities";
    return failure();
  }
  for (auto [index, root] : llvm::enumerate(children)) {
    if (index == receiverIndex)
      continue;
    uint32_t constraintBlock = OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
    if (auto block =
            root->getAttrOfType<IntegerAttr>(randomConstraintBlockAttrName)) {
      APInt value = block.getValue();
      if (value.isNegative() || value.getActiveBits() > 64 ||
          value.getZExtValue() >= constraintCount) {
        emitError(getSemanticLocation(root))
            << "random constraint block index is malformed";
        return failure();
      }
      constraintBlock = static_cast<uint32_t>(value.getZExtValue());
    }
    SmallVector<Operation *> items = isa<semantic::SVConstraintListOp>(root)
                                         ? getChildren(root)
                                         : SmallVector<Operation *>{root};
    for (Operation *item : items) {
      bool hasHard = false;
      SmallVector<semantic::SVExpressionConstraintOp> softConstraints;
      item->walk([&](Operation *nested) {
        if (auto expression =
                dyn_cast<semantic::SVExpressionConstraintOp>(nested)) {
          if (expression.getIsSoft())
            softConstraints.push_back(expression);
          else
            hasHard = true;
        } else if (isa<semantic::SVUniquenessConstraintOp>(nested)) {
          hasHard = true;
        }
      });
      if (hasHard) {
        activeProgramConstraintBlock = constraintBlock;
        if (failed(emitProgramConstraint(item, /*softTarget=*/nullptr)))
          return failure();
        instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false,
                    constraintBlock);
        emittedHard = true;
      }
      for (semantic::SVExpressionConstraintOp soft : softConstraints) {
        bool containsDist = false;
        soft->walk([&](semantic::SVDistExpressionOp) { containsDist = true; });
        if (containsDist) {
          emitError(getSemanticLocation(soft))
              << "soft dist constraints are not executable yet";
          return failure();
        }
        activeProgramConstraintBlock = constraintBlock;
        if (failed(emitProgramConstraint(item, soft)))
          return failure();
        instruction(OBELISK_RT_RANDOM_END_SOFT_V1, 1, false, constraintBlock,
                    softPriorities.lookup(soft));
        emittedSoft = true;
      }
    }
  }
  // A statically distinct path can reach the same active random object. The
  // flattened solver gives each occurrence its own bit slice, so identify the
  // owner at runtime and make corresponding fields equal exactly when both
  // paths are active aliases. This preserves the one-variable-per-object rule
  // from IEEE 1800 while leaving non-aliased paths independent.
  for (const AliasEquality &alias : aliasEqualities) {
    uint32_t capture = static_cast<uint32_t>(programCaptures.size());
    programCaptures.push_back(
        arith::ExtUIOp::create(builder, location, i64, alias.active));
    instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 1, false, capture);
    instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, alias.width, false,
                alias.leftOffset);
    instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, alias.width, false,
                alias.rightOffset);
    instruction(OBELISK_RT_RANDOM_EQ_V1, 1);
    instruction(OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1, 1);
    instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1);
    emittedHard = true;
  }
  if (!emittedHard) {
    emitLiteral(true);
    instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1);
  }
  if (!distPlans.empty() && hasSolveBefore) {
    emitError(location)
        << "dist combined with solve before is not executable yet";
    return failure();
  }
  for (auto [index, plan] : llvm::enumerate(distPlans))
    for (const DistPlan &other : ArrayRef(distPlans).drop_front(index + 1))
      if (plan.propertyIndex == other.propertyIndex) {
        emitError(getSemanticLocation(other.source))
            << "multiple active dist constraints for one property are not "
               "executable yet";
        return failure();
      }
  thisObject = programSavedThis;
  randomizeCandidateValues = std::move(programSavedCandidates);
  restoreProgramBindings.release();

  SmallVector<uint8_t> program;
  auto append16 = [&](uint16_t value) {
    program.push_back(static_cast<uint8_t>(value));
    program.push_back(static_cast<uint8_t>(value >> 8));
  };
  auto append32 = [&](uint32_t value) {
    for (unsigned index = 0; index != 4; ++index)
      program.push_back(static_cast<uint8_t>(value >> (index * 8)));
  };
  auto append64 = [&](uint64_t value) {
    for (unsigned index = 0; index != 8; ++index)
      program.push_back(static_cast<uint8_t>(value >> (index * 8)));
  };
  uint32_t programFlags = emittedSoft ? OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT : 0;
  if (hasSolveBefore)
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE;
  if (!distPlans.empty())
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_DIST;
  bool hasFiniteDomains = llvm::any_of(planned, [](const Property &property) {
    return !property.domains.empty();
  });
  if (hasRuntimeForeachConstraint && hasFiniteDomains) {
    emitError(location)
        << "foreach constraints over finite semantic random domains require "
           "dynamic domain-index traversal";
    return failure();
  }
  if (hasFiniteDomains)
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS;
  bool wideProgram = totalWidth > 64 || llvm::any_of(
                                                programInstructions,
                                                [](const EncodedInstruction &op) {
                                                  return op.width > 64;
                                                });
  append32(OBELISK_RT_RANDOM_PROGRAM_MAGIC);
  append16(wideProgram ? OBELISK_RT_RANDOM_PROGRAM_VERSION_V2
                       : OBELISK_RT_RANDOM_PROGRAM_VERSION_V1);
  append16(wideProgram ? OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2
                       : OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE);
  append32(static_cast<uint32_t>(totalWidth));
  append32(static_cast<uint32_t>(programInstructions.size()));
  append32(static_cast<uint32_t>(programCaptures.size()));
  append32(programFlags);
  SmallVector<uint64_t> literalWords;
  SmallVector<uint32_t> instructionAuxiliaries;
  if (wideProgram) {
    for (const EncodedInstruction &encoded : programInstructions) {
      uint32_t auxiliary = 0;
      if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
        APInt literal = encoded.literal.value_or(
            APInt(encoded.width, encoded.immediate));
        uint64_t words = literal.getNumWords();
        if (literalWords.size() > UINT32_MAX ||
            words > UINT32_MAX - literalWords.size()) {
          emitError(location) << "wide random literal pool exceeds 32 bits";
          return failure();
        }
        auxiliary = static_cast<uint32_t>(literalWords.size());
        literalWords.append(literal.getRawData(),
                            literal.getRawData() + words);
      } else if (encoded.opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
        if (encoded.immediate > UINT32_MAX) {
          emitError(location) << "random soft priority exceeds 32 bits";
          return failure();
        }
        auxiliary = static_cast<uint32_t>(encoded.immediate);
      } else if (encoded.immediate != 0) {
        emitError(location)
            << "wide random instruction has an unencodable auxiliary";
        return failure();
      }
      instructionAuxiliaries.push_back(auxiliary);
    }
    append32(static_cast<uint32_t>(literalWords.size()));
    append32(0);
  }
  for (auto [index, encoded] : llvm::enumerate(programInstructions)) {
    program.push_back(encoded.opcode);
    if (wideProgram) {
      program.push_back(encoded.flags);
      append16(0);
      append32(encoded.width);
      append32(encoded.operand);
      append32(instructionAuxiliaries[index]);
    } else {
      program.push_back(static_cast<uint8_t>(encoded.width));
      program.push_back(encoded.flags);
      program.push_back(0);
      append32(encoded.operand);
      append64(encoded.literal ? encoded.literal->getZExtValue()
                               : encoded.immediate);
    }
  }
  for (uint64_t word : literalWords)
    append64(word);
  if (hasSolveBefore) {
    append32(static_cast<uint32_t>(solveBeforeEdges.size()));
    for (const SolveBeforeEdge &edge : solveBeforeEdges) {
      append64(edge.beforeMask);
      append64(edge.afterMask);
      append32(edge.constraintBlock);
      append32(0);
    }
  }
  if (!distPlans.empty()) {
    append32(static_cast<uint32_t>(distPlans.size()));
    uint32_t recordCount = 0;
    for (const DistPlan &plan : distPlans)
      recordCount += static_cast<uint32_t>(plan.ranges.size());
    append32(recordCount);
    for (auto [group, plan] : llvm::enumerate(distPlans)) {
      for (const DistRangePlan &range : plan.ranges) {
        append32(static_cast<uint32_t>(group));
        append32(plan.constraintBlock);
        append32(plan.propertyOffset);
        append16(static_cast<uint16_t>(planned[plan.propertyIndex].width));
        append16(0);
        append64(range.lower);
        append64(range.cardinality);
        append64(range.coefficient);
        append32(range.weightCapture);
        uint32_t flags =
            range.weightSigned ? OBELISK_RT_RANDOM_DIST_WEIGHT_SIGNED : 0;
        if (planned[plan.propertyIndex].isSigned)
          flags |= OBELISK_RT_RANDOM_DIST_TARGET_SIGNED;
        append32(flags);
      }
    }
  }
  if (hasFiniteDomains) {
    uint32_t groupCount = 0;
    uint32_t recordCount = 0;
    for (const Property &property : planned) {
      groupCount += static_cast<uint32_t>(property.domains.size());
      for (const PropertyDomain &domain : property.domains)
        recordCount += static_cast<uint32_t>(domain.patterns.size());
    }
    append32(groupCount);
    append32(recordCount);
    uint32_t group = 0;
    uint32_t propertyOffset = 0;
    for (const Property &property : planned) {
      for (const PropertyDomain &domain : property.domains) {
        for (const DomainPattern &pattern : domain.patterns) {
          append32(group);
          append32(propertyOffset + domain.offset);
          append16(static_cast<uint16_t>(domain.width));
          append16(0);
          append32(0);
          append64(pattern.mask);
          append64(pattern.value);
        }
        ++group;
      }
      propertyOffset += property.width;
    }
  }
  constexpr uint64_t maxFallbackAttempts = uint64_t{1} << 20;
  uint64_t fallbackAttempts = 1;
  bool completeFallbackDomain = true;
  for (const Property &property : planned) {
    if (property.isRandC)
      continue;
    uint64_t cardinality = propertyDomainCardinality(property);
    if (cardinality == 0 || cardinality > maxFallbackAttempts ||
        fallbackAttempts > maxFallbackAttempts / cardinality) {
      fallbackAttempts = maxFallbackAttempts;
      completeFallbackDomain = false;
      break;
    }
    fallbackAttempts *= cardinality;
  }
  if (hasFiniteDomains && !distPlans.empty() && !completeFallbackDomain) {
    emitError(location)
        << "dist over a finite semantic domain currently requires exhaustive "
           "fallback traversal of at most 2^20 aggregate assignments";
    return failure();
  }
  solver::RandomProgramAnalysis analysis = solver::analyzeRandomProgram(
      program.data(), program.size(), /*resourceLimit=*/100000,
      /*preferGlobalAssignmentTable=*/hasSolveBefore);
  bool softProposalExact =
      !hasSoftConstraint || analysis.softPreferencesResolved;

  SmallVector<APInt> proposalAssignments;
  constexpr size_t maxMaterializedAssignmentTableSize = 16;
  APInt aggregateMask = domainMask;
  bool validAssignmentTable =
      distPlans.empty() && analysis.assignmentTables.empty() &&
      !analysis.assignmentTable.empty() &&
      analysis.assignmentTable.size() <= maxMaterializedAssignmentTableSize &&
      llvm::all_of(analysis.assignmentTable, [&](const APInt &assignment) {
        return assignment.getBitWidth() <= assignmentWidth &&
               (assignment.zextOrTrunc(assignmentWidth) & ~aggregateMask)
                   .isZero();
      });
  bool powerOfTwoAssignmentTable =
      validAssignmentTable && (analysis.assignmentTable.size() &
                               (analysis.assignmentTable.size() - 1)) == 0;
  if (validAssignmentTable)
    for (const APInt &assignment : analysis.assignmentTable)
      proposalAssignments.push_back(
          assignment.zextOrTrunc(assignmentWidth));

  struct SolveBeforeTableNode {
    SmallVector<unsigned> children;
    SmallVector<APInt> assignments;
  };
  SmallVector<SolveBeforeTableNode> solveBeforeTableNodes;
  std::optional<unsigned> solveBeforeTableRoot;
  std::function<unsigned(ArrayRef<APInt>, unsigned)> buildSolveBeforeTable;
  if (hasSolveBefore) {
    buildSolveBeforeTable = [&](ArrayRef<APInt> rows,
                                unsigned layer) -> unsigned {
      unsigned node = solveBeforeTableNodes.size();
      solveBeforeTableNodes.emplace_back();
      if (layer == solveBeforeLayerMasks.size()) {
        solveBeforeTableNodes[node].assignments.append(rows.begin(),
                                                       rows.end());
        return node;
      }
      SmallVector<std::pair<uint64_t, SmallVector<APInt>>> groups;
      for (const APInt &row : rows) {
        uint64_t key = row.getZExtValue() & solveBeforeLayerMasks[layer];
        auto group = llvm::find_if(
            groups, [&](const auto &entry) { return entry.first == key; });
        if (group == groups.end()) {
          groups.emplace_back(key, SmallVector<APInt>{});
          group = std::prev(groups.end());
        }
        group->second.push_back(row);
      }
      llvm::sort(groups, [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
      });
      SmallVector<unsigned> children;
      for (const auto &group : groups)
        children.push_back(buildSolveBeforeTable(group.second, layer + 1));
      solveBeforeTableNodes[node].children = std::move(children);
      return node;
    };
    if (validAssignmentTable && softProposalExact)
      solveBeforeTableRoot = buildSolveBeforeTable(proposalAssignments, 0);
  }

  struct ProposalAssignmentTable {
    APInt mask;
    SmallVector<APInt> assignments;
  };
  SmallVector<ProposalAssignmentTable> proposalAssignmentTables;
  APInt proposalAssignmentTableMask = APInt::getZero(assignmentWidth);
  auto coversWholeProperties = [&](const APInt &tableMask) {
    unsigned offset = 0;
    for (const Property &property : planned) {
      APInt propertyMask =
          APInt::getLowBitsSet(assignmentWidth, property.width).shl(offset);
      APInt overlap = tableMask & propertyMask;
      if (!overlap.isZero() && overlap != propertyMask)
        return false;
      offset += property.width;
    }
    return true;
  };
  // Component tables are sampled once and then held while the residual
  // proposal advances. Use them for soft constraints only after the solver has
  // incorporated the selected preference set into the analyzed formula.
  bool validAssignmentTables = analysis.assignmentTable.empty() &&
                               distPlans.empty() && softProposalExact &&
                               !analysis.assignmentTables.empty();
  for (const solver::RandomAssignmentTable &table : analysis.assignmentTables) {
    APInt normalizedMask = table.mask.zextOrTrunc(assignmentWidth);
    bool valid =
        table.mask.getBitWidth() <= assignmentWidth && !table.mask.isZero() &&
        (normalizedMask & ~aggregateMask).isZero() &&
        coversWholeProperties(normalizedMask) &&
        (normalizedMask & proposalAssignmentTableMask).isZero() &&
        !table.assignments.empty() &&
        table.assignments.size() <= maxMaterializedAssignmentTableSize &&
        llvm::all_of(table.assignments, [&](const APInt &assignment) {
          return assignment.getBitWidth() <= assignmentWidth &&
                 (assignment.zextOrTrunc(assignmentWidth) & ~normalizedMask)
                     .isZero();
        });
    if (!valid) {
      validAssignmentTables = false;
      break;
    }
    proposalAssignmentTableMask |= normalizedMask;
    SmallVector<APInt> normalizedAssignments;
    for (const APInt &assignment : table.assignments)
      normalizedAssignments.push_back(
          assignment.zextOrTrunc(assignmentWidth));
    proposalAssignmentTables.push_back(
        {normalizedMask, std::move(normalizedAssignments)});
  }
  if (!validAssignmentTables) {
    proposalAssignmentTables.clear();
    proposalAssignmentTableMask = APInt::getZero(assignmentWidth);
  }
  SmallVector<unsigned> solveBeforeComponentTableRoots;

  struct ProposalDomain {
    uint32_t offset;
    unsigned width;
    uint64_t lower;
    uint64_t cardinality;
    bool powerOfTwo;
    bool isSigned;
  };
  SmallVector<ProposalDomain> proposalDomains;
  struct ProposalFiniteDomain {
    uint32_t offset;
    const Property *property;
    uint64_t cardinality;
  };
  SmallVector<ProposalFiniteDomain> proposalFiniteDomains;
  struct ProposalCaptureDomain {
    uint32_t offset;
    unsigned width;
    std::optional<uint32_t> lowerCapture;
    std::optional<uint32_t> upperCapture;
    bool lowerExclusive;
    bool upperExclusive;
    bool isSigned;
    std::optional<uint64_t> staticLower;
    std::optional<uint64_t> staticUpper;
    Value lower;
    Value cardinality;
  };
  SmallVector<ProposalCaptureDomain> proposalCaptureDomains;
  size_t materializedCaptureBounds = 0;
  struct ProposalAlias {
    uint32_t targetOffset;
    uint32_t sourceOffset;
    unsigned width;
  };
  SmallVector<ProposalAlias> proposalAliases;
  struct ProposalDefinition {
    uint32_t targetOffset;
    unsigned width;
    uint32_t expressionBegin;
    uint32_t expressionEnd;
  };
  SmallVector<ProposalDefinition> proposalDefinitions;
  uint32_t propertyOffset = 0;
  for (const Property &property : planned) {
    APInt propertyMask =
        APInt::getLowBitsSet(assignmentWidth, property.width)
            .shl(propertyOffset);
    if (!property.domains.empty()) {
      if (!property.isRandC && !validAssignmentTable &&
          (proposalAssignmentTableMask & propertyMask) != propertyMask)
        proposalFiniteDomains.push_back({propertyOffset,
                                         &property,
                                         propertyDomainCardinality(property)});
      propertyOffset += property.width;
      continue;
    }
    auto found = llvm::find_if(analysis.domains,
                               [&](const solver::RandomVariableDomain &domain) {
                                 return domain.offset == propertyOffset &&
                                        domain.width == property.width;
                               });
    if (found != analysis.domains.end() && property.width <= 64 &&
        found->lower.getBitWidth() == property.width &&
        found->upper.getBitWidth() == property.width &&
        found->lower.ule(found->upper)) {
      uint64_t fullMaximum = property.width == 64
                                 ? UINT64_MAX
                                 : (uint64_t{1} << property.width) - 1;
      uint64_t lower = found->lower.getZExtValue();
      uint64_t upper = found->upper.getZExtValue();
      uint64_t distance = upper - lower;
      uint64_t cardinality = distance + 1;
      if (upper <= fullMaximum && cardinality != 0)
        proposalDomains.push_back({propertyOffset,
                                   property.width,
                                   lower,
                                   cardinality,
                                   (cardinality & (cardinality - 1)) == 0,
                                   found->isSigned});
    }
    propertyOffset += property.width;
  }
  llvm::erase_if(proposalDomains, [&](const ProposalDomain &domain) {
    return llvm::any_of(distPlans, [&](const DistPlan &plan) {
      return plan.propertyOffset == domain.offset &&
             planned[plan.propertyIndex].width == domain.width;
    });
  });
  size_t materializedDomains = proposalDomains.size();
  auto isPropertyField = [&](uint32_t offset, uint32_t width) {
    uint32_t currentOffset = 0;
    for (const Property &property : planned) {
      if (currentOffset == offset && property.width == width)
        return true;
      currentOffset += property.width;
    }
    return false;
  };
  for (const solver::RandomVariableCaptureBound &bound :
       analysis.captureBounds) {
    bool targetsDist = llvm::any_of(distPlans, [&](const DistPlan &plan) {
      return plan.propertyOffset == bound.offset &&
             planned[plan.propertyIndex].width == bound.width;
    });
    if (targetsDist)
      continue;
    bool conflicts = bound.width == 0 || bound.width > 64 ||
                     bound.captureIndex >= programCaptures.size() ||
                     !isPropertyField(bound.offset, bound.width);
    if (conflicts)
      continue;
    auto found = llvm::find_if(proposalCaptureDomains,
                               [&](const ProposalCaptureDomain &selected) {
                                 return selected.offset == bound.offset &&
                                        selected.width == bound.width;
                               });
    if (found != proposalCaptureDomains.end() &&
        found->isSigned != bound.isSigned)
      continue;
    if (found == proposalCaptureDomains.end()) {
      auto staticDomain =
          llvm::find_if(proposalDomains, [&](const ProposalDomain &domain) {
            return domain.offset == bound.offset && domain.width == bound.width;
          });
      if (staticDomain != proposalDomains.end() &&
          staticDomain->isSigned != bound.isSigned)
        continue;
      std::optional<uint64_t> staticLower;
      std::optional<uint64_t> staticUpper;
      if (staticDomain != proposalDomains.end()) {
        staticLower = staticDomain->lower;
        staticUpper = staticDomain->lower + staticDomain->cardinality - 1;
        proposalDomains.erase(staticDomain);
      }
      proposalCaptureDomains.push_back({bound.offset,
                                        bound.width,
                                        std::nullopt,
                                        std::nullopt,
                                        false,
                                        false,
                                        bound.isSigned,
                                        staticLower,
                                        staticUpper,
                                        {},
                                        {}});
      found = std::prev(proposalCaptureDomains.end());
    }
    bool lower = bound.kind == solver::RandomCaptureBoundKind::LowerInclusive ||
                 bound.kind == solver::RandomCaptureBoundKind::LowerExclusive;
    std::optional<uint32_t> &capture =
        lower ? found->lowerCapture : found->upperCapture;
    if (capture)
      continue;
    capture = bound.captureIndex;
    if (lower)
      found->lowerExclusive =
          bound.kind == solver::RandomCaptureBoundKind::LowerExclusive;
    else
      found->upperExclusive =
          bound.kind == solver::RandomCaptureBoundKind::UpperExclusive;
    ++materializedCaptureBounds;
  }
  for (const solver::RandomVariableAlias &alias : analysis.aliases) {
    if (!distPlans.empty())
      break;
    bool sourceIsTarget = llvm::any_of(
        analysis.aliases, [&](const solver::RandomVariableAlias &other) {
          return other.targetOffset == alias.sourceOffset;
        });
    unsigned targetCount = llvm::count_if(
        analysis.aliases, [&](const solver::RandomVariableAlias &other) {
          return other.targetOffset == alias.targetOffset;
        });
    if (alias.targetOffset != alias.sourceOffset &&
        isPropertyField(alias.targetOffset, alias.width) &&
        isPropertyField(alias.sourceOffset, alias.width) && !sourceIsTarget &&
        targetCount == 1)
      proposalAliases.push_back(
          {alias.targetOffset, alias.sourceOffset, alias.width});
  }
  if (hasSolveBefore && !proposalAliases.empty()) {
    auto solveLayerForField = [&](uint32_t fieldOffset, unsigned fieldWidth) {
      uint64_t valueMask =
          fieldWidth == 64 ? UINT64_MAX : (uint64_t{1} << fieldWidth) - 1;
      uint64_t fieldMask = valueMask << fieldOffset;
      for (auto [layerIndex, layerMask] :
           llvm::enumerate(solveBeforeLayerMasks))
        if ((fieldMask & layerMask) == fieldMask)
          return static_cast<unsigned>(layerIndex);
      return static_cast<unsigned>(solveBeforeLayerMasks.size());
    };
    SmallVector<ProposalAlias> orientedAliases;
    SmallVector<bool> consumed(proposalAliases.size(), false);
    for (unsigned index = 0; index != proposalAliases.size(); ++index) {
      if (consumed[index])
        continue;
      const ProposalAlias &first = proposalAliases[index];
      SmallVector<uint32_t> members{first.sourceOffset};
      for (unsigned other = index; other != proposalAliases.size(); ++other) {
        const ProposalAlias &alias = proposalAliases[other];
        if (alias.sourceOffset != first.sourceOffset ||
            alias.width != first.width)
          continue;
        consumed[other] = true;
        members.push_back(alias.targetOffset);
      }
      bool hasDefinition = llvm::any_of(
          analysis.definitions,
          [&](const solver::RandomVariableDefinition &definition) {
            return definition.width == first.width &&
                   llvm::is_contained(members, definition.targetOffset);
          });
      uint32_t source = first.sourceOffset;
      if (!hasDefinition)
        source = *llvm::min_element(members, [&](uint32_t lhs, uint32_t rhs) {
          return std::pair(solveLayerForField(lhs, first.width), lhs) <
                 std::pair(solveLayerForField(rhs, first.width), rhs);
        });
      for (uint32_t member : members)
        if (member != source)
          orientedAliases.push_back({member, source, first.width});
    }
    proposalAliases = std::move(orientedAliases);
  }
  llvm::erase_if(proposalDomains, [&](const ProposalDomain &domain) {
    auto alias =
        llvm::find_if(proposalAliases, [&](const ProposalAlias &candidate) {
          return candidate.targetOffset == domain.offset &&
                 candidate.width == domain.width;
        });
    if (alias == proposalAliases.end())
      return false;
    auto sourceDomain =
        llvm::find_if(proposalDomains, [&](const ProposalDomain &candidate) {
          return candidate.offset == alias->sourceOffset &&
                 candidate.width == alias->width;
        });
    return sourceDomain != proposalDomains.end() &&
           sourceDomain->lower == domain.lower &&
           sourceDomain->cardinality == domain.cardinality &&
           sourceDomain->isSigned == domain.isSigned;
  });
  auto canonicalProposalField = [&](uint32_t offset, unsigned width) {
    auto alias = llvm::find_if(proposalAliases, [&](const ProposalAlias &item) {
      return item.targetOffset == offset && item.width == width;
    });
    return alias == proposalAliases.end() ? offset : alias->sourceOffset;
  };

  auto isDefinitionUnary = [](uint8_t opcode) {
    return opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
           opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1;
  };
  auto isDefinitionBinary = [](uint8_t opcode) {
    return (opcode >= OBELISK_RT_RANDOM_ADD_V1 &&
            opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1) ||
           (opcode >= OBELISK_RT_RANDOM_DIV_V1 &&
            opcode <= OBELISK_RT_RANDOM_POWER_V1);
  };
  for (const solver::RandomVariableDefinition &definition :
       analysis.definitions) {
    if (!distPlans.empty())
      break;
    if (!isPropertyField(definition.targetOffset, definition.width) ||
        definition.expressionBegin >= definition.expressionEnd ||
        definition.expressionEnd > programInstructions.size())
      continue;
    SmallVector<unsigned> widths;
    bool supported = true;
    for (const EncodedInstruction &encoded :
         llvm::ArrayRef(programInstructions)
             .slice(definition.expressionBegin,
                    definition.expressionEnd - definition.expressionBegin)) {
      if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
        uint32_t sourceOffset =
            canonicalProposalField(encoded.operand, encoded.width);
        uint64_t variableEnd =
            static_cast<uint64_t>(sourceOffset) + encoded.width;
        uint64_t targetEnd =
            static_cast<uint64_t>(definition.targetOffset) + definition.width;
        bool overlapsTarget =
            sourceOffset < targetEnd && definition.targetOffset < variableEnd;
        if (overlapsTarget || sourceOffset >= totalWidth ||
            encoded.width > totalWidth - sourceOffset) {
          supported = false;
          break;
        }
        widths.push_back(encoded.width);
      } else if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
        if (encoded.operand >= programCaptures.size()) {
          supported = false;
          break;
        }
        widths.push_back(encoded.width);
      } else if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
        widths.push_back(encoded.width);
      } else if (isDefinitionUnary(encoded.opcode)) {
        if (widths.empty()) {
          supported = false;
          break;
        }
        widths.back() = encoded.width;
      } else if (isDefinitionBinary(encoded.opcode)) {
        if (widths.size() < 2) {
          supported = false;
          break;
        }
        widths.pop_back();
        widths.back() = encoded.width;
      } else if (encoded.opcode == OBELISK_RT_RANDOM_SELECT_V1) {
        if (widths.size() < 3) {
          supported = false;
          break;
        }
        widths.pop_back();
        widths.pop_back();
        widths.back() = encoded.width;
      } else {
        supported = false;
        break;
      }
    }
    if (supported && widths.size() == 1 && widths.front() == definition.width)
      proposalDefinitions.push_back({definition.targetOffset, definition.width,
                                     definition.expressionBegin,
                                     definition.expressionEnd});
  }

  auto selectAssignmentTable = [&](ArrayRef<APInt> assignments,
                                    Value index) -> Value {
    Value assignment = constantAssignment(assignments.front());
    for (auto [tableIndex, tableAssignment] :
         llvm::enumerate(llvm::drop_begin(assignments))) {
      Value selected =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                index, constantLike(index, tableIndex + 1));
      assignment = arith::SelectOp::create(
          builder, location, selected, constantAssignment(tableAssignment),
          assignment);
    }
    return assignment;
  };
  Value sampledSolveBeforeAssignment;
  Value sampledComponentAssignment;
  Value sampledDistAssignment;
  APInt sampledDistMask = APInt::getZero(assignmentWidth);
  auto materializeProposal =
      [&](Value rawAssignment,
          ArrayRef<Value> sampledDomainIndices) -> FailureOr<Value> {
    size_t sampledDomainIndex = 0;
    if (sampledSolveBeforeAssignment)
      return sampledSolveBeforeAssignment;
    if (!proposalAssignments.empty()) {
      if (proposalAssignments.size() == 1)
        return constantAssignment(proposalAssignments.front());
      Value index =
          powerOfTwoAssignmentTable
              ? arith::AndIOp::create(
                    builder, location, rawAssignment,
                    constantAssignment64(proposalAssignments.size() - 1))
                    .getResult()
              : arith::RemUIOp::create(builder, location, rawAssignment,
                                       constantAssignment64(
                                           proposalAssignments.size()))
                    .getResult();
      return selectAssignmentTable(proposalAssignments, index);
    }
    Value assignment = rawAssignment;
    if (sampledComponentAssignment)
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constantAssignment(
                                    ~proposalAssignmentTableMask)),
          sampledComponentAssignment);
    for (const ProposalDomain &domain : proposalDomains) {
      Value fieldBits;
      if (domain.powerOfTwo) {
        fieldBits = rawAssignment;
        if (domain.offset != 0)
          fieldBits = arith::ShRUIOp::create(builder, location, fieldBits,
                                             constantAssignment64(domain.offset));
        fieldBits = arith::AndIOp::create(builder, location, fieldBits,
                                          constantAssignment64(
                                              domain.cardinality - 1));
      } else {
        fieldBits = sampledDomainIndices[sampledDomainIndex++];
      }
      if (domain.lower != 0)
        fieldBits = arith::AddIOp::create(builder, location, fieldBits,
                                          constantLike(fieldBits, domain.lower));
      if (domain.isSigned)
        fieldBits = arith::XOrIOp::create(
            builder, location, fieldBits,
            constantLike(fieldBits,
                         uint64_t{1} << (domain.width - 1)));
      if (fieldBits.getType() != assignmentType)
        fieldBits = arith::ExtUIOp::create(builder, location, assignmentType,
                                           fieldBits);
      if (domain.offset != 0)
        fieldBits = arith::ShLIOp::create(builder, location, fieldBits,
                                          constantAssignment64(domain.offset));
      APInt fieldMask = APInt::getLowBitsSet(assignmentWidth, domain.width)
                            .shl(domain.offset);
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constantAssignment(~fieldMask)),
          fieldBits);
    }
    for (const ProposalFiniteDomain &domain : proposalFiniteDomains) {
      Value fieldIndex = sampledDomainIndices[sampledDomainIndex++];
      Value fieldBits =
          materializePropertyDomainIndex(*domain.property, fieldIndex);
      if (fieldBits.getType() != assignmentType)
        fieldBits = arith::ExtUIOp::create(builder, location, assignmentType,
                                           fieldBits);
      if (domain.offset != 0)
        fieldBits = arith::ShLIOp::create(builder, location, fieldBits,
                                          constantAssignment64(domain.offset));
      APInt fieldMask =
          APInt::getLowBitsSet(assignmentWidth, domain.property->width)
              .shl(domain.offset);
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constantAssignment(~fieldMask)),
          fieldBits);
    }
    for (const ProposalCaptureDomain &bound : proposalCaptureDomains) {
      Value fieldBits = sampledDomainIndices[sampledDomainIndex++];
      fieldBits =
          arith::AddIOp::create(builder, location, fieldBits, bound.lower);
      if (bound.isSigned)
        fieldBits =
            arith::XOrIOp::create(builder, location, fieldBits,
                                  constant64(uint64_t{1} << (bound.width - 1)));
      if (fieldBits.getType() != assignmentType)
        fieldBits = arith::ExtUIOp::create(builder, location, assignmentType,
                                           fieldBits);
      if (bound.offset != 0)
        fieldBits = arith::ShLIOp::create(builder, location, fieldBits,
                                          constantAssignment64(bound.offset));
      APInt fieldMask = APInt::getLowBitsSet(assignmentWidth, bound.width)
                            .shl(bound.offset);
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constantAssignment(~fieldMask)),
          fieldBits);
    }
    assert(sampledDomainIndex == sampledDomainIndices.size() &&
           "proposal domain sample count must match the materialized domains");
    struct DefinitionValue {
      Value bits;
      unsigned width;
    };
    auto maskDefinitionValue = [&](Value bits, unsigned width) {
      if (assignmentWidth <= 64) {
        if (width == 64)
          return bits;
        return arith::AndIOp::create(
                   builder, location, bits,
                   constant64((uint64_t{1} << width) - 1))
            .getResult();
      }
      auto inputType = cast<IntegerType>(bits.getType());
      if (inputType.getWidth() == width)
        return bits;
      auto outputType = IntegerType::get(function.getContext(), width);
      if (inputType.getWidth() > width)
        return arith::TruncIOp::create(builder, location, outputType, bits)
            .getResult();
      return arith::ExtUIOp::create(builder, location, outputType, bits)
          .getResult();
    };
    auto resizeDefinitionValue = [&](DefinitionValue input, unsigned width,
                                     bool signExtend) {
      if (assignmentWidth <= 64) {
        Value bits = input.bits;
        if (input.width < width && signExtend) {
          unsigned shift = 64 - input.width;
          bits = arith::ShLIOp::create(builder, location, bits,
                                       constant64(shift));
          bits = arith::ShRSIOp::create(builder, location, bits,
                                        constant64(shift));
        }
        return maskDefinitionValue(bits, width);
      }
      if (input.width == width)
        return input.bits;
      auto outputType = IntegerType::get(function.getContext(), width);
      if (input.width > width)
        return arith::TruncIOp::create(builder, location, outputType, input.bits)
            .getResult();
      if (signExtend)
        return arith::ExtSIOp::create(builder, location, outputType, input.bits)
            .getResult();
      return arith::ExtUIOp::create(builder, location, outputType, input.bits)
          .getResult();
    };
    auto definitionTruth = [&](Value bits) {
      return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                   bits, assignmentWidth <= 64
                                             ? constant64(0)
                                             : constantLike(bits, 0))
          .getResult();
    };
    auto definitionBooleanBits = [&](Value condition) {
      if (assignmentWidth <= 64)
        return arith::ExtUIOp::create(builder, location, i64, condition)
            .getResult();
      return condition;
    };
    auto definitionConstant = [&](unsigned width, const APInt &value) {
      if (assignmentWidth <= 64)
        return constant64(value.zextOrTrunc(64).getZExtValue());
      auto type = IntegerType::get(function.getContext(), width);
      return arith::ConstantOp::create(
                 builder, location, type,
                 builder.getIntegerAttr(type, value.zextOrTrunc(width)))
          .getResult();
    };
    for (const ProposalDefinition &definition : proposalDefinitions) {
      SmallVector<DefinitionValue> stack;
      for (const EncodedInstruction &encoded :
           llvm::ArrayRef(programInstructions)
               .slice(definition.expressionBegin,
                      definition.expressionEnd - definition.expressionBegin)) {
        bool signedOperation =
            (encoded.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
        if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
          uint32_t sourceOffset =
              canonicalProposalField(encoded.operand, encoded.width);
          Value bits = assignment;
          if (sourceOffset != 0)
            bits = arith::ShRUIOp::create(builder, location, bits,
                                          constantAssignment64(sourceOffset));
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
          stack.push_back({maskDefinitionValue(programCaptures[encoded.operand],
                                               encoded.width),
                           encoded.width});
          continue;
        }
        if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
          APInt literal = encoded.literal.value_or(
              APInt(encoded.width, encoded.immediate));
          stack.push_back({definitionConstant(encoded.width, literal),
                           encoded.width});
          continue;
        }
        if (encoded.opcode == OBELISK_RT_RANDOM_SELECT_V1) {
          if (stack.size() < 3)
            return failure();
          DefinitionValue falseValue = stack.pop_back_val();
          DefinitionValue trueValue = stack.pop_back_val();
          DefinitionValue condition = stack.pop_back_val();
          Value bits = arith::SelectOp::create(
              builder, location, definitionTruth(condition.bits),
              resizeDefinitionValue(trueValue, encoded.width, signedOperation),
              resizeDefinitionValue(falseValue, encoded.width,
                                    signedOperation));
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        if (isDefinitionUnary(encoded.opcode)) {
          if (stack.empty())
            return failure();
          DefinitionValue input = stack.pop_back_val();
          Value bits = input.bits;
          if (encoded.opcode <= OBELISK_RT_RANDOM_BIT_NOT_V1)
            bits = resizeDefinitionValue(input, encoded.width, signedOperation);
          if (encoded.opcode == OBELISK_RT_RANDOM_NEG_V1)
            bits =
                arith::SubIOp::create(builder, location, constantLike(bits, 0),
                                      bits);
          else if (encoded.opcode == OBELISK_RT_RANDOM_BIT_NOT_V1)
            bits = arith::XOrIOp::create(
                builder, location, bits,
                definitionConstant(encoded.width,
                                   APInt::getAllOnes(encoded.width)));
          else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_AND_V1 ||
                   encoded.opcode == OBELISK_RT_RANDOM_REDUCE_NAND_V1) {
            Value allOnes = definitionConstant(
                input.width, APInt::getAllOnes(input.width));
            arith::CmpIPredicate predicate =
                encoded.opcode == OBELISK_RT_RANDOM_REDUCE_AND_V1
                    ? arith::CmpIPredicate::eq
                    : arith::CmpIPredicate::ne;
            bits = definitionBooleanBits(arith::CmpIOp::create(
                builder, location, predicate, input.bits, allOnes));
          } else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_OR_V1) {
            bits = definitionBooleanBits(definitionTruth(input.bits));
          } else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_XOR_V1 ||
                     encoded.opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1) {
            bits = assignmentWidth <= 64
                       ? constant64(0)
                       : arith::ConstantOp::create(
                             builder, location, builder.getI1Type(),
                             builder.getBoolAttr(false))
                             .getResult();
            for (unsigned bit = 0; bit != input.width; ++bit) {
              Value current = input.bits;
              if (bit != 0)
                current = arith::ShRUIOp::create(builder, location, current,
                                                 constantLike(current, bit));
              current = arith::AndIOp::create(builder, location, current,
                                              constantLike(current, 1));
              bits = arith::XOrIOp::create(
                  builder, location, bits, definitionTruth(current));
            }
            if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1)
              bits = arith::XOrIOp::create(
                  builder, location, bits,
                  assignmentWidth <= 64
                      ? constant64(1)
                      : arith::ConstantOp::create(builder, location,
                                                  builder.getI1Type(),
                                                  builder.getBoolAttr(true))
                            .getResult());
          } else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_NOR_V1 ||
                     encoded.opcode == OBELISK_RT_RANDOM_LOGICAL_NOT_V1) {
            bits = definitionBooleanBits(arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::eq, input.bits,
                constantLike(input.bits, 0)));
          }
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        if (!isDefinitionBinary(encoded.opcode) || stack.size() < 2)
          return failure();
        DefinitionValue rhs = stack.pop_back_val();
        DefinitionValue lhs = stack.pop_back_val();
        if (encoded.opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1 ||
            encoded.opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_V1 ||
            encoded.opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1) {
          bool arithmeticRight =
              encoded.opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1;
          unsigned operationWidth = std::max(encoded.width, rhs.width);
          Value left = resizeDefinitionValue(lhs, operationWidth,
                                             arithmeticRight);
          Value right =
              resizeDefinitionValue(rhs, operationWidth, /*signExtend=*/false);
          Value oversized = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::uge, right,
              constantLike(right, encoded.width));
          Value safeAmount = arith::SelectOp::create(
              builder, location, oversized, constantLike(right, 0), right);
          Value shifted;
          if (encoded.opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1)
            shifted =
                arith::ShLIOp::create(builder, location, left, safeAmount);
          else if (arithmeticRight)
            shifted =
                arith::ShRSIOp::create(builder, location, left, safeAmount);
          else
            shifted =
                arith::ShRUIOp::create(builder, location, left, safeAmount);
          Value oversizedResult = constantLike(left, 0);
          if (arithmeticRight) {
            Value negative = arith::CmpIOp::create(builder, location,
                                                   arith::CmpIPredicate::slt,
                                                   left,
                                                   constantLike(left, 0));
            oversizedResult = arith::SelectOp::create(
                builder, location, negative,
                definitionConstant(operationWidth,
                                   APInt::getAllOnes(operationWidth)),
                constantLike(left, 0));
          }
          Value bits = arith::SelectOp::create(builder, location, oversized,
                                               oversizedResult, shifted);
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        if (encoded.opcode == OBELISK_RT_RANDOM_POWER_V1) {
          Value base =
              resizeDefinitionValue(lhs, encoded.width, signedOperation);
          Value bits = definitionConstant(encoded.width, APInt(encoded.width, 1));
          for (unsigned bit = 0; bit != rhs.width; ++bit) {
            Value exponentBit = rhs.bits;
            if (bit != 0)
              exponentBit = arith::ShRUIOp::create(
                  builder, location, exponentBit,
                  constantLike(exponentBit, bit));
            exponentBit = arith::AndIOp::create(builder, location, exponentBit,
                                                constantLike(exponentBit, 1));
            Value multiplied =
                arith::MulIOp::create(builder, location, bits, base);
            multiplied = maskDefinitionValue(multiplied, encoded.width);
            bits = arith::SelectOp::create(builder, location,
                                           definitionTruth(exponentBit),
                                           multiplied, bits);
            if (bit + 1 != rhs.width)
              base = maskDefinitionValue(
                  arith::MulIOp::create(builder, location, base, base),
                  encoded.width);
          }
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        bool logical = encoded.opcode >= OBELISK_RT_RANDOM_LOGICAL_AND_V1 &&
                       encoded.opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1;
        unsigned operandWidth =
            encoded.opcode >= OBELISK_RT_RANDOM_EQ_V1 &&
                    encoded.opcode <= OBELISK_RT_RANDOM_LT_V1
                ? std::max(lhs.width, rhs.width)
                : encoded.width;
        Value left =
            logical ? lhs.bits
                    : resizeDefinitionValue(lhs, operandWidth, signedOperation);
        Value right =
            logical ? rhs.bits
                    : resizeDefinitionValue(rhs, operandWidth, signedOperation);
        Value bits;
        switch (encoded.opcode) {
        case OBELISK_RT_RANDOM_ADD_V1:
          bits = arith::AddIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_SUB_V1:
          bits = arith::SubIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_MUL_V1:
          bits = arith::MulIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_DIV_V1:
        case OBELISK_RT_RANDOM_MOD_V1:
          if (signedOperation) {
            Value overflow = arith::AndIOp::create(
                builder, location,
                arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq, left,
                                      definitionConstant(
                                          operandWidth,
                                          APInt::getSignedMinValue(
                                              operandWidth))),
                arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq, right,
                                      definitionConstant(
                                          operandWidth,
                                          APInt::getAllOnes(operandWidth))));
            Value safeRight = arith::SelectOp::create(
                builder, location, overflow, constantLike(right, 1), right);
            if (encoded.opcode == OBELISK_RT_RANDOM_DIV_V1) {
              Value quotient =
                  arith::DivSIOp::create(builder, location, left, safeRight);
              bits = arith::SelectOp::create(builder, location, overflow,
                                             left, quotient);
            } else {
              Value remainder =
                  arith::RemSIOp::create(builder, location, left, safeRight);
              bits = arith::SelectOp::create(builder, location, overflow,
                                             constantLike(left, 0), remainder);
            }
          } else if (encoded.opcode == OBELISK_RT_RANDOM_DIV_V1) {
            bits = arith::DivUIOp::create(builder, location, left, right);
          } else {
            bits = arith::RemUIOp::create(builder, location, left, right);
          }
          break;
        case OBELISK_RT_RANDOM_BIT_AND_V1:
          bits = arith::AndIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_BIT_OR_V1:
          bits = arith::OrIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_BIT_XOR_V1:
          bits = arith::XOrIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_BIT_XNOR_V1:
          bits = arith::XOrIOp::create(builder, location, left, right);
          bits = arith::XOrIOp::create(
              builder, location, bits,
              definitionConstant(encoded.width,
                                 APInt::getAllOnes(encoded.width)));
          break;
        case OBELISK_RT_RANDOM_EQ_V1:
        case OBELISK_RT_RANDOM_NE_V1:
        case OBELISK_RT_RANDOM_GE_V1:
        case OBELISK_RT_RANDOM_GT_V1:
        case OBELISK_RT_RANDOM_LE_V1:
        case OBELISK_RT_RANDOM_LT_V1: {
          arith::CmpIPredicate predicate;
          switch (encoded.opcode) {
          case OBELISK_RT_RANDOM_EQ_V1:
            predicate = arith::CmpIPredicate::eq;
            break;
          case OBELISK_RT_RANDOM_NE_V1:
            predicate = arith::CmpIPredicate::ne;
            break;
          case OBELISK_RT_RANDOM_GE_V1:
            predicate = signedOperation ? arith::CmpIPredicate::sge
                                        : arith::CmpIPredicate::uge;
            break;
          case OBELISK_RT_RANDOM_GT_V1:
            predicate = signedOperation ? arith::CmpIPredicate::sgt
                                        : arith::CmpIPredicate::ugt;
            break;
          case OBELISK_RT_RANDOM_LE_V1:
            predicate = signedOperation ? arith::CmpIPredicate::sle
                                        : arith::CmpIPredicate::ule;
            break;
          case OBELISK_RT_RANDOM_LT_V1:
            predicate = signedOperation ? arith::CmpIPredicate::slt
                                        : arith::CmpIPredicate::ult;
            break;
          default:
            return failure();
          }
          bits = definitionBooleanBits(arith::CmpIOp::create(
              builder, location, predicate, left, right));
          break;
        }
        case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
        case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
        case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
        case OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1: {
          Value leftTruth = definitionTruth(lhs.bits);
          Value rightTruth = definitionTruth(rhs.bits);
          Value predicate;
          if (encoded.opcode == OBELISK_RT_RANDOM_LOGICAL_AND_V1)
            predicate =
                arith::AndIOp::create(builder, location, leftTruth, rightTruth);
          else if (encoded.opcode == OBELISK_RT_RANDOM_LOGICAL_OR_V1)
            predicate =
                arith::OrIOp::create(builder, location, leftTruth, rightTruth);
          else if (encoded.opcode == OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1) {
            Value leftFalse = arith::CmpIOp::create(builder, location,
                                                    arith::CmpIPredicate::eq,
                                                    lhs.bits,
                                                    constantLike(lhs.bits, 0));
            predicate =
                arith::OrIOp::create(builder, location, leftFalse, rightTruth);
          } else {
            predicate = arith::CmpIOp::create(builder, location,
                                              arith::CmpIPredicate::eq,
                                              leftTruth, rightTruth);
          }
          bits = definitionBooleanBits(predicate);
          break;
        }
        default:
          return failure();
        }
        stack.push_back(
            {maskDefinitionValue(bits, encoded.width), encoded.width});
      }
      if (stack.size() != 1 || stack.back().width != definition.width)
        return failure();
      Value fieldBits = stack.back().bits;
      if (fieldBits.getType() != assignmentType)
        fieldBits = arith::ExtUIOp::create(builder, location, assignmentType,
                                           fieldBits);
      if (definition.targetOffset != 0)
        fieldBits = arith::ShLIOp::create(builder, location, fieldBits,
                                          constantAssignment64(
                                              definition.targetOffset));
      APInt targetMask =
          APInt::getLowBitsSet(assignmentWidth, definition.width)
              .shl(definition.targetOffset);
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constantAssignment(~targetMask)),
          fieldBits);
    }
    SmallVector<std::tuple<uint32_t, unsigned, Value>> aliasSources;
    for (const ProposalAlias &alias : proposalAliases) {
      auto cachedSource = llvm::find_if(aliasSources, [&](const auto &source) {
        return std::get<0>(source) == alias.sourceOffset &&
               std::get<1>(source) == alias.width;
      });
      Value sourceBits;
      if (cachedSource != aliasSources.end()) {
        sourceBits = std::get<2>(*cachedSource);
      } else {
        sourceBits = assignment;
        if (alias.sourceOffset != 0)
          sourceBits = arith::ShRUIOp::create(builder, location, sourceBits,
                                              constantAssignment64(
                                                  alias.sourceOffset));
        sourceBits = maskDefinitionValue(sourceBits, alias.width);
        aliasSources.emplace_back(alias.sourceOffset, alias.width, sourceBits);
      }
      if (sourceBits.getType() != assignmentType)
        sourceBits = arith::ExtUIOp::create(builder, location, assignmentType,
                                            sourceBits);
      if (alias.targetOffset != 0)
        sourceBits = arith::ShLIOp::create(builder, location, sourceBits,
                                           constantAssignment64(
                                               alias.targetOffset));
      APInt targetMask = APInt::getLowBitsSet(assignmentWidth, alias.width)
                             .shl(alias.targetOffset);
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constantAssignment(~targetMask)),
          sourceBits);
    }
    if (sampledDistAssignment)
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constantAssignment(
                                    ~sampledDistMask)),
          sampledDistAssignment);
    return assignment;
  };
  auto overwritesFiniteDomain = [&](const ProposalFiniteDomain &domain) {
    bool definitionTarget = llvm::any_of(
        proposalDefinitions, [&](const ProposalDefinition &definition) {
          return definition.targetOffset == domain.offset &&
                 definition.width == domain.property->width;
        });
    bool aliasTarget =
        llvm::any_of(proposalAliases, [&](const ProposalAlias &alias) {
          return alias.targetOffset == domain.offset &&
                 alias.width == domain.property->width;
        });
    return definitionTarget || aliasTarget;
  };
  bool overwritesProposalDomain =
      llvm::any_of(proposalDomains,
                   [&](const ProposalDomain &domain) {
                     bool definitionTarget = llvm::any_of(
                         proposalDefinitions,
                         [&](const ProposalDefinition &definition) {
                           return definition.targetOffset == domain.offset &&
                                  definition.width == domain.width;
                         });
                     auto alias = llvm::find_if(
                         proposalAliases, [&](const ProposalAlias &candidate) {
                           return candidate.targetOffset == domain.offset &&
                                  candidate.width == domain.width;
                         });
                     if (alias == proposalAliases.end())
                       return definitionTarget;
                     auto sourceDomain = llvm::find_if(
                         proposalDomains, [&](const ProposalDomain &candidate) {
                           return candidate.offset == alias->sourceOffset &&
                                  candidate.width == alias->width;
                         });
                     bool sameAliasDomain =
                         sourceDomain != proposalDomains.end() &&
                         sourceDomain->lower == domain.lower &&
                         sourceDomain->cardinality == domain.cardinality &&
                         sourceDomain->isSigned == domain.isSigned;
                     return definitionTarget || !sameAliasDomain;
                   }) ||
      llvm::any_of(proposalFiniteDomains, overwritesFiniteDomain) ||
      llvm::any_of(
          proposalCaptureDomains, [&](const ProposalCaptureDomain &bound) {
            bool definitionTarget = llvm::any_of(
                proposalDefinitions, [&](const ProposalDefinition &definition) {
                  return definition.targetOffset == bound.offset &&
                         definition.width == bound.width;
                });
            bool aliasTarget =
                llvm::any_of(proposalAliases, [&](const ProposalAlias &alias) {
                  return alias.targetOffset == bound.offset &&
                         alias.width == bound.width;
                });
            return definitionTarget || aliasTarget;
          });
  bool tableProposal = !analysis.assignmentTable.empty();
  bool componentTableProposal = !analysis.assignmentTables.empty();
  bool materializesCompleteProposal =
      tableProposal
          ? proposalAssignments.size() == analysis.assignmentTable.size()
      : componentTableProposal
          ? proposalAssignmentTables.size() ==
                    analysis.assignmentTables.size() &&
                materializedDomains == analysis.domains.size() &&
                materializedCaptureBounds == analysis.captureBounds.size() &&
                proposalAliases.size() == analysis.aliases.size() &&
                proposalDefinitions.size() == analysis.definitions.size()
          : materializedDomains == analysis.domains.size() &&
                materializedCaptureBounds == analysis.captureBounds.size() &&
                proposalAliases.size() == analysis.aliases.size() &&
                proposalDefinitions.size() == analysis.definitions.size();
  bool exactProposal = !hasRuntimeForeachConstraint &&
                       analysis.proposalExact && softProposalExact &&
                       !hasRandC && distPlans.empty() &&
                       !overwritesProposalDomain &&
                       materializesCompleteProposal;

  // A closed definition has no randomized or captured inputs, so applying it
  // after a solver-derived target interval cannot invalidate that interval.
  // This common constant-expression case is complete even though the generic
  // overwrite check above must remain conservative for dependent definitions.
  auto isClosedDefinition = [&](const ProposalDefinition &definition) {
    return llvm::none_of(
        llvm::ArrayRef(programInstructions)
            .slice(definition.expressionBegin,
                   definition.expressionEnd - definition.expressionBegin),
        [](const EncodedInstruction &encoded) {
          return encoded.opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1 ||
                 encoded.opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1 ||
                 encoded.opcode == OBELISK_RT_RANDOM_DIV_V1 ||
                 encoded.opcode == OBELISK_RT_RANDOM_MOD_V1;
        });
  };
  bool closedDefinitionProposal =
      analysis.proposalExact && softProposalExact && !hasRandC &&
      distPlans.empty() && materializesCompleteProposal &&
      !proposalDefinitions.empty() && proposalAliases.empty() &&
      proposalFiniteDomains.empty() && proposalCaptureDomains.empty() &&
      llvm::all_of(proposalDefinitions, isClosedDefinition);
  exactProposal |= closedDefinitionProposal;
  // Mode changes can reach the residual block even when the default-mode
  // proposal is exact. Reject ordered or weighted metadata that the wide
  // runtime cannot interpret instead of generating a latent failure. Finite
  // semantic domains use the same versioned mixed-radix traversal as the
  // narrow residual solver.
  if (wideProgram && (hasSolveBefore || !distPlans.empty())) {
    emitError(location)
        << "wide residual random fallback does not yet support solve before "
           "or dist";
    return failure();
  }

  auto structuralProposalFollowsSolveOrder = [&]() {
    auto propertyIndexForField = [&](uint32_t fieldOffset,
                                     unsigned fieldWidth)
        -> std::optional<unsigned> {
      uint32_t offset = 0;
      for (auto [index, property] : llvm::enumerate(planned)) {
        if (offset == fieldOffset && property.width == fieldWidth)
          return index;
        offset += property.width;
      }
      return std::nullopt;
    };

    SmallVector<uint64_t> dependencies(planned.size(), 0);
    for (const ProposalAlias &alias : proposalAliases) {
      std::optional<unsigned> target =
          propertyIndexForField(alias.targetOffset, alias.width);
      std::optional<unsigned> source =
          propertyIndexForField(alias.sourceOffset, alias.width);
      if (!target || !source)
        return false;
      dependencies[*target] |= uint64_t{1} << *source;
    }
    for (const ProposalDefinition &definition : proposalDefinitions) {
      std::optional<unsigned> target =
          propertyIndexForField(definition.targetOffset, definition.width);
      if (!target)
        return false;
      for (const EncodedInstruction &encoded :
           llvm::ArrayRef(programInstructions)
               .slice(definition.expressionBegin,
                      definition.expressionEnd - definition.expressionBegin)) {
        if (encoded.opcode != OBELISK_RT_RANDOM_PUSH_VARIABLE_V1)
          continue;
        uint32_t sourceOffset =
            canonicalProposalField(encoded.operand, encoded.width);
        std::optional<unsigned> source =
            propertyIndexForField(sourceOffset, encoded.width);
        if (!source)
          return false;
        dependencies[*target] |= uint64_t{1} << *source;
      }
    }

    // Compute transitive property dependencies so an unordered intermediate
    // cannot hide a later-to-earlier solve dependency.
    for (unsigned intermediate = 0; intermediate != planned.size();
         ++intermediate) {
      uint64_t intermediateBit = uint64_t{1} << intermediate;
      for (uint64_t &propertyDependencies : dependencies)
        if ((propertyDependencies & intermediateBit) != 0)
          propertyDependencies |= dependencies[intermediate];
    }

    SmallVector<std::optional<unsigned>> propertyLayers(planned.size());
    for (auto [property, propertyMask] : llvm::enumerate(propertyMasks)) {
      std::optional<unsigned> selectedLayer;
      uint64_t orderedBits = 0;
      for (auto [layerIndex, layerMask] :
           llvm::enumerate(solveBeforeLayerMasks)) {
        uint64_t bits = propertyMask & layerMask;
        if (bits == 0)
          continue;
        orderedBits |= bits;
        if (selectedLayer && *selectedLayer != layerIndex)
          return false;
        selectedLayer = layerIndex;
      }
      // Generated structural definitions and aliases operate on whole
      // properties. A path-level order that covers only part of a property
      // must stay in the bit-mask-aware table or residual runtime paths.
      if (orderedBits != 0 && orderedBits != propertyMask)
        return false;
      propertyLayers[property] = selectedLayer;
    }
    for (unsigned target = 0; target != planned.size(); ++target) {
      if (!propertyLayers[target])
        continue;
      for (unsigned source = 0; source != planned.size(); ++source) {
        if ((dependencies[target] & (uint64_t{1} << source)) == 0 ||
            !propertyLayers[source])
          continue;
        if (*propertyLayers[source] > *propertyLayers[target])
          return false;
      }
    }
    return true;
  };

  bool solveBeforeRequiresRuntime = hasSolveBefore && !softProposalExact;
  if (hasSolveBefore && !solveBeforeTableRoot &&
      analysis.satisfiability != solver::Satisfiability::Unsatisfiable) {
    uint64_t orderedPropertyMask = 0;
    for (uint64_t layerMask : solveBeforeLayerMasks)
      orderedPropertyMask |= layerMask;

    uint64_t uncoveredOrderedMask =
        orderedPropertyMask & ~proposalAssignmentTableMask.getZExtValue();
    if (uncoveredOrderedMask != 0) {
      // This also rejects a path-level order that covers only part of a
      // property. Whole-property aliases and definitions cannot preserve that
      // distribution even when component partitioning happens not to expose a
      // cross-layer component.
      bool validStructuralOrder = structuralProposalFollowsSolveOrder();
      if (!exactProposal || !analysis.hasConstraintComponentPartition ||
          !validStructuralOrder)
        solveBeforeRequiresRuntime = true;
    }
    if (!solveBeforeRequiresRuntime && validAssignmentTables)
      for (const ProposalAssignmentTable &table : proposalAssignmentTables)
        solveBeforeComponentTableRoots.push_back(
            buildSolveBeforeTable(table.assignments, 0));
  }

  if (solveBeforeRequiresRuntime && !completeFallbackDomain) {
    emitError(location)
        << "solve before residual fallback requires exhaustive traversal of "
           "at most 2^20 semantic assignments; compile-time planning could "
           "not preserve the ordered distribution";
    return failure();
  }

  // An ordered residual solve must start from the single aggregate draw and
  // evaluate the original formula. Do not speculatively consume draws for a
  // compile-time proposal that this path cannot use, or reject a dynamic
  // capture bound before the residual solver sees the complete relation.
  if (solveBeforeRequiresRuntime) {
    solveBeforeTableRoot.reset();
    solveBeforeComponentTableRoots.clear();
    validAssignmentTable = false;
    validAssignmentTables = false;
    proposalDomains.clear();
    proposalCaptureDomains.clear();
  }

  // The compiler plan is proven for the all-enabled constraint set. If a
  // block is disabled at runtime, bypass plan-specific bounds, tables, and
  // extra draws; start a plain masked-domain search from the one object-stream
  // draw already consumed above. This avoids accidentally retaining the
  // distribution or failure conditions of a disabled constraint.
  Block *disabledCheckBlock = addBlock();
  Block *enabledSamplingBlock = addBlock();
  cf::CondBranchOp::create(builder, location, allPropertiesDisabled,
                           disabledCheckBlock, ValueRange{},
                           enabledSamplingBlock, ValueRange{});
  setCurrent(enabledSamplingBlock);
  Value randomDraw = nextAssignment(state);
  Value start = arith::OrIOp::create(
      builder, location,
      arith::AndIOp::create(builder, location, randomDraw, mutableMask),
      fixedAssignment);
  Value modeStart = start;
  Value modeState = state;
  Block *modeSamplingDispatchBlock = addBlock();
  Block *plannedSamplingBlock = addBlock();
  Value usePlannedSampling = allConstraintsEnabled;
  if (hasFiniteDomains && !distPlans.empty()) {
    usePlannedSampling = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
  } else if (hasSolveBefore || !distPlans.empty() || hasFiniteDomains ||
             validAssignmentTable || validAssignmentTables ||
             !proposalDomains.empty() || !proposalFiniteDomains.empty() ||
             !proposalCaptureDomains.empty() || !proposalAliases.empty() ||
             !proposalDefinitions.empty()) {
    // A compile-time proposal is checked as one aggregate assignment. With a
    // partial rand_mode only the enabled fields would be committed, so the
    // resulting object need not be the assignment that satisfied the check.
    // Use the masked residual path unless every planned property is enabled.
    usePlannedSampling = arith::AndIOp::create(
        builder, location, usePlannedSampling, randomizationEnabled);
  }
  cf::CondBranchOp::create(builder, location, usePlannedSampling,
                           plannedSamplingBlock, ValueRange{},
                           modeSamplingDispatchBlock, ValueRange{});
  setCurrent(plannedSamplingBlock);

  auto sampleBoundedIndex = [&](uint64_t cardinality, Value draw,
                                Value &streamState) -> Value {
    if ((cardinality & (cardinality - 1)) == 0)
      return arith::AndIOp::create(builder, location, draw,
                                   constant64(cardinality - 1));
    uint64_t remainder = ((UINT64_MAX % cardinality) + 1) % cardinality;
    uint64_t limit = UINT64_MAX - (remainder - 1);
    Block *boundedLoop = addBlock();
    Block *boundedDone = addBlock();
    Value boundedState = boundedLoop->addArgument(i64, location);
    Value boundedDraw = boundedLoop->addArgument(i64, location);
    Value finalState = boundedDone->addArgument(i64, location);
    Value boundedIndex = boundedDone->addArgument(i64, location);

    cf::BranchOp::create(builder, location, boundedLoop,
                         ValueRange{streamState, draw});
    setCurrent(boundedLoop);
    Value index = arith::RemUIOp::create(builder, location, boundedDraw,
                                         constant64(cardinality));
    Value accepted =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                              boundedDraw, constant64(limit));
    Value retryState = boundedState;
    Value retryDraw = next64(retryState);
    cf::CondBranchOp::create(builder, location, accepted, boundedDone,
                             ValueRange{boundedState, index}, boundedLoop,
                             ValueRange{retryState, retryDraw});

    setCurrent(boundedDone);
    streamState = finalState;
    return boundedIndex;
  };

  auto sampleDynamicBoundedIndex = [&](Value cardinality, Value draw,
                                       Value &streamState) -> Value {
    // A zero cardinality represents the full 2^64-element domain. Use one as
    // a safe modulo divisor in that case, then select the original draw as the
    // index. Otherwise, rejecting values at or above the unsigned limit
    // removes the short tail of modulo buckets.
    Value fullCardinality =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              cardinality, constant64(0));
    Value safeCardinality = arith::SelectOp::create(
        builder, location, fullCardinality, constant64(1), cardinality);
    Value negativeCardinality = arith::SubIOp::create(
        builder, location, constant64(0), safeCardinality);
    Value rejectionSize = arith::RemUIOp::create(
        builder, location, negativeCardinality, safeCardinality);
    Value limit =
        arith::SubIOp::create(builder, location, constant64(0), rejectionSize);
    Value acceptsAll =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              rejectionSize, constant64(0));
    Block *boundedLoop = addBlock();
    Block *boundedDone = addBlock();
    Value boundedState = boundedLoop->addArgument(i64, location);
    Value boundedDraw = boundedLoop->addArgument(i64, location);
    Value finalState = boundedDone->addArgument(i64, location);
    Value boundedIndex = boundedDone->addArgument(i64, location);

    cf::BranchOp::create(builder, location, boundedLoop,
                         ValueRange{streamState, draw});
    setCurrent(boundedLoop);
    Value reducedIndex =
        arith::RemUIOp::create(builder, location, boundedDraw, safeCardinality);
    Value index = arith::SelectOp::create(builder, location, fullCardinality,
                                          boundedDraw, reducedIndex);
    Value belowLimit = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, boundedDraw, limit);
    Value accepted =
        arith::OrIOp::create(builder, location, acceptsAll, belowLimit);
    Value retryState = boundedState;
    Value retryDraw = next64(retryState);
    cf::CondBranchOp::create(builder, location, accepted, boundedDone,
                             ValueRange{boundedState, index}, boundedLoop,
                             ValueRange{retryState, retryDraw});

    setCurrent(boundedDone);
    streamState = finalState;
    return boundedIndex;
  };

  struct MaterializedDistPlan {
    const DistPlan *plan;
    SmallVector<Value> weights;
    Value totalWeight;
  };
  SmallVector<MaterializedDistPlan> materializedDistPlans;
  Value distWeightsValid;
  auto requireValidDistWeights = [&](Value valid) {
    distWeightsValid = distWeightsValid
                           ? arith::AndIOp::create(builder, location,
                                                   distWeightsValid, valid)
                                 .getResult()
                           : valid;
  };
  for (const DistPlan &plan : distPlans) {
    MaterializedDistPlan materialized{&plan, {}, constant64(0)};
    for (const DistRangePlan &range : plan.ranges) {
      if (range.weightSigned)
        requireValidDistWeights(arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::sge, range.weight,
            constant64(0)));
      Value zero = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, range.weight,
          constant64(0));
      Value safeWeight = arith::SelectOp::create(
          builder, location, zero, constant64(1), range.weight);
      Value scaled = arith::MulIOp::create(
          builder, location, range.weight,
          constant64(range.selectionCoefficient));
      Value recovered = arith::DivUIOp::create(builder, location, scaled,
                                                safeWeight);
      Value productValid = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, recovered,
          constant64(range.selectionCoefficient));
      productValid = arith::OrIOp::create(builder, location, zero, productValid);
      requireValidDistWeights(productValid);
      Value updated = arith::AddIOp::create(builder, location,
                                             materialized.totalWeight, scaled);
      Value sumValid = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::uge, updated,
          materialized.totalWeight);
      requireValidDistWeights(sumValid);
      materialized.totalWeight = updated;
      materialized.weights.push_back(scaled);
    }
    requireValidDistWeights(arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne,
        materialized.totalWeight, constant64(0)));
    materializedDistPlans.push_back(std::move(materialized));
  }

  Block *invalidDistWeightsBlock = nullptr;
  Value invalidDistWeightsState;
  if (distWeightsValid) {
    invalidDistWeightsState = state;
    Block *validDistWeightsBlock = addBlock();
    invalidDistWeightsBlock = addBlock();
    cf::CondBranchOp::create(builder, location, distWeightsValid,
                             validDistWeightsBlock, ValueRange{},
                             invalidDistWeightsBlock, ValueRange{});
    setCurrent(validDistWeightsBlock);
  }

  if (!materializedDistPlans.empty()) {
    sampledDistAssignment = constantAssignment64(0);
    for (const MaterializedDistPlan &materialized : materializedDistPlans) {
      Value choice = sampleDynamicBoundedIndex(
          materialized.totalWeight, next64(state), state);
      const DistRangePlan *first = &materialized.plan->ranges.front();
      Value selectedLower = constant64(first->lower);
      Value selectedCardinality = constant64(first->cardinality);
      Value cumulative = materialized.weights.front();
      for (size_t index = 1; index != materialized.plan->ranges.size();
           ++index) {
        const DistRangePlan &range = materialized.plan->ranges[index];
        Value selected = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::uge, choice, cumulative);
        selectedLower = arith::SelectOp::create(
            builder, location, selected, constant64(range.lower),
            selectedLower);
        selectedCardinality = arith::SelectOp::create(
            builder, location, selected, constant64(range.cardinality),
            selectedCardinality);
        cumulative = arith::AddIOp::create(
            builder, location, cumulative, materialized.weights[index]);
      }
      Value field = arith::AddIOp::create(
          builder, location, selectedLower,
          sampleDynamicBoundedIndex(selectedCardinality, next64(state),
                                    state));
      const Property &property =
          planned[materialized.plan->propertyIndex];
      if (property.isSigned)
        field = arith::XOrIOp::create(
            builder, location, field,
            constant64(uint64_t{1} << (property.width - 1)));
      uint64_t localFieldMask = property.width == 64
                                    ? UINT64_MAX
                                    : (uint64_t{1} << property.width) - 1;
      field = arith::AndIOp::create(builder, location, field,
                                    constant64(localFieldMask));
      if (field.getType() != assignmentType)
        field = arith::ExtUIOp::create(builder, location, assignmentType, field);
      if (materialized.plan->propertyOffset != 0) {
        field = arith::ShLIOp::create(
            builder, location, field,
            constantAssignment64(materialized.plan->propertyOffset));
      }
      APInt fieldMask = APInt::getLowBitsSet(assignmentWidth, property.width)
                            .shl(materialized.plan->propertyOffset);
      sampledDistAssignment = arith::OrIOp::create(
          builder, location, sampledDistAssignment, field);
      sampledDistMask |= fieldMask;
    }
  }

  Value captureBoundsValid;
  auto requireValidCaptureBounds = [&](Value valid) {
    captureBoundsValid = captureBoundsValid
                             ? arith::AndIOp::create(builder, location,
                                                     captureBoundsValid, valid)
                                   .getResult()
                             : valid;
  };
  for (ProposalCaptureDomain &domain : proposalCaptureDomains) {
    uint64_t valueMask =
        domain.width == 64 ? UINT64_MAX : (uint64_t{1} << domain.width) - 1;
    auto captureValue = [&](uint32_t index) {
      Value value = arith::AndIOp::create(
          builder, location, programCaptures[index], constant64(valueMask));
      if (domain.isSigned)
        value = arith::XOrIOp::create(
            builder, location, value,
            constant64(uint64_t{1} << (domain.width - 1)));
      return value;
    };
    domain.lower = constant64(domain.staticLower.value_or(0));
    Value upper = constant64(domain.staticUpper.value_or(valueMask));
    if (domain.lowerCapture) {
      Value capturedLower = captureValue(*domain.lowerCapture);
      if (domain.lowerExclusive) {
        requireValidCaptureBounds(
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  capturedLower, constant64(valueMask)));
        capturedLower = arith::AddIOp::create(builder, location, capturedLower,
                                              constant64(1));
      }
      domain.lower = domain.staticLower
                         ? arith::MaxUIOp::create(builder, location,
                                                  domain.lower, capturedLower)
                               .getResult()
                         : capturedLower;
    }
    if (domain.upperCapture) {
      Value capturedUpper = captureValue(*domain.upperCapture);
      if (domain.upperExclusive) {
        requireValidCaptureBounds(
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  capturedUpper, constant64(0)));
        capturedUpper = arith::SubIOp::create(builder, location, capturedUpper,
                                              constant64(1));
      }
      upper = domain.staticUpper ? arith::MinUIOp::create(builder, location,
                                                          upper, capturedUpper)
                                       .getResult()
                                 : capturedUpper;
    }
    if ((domain.lowerCapture && domain.upperCapture) || domain.staticLower ||
        domain.staticUpper) {
      requireValidCaptureBounds(arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, domain.lower, upper));
    }
    domain.cardinality = arith::AddIOp::create(
        builder, location,
        arith::SubIOp::create(builder, location, upper, domain.lower),
        constant64(1));
  }

  Block *invalidCaptureBoundsBlock = nullptr;
  Value invalidCaptureBoundsState;
  if (captureBoundsValid) {
    invalidCaptureBoundsState = state;
    Block *validCaptureBoundsBlock = addBlock();
    invalidCaptureBoundsBlock = addBlock();
    cf::CondBranchOp::create(builder, location, captureBoundsValid,
                             validCaptureBoundsBlock, ValueRange{},
                             invalidCaptureBoundsBlock, ValueRange{});
    setCurrent(validCaptureBoundsBlock);
  }

  std::function<Value(unsigned, std::optional<Value>)> sampleSolveTable;
  if (solveBeforeTableRoot || !solveBeforeComponentTableRoots.empty()) {
    sampleSolveTable = [&](unsigned nodeIndex,
                           std::optional<Value> availableDraw) -> Value {
      const SolveBeforeTableNode &node = solveBeforeTableNodes[nodeIndex];
      if (node.children.empty()) {
        if (node.assignments.size() == 1)
          return constantAssignment(node.assignments.front());
        Value draw = availableDraw ? *availableDraw : next64(state);
        Value index =
            sampleBoundedIndex(node.assignments.size(), draw, state);
        return selectAssignmentTable(node.assignments, index);
      }
      if (node.children.size() == 1)
        return sampleSolveTable(node.children.front(), availableDraw);

      Value draw = availableDraw ? *availableDraw : next64(state);
      Value index = sampleBoundedIndex(node.children.size(), draw, state);
      Value branchState = state;
      Block *merge = addBlock();
      Value mergedState = merge->addArgument(i64, location);
      Value mergedAssignment = merge->addArgument(assignmentType, location);
      SmallVector<Block *> branches;
      for (size_t child = 0; child != node.children.size(); ++child)
        branches.push_back(addBlock());

      for (size_t child = 0; child + 1 != node.children.size(); ++child) {
        Block *nextDispatch = addBlock();
        Value selected =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  index, constant64(child));
        cf::CondBranchOp::create(builder, location, selected, branches[child],
                                 ValueRange{}, nextDispatch, ValueRange{});
        setCurrent(nextDispatch);
      }
      cf::BranchOp::create(builder, location, branches.back(), ValueRange{});

      for (auto [child, branch] : llvm::enumerate(branches)) {
        setCurrent(branch);
        state = branchState;
        Value assignment = sampleSolveTable(node.children[child], std::nullopt);
        cf::BranchOp::create(builder, location, merge,
                             ValueRange{state, assignment});
      }
      setCurrent(merge);
      state = mergedState;
      return mergedAssignment;
    };
  }
  if (solveBeforeTableRoot) {
    sampledSolveBeforeAssignment =
        sampleSolveTable(*solveBeforeTableRoot, randomDraw);
  }

  if (validAssignmentTable && !solveBeforeTableRoot &&
      !powerOfTwoAssignmentTable) {
    Value tableDraw =
        assignmentType == i64
            ? randomDraw
            : arith::TruncIOp::create(builder, location, i64, randomDraw)
                  .getResult();
    Value boundedIndex =
        sampleBoundedIndex(proposalAssignments.size(), tableDraw, state);
    start = assignmentType == i64
                ? boundedIndex
                : arith::ExtUIOp::create(builder, location, assignmentType,
                                         boundedIndex)
                      .getResult();
  }

  if (validAssignmentTables) {
    sampledComponentAssignment = constantAssignment64(0);
    if (!solveBeforeComponentTableRoots.empty()) {
      for (unsigned root : solveBeforeComponentTableRoots) {
        Value selected = sampleSolveTable(root, std::nullopt);
        sampledComponentAssignment = arith::OrIOp::create(
            builder, location, sampledComponentAssignment, selected);
      }
    } else {
      for (const ProposalAssignmentTable &table : proposalAssignmentTables) {
        Value index = constant64(0);
        if (table.assignments.size() != 1) {
          Value draw = next64(state);
          index = sampleBoundedIndex(table.assignments.size(), draw, state);
        }
        Value selected = selectAssignmentTable(table.assignments, index);
        sampledComponentAssignment = arith::OrIOp::create(
            builder, location, sampledComponentAssignment, selected);
      }
    }
  }
  auto sampleProposalDomainIndices = [&](Value &streamState) {
    SmallVector<Value> sampledIndices;
    for (const ProposalDomain &domain : proposalDomains) {
      if (domain.powerOfTwo)
        continue;
      Value draw = next64(streamState);
      sampledIndices.push_back(
          sampleBoundedIndex(domain.cardinality, draw, streamState));
    }
    for (const ProposalFiniteDomain &domain : proposalFiniteDomains) {
      Value draw = next64(streamState);
      sampledIndices.push_back(
          domain.cardinality == 0
              ? draw
              : sampleBoundedIndex(domain.cardinality, draw, streamState));
    }
    for (const ProposalCaptureDomain &bound : proposalCaptureDomains) {
      Value draw = next64(streamState);
      sampledIndices.push_back(sampleDynamicBoundedIndex(
          bound.cardinality, draw, streamState));
    }
    return sampledIndices;
  };
  SmallVector<Value> sampledDomainIndices =
      sampleProposalDomainIndices(state);
  sim::SimManagedStoreOp::create(builder, location, state, stateReference);

  Block *dispatchBlock = current;
  Block *loop = addBlock();
  Block *advance = addBlock();
  Block *fallbackBlock = addBlock();
  Block *modeLoop = addBlock();
  Block *modeAdvance = addBlock();
  Block *modeFallbackBlock = addBlock();
  Block *commit = addBlock();
  Block *postBlock = addBlock();
  Block *failedBlock = addBlock();
  Block *done = addBlock();
  Block *singleRandCLoop = nullptr;
  Block *singleRandCAdvance = nullptr;
  Block *singleRandCCommit = nullptr;
  if (!checkerOnly && planned.size() == 1 && planned.front().isRandC) {
    singleRandCLoop = addBlock();
    singleRandCAdvance = addBlock();
    singleRandCCommit = addBlock();
  }
  Value counter = loop->addArgument(assignmentType, location);
  Value attempt = loop->addArgument(i64, location);
  SmallVector<Value> loopSampledDomainIndices;
  loopSampledDomainIndices.reserve(sampledDomainIndices.size());
  for (size_t index = 0; index != sampledDomainIndices.size(); ++index)
    loopSampledDomainIndices.push_back(loop->addArgument(i64, location));
  Value fallbackStart = fallbackBlock->addArgument(assignmentType, location);
  Value modeCounter = modeLoop->addArgument(assignmentType, location);
  Value modeAttempt = modeLoop->addArgument(i64, location);
  Value modeFallbackStart =
      modeFallbackBlock->addArgument(assignmentType, location);
  Value commitCounter = commit->addArgument(assignmentType, location);
  Value doneResult = done->addArgument(builder.getI1Type(), location);

  if (invalidCaptureBoundsBlock) {
    setCurrent(invalidCaptureBoundsBlock);
    sim::SimManagedStoreOp::create(builder, location, invalidCaptureBoundsState,
                                   stateReference);
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  }
  if (invalidDistWeightsBlock) {
    setCurrent(invalidDistWeightsBlock);
    sim::SimManagedStoreOp::create(builder, location, invalidDistWeightsState,
                                   stateReference);
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  }

  auto materializeCandidates =
      [&](Value assignment) -> FailureOr<SmallVector<Value>> {
    SmallVector<Value> candidates;
    unsigned offset = 0;
    for (const Property &property : planned) {
      Value bits = assignment;
      if (offset != 0)
        bits =
            arith::ShRUIOp::create(builder, location, bits,
                                   constantAssignment64(offset));
      Type integerType =
          IntegerType::get(function.getContext(), property.width);
      if (property.width != assignmentWidth)
        bits = arith::TruncIOp::create(builder, location, integerType, bits);
      FailureOr<Value> converted =
          convert(bits, property.type, false, location, property.isSigned);
      if (failed(converted))
        return failure();
      candidates.push_back(*converted);
      offset += property.width;
    }
    return candidates;
  };

  struct ConstraintCheck {
    Value hard;
    Value preferred;
  };
  auto materializeConstraintCheck =
      [&](Value assignment) -> FailureOr<ConstraintCheck> {
    FailureOr<SmallVector<Value>> candidates =
        materializeCandidates(assignment);
    if (failed(candidates))
      return failure();
    Value savedThis = thisObject;
    SmallVector<Value> savedCandidates = std::move(randomizeCandidateValues);
    llvm::scope_exit restoreBindings([&] {
      thisObject = savedThis;
      randomizeCandidateValues = std::move(savedCandidates);
    });
    thisObject = receiver;
    randomizeCandidateValues = *candidates;

    Value softSatisfied;
    if (hasSoftConstraint)
      softSatisfied = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));

    std::function<FailureOr<Value>(Operation *, Operation *)> lowerConstraint =
        [&](Operation *constraint,
            Operation *softTarget) -> FailureOr<Value> {
      SmallVector<Operation *> nested = getChildren(constraint);
      if (isa<semantic::SVConstraintListOp>(constraint)) {
        Value result = arith::ConstantOp::create(
            builder, getSemanticLocation(constraint), builder.getI1Type(),
            builder.getBoolAttr(true));
        for (Operation *item : nested) {
          FailureOr<Value> itemResult = lowerConstraint(item, softTarget);
          if (failed(itemResult))
            return failure();
          result = arith::AndIOp::create(builder, getSemanticLocation(item),
                                         result, *itemResult);
        }
        return result;
      }
      if (isa<semantic::SVSolveBeforeConstraintOp>(constraint))
        return arith::ConstantOp::create(
                   builder, getSemanticLocation(constraint),
                   builder.getI1Type(), builder.getBoolAttr(true))
            .getResult();
      if (auto expression =
              dyn_cast<semantic::SVExpressionConstraintOp>(constraint)) {
        bool selected = softTarget ? softTarget == constraint
                                   : !expression.getIsSoft();
        if (!selected)
          return arith::ConstantOp::create(
                     builder, getSemanticLocation(constraint),
                     builder.getI1Type(), builder.getBoolAttr(true))
              .getResult();
        if (nested.size() != 1) {
          emitError(getSemanticLocation(constraint))
              << "expression constraint does not contain one predicate";
          return failure();
        }
        if (isa<semantic::SVDistExpressionOp>(nested.front())) {
          auto plan = llvm::find_if(distPlans, [&](const DistPlan &candidate) {
            return candidate.source == nested.front();
          });
          if (plan == distPlans.end() ||
              plan->propertyIndex >= randomizeCandidateValues.size()) {
            emitError(getSemanticLocation(nested.front()))
                << "dist constraint has no frozen weighted plan";
            return failure();
          }
          const Property &property = planned[plan->propertyIndex];
          FailureOr<Value> scalar = toPackedScalar(
              randomizeCandidateValues[plan->propertyIndex],
              getSemanticLocation(nested.front()));
          FailureOr<Value> extended =
              succeeded(scalar)
                  ? convert(*scalar, i64, false,
                            getSemanticLocation(nested.front()), false)
                  : FailureOr<Value>(failure());
          if (failed(extended))
            return failure();
          Value field = *extended;
          uint64_t valueMask = property.width == 64
                                   ? UINT64_MAX
                                   : (uint64_t{1} << property.width) - 1;
          field = arith::AndIOp::create(builder, location, field,
                                        constant64(valueMask));
          if (property.isSigned)
            field = arith::XOrIOp::create(
                builder, location, field,
                constant64(uint64_t{1} << (property.width - 1)));
          Value supported = arith::ConstantOp::create(
              builder, location, builder.getI1Type(), builder.getBoolAttr(false));
          for (const DistRangePlan &range : plan->ranges) {
            Value matches;
            if (range.cardinality == 0) {
              matches = arith::ConstantOp::create(
                  builder, location, builder.getI1Type(),
                  builder.getBoolAttr(true));
            } else {
              Value atOrAbove = arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::uge, field,
                  constant64(range.lower));
              Value distance = arith::SubIOp::create(
                  builder, location, field, constant64(range.lower));
              Value belowEnd = arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::ult, distance,
                  constant64(range.cardinality));
              matches = arith::AndIOp::create(builder, location, atOrAbove,
                                               belowEnd);
            }
            arith::CmpIPredicate positivePredicate =
                range.weightSigned ? arith::CmpIPredicate::sgt
                                   : arith::CmpIPredicate::ugt;
            Value positive = arith::CmpIOp::create(
                builder, location, positivePredicate, range.weight,
                constant64(0));
            Value active =
                arith::AndIOp::create(builder, location, matches, positive);
            supported = arith::OrIOp::create(builder, location, supported,
                                              active);
          }
          return supported;
        }
        FailureOr<Value> value = lowerExpression(nested.front());
        if (failed(value))
          return failure();
        FailureOr<Value> predicate =
            truthValue(*value, getSemanticLocation(constraint));
        if (failed(predicate))
          return failure();
        return *predicate;
      }
      if (isa<semantic::SVImplicationConstraintOp>(constraint)) {
        if (nested.size() != 2) {
          emitError(getSemanticLocation(constraint))
              << "implication constraint does not contain a predicate and body";
          return failure();
        }
        FailureOr<Value> predicateValue = lowerExpression(nested.front());
        FailureOr<Value> body =
            lowerConstraint(nested.back(), softTarget);
        if (failed(predicateValue) || failed(body))
          return failure();
        FailureOr<Value> predicate =
            truthValue(*predicateValue, getSemanticLocation(nested.front()));
        if (failed(predicate))
          return failure();
        Value falseValue = arith::ConstantOp::create(
            builder, getSemanticLocation(constraint), builder.getI1Type(),
            builder.getBoolAttr(false));
        Value notPredicate = arith::CmpIOp::create(
            builder, getSemanticLocation(constraint), arith::CmpIPredicate::eq,
            *predicate, falseValue);
        return arith::OrIOp::create(builder, getSemanticLocation(constraint),
                                    notPredicate, *body)
            .getResult();
      }
      if (auto conditional =
              dyn_cast<semantic::SVConditionalConstraintOp>(constraint)) {
        size_t expected = conditional.getHasElse() ? 3 : 2;
        if (nested.size() != expected) {
          emitError(getSemanticLocation(constraint))
              << "conditional constraint has inconsistent branch inventory";
          return failure();
        }
        FailureOr<Value> predicateValue = lowerExpression(nested[0]);
        FailureOr<Value> thenValue = lowerConstraint(nested[1], softTarget);
        if (failed(predicateValue) || failed(thenValue))
          return failure();
        FailureOr<Value> predicate =
            truthValue(*predicateValue, getSemanticLocation(nested[0]));
        if (failed(predicate))
          return failure();
        Value elseValue;
        if (conditional.getHasElse()) {
          FailureOr<Value> loweredElse =
              lowerConstraint(nested[2], softTarget);
          if (failed(loweredElse))
            return failure();
          elseValue = *loweredElse;
        } else {
          elseValue = arith::ConstantOp::create(
              builder, getSemanticLocation(constraint), builder.getI1Type(),
              builder.getBoolAttr(true));
        }
        return arith::SelectOp::create(builder, getSemanticLocation(constraint),
                                       *predicate, *thenValue, elseValue)
            .getResult();
      }
      if (isa<semantic::SVUniquenessConstraintOp>(constraint)) {
        if (softTarget)
          return arith::ConstantOp::create(
                     builder, getSemanticLocation(constraint),
                     builder.getI1Type(), builder.getBoolAttr(true))
              .getResult();
        SmallVector<Value> values;
        for (Operation *item : nested) {
          FailureOr<Value> value = lowerExpression(item);
          if (failed(value))
            return failure();
          values.push_back(*value);
        }
        Value result = arith::ConstantOp::create(
            builder, getSemanticLocation(constraint), builder.getI1Type(),
            builder.getBoolAttr(true));
        for (size_t left = 0; left < values.size(); ++left)
          for (size_t right = left + 1; right < values.size(); ++right) {
            if (values[left].getType() != values[right].getType()) {
              emitError(getSemanticLocation(constraint))
                  << "uniqueness operands do not have one normalized type";
              return failure();
            }
            FailureOr<Value> equal = conditionalEqual(
                values[left], values[right], values[left].getType(),
                getSemanticLocation(constraint), true);
            if (failed(equal))
              return failure();
            Value falseValue = arith::ConstantOp::create(
                builder, getSemanticLocation(constraint), builder.getI1Type(),
                builder.getBoolAttr(false));
            Value distinct = arith::CmpIOp::create(
                builder, getSemanticLocation(constraint),
                arith::CmpIPredicate::eq, *equal, falseValue);
            result = arith::AndIOp::create(
                builder, getSemanticLocation(constraint), result, distinct);
          }
        return result;
      }
      if (auto foreach =
              dyn_cast<semantic::SVForeachConstraintOp>(constraint)) {
        if (nested.size() != 2) {
          emitError(getSemanticLocation(constraint))
              << "foreach constraint does not contain an array and body";
          return failure();
        }
        ArrayAttr metadata = foreach.getLoopDimensions();
        if (metadata.size() != 1) {
          emitError(getSemanticLocation(constraint))
              << "executable foreach constraints currently require one "
                 "array dimension";
          return failure();
        }
        auto dimension = dyn_cast<DictionaryAttr>(metadata[0]);
        auto hasIterator =
            dimension ? dimension.getAs<BoolAttr>(
                            foreach_metadata::hasIterator)
                      : BoolAttr{};
        auto hasStaticRange =
            dimension ? dimension.getAs<BoolAttr>(
                            foreach_metadata::hasStaticRange)
                      : BoolAttr{};
        auto iteratorPath =
            dimension ? dimension.getAs<StringAttr>(
                            foreach_metadata::iteratorPath)
                      : StringAttr{};
        auto semanticIteratorType =
            dimension ? dimension.getAs<TypeAttr>(
                            foreach_metadata::iteratorType)
                      : TypeAttr{};
        if (!dimension || !hasIterator || !hasIterator.getValue() ||
            !hasStaticRange || hasStaticRange.getValue() || !iteratorPath ||
            !semanticIteratorType) {
          emitError(getSemanticLocation(constraint))
              << "runtime foreach constraint metadata is malformed";
          return failure();
        }
        FailureOr<Type> iteratorType = normalizeSemanticType(
            semanticIteratorType.getValue(), getSemanticLocation(constraint));
        FailureOr<Value> collection = lowerExpression(nested.front());
        if (failed(iteratorType) || failed(collection))
          return failure();
        if (!isa<sim::DynamicArrayType, sim::QueueType>(
                collection->getType())) {
          emitError(getSemanticLocation(nested.front()))
              << "runtime foreach constraint requires a dynamic array or "
                 "queue";
          return failure();
        }

        Location foreachLocation = getSemanticLocation(constraint);
        Type indexType = builder.getI64Type();
        auto indexConstant = [&](uint64_t value) -> Value {
          return arith::ConstantOp::create(
              builder, foreachLocation, indexType,
              builder.getI64IntegerAttr(value));
        };
        Value trueValue = arith::ConstantOp::create(
            builder, foreachLocation, builder.getI1Type(),
            builder.getBoolAttr(true));
        Value count = sim::SimContainerSizeOp::create(
            builder, foreachLocation, indexType, *collection);
        Block *header = addBlock();
        Value ordinal = header->addArgument(indexType, foreachLocation);
        Value accumulated =
            header->addArgument(builder.getI1Type(), foreachLocation);
        Block *body = addBlock();
        Block *exit = addBlock();
        Value result = exit->addArgument(builder.getI1Type(), foreachLocation);
        cf::BranchOp::create(builder, foreachLocation, header,
                             ValueRange{indexConstant(0), trueValue});
        setCurrent(header);
        Value more = arith::CmpIOp::create(
            builder, foreachLocation, arith::CmpIPredicate::ult, ordinal,
            count);
        cf::CondBranchOp::create(builder, foreachLocation, more, body,
                                 ValueRange{}, exit,
                                 ValueRange{accumulated});

        setCurrent(body);
        auto previous = values.find(iteratorPath.getValue());
        bool hadPrevious = previous != values.end();
        Value saved = hadPrevious ? previous->second : Value{};
        FailureOr<Value> iterator =
            convert(ordinal, *iteratorType, true, foreachLocation, true);
        if (failed(iterator))
          return failure();
        values[iteratorPath.getValue()] = *iterator;
        FailureOr<Value> item = lowerConstraint(nested.back(), softTarget);
        if (hadPrevious)
          values[iteratorPath.getValue()] = saved;
        else
          values.erase(iteratorPath.getValue());
        if (failed(item))
          return failure();
        Value nextResult = arith::AndIOp::create(
            builder, foreachLocation, accumulated, *item);
        Value nextOrdinal = arith::AddIOp::create(
            builder, foreachLocation, ordinal, indexConstant(1));
        cf::BranchOp::create(builder, foreachLocation, header,
                             ValueRange{nextOrdinal, nextResult});
        setCurrent(exit);
        return result;
      }
      emitError(getSemanticLocation(constraint))
          << "unsupported executable constraint node " << constraint->getName();
      return failure();
    };

    Value satisfied = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    uint64_t domainPropertyOffset = 0;
    for (const Property &property : planned) {
      for (const PropertyDomain &domain : property.domains) {
        Value field = assignment;
        uint64_t offset = domainPropertyOffset + domain.offset;
        if (offset != 0)
          field = arith::ShRUIOp::create(builder, location, field,
                                         constantAssignment64(offset));
        uint64_t fieldMask =
            domain.width == 64 ? UINT64_MAX : (uint64_t{1} << domain.width) - 1;
        field = arith::AndIOp::create(builder, location, field,
                                      constantAssignment64(fieldMask));
        Value member = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(false));
        for (const DomainPattern &pattern : domain.patterns) {
          Value masked = arith::AndIOp::create(builder, location, field,
                                               constantAssignment64(
                                                   pattern.mask));
          Value matches =
              arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                    masked,
                                    constantAssignment64(pattern.value));
          member = arith::OrIOp::create(builder, location, member, matches);
        }
        Value mutableField = mutableMask;
        if (offset != 0)
          mutableField = arith::ShRUIOp::create(builder, location, mutableField,
                                                constantAssignment64(offset));
        mutableField = arith::AndIOp::create(builder, location, mutableField,
                                             constantAssignment64(fieldMask));
        Value inactive =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  mutableField, constantAssignment64(0));
        Value accepted =
            arith::OrIOp::create(builder, location, member, inactive);
        satisfied =
            arith::AndIOp::create(builder, location, satisfied, accepted);
      }
      domainPropertyOffset += property.width;
    }
    for (auto [index, constraint] : llvm::enumerate(children)) {
      if (index == receiverIndex)
        continue;
      bool hasHard = false;
      SmallVector<semantic::SVExpressionConstraintOp> softConstraints;
      constraint->walk([&](Operation *nested) {
        if (auto expression =
                dyn_cast<semantic::SVExpressionConstraintOp>(nested)) {
          if (expression.getIsSoft())
            softConstraints.push_back(expression);
          else
            hasHard = true;
        } else if (isa<semantic::SVUniquenessConstraintOp>(nested)) {
          hasHard = true;
        }
      });
      FailureOr<Value> hard =
          hasHard
              ? lowerConstraint(constraint, /*softTarget=*/nullptr)
              : FailureOr<Value>(arith::ConstantOp::create(
                                     builder, getSemanticLocation(constraint),
                                     builder.getI1Type(),
                                     builder.getBoolAttr(true))
                                     .getResult());
      if (failed(hard))
        return failure();
      SmallVector<Value> softValues;
      for (semantic::SVExpressionConstraintOp soft : softConstraints) {
        FailureOr<Value> value = lowerConstraint(constraint, soft);
        if (failed(value))
          return failure();
        softValues.push_back(*value);
      }
      if (auto block = constraint->getAttrOfType<IntegerAttr>(
              randomConstraintBlockAttrName)) {
        APInt blockIndex = block.getValue();
        if (blockIndex.isNegative() || blockIndex.getActiveBits() > 64 ||
            blockIndex.getZExtValue() >= constraintCount) {
          emitError(getSemanticLocation(constraint))
              << "random constraint block index is malformed";
          return failure();
        }
        uint64_t blockBit = uint64_t{1} << blockIndex.getZExtValue();
        Value selected = arith::AndIOp::create(
            builder, location, relevantConstraintMode,
            constant64(blockBit));
        Value disabled = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, selected,
            constant64(0));
        Value trueValue = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(true));
        hard = arith::SelectOp::create(builder, location, disabled, trueValue,
                                       *hard)
                   .getResult();
        for (Value &soft : softValues)
          soft = arith::SelectOp::create(builder, location, disabled,
                                         trueValue, soft);
      }
      satisfied = arith::AndIOp::create(builder, location, satisfied, *hard);
      for (Value soft : softValues)
        softSatisfied = arith::AndIOp::create(builder, location, softSatisfied,
                                              soft);
    }
    thisObject = savedThis;
    randomizeCandidateValues = std::move(savedCandidates);
    restoreBindings.release();
    Value preferred =
        hasSoftConstraint
            ? arith::AndIOp::create(builder, location, satisfied, softSatisfied)
                  .getResult()
            : satisfied;
    return ConstraintCheck{satisfied, preferred};
  };

  // A randc value is solved before ordinary random variables, but a value
  // excluded by the active constraints is skipped rather than making the
  // whole randomize call fail. For the single-property case there are no
  // later random variables to solve, so walk the remainder of the keyed
  // permutation directly. Count semantic values (rather than raw encoded
  // positions) so enum holes do not consume the exhaustion budget.
  //
  // IEEE 1800-2017 18.4.2 requires a new permutation when none of the
  // remaining values can satisfy the constraints. Trying one complete
  // semantic domain also proves that a fixed active constraint set is
  // unsatisfiable without looping forever.
  if (singleRandCLoop) {
    const Property &property = planned.front();
    uint64_t semanticCardinality = propertyDomainCardinality(property);
    Value randcCandidate =
        singleRandCLoop->addArgument(assignmentType, location);
    Value randcKey = singleRandCLoop->addArgument(i64, location);
    Value randcPosition = singleRandCLoop->addArgument(i64, location);
    Value randcRemaining = singleRandCLoop->addArgument(i64, location);
    Value randcFreshCycle =
        singleRandCLoop->addArgument(builder.getI1Type(), location);
    Value advanceKey = singleRandCAdvance->addArgument(i64, location);
    Value advancePosition =
        singleRandCAdvance->addArgument(i64, location);
    Value advanceRemaining =
        singleRandCAdvance->addArgument(i64, location);
    Value advanceFreshCycle =
        singleRandCAdvance->addArgument(builder.getI1Type(), location);
    Value commitCandidate =
        singleRandCCommit->addArgument(assignmentType, location);
    Value commitKey = singleRandCCommit->addArgument(i64, location);
    Value commitPosition =
        singleRandCCommit->addArgument(i64, location);

    setCurrent(singleRandCLoop);
    FailureOr<ConstraintCheck> randcCheck =
        materializeConstraintCheck(randcCandidate);
    if (failed(randcCheck))
      return failure();
    cf::CondBranchOp::create(
        builder, location, randcCheck->preferred, singleRandCCommit,
        ValueRange{randcCandidate, randcKey, randcPosition},
        singleRandCAdvance,
        ValueRange{randcKey, randcPosition, randcRemaining,
                   randcFreshCycle});

    setCurrent(singleRandCAdvance);
    Value nextRemaining = arith::SubIOp::create(
        builder, location, advanceRemaining, constant64(1));
    if (semanticCardinality == 1) {
      cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
    } else {
      Value exhausted = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, nextRemaining,
          constant64(0));
      exhausted = arith::AndIOp::create(builder, location, exhausted,
                                        advanceFreshCycle);
      Block *candidateEntry = addBlock();
      Block *rekeyBlock = addBlock();
      Block *cycleBlock = addBlock();
      Value entryKey = candidateEntry->addArgument(i64, location);
      Value entryPosition = candidateEntry->addArgument(i64, location);
      Value entryRemaining = candidateEntry->addArgument(i64, location);
      Value entryFreshCycle =
          candidateEntry->addArgument(builder.getI1Type(), location);
      Value activeKey = cycleBlock->addArgument(i64, location);
      Value activePosition = cycleBlock->addArgument(i64, location);
      Value activeRemaining = cycleBlock->addArgument(i64, location);
      Value activeFreshCycle =
          cycleBlock->addArgument(builder.getI1Type(), location);
      cf::CondBranchOp::create(builder, location, exhausted, failedBlock,
                               ValueRange{}, candidateEntry,
                               ValueRange{advanceKey, advancePosition,
                                          nextRemaining, advanceFreshCycle});

      setCurrent(candidateEntry);
      Value needsRekey = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, entryPosition,
          constant64(0));
      cf::CondBranchOp::create(builder, location, needsRekey, rekeyBlock,
                               ValueRange{}, cycleBlock,
                               ValueRange{entryKey, entryPosition,
                                          entryRemaining, entryFreshCycle});

      setCurrent(rekeyBlock);
      Value rekeyState = sim::SimManagedLoadOp::create(
          builder, location, i64, stateReference);
      Value newKey = next64(rekeyState);
      sim::SimManagedStoreOp::create(builder, location, rekeyState,
                                     stateReference);
      cf::BranchOp::create(builder, location, cycleBlock,
                           ValueRange{newKey, entryPosition,
                                      constant64(semanticCardinality),
                                      arith::ConstantOp::create(
                                          builder, location,
                                          builder.getI1Type(),
                                          builder.getBoolAttr(true))});

      setCurrent(cycleBlock);
      unsigned cycleWidth = llvm::Log2_64_Ceil(semanticCardinality);
      auto cycle = sim::SimRandomCycleNextOp::create(
          builder, location, activeKey, activePosition,
          builder.getI32IntegerAttr(cycleWidth));
      Value valid = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, cycle.getValue(),
          constant64(semanticCardinality));
      Value semanticValue =
          property.domains.empty()
              ? cycle.getValue()
              : materializePropertyDomainIndex(property, cycle.getValue());
      if (assignmentType != i64)
        semanticValue = arith::ExtUIOp::create(
            builder, location, assignmentType, semanticValue);
      cf::CondBranchOp::create(
          builder, location, valid, singleRandCLoop,
          ValueRange{semanticValue, activeKey, cycle.getNextPosition(),
                     activeRemaining, activeFreshCycle},
          candidateEntry,
          ValueRange{activeKey, cycle.getNextPosition(), activeRemaining,
                     activeFreshCycle});
    }

    setCurrent(singleRandCCommit);
    FailureOr<SmallVector<Value>> candidates =
        materializeCandidates(commitCandidate);
    if (failed(candidates))
      return failure();
    if (failed(storeReference(property.reference, candidates->front(),
                              location)) ||
        failed(storeReference(property.randcKeyReference, commitKey,
                              location)) ||
        failed(storeReference(property.randcPositionReference, commitPosition,
                              location)))
      return failure();
    cf::BranchOp::create(builder, location, postBlock);
  }

  setCurrent(disabledCheckBlock);
  FailureOr<ConstraintCheck> disabledCheck =
      materializeConstraintCheck(currentAssignment);
  if (failed(disabledCheck))
    return failure();
  cf::CondBranchOp::create(builder, location, disabledCheck->hard, postBlock,
                           ValueRange{}, failedBlock, ValueRange{});

  setCurrent(loop);
  FailureOr<Value> proposal =
      materializeProposal(counter, loopSampledDomainIndices);
  if (failed(proposal))
    return failure();
  if (exactProposal) {
    Block *partialCheck = addBlock();
    cf::CondBranchOp::create(builder, location, randomizationEnabled, commit,
                             ValueRange{*proposal}, partialCheck, ValueRange{});
    setCurrent(partialCheck);
    Value assignment = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, *proposal, mutableMask),
        fixedAssignment);
    FailureOr<ConstraintCheck> check = materializeConstraintCheck(assignment);
    if (failed(check))
      return failure();
    cf::CondBranchOp::create(builder, location, check->preferred, commit,
                             ValueRange{assignment}, advance, ValueRange{});
  } else {
    Value assignment = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, *proposal, mutableMask),
        fixedAssignment);
    FailureOr<ConstraintCheck> check = materializeConstraintCheck(assignment);
    if (failed(check))
      return failure();
    cf::CondBranchOp::create(builder, location, check->preferred, commit,
                             ValueRange{assignment}, advance, ValueRange{});
  }
  setCurrent(advance);
  // Retrying adjacent aggregate assignments can leave a higher random field
  // unchanged for the entire bounded sampling window.  Besides biasing the
  // accepted solutions, that makes a dense conditional constraint such as
  // `guard -> value == 0` spuriously fail whenever `guard` is packed above a
  // wide value.  Draw a fresh aggregate proposal and fresh independently
  // sampled domain indices for every retry.  Rejection sampling from the
  // uniform object stream preserves the required uniform distribution over
  // accepted legal assignments.
  Value next;
  SmallVector<Value> nextSampledDomainIndices;
  if (hasRuntimeForeachConstraint) {
    // The dynamic predicate is absent from the fixed-arity residual program.
    // Traverse the bounded aggregate domain cyclically from the initial
    // object-stream draw so a complete domain (at most 2^20 assignments) is
    // proved rather than probabilistically sampled with replacement.
    next = arith::AddIOp::create(builder, location, counter, constant64(1));
    nextSampledDomainIndices.assign(loopSampledDomainIndices.begin(),
                                    loopSampledDomainIndices.end());
  } else {
    Value retryState = sim::SimManagedLoadOp::create(
        builder, location, i64, stateReference);
    next = nextAssignment(retryState);
    nextSampledDomainIndices = sampleProposalDomainIndices(retryState);
    sim::SimManagedStoreOp::create(builder, location, retryState,
                                   stateReference);
  }
  next = arith::OrIOp::create(
      builder, location,
      arith::AndIOp::create(builder, location, next, mutableMask),
      fixedAssignment);
  Value nextAttempt =
      arith::AddIOp::create(builder, location, attempt, constant64(1));
  Value exhausted =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                            nextAttempt,
                            constant64(hasRuntimeForeachConstraint
                                           ? fallbackAttempts
                                           : 64));
  SmallVector<Value> retryArguments{next, nextAttempt};
  llvm::append_range(retryArguments, nextSampledDomainIndices);
  if (hasRuntimeForeachConstraint)
    cf::CondBranchOp::create(builder, location, exhausted, failedBlock,
                             ValueRange{}, loop, retryArguments);
  else
    cf::CondBranchOp::create(builder, location, exhausted, fallbackBlock,
                             ValueRange{next}, loop, retryArguments);

  setCurrent(fallbackBlock);
  if (analysis.satisfiability == solver::Satisfiability::Unsatisfiable) {
    emitWarning(location)
        << "randomize hard constraints are statically unsatisfiable ("
        << analysis.backend << ")";
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  } else if (wideProgram) {
    Value fallbackState = sim::SimManagedLoadOp::create(
        builder, location, i64, stateReference);
    auto fallback = sim::SimRandomSolveWideOp::create(
        builder, location, function.getBody().front().getArgument(0),
        fallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), fallbackState, increment,
        programCaptures,
        builder.getStringAttr(StringRef(
            reinterpret_cast<const char *>(program.data()), program.size())));
    sim::SimManagedStoreOp::create(builder, location,
                                   fallback.getNextRngState(), stateReference);
    cf::CondBranchOp::create(builder, location, fallback.getSuccess(), commit,
                             ValueRange{fallback.getAssignment()}, failedBlock,
                             ValueRange{});
  } else {
    Value fallbackState = sim::SimManagedLoadOp::create(
        builder, location, i64, stateReference);
    auto fallback = sim::SimRandomSolveOp::create(
        builder, location, function.getBody().front().getArgument(0),
        fallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), fallbackState, increment,
        programCaptures,
        builder.getStringAttr(StringRef(
            reinterpret_cast<const char *>(program.data()), program.size())));
    sim::SimManagedStoreOp::create(builder, location,
                                   fallback.getNextRngState(), stateReference);
    cf::CondBranchOp::create(builder, location, fallback.getSuccess(), commit,
                             ValueRange{fallback.getAssignment()}, failedBlock,
                             ValueRange{});
  }

  setCurrent(modeSamplingDispatchBlock);
  sim::SimManagedStoreOp::create(builder, location, modeState, stateReference);
  if (!hasRuntimeForeachConstraint && (hasSolveBefore || hasFiniteDomains))
    cf::BranchOp::create(builder, location, modeFallbackBlock,
                         ValueRange{modeStart});
  else
    cf::BranchOp::create(builder, location, modeLoop,
                         ValueRange{modeStart, constant64(0)});

  setCurrent(modeLoop);
  FailureOr<ConstraintCheck> modeCheck =
      materializeConstraintCheck(modeCounter);
  if (failed(modeCheck))
    return failure();
  cf::CondBranchOp::create(builder, location, modeCheck->preferred, commit,
                           ValueRange{modeCounter}, modeAdvance, ValueRange{});

  setCurrent(modeAdvance);
  Value modeNext;
  if (hasRuntimeForeachConstraint) {
    modeNext = arith::AddIOp::create(builder, location, modeCounter,
                                     constant64(1));
  } else {
    Value modeRetryState = sim::SimManagedLoadOp::create(
        builder, location, i64, stateReference);
    modeNext = nextAssignment(modeRetryState);
    sim::SimManagedStoreOp::create(builder, location, modeRetryState,
                                   stateReference);
  }
  modeNext = arith::OrIOp::create(
      builder, location,
      arith::AndIOp::create(builder, location, modeNext, mutableMask),
      fixedAssignment);
  Value modeNextAttempt =
      arith::AddIOp::create(builder, location, modeAttempt, constant64(1));
  Value modeExhausted =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                            modeNextAttempt,
                            constant64(hasRuntimeForeachConstraint
                                           ? fallbackAttempts
                                           : 64));
  if (hasRuntimeForeachConstraint)
    cf::CondBranchOp::create(builder, location, modeExhausted, failedBlock,
                             ValueRange{}, modeLoop,
                             ValueRange{modeNext, modeNextAttempt});
  else
    cf::CondBranchOp::create(builder, location, modeExhausted,
                             modeFallbackBlock, ValueRange{modeNext}, modeLoop,
                             ValueRange{modeNext, modeNextAttempt});

  setCurrent(modeFallbackBlock);
  if (wideProgram) {
    Value fallbackState = sim::SimManagedLoadOp::create(
        builder, location, i64, stateReference);
    auto modeFallback = sim::SimRandomSolveWideOp::create(
        builder, location, function.getBody().front().getArgument(0),
        modeFallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), fallbackState, increment,
        programCaptures,
        builder.getStringAttr(StringRef(
            reinterpret_cast<const char *>(program.data()), program.size())));
    sim::SimManagedStoreOp::create(
        builder, location, modeFallback.getNextRngState(), stateReference);
    cf::CondBranchOp::create(builder, location, modeFallback.getSuccess(),
                             commit,
                             ValueRange{modeFallback.getAssignment()},
                             failedBlock, ValueRange{});
  } else {
    Value fallbackState = sim::SimManagedLoadOp::create(
        builder, location, i64, stateReference);
    auto modeFallback = sim::SimRandomSolveOp::create(
        builder, location, function.getBody().front().getArgument(0),
        modeFallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), fallbackState, increment,
        programCaptures,
        builder.getStringAttr(StringRef(
            reinterpret_cast<const char *>(program.data()), program.size())));
    sim::SimManagedStoreOp::create(
        builder, location, modeFallback.getNextRngState(), stateReference);
    cf::CondBranchOp::create(builder, location, modeFallback.getSuccess(),
                             commit,
                             ValueRange{modeFallback.getAssignment()},
                             failedBlock, ValueRange{});
  }

  // Static UNSAT is known for every possible runtime capture value when all
  // constraint blocks are enabled. Bypass planned sampling instead of spending
  // its deterministic rejection budget on a set the compiler has already
  // proved impossible. A disabled block takes the separate live-mask path,
  // where the reduced constraint set may be satisfiable. The object stream
  // draw above remains observable in either case.
  setCurrent(dispatchBlock);
  if (analysis.satisfiability == solver::Satisfiability::Unsatisfiable)
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  else if (singleRandCLoop)
    cf::BranchOp::create(
        builder, location, singleRandCLoop,
        ValueRange{fixedAssignment, planned.front().nextRandcKey,
                   planned.front().nextRandcPosition,
                   constant64(propertyDomainCardinality(planned.front())),
                   arith::ConstantOp::create(builder, location,
                                             builder.getI1Type(),
                                             builder.getBoolAttr(false))});
  else if (solveBeforeRequiresRuntime)
    cf::BranchOp::create(builder, location, fallbackBlock, ValueRange{start});
  else {
    SmallVector<Value> initialArguments{start, constant64(0)};
    llvm::append_range(initialArguments, sampledDomainIndices);
    cf::BranchOp::create(builder, location, loop, initialArguments);
  }

  setCurrent(commit);
  FailureOr<SmallVector<Value>> committed =
      materializeCandidates(commitCounter);
  if (failed(committed))
    return failure();
  Block *commitDone = addBlock();
  for (auto [property, candidate, enabled] :
       llvm::zip_equal(planned, *committed, propertyEnabled)) {
    Block *store = addBlock();
    Block *nextProperty = addBlock();
    cf::CondBranchOp::create(builder, location, enabled, store, ValueRange{},
                             nextProperty, ValueRange{});
    setCurrent(store);
    if (property.nestedObjectReference) {
      FailureOr<Value> loadedObject =
          loadReference(property.nestedObjectReference, location);
      if (failed(loadedObject))
        return failure();
      Value object = *loadedObject;
      if (object.getType() != property.nestedObjectType)
        object = sim::SimClassCastOp::create(
            builder, location, property.nestedObjectType, object);
      auto concreteType = cast<sim::ClassHandleType>(property.nestedObjectType);
      for (const ObjectPathElement &element : property.nestedObjectPath) {
        Type edgeReferenceType = sim::ManagedRefType::get(
            function.getContext(), element.storageType,
            concreteType.getClassName());
        Value edgeReference = sim::SimClassFieldRefOp::create(
            builder, location, edgeReferenceType, object, element.field);
        FailureOr<Value> loadedEdge = loadReference(edgeReference, location);
        if (failed(loadedEdge))
          return failure();
        object = *loadedEdge;
        if (object.getType() != element.concreteType)
          object = sim::SimClassCastOp::create(
              builder, location, element.concreteType, object);
        concreteType = cast<sim::ClassHandleType>(element.concreteType);
      }
      Type nestedFieldType = property.isContainerSize ? property.containerType
                                                      : property.type;
      Type fieldReferenceType = sim::ManagedRefType::get(
          function.getContext(), nestedFieldType, concreteType.getClassName());
      Value fieldReference = sim::SimClassFieldRefOp::create(
          builder, location, fieldReferenceType, object,
          property.nestedField);
      if (property.isContainerSize) {
        FailureOr<Value> oldContainer = loadReference(fieldReference, location);
        FailureOr<Value> scalarSize = toPackedScalar(candidate, location);
        FailureOr<Value> size =
            succeeded(scalarSize)
                ? convert(*scalarSize, i64, false, location, false)
                : FailureOr<Value>(failure());
        if (failed(oldContainer) || failed(size))
          return failure();
        Value resized = sim::SimContainerCreateLikeOp::create(
            builder, location, property.containerType, *oldContainer,
            *oldContainer, *size);
        sim::SimManagedStoreOp::create(builder, location, resized,
                                       fieldReference);
      } else if (failed(storeReference(fieldReference, candidate, location))) {
        return failure();
      }
    } else if (property.isContainerSize) {
      FailureOr<Value> oldContainer =
          loadReference(property.reference, location);
      FailureOr<Value> scalarSize = toPackedScalar(candidate, location);
      FailureOr<Value> size =
          succeeded(scalarSize)
              ? convert(*scalarSize, i64, false, location, false)
              : FailureOr<Value>(failure());
      if (failed(oldContainer) || failed(size))
        return failure();
      Value resized = sim::SimContainerCreateLikeOp::create(
          builder, location, property.containerType, *oldContainer,
          *oldContainer, *size);
      sim::SimManagedStoreOp::create(builder, location, resized,
                                     property.reference);
    } else if (failed(storeReference(property.reference, candidate, location)))
      return failure();
    if (property.isRandC) {
      if (failed(storeReference(property.randcKeyReference,
                                property.nextRandcKey, location)) ||
          failed(storeReference(property.randcPositionReference,
                                property.nextRandcPosition, location)))
        return failure();
    }
    cf::BranchOp::create(builder, location, nextProperty);
    setCurrent(nextProperty);
  }
  cf::BranchOp::create(builder, location, commitDone);
  setCurrent(commitDone);
  struct NestedContainerRuntime {
    Type ownerType;
    FlatSymbolRefAttr field;
    Value objectID;
    Value active;
  };
  SmallVector<NestedContainerRuntime> nestedContainerRuntimes;
  for (auto [containerIndex, property] :
       llvm::enumerate(plannedContainers)) {
    if (property.inertClassHandles)
      continue;
    Block *entryBlock = addBlock();
    Block *header = addBlock();
    Block *body = addBlock();
    Block *nextProperty = addBlock();
    Value propertyMode = arith::AndIOp::create(
        builder, location, relevantMode,
        constant64(uint64_t{1} << property.modeIndex));
    Value enabled =
        checkerOnly
            ? arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false))
                  .getResult()
            : arith::CmpIOp::create(builder, location,
                                    arith::CmpIPredicate::eq, propertyMode,
                                    constant64(0))
                  .getResult();
    Value container;
    Block *randomizeBlock;
    if (property.nestedObjectReference) {
      bool needsIdentity = containerNeedsIdentity[containerIndex];
      Block *inactiveBlock = needsIdentity ? addBlock() : nextProperty;
      cf::CondBranchOp::create(builder, location, enabled, entryBlock,
                               ValueRange{}, inactiveBlock, ValueRange{});
      setCurrent(entryBlock);
      FailureOr<Value> loadedObject =
          loadReference(property.nestedObjectReference, location);
      if (failed(loadedObject))
        return failure();
      Value isNull = sim::SimManagedIsNullOp::create(
          builder, location, builder.getI1Type(), *loadedObject);
      Block *objectBlock = addBlock();
      cf::CondBranchOp::create(builder, location, isNull, inactiveBlock,
                               ValueRange{}, objectBlock, ValueRange{});
      setCurrent(objectBlock);
      Value object = *loadedObject;
      if (object.getType() != property.nestedObjectType)
        object = sim::SimClassCastOp::create(
            builder, location, property.nestedObjectType, object);
      auto concreteType = cast<sim::ClassHandleType>(property.nestedObjectType);
      FlatSymbolRefAttr objectModeField = property.nestedModeField;
      for (const ObjectPathElement &element : property.nestedObjectPath) {
        Type modeReferenceType = sim::ManagedRefType::get(
            function.getContext(), i64, concreteType.getClassName());
        Value modeReference = sim::SimClassFieldRefOp::create(
            builder, location, modeReferenceType, object, objectModeField);
        Value objectMode = sim::SimManagedLoadOp::create(
            builder, location, i64, modeReference);
        Value edgeModeBit = arith::AndIOp::create(
            builder, location, objectMode,
            constant64(uint64_t{1} << element.modeIndex));
        Value edgeEnabled = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, edgeModeBit,
            constant64(0));
        Type edgeReferenceType = sim::ManagedRefType::get(
            function.getContext(), element.storageType,
            concreteType.getClassName());
        Value edgeReference = sim::SimClassFieldRefOp::create(
            builder, location, edgeReferenceType, object, element.field);
        FailureOr<Value> loadedEdge = loadReference(edgeReference, location);
        if (failed(loadedEdge))
          return failure();
        Value edgeNull = sim::SimManagedIsNullOp::create(
            builder, location, builder.getI1Type(), *loadedEdge);
        Value edgeNonNull = arith::XOrIOp::create(
            builder, location, edgeNull,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true)));
        Value edgeActive = arith::AndIOp::create(
            builder, location, edgeEnabled, edgeNonNull);
        Block *edgeBlock = addBlock();
        cf::CondBranchOp::create(builder, location, edgeActive, edgeBlock,
                                 ValueRange{}, inactiveBlock, ValueRange{});
        setCurrent(edgeBlock);
        object = *loadedEdge;
        if (object.getType() != element.concreteType)
          object = sim::SimClassCastOp::create(
              builder, location, element.concreteType, object);
        concreteType = cast<sim::ClassHandleType>(element.concreteType);
        objectModeField = element.modeField;
      }
      Type modeReferenceType = sim::ManagedRefType::get(
          function.getContext(), i64, concreteType.getClassName());
      Value childModeReference = sim::SimClassFieldRefOp::create(
          builder, location, modeReferenceType, object, objectModeField);
      Value childMode = sim::SimManagedLoadOp::create(
          builder, location, i64, childModeReference);
      Value childModeBit = arith::AndIOp::create(
          builder, location, childMode,
          constant64(uint64_t{1} << property.nestedModeIndex));
      Value childEnabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, childModeBit,
          constant64(0));
      Block *childActiveBlock = addBlock();
      cf::CondBranchOp::create(builder, location, childEnabled,
                               childActiveBlock, ValueRange{}, inactiveBlock,
                               ValueRange{});
      setCurrent(childActiveBlock);
      Type fieldReferenceType = sim::ManagedRefType::get(
          function.getContext(), property.type, concreteType.getClassName());
      Value containerReference = sim::SimClassFieldRefOp::create(
          builder, location, fieldReferenceType, object,
          property.nestedField);
      container = sim::SimManagedLoadOp::create(
          builder, location, property.type, containerReference);
      randomizeBlock = childActiveBlock;
      if (needsIdentity) {
        Block *resolvedBlock = addBlock();
        resolvedBlock->addArgument(property.type, location);
        resolvedBlock->addArgument(i64, location);
        resolvedBlock->addArgument(builder.getI1Type(), location);
        Value objectID = sim::SimClassIdOp::create(builder, location, object);
        cf::BranchOp::create(builder, location, resolvedBlock,
                             ValueRange{container, objectID,
                                        arith::ConstantOp::create(
                                            builder, location,
                                            builder.getI1Type(),
                                            builder.getBoolAttr(true))});

        setCurrent(inactiveBlock);
        Value missingContainer =
            createDefaultValue(builder, location, property.type);
        if (!missingContainer)
          return failure();
        cf::BranchOp::create(
            builder, location, resolvedBlock,
            ValueRange{missingContainer, constant64(0),
                       arith::ConstantOp::create(
                           builder, location, builder.getI1Type(),
                           builder.getBoolAttr(false))});

        setCurrent(resolvedBlock);
        container = resolvedBlock->getArgument(0);
        objectID = resolvedBlock->getArgument(1);
        Value active = resolvedBlock->getArgument(2);
        Type ownerType = containerOwnerType(property);
        Value alreadyVisited = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(false));
        for (const NestedContainerRuntime &previous : nestedContainerRuntimes) {
          if (previous.ownerType != ownerType ||
              previous.field != property.nestedField)
            continue;
          Value sameID = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, objectID,
              previous.objectID);
          Value previousMatch = arith::AndIOp::create(
              builder, location, previous.active, sameID);
          alreadyVisited = arith::OrIOp::create(
              builder, location, alreadyVisited, previousMatch);
        }
        nestedContainerRuntimes.push_back(
            {ownerType, property.nestedField, objectID, active});
        Value firstVisit = arith::XOrIOp::create(
            builder, location, alreadyVisited,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true)));
        Value randomize =
            arith::AndIOp::create(builder, location, active, firstVisit);
        randomizeBlock = addBlock();
        cf::CondBranchOp::create(builder, location, randomize, randomizeBlock,
                                 ValueRange{}, nextProperty, ValueRange{});
        setCurrent(randomizeBlock);
      }
    } else {
      randomizeBlock = entryBlock;
      cf::CondBranchOp::create(builder, location, enabled, entryBlock,
                               ValueRange{}, nextProperty, ValueRange{});
      setCurrent(entryBlock);
      container = sim::SimManagedLoadOp::create(
          builder, location, property.type, property.reference);
    }
    Value size = sim::SimContainerSizeOp::create(
        builder, location, i64, container);
    Value containerState = sim::SimManagedLoadOp::create(
        builder, location, i64, stateReference);
    header->addArgument(i64, location);
    header->addArgument(i64, location);
    cf::BranchOp::create(builder, location, header,
                         ValueRange{constant64(0), containerState});

    setCurrent(header);
    Value index = header->getArgument(0);
    Value loopState = header->getArgument(1);
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{},
                             nextProperty, ValueRange{});

    setCurrent(body);
    Value nextState = loopState;
    Value bits = next64(nextState);
    Type scalarType = IntegerType::get(function.getContext(),
                                       property.elementWidth);
    if (property.elementWidth != 64)
      bits = arith::TruncIOp::create(builder, location, scalarType, bits);
    FailureOr<Value> element =
        convert(bits, property.elementType, false, location, false);
    if (failed(element))
      return failure();
    sim::SimContainerWriteOp::create(builder, location, container, index,
                                     *element);
    sim::SimManagedStoreOp::create(builder, location, nextState,
                                   stateReference);
    Value nextIndex = arith::AddIOp::create(builder, location, index,
                                            constant64(1));
    cf::BranchOp::create(builder, location, header,
                         ValueRange{nextIndex, nextState});
    setCurrent(nextProperty);
  }
  cf::BranchOp::create(builder, location, postBlock);

  setCurrent(postBlock);
  if (failed(callLifecycleHook(randomPostHookAttrName,
                               randomPostHookOwnerAttrName,
                               randomPostHookCapturesAttrName,
                               randomPostHookReadCapturesAttrName)))
    return failure();
  for (const NestedHookRuntime &runtime : nestedHookRuntimes) {
    if (!runtime.plan.getAs<FlatSymbolRefAttr>("post_callee"))
      continue;
    Block *callBlock = addBlock();
    Block *mergeBlock = addBlock();
    cf::CondBranchOp::create(builder, location, runtime.enabled, callBlock,
                             ValueRange{}, mergeBlock, ValueRange{});
    setCurrent(callBlock);
    if (failed(callNestedHook(runtime.plan, "post", runtime.object)))
      return failure();
    cf::BranchOp::create(builder, location, mergeBlock);
    setCurrent(mergeBlock);
  }
  Value success = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(true));
  cf::BranchOp::create(builder, location, done, ValueRange{success});

  setCurrent(failedBlock);
  Value noSolution = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(false));
  cf::BranchOp::create(builder, location, done, ValueRange{noSolution});

  setCurrent(done);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  return convert(doneResult, *resultType, false, location);
}

} // namespace obelisk::simlowering
