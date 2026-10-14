//===- LowerUnitRandomize.cpp - Lower randomize() calls ---------------===//
//
// Lowering of SystemVerilog randomize() calls: the constraint program the
// solver executes, its proposal tables and finite domains, and the inline
// sampling fast paths emitted when a class needs no solver call.
//
//===----------------------------------------------------------------------===//

#include "LowerUnit.h"
#include "PrepareRandomFunctions.h"
#include "RandomArithmetic.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Solver/ConstraintSolver.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/bit.h"

#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <tuple>

using namespace mlir;

namespace obelisk::simlowering {
FailureOr<Value> UnitLowering::lowerRandomize(semantic::SVCallExpressionOp op,
                                              Value receiverOverride) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (!receiverOverride && op->hasAttr(randomizeHelperReceiverAttrName)) {
    Block &entry = function.getBody().front();
    if (entry.getNumArguments() < 2 ||
        !isa<sim::ClassHandleType>(entry.getArgument(1).getType())) {
      emitError(location)
          << "shared randomize helper has no class receiver argument";
      return failure();
    }
    receiverOverride = entry.getArgument(1);
  }
  auto lowerSharedHelper = [&](semantic::SVCallExpressionOp alternative,
                               Value receiver,
                               Type resultType) -> FailureOr<Value> {
    auto helper =
        alternative->getAttrOfType<FlatSymbolRefAttr>(randomizeHelperAttrName);
    auto planClass = alternative->getAttrOfType<FlatSymbolRefAttr>(
        randomizePlanClassAttrName);
    if (!helper || !planClass)
      return failure();
    llvm::StringSet<> readCaptures;
    if (auto reads = alternative->getAttrOfType<ArrayAttr>(
            randomizeHelperReadCapturesAttrName))
      for (Attribute read : reads)
        readCaptures.insert(cast<StringAttr>(read).getValue());
    for (const auto &read : readCaptures) {
      Value capture = values.lookup(read.getKey());
      if (!capture)
        capture = lvalues.lookup(read.getKey());
      if (capture)
        recordSensitivity(capture);
    }
    SmallVector<Value> arguments;
    if (auto captures = alternative->getAttrOfType<ArrayAttr>(
            randomizeHelperCapturesAttrName))
      for (Attribute captureAttr : captures) {
        StringRef path = cast<StringAttr>(captureAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture) {
          emitError(location)
              << "shared randomize helper capture has no local binding: "
              << path;
          return failure();
        }
        arguments.push_back(capture);
      }
    Type receiverType =
        sim::ClassHandleType::get(function.getContext(), planClass);
    if (receiver.getType() != receiverType)
      receiver = sim::SimClassCastOp::create(builder, location, receiverType,
                                             receiver);
    auto invocation = sim::SimClassDirectCallOp::create(
        builder, location, TypeRange{resultType}, helper, receiver, arguments);
    if (failed(emitTerminationPoll(location)))
      return failure();
    return invocation.getResult(0);
  };
  if (op->hasAttr(randomizeNestedDispatchAttrName)) {
    auto receiverIndexAttr =
        op->getAttrOfType<IntegerAttr>(randomReceiverIndexAttrName);
    auto field = op->getAttrOfType<FlatSymbolRefAttr>(
        randomizeNestedDispatchFieldAttrName);
    auto storageTypeAttr =
        op->getAttrOfType<TypeAttr>(randomizeNestedDispatchStorageAttrName);
    auto dispatchPath =
        op->getAttrOfType<ArrayAttr>(randomizeNestedDispatchPathAttrName);
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
      emitError(location) << "nested randomize dispatch has malformed metadata";
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
    if (auto planClass =
            op->getAttrOfType<FlatSymbolRefAttr>(randomizePlanClassAttrName)) {
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
              if (selected.getAs<ArrayAttr>("path") == dispatchSelectionPath)
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
        auto pathField = element ? element.getAs<FlatSymbolRefAttr>("field")
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
          emitError(location) << "nested randomize dispatch path is malformed";
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
    Value isNull = sim::SimManagedIsNullOp::create(builder, location,
                                                   builder.getI1Type(), object);
    cf::CondBranchOp::create(builder, location, isNull, nullBlock, ValueRange{},
                             nonNullBlock, ValueRange{});
    setCurrent(nullBlock);
    FailureOr<Value> nullResult =
        nullAlternative->hasAttr(randomizeHelperAttrName)
            ? lowerSharedHelper(nullAlternative, *loweredReceiver, *resultType)
            : lowerRandomize(nullAlternative, *loweredReceiver);
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
      Value matches = sim::SimClassIsInstanceOp::create(builder, location,
                                                        object, planClass);
      Block *selected = addBlock();
      Block *next = addBlock();
      cf::CondBranchOp::create(builder, location, matches, selected,
                               ValueRange{}, next, ValueRange{});
      setCurrent(selected);
      FailureOr<Value> result =
          alternative->hasAttr(randomizeHelperAttrName)
              ? lowerSharedHelper(alternative, *loweredReceiver, *resultType)
              : lowerRandomize(alternative, *loweredReceiver);
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
    FailureOr<Value> loweredReceiver =
        receiverOverride ? FailureOr<Value>(receiverOverride)
                         : lowerExpression(children[receiverIndex]);
    if (failed(loweredReceiver) ||
        !isa<sim::ClassHandleType>((*loweredReceiver).getType())) {
      emitError(location)
          << "randomize dispatch receiver is not a class object";
      return failure();
    }

    SmallVector<semantic::SVCallExpressionOp> alternatives;
    for (Operation *child : children)
      if (auto alternative = dyn_cast<semantic::SVCallExpressionOp>(child);
          alternative && alternative->hasAttr(randomizePlanClassAttrName) &&
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
      FailureOr<Value> result =
          alternative->hasAttr(randomizeHelperAttrName)
              ? lowerSharedHelper(alternative, *loweredReceiver, *resultType)
              : lowerRandomize(alternative, *loweredReceiver);
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
  auto constraintTemplateAttr =
      op->getAttrOfType<FlatSymbolRefAttr>(randomizeConstraintTemplateAttrName);
  bool scopeRandomize = op->hasAttr(randomizeScopeAttrName);
  if ((!scopeRandomize && children.empty()) || !properties ||
      !containerProperties || !nestedConstraintModes || !nestedHooks ||
      !recursiveAliasGuards || !totalWidthAttr ||
      (!scopeRandomize && !receiverIndexAttr) || !constraintCountAttr) {
    emitError(location) << "randomize call has no frozen constraint plan";
    return failure();
  }
  APInt totalWidthValue = totalWidthAttr.getValue();
  APInt constraintCountValue = constraintCountAttr.getValue();
  if (totalWidthValue.isNegative() || totalWidthValue.getActiveBits() > 64 ||
      constraintCountValue.isNegative() ||
      constraintCountValue.getActiveBits() > 64) {
    emitError(location) << "randomize call has malformed constraint metadata";
    return failure();
  }
  unsigned receiverIndex = std::numeric_limits<unsigned>::max();
  if (!scopeRandomize) {
    APInt receiverIndexValue = receiverIndexAttr.getValue();
    if (receiverIndexValue.isNegative() ||
        receiverIndexValue.getActiveBits() > 64 ||
        receiverIndexValue.getZExtValue() >= children.size()) {
      emitError(location) << "randomize call has malformed receiver metadata";
      return failure();
    }
    receiverIndex = static_cast<unsigned>(receiverIndexValue.getZExtValue());
  }
  auto isReceiverChild = [&](size_t index) {
    return !scopeRandomize && index == receiverIndex;
  };
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

  Value receiver;
  sim::ClassHandleType objectType;
  if (scopeRandomize) {
    if (receiverOverride || constraintTemplateAttr) {
      emitError(location)
          << "std::randomize cannot carry an object receiver or class "
             "constraint template";
      return failure();
    }
  } else {
    FailureOr<Value> loweredReceiver =
        receiverOverride ? FailureOr<Value>(receiverOverride)
                         : lowerExpression(children[receiverIndex]);
    if (receiverOverride) {
      auto planClass =
          op->getAttrOfType<FlatSymbolRefAttr>(randomizePlanClassAttrName);
      if (!planClass) {
        emitError(location)
            << "randomize receiver override has no target class";
        return failure();
      }
      Type targetType =
          sim::ClassHandleType::get(function.getContext(), planClass);
      if ((*loweredReceiver).getType() != targetType)
        loweredReceiver = sim::SimClassCastOp::create(
                              builder, location, targetType, *loweredReceiver)
                              .getResult();
    }
    objectType =
        succeeded(loweredReceiver)
            ? dyn_cast<sim::ClassHandleType>((*loweredReceiver).getType())
            : sim::ClassHandleType{};
    if (failed(loweredReceiver) || !objectType)
      return failure();
    receiver = *loweredReceiver;
  }

  sim::SimRandomConstraintTemplateOp constraintTemplate;
  if (constraintTemplateAttr) {
    constraintTemplate =
        lookupNearestSymbolFrom<sim::SimRandomConstraintTemplateOp>(
            function, constraintTemplateAttr);
    auto planClass =
        op->getAttrOfType<FlatSymbolRefAttr>(randomizePlanClassAttrName);
    if (!constraintTemplate || !planClass ||
        constraintTemplate.getOwnerAttr() != planClass ||
        !staticConstraintStorages) {
      emitError(location)
          << "randomize constraint-template metadata is inconsistent";
      return failure();
    }
    ArrayAttr blocks = constraintTemplate.getConstraintBlocks();
    if (blocks.size() != constraintCount) {
      emitError(location)
          << "randomize constraint template has a different block inventory";
      return failure();
    }
    unsigned softCount = 0;
    for (auto [index, blockAttr] : llvm::enumerate(blocks)) {
      auto block = cast<sim::RandomConstraintBlockReferenceAttr>(blockAttr);
      int64_t expectedStorage = staticConstraintStorages[index];
      switch (block.getKind()) {
      case sim::RandomConstraintBlockReferenceKind::ObjectBlock:
        if (block.getIndex().getValue().getZExtValue() != index ||
            expectedStorage != -1) {
          emitError(location)
              << "randomize constraint template object block is inconsistent";
          return failure();
        }
        break;
      case sim::RandomConstraintBlockReferenceKind::Storage:
        if (expectedStorage < 0 ||
            block.getStorage().getValue().getZExtValue() !=
                static_cast<uint64_t>(expectedStorage)) {
          emitError(location)
              << "randomize constraint template static block is inconsistent";
          return failure();
        }
        break;
      }
    }
    constraintTemplate.getBody().walk(
        [&](sim::SimRandomSoftConstraintOp) { ++softCount; });
    if (softCount > 64) {
      emitError(location)
          << "random constraint template exceeds 64 soft priorities";
      return failure();
    }
  }

  auto callLifecycleHook = [&](StringRef calleeAttr, StringRef ownerAttr,
                               StringRef capturesAttr,
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
      hookReceiver = sim::SimClassCastOp::create(builder, location, ownerType,
                                                 hookReceiver);
    llvm::StringSet<> readCaptures;
    if (auto reads = op->getAttrOfType<ArrayAttr>(readsAttr))
      for (Attribute read : reads)
        readCaptures.insert(cast<StringAttr>(read).getValue());
    for (const auto &read : readCaptures) {
      Value capture = values.lookup(read.getKey());
      if (!capture)
        capture = lvalues.lookup(read.getKey());
      if (capture)
        recordSensitivity(capture);
    }
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
        arguments.push_back(capture);
      }
    sim::SimClassDirectCallOp::create(builder, location, TypeRange{}, callee,
                                      hookReceiver, arguments);
    return emitTerminationPoll(location);
  };
  if (failed(callLifecycleHook(
          randomPreHookAttrName, randomPreHookOwnerAttrName,
          randomPreHookCapturesAttrName, randomPreHookReadCapturesAttrName)))
    return failure();

  struct DomainPattern {
    APInt mask;
    APInt value;
    APInt freeMask;
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
    FlatSymbolRefAttr field;
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
    Value elementModeReference{};
    unsigned elementWidth = 0;
    Value disabledElements{};
    Value originalScalar{};
    Value currentValue{};
    DenseI64ArrayAttr storagePath{};
    Type storageType{};
    uint64_t parentModeMask = 0;
    uint64_t staticModeMask = UINT64_MAX;
    Value randcDomainReference{};
    Value nextRandcDomain{};
  };
  SmallVector<Property, 0> planned;
  uint64_t plannedWidth = 0;
  Type i64 = builder.getI64Type();
  auto captureProperty = [&](const Property &property) {
    CapturedLValue destination;
    destination.semanticNode = op;
    destination.type =
        property.storagePath ? property.storageType : property.type;
    destination.reference = property.reference;
    if (property.storagePath)
      for (int64_t ordinal : property.storagePath.asArrayRef()) {
        CapturedLValue member;
        member.kind = CapturedLValue::Kind::AggregateElement;
        member.semanticNode = op;
        member.type = sim::getAggregateElementType(destination.type, ordinal);
        member.ordinal = ordinal;
        member.children.push_back(std::move(destination));
        destination = std::move(member);
      }
    return destination;
  };
  auto loadProperty = [&](const Property &property) -> FailureOr<Value> {
    return loadCapturedLValue(captureProperty(property), location);
  };
  auto storeProperty = [&](const Property &property,
                           Value value) -> LogicalResult {
    auto destination = captureProperty(property);
    return writeCapturedLValue(destination, value, property.isSigned, false,
                               location);
  };
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
      auto modeIndexAttr = element
                               ? element.getAs<IntegerAttr>("rand_mode_index")
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
      auto declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, concreteType.getClassName());
      while (declaration &&
             !declaration->hasAttr("simulation.random_mode_field")) {
        if (!declaration.getBaseAttr())
          break;
        declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, declaration.getBaseAttr());
      }
      auto modeField = declaration
                           ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                 "simulation.random_mode_field")
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
    auto modeIndexAttr =
        property ? property.getAs<IntegerAttr>(randomPropertyModeIndexAttrName)
                 : IntegerAttr{};
    auto signedAttr =
        property ? property.getAs<BoolAttr>("is_signed") : BoolAttr{};
    auto randcAttr =
        property ? property.getAs<BoolAttr>("is_randc") : BoolAttr{};
    auto modeStorageAttr =
        property
            ? property.getAs<IntegerAttr>(randomPropertyModeStorageAttrName)
            : IntegerAttr{};
    bool isContainerSize =
        property && property.contains(randomContainerSizeAttrName);
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
        property
            ? property.getAs<FlatSymbolRefAttr>(randomNestedObjectFieldAttrName)
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
    std::optional<unsigned> typeWidth = getRandomPropertyWidth(type);
    if (!typeWidth || *typeWidth != width) {
      emitError(location) << "randomize property type width is inconsistent";
      return failure();
    }
    auto storagePath = property.getAs<DenseI64ArrayAttr>("storage_path");
    auto storageTypeAttr = property.getAs<TypeAttr>("storage_type");
    auto parentModeMaskAttr = property.getAs<IntegerAttr>("parent_mode_mask");
    auto staticModeMaskAttr = property.getAs<IntegerAttr>("static_mode_mask");
    if (storagePath || storageTypeAttr || parentModeMaskAttr ||
        staticModeMaskAttr) {
      if (!storagePath || storagePath.empty() || !storageTypeAttr ||
          !parentModeMaskAttr || !staticModeMaskAttr ||
          parentModeMaskAttr.getValue().getBitWidth() != 64 ||
          staticModeMaskAttr.getValue().getBitWidth() != 64 ||
          isContainerSize || isNestedObject) {
        emitError(location) << "random structure member storage is malformed";
        return failure();
      }
      Type selected = storageTypeAttr.getValue();
      for (int64_t ordinal : storagePath.asArrayRef()) {
        auto structure = dyn_cast<sim::UnpackedStructType>(selected);
        if (!structure || ordinal < 0 ||
            static_cast<uint64_t>(ordinal) >=
                sim::getAggregateNumElements(selected)) {
          emitError(location) << "random structure member path is malformed";
          return failure();
        }
        selected = sim::getAggregateElementType(selected, ordinal);
      }
      if (selected != type) {
        emitError(location)
            << "random structure member path type is inconsistent";
        return failure();
      }
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
      auto nestedDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, cast<sim::ClassHandleType>(nestedObjectTypeAttr.getValue())
                        .getClassName());
      while (nestedDeclaration &&
             !nestedDeclaration->hasAttr("simulation.random_mode_field")) {
        if (!nestedDeclaration.getBaseAttr())
          break;
        nestedDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, nestedDeclaration.getBaseAttr());
      }
      nestedModeField =
          nestedDeclaration
              ? nestedDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                    "simulation.random_mode_field")
              : FlatSymbolRefAttr{};
      if (!nestedModeField) {
        emitError(location) << "nested rand object has no rand_mode field";
        return failure();
      }
    } else if (field) {
      Type referencedType = isContainerSize ? containerTypeAttr.getValue()
                            : storagePath   ? storageTypeAttr.getValue()
                                            : type;
      Type referenceType = sim::ManagedRefType::get(
          function.getContext(), referencedType, objectType.getClassName());
      reference = sim::SimClassFieldRefOp::create(
          builder, location, referenceType, receiver, field);
    } else {
      reference = lvalues.lookup(referencePath.getValue());
      if (!reference || getReferenceElementType(reference) !=
                            (storagePath ? storageTypeAttr.getValue() : type)) {
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
    auto elementModeField =
        property.getAs<FlatSymbolRefAttr>("element_mode_field");
    auto elementModeStorage =
        property.getAs<IntegerAttr>("element_mode_storage");
    auto elementWidth = property.getAs<IntegerAttr>("element_width");
    Value elementModeReference;
    if (elementModeField || elementModeStorage || elementWidth) {
      auto layout = getFixedRandomArrayLayout(type);
      if (!layout || !elementWidth || elementWidth.getValue().isNegative() ||
          elementWidth.getValue().getActiveBits() > 32 ||
          elementWidth.getValue().getZExtValue() != layout->elementWidth ||
          static_cast<bool>(elementModeField) ==
              static_cast<bool>(elementModeStorage) ||
          isContainerSize || isNestedObject || (elementModeField && !field) ||
          (elementModeStorage &&
           (field || elementModeStorage.getValue().isNegative() ||
            elementModeStorage.getValue().getActiveBits() > 63))) {
        emitError(location) << "randomize array element modes are malformed";
        return failure();
      }
      if (!op->hasAttr(randomizeExplicitPropertiesAttrName)) {
        Type bitmapType = builder.getIntegerType(layout->elementCount);
        if (elementModeField)
          elementModeReference = sim::SimClassFieldRefOp::create(
              builder, location,
              sim::ManagedRefType::get(function.getContext(), bitmapType,
                                       objectType.getClassName()),
              receiver, elementModeField);
        else
          elementModeReference = sim::SimContextStorageOp::create(
              builder, location,
              sim::RefType::get(function.getContext(), bitmapType),
              function.getBody().front().getArgument(0), elementModeStorage);
      }
    }
    if (referencePath && !op->hasAttr(randomizeExplicitPropertiesAttrName) &&
        !randomModeStorage) {
      emitError(location)
          << "ordinary static random property has no shared rand_mode state";
      return failure();
    }
    Value randcKeyReference;
    Value randcPositionReference;
    Value randcDomainReference;
    if (randcAttr.getValue()) {
      auto cyclicWidth = sim::getPackedWidth(type);
      if (auto layout = getFixedRandomArrayLayout(type))
        cyclicWidth = layout->elementWidth;
      Type wordState = getRandomCyclicStateType(type, i64);
      Type domainState = getRandomCyclicStateType(
          type, sim::StringType::get(function.getContext()));
      auto keyField = property.getAs<FlatSymbolRefAttr>("randc_key_field");
      auto positionField =
          property.getAs<FlatSymbolRefAttr>("randc_position_field");
      auto domainField =
          property.getAs<FlatSymbolRefAttr>("randc_domain_field");
      auto keyPath = property.getAs<StringAttr>(randomRandCKeyPathAttrName);
      auto positionPath =
          property.getAs<StringAttr>(randomRandCPositionPathAttrName);
      auto domainPath =
          property.getAs<StringAttr>(randomRandCDomainPathAttrName);
      bool hasKeyField = static_cast<bool>(keyField);
      bool hasPositionField = static_cast<bool>(positionField);
      bool hasKeyPath = static_cast<bool>(keyPath);
      bool hasPositionPath = static_cast<bool>(positionPath);
      if (!cyclicWidth || *cyclicWidth > 32 ||
          hasKeyField != hasPositionField ||
          hasKeyField != static_cast<bool>(domainField) ||
          hasKeyPath != hasPositionPath ||
          hasKeyPath != static_cast<bool>(domainPath) ||
          hasKeyField == hasKeyPath ||
          hasKeyField != static_cast<bool>(field)) {
        emitError(location) << "randc property plan is malformed";
        return failure();
      }
      if (hasKeyField) {
        Type stateReferenceType = sim::ManagedRefType::get(
            function.getContext(), wordState, objectType.getClassName());
        randcKeyReference = sim::SimClassFieldRefOp::create(
            builder, location, stateReferenceType, receiver, keyField);
        randcPositionReference = sim::SimClassFieldRefOp::create(
            builder, location, stateReferenceType, receiver, positionField);
        randcDomainReference = sim::SimClassFieldRefOp::create(
            builder, location,
            sim::ManagedRefType::get(function.getContext(), domainState,
                                     objectType.getClassName()),
            receiver, domainField);
      } else {
        randcKeyReference = lvalues.lookup(keyPath.getValue());
        randcPositionReference = lvalues.lookup(positionPath.getValue());
        randcDomainReference = lvalues.lookup(domainPath.getValue());
        if (!randcKeyReference || !randcPositionReference ||
            !randcDomainReference ||
            getReferenceElementType(randcKeyReference) != wordState ||
            getReferenceElementType(randcPositionReference) != wordState ||
            getReferenceElementType(randcDomainReference) != domainState) {
          emitError(location)
              << "static randc state has no typed unit-local references";
          return failure();
        }
      }
    }
    SmallVector<PropertyDomain> propertyDomains;
    if (auto domains = property.getAs<ArrayAttr>("domains")) {
      llvm::SmallBitVector coveredBits(width);
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
        if (domainWidth == 0 || domainOffset > width ||
            domainWidth > width - domainOffset) {
          emitError(location) << "randomize property domain is out of range";
          return failure();
        }
        // Domain ranges are disjoint. Visit each covered bit once rather than
        // rebuilding an aggregate-width mask for every array leaf.
        for (uint64_t bit = domainOffset; bit != domainOffset + domainWidth;
             ++bit) {
          if (coveredBits.test(bit)) {
            emitError(location) << "randomize property domains overlap";
            return failure();
          }
          coveredBits.set(bit);
        }
        PropertyDomain propertyDomain{static_cast<uint32_t>(domainOffset),
                                      static_cast<unsigned>(domainWidth),
                                      {},
                                      0};
        SmallVector<solver::RandomDomainPattern> overlapPatterns;
        APInt cardinality(domainWidth + 1, 0);
        for (Attribute patternAttr : patternsAttr) {
          auto pattern = dyn_cast<DictionaryAttr>(patternAttr);
          auto maskAttr =
              pattern ? pattern.getAs<IntegerAttr>("mask") : IntegerAttr{};
          auto valueAttr =
              pattern ? pattern.getAs<IntegerAttr>("value") : IntegerAttr{};
          if (!maskAttr || !valueAttr ||
              maskAttr.getValue().getActiveBits() > domainWidth ||
              valueAttr.getValue().getActiveBits() > domainWidth) {
            emitError(location)
                << "randomize property domain pattern is malformed";
            return failure();
          }
          APInt patternMask = maskAttr.getValue().zextOrTrunc(domainWidth);
          APInt patternValue = valueAttr.getValue().zextOrTrunc(domainWidth);
          if (!(patternValue & ~patternMask).isZero()) {
            emitError(location)
                << "randomize property domain pattern is inconsistent";
            return failure();
          }
          overlapPatterns.push_back({patternMask, patternValue});
          APInt freeMask = ~patternMask;
          unsigned freeBits = freeMask.popcount();
          cardinality += APInt::getOneBitSet(domainWidth + 1, freeBits);
          uint64_t patternCardinality =
              freeBits >= 64 ? 0 : uint64_t{1} << freeBits;
          propertyDomain.patterns.push_back(
              {patternMask, patternValue, freeMask, patternCardinality});
        }
        solver::Satisfiability overlap =
            solver::checkRandomDomainOverlap(overlapPatterns);
        if (overlap != solver::Satisfiability::Unsatisfiable) {
          emitError(location)
              << (overlap == solver::Satisfiability::Satisfiable
                      ? "randomize property domain patterns overlap"
                      : "could not prove disjoint randomize property domain "
                        "patterns within compiler solver resources");
          return failure();
        }
        if (cardinality == APInt::getOneBitSet(domainWidth + 1, domainWidth))
          continue;
        // Generated-code radix paths use a 64-bit count even when the encoded
        // value is wider. Larger counts use the compiler-generated sampler.
        propertyDomain.cardinality =
            cardinality.getActiveBits() <= 64 ? cardinality.getZExtValue() : 0;
        propertyDomains.push_back(std::move(propertyDomain));
      }
    }
    SmallVector<ObjectPathElement> nestedObjectPath;
    if (failed(parseNestedObjectPath(nestedObjectPathAttr, nestedObjectPath)))
      return failure();
    planned.push_back(
        {field,
         type,
         static_cast<unsigned>(width),
         static_cast<unsigned>(modeIndexAttr.getValue().getZExtValue()),
         isContainerSize,
         containerTypeAttr ? containerTypeAttr.getValue() : Type{},
         sizeConstraintMaskAttr
             ? sizeConstraintMaskAttr.getValue().getZExtValue()
             : 0,
         unconditionalSizeConstraintAttr &&
             unconditionalSizeConstraintAttr.getValue(),
         nestedObjectReference,
         nestedObjectTypeAttr ? nestedObjectTypeAttr.getValue() : Type{},
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
    planned.back().elementModeReference = elementModeReference;
    planned.back().randcDomainReference = randcDomainReference;
    planned.back().storagePath = storagePath;
    planned.back().storageType =
        storageTypeAttr ? storageTypeAttr.getValue() : Type{};
    planned.back().parentModeMask =
        parentModeMaskAttr ? parentModeMaskAttr.getValue().getZExtValue() : 0;
    planned.back().staticModeMask =
        staticModeMaskAttr ? staticModeMaskAttr.getValue().getZExtValue()
                           : UINT64_MAX;
    planned.back().elementWidth =
        elementWidth
            ? static_cast<unsigned>(elementWidth.getValue().getZExtValue())
            : 0;
    plannedWidth += width;
  }
  if (plannedWidth != totalWidth) {
    emitError(location) << "randomize property plan width is inconsistent";
    return failure();
  }
  bool hasRealProperties = llvm::any_of(planned, [](const Property &property) {
    return isa<FloatType>(property.type);
  });
#ifndef OBELISK_ENABLE_Z3
  if (hasRealProperties) {
    emitError(location) << "IEEE real randomization requires compiler Z3 plans";
    return failure();
  }
#endif
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
        property ? property.getAs<IntegerAttr>("element_width") : IntegerAttr{};
    auto modeIndexAttr =
        property ? property.getAs<IntegerAttr>(randomPropertyModeIndexAttrName)
                 : IntegerAttr{};
    auto nestedObjectField =
        property
            ? property.getAs<FlatSymbolRefAttr>(randomNestedObjectFieldAttrName)
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
    bool hasCompleteNestedObject = nestedObjectField && nestedObjectTypeAttr &&
                                   nestedObjectStorageTypeAttr &&
                                   nestedModeIndexAttr;
    bool hasAnyNestedObject = nestedObjectField || nestedObjectTypeAttr ||
                              nestedObjectStorageTypeAttr ||
                              nestedModeIndexAttr;
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
      auto nestedDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, cast<sim::ClassHandleType>(nestedObjectTypeAttr.getValue())
                        .getClassName());
      while (nestedDeclaration &&
             !nestedDeclaration->hasAttr("simulation.random_mode_field")) {
        if (!nestedDeclaration.getBaseAttr())
          break;
        nestedDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, nestedDeclaration.getBaseAttr());
      }
      nestedModeField =
          nestedDeclaration
              ? nestedDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                    "simulation.random_mode_field")
              : FlatSymbolRefAttr{};
      if (!nestedModeField) {
        emitError(location) << "nested rand object has no rand_mode field";
        return failure();
      }
    } else {
      Type referenceType =
          sim::ManagedRefType::get(function.getContext(), typeAttr.getValue(),
                                   objectType.getClassName());
      reference = sim::SimClassFieldRefOp::create(
          builder, location, referenceType, receiver, field);
    }
    SmallVector<ObjectPathElement> nestedObjectPath;
    if ((!isNestedObject && nestedObjectPathAttr) ||
        failed(parseNestedObjectPath(nestedObjectPathAttr, nestedObjectPath)))
      return failure();
    plannedContainers.push_back(
        {typeAttr.getValue(), elementTypeAttr.getValue(),
         static_cast<unsigned>(elementWidth),
         static_cast<unsigned>(modeIndexAttr.getValue().getZExtValue()),
         reference, nestedObjectReference,
         nestedObjectTypeAttr ? nestedObjectTypeAttr.getValue() : Type{}, field,
         nestedModeIndexAttr
             ? static_cast<unsigned>(
                   nestedModeIndexAttr.getValue().getZExtValue())
             : 0,
         nestedModeField, std::move(nestedObjectPath), inertClassHandles,
         nestedObjectField});
  }
  SmallVector<bool> containerNeedsIdentity(plannedContainers.size(), false);
  auto sameContainerOccurrence = [](const ContainerProperty &left,
                                    const ContainerProperty &right) {
    if (left.nestedObjectRootField != right.nestedObjectRootField ||
        left.nestedObjectPath.size() != right.nestedObjectPath.size())
      return false;
    return llvm::equal(left.nestedObjectPath, right.nestedObjectPath,
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
  bool hasRuntimeForeachConstraint = false;
  for (auto [index, child] : llvm::enumerate(children))
    if (!isReceiverChild(index))
      child->walk([&](semantic::SVForeachConstraintOp) {
        hasRuntimeForeachConstraint = true;
      });
  bool useCyclicPlan = false;
#ifdef OBELISK_ENABLE_Z3
  useCyclicPlan = !checkerOnly && hasRandC && !hasRuntimeForeachConstraint;
#endif
  if (!checkerOnly && !useCyclicPlan &&
      llvm::any_of(planned, [](const Property &property) {
        return property.isRandC && isa<sim::UnpackedArrayType>(property.type);
      })) {
    emitError(location) << "randc arrays require compiler sampling plans "
                           "without runtime foreach constraints";
    return failure();
  }

  Value stateReference;
  Value randomContext;
  Value state;
  Value increment;
  Value mode;
  Value constraintMode;
  if (scopeRandomize) {
    // IEEE 1800-2017 18.12 and 18.14: scope randomization belongs to the
    // calling process's random stream, the same stream used by $urandom and
    // process random-state methods. Keep the opaque increment unchanged while
    // the generated solver advances and commits the state word.
    randomContext = function.getBody().front().getArgument(0);
    auto randomState =
        sim::SimRandomStateOp::create(builder, location, randomContext);
    state = randomState.getState();
    increment = randomState.getIncrement();
    mode = arith::ConstantOp::create(builder, location, i64,
                                     builder.getI64IntegerAttr(0));
    constraintMode = arith::ConstantOp::create(builder, location, i64,
                                               builder.getI64IntegerAttr(0));
  } else {
    sim::SimClassDeclOp declaration =
        lookupNearestSymbolFrom<sim::SimClassDeclOp>(function,
                                                     objectType.getClassName());
    while (declaration &&
           !declaration->hasAttr("simulation.random_state_field")) {
      if (!declaration.getBaseAttr())
        break;
      declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, declaration.getBaseAttr());
    }
    auto stateField = declaration
                          ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                "simulation.random_state_field")
                          : FlatSymbolRefAttr{};
    auto incrementField = declaration
                              ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                    "simulation.random_increment_field")
                              : FlatSymbolRefAttr{};
    auto modeField = declaration
                         ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                               "simulation.random_mode_field")
                         : FlatSymbolRefAttr{};
    auto constraintModeField =
        declaration ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                          "simulation.constraint_mode_field")
                    : FlatSymbolRefAttr{};
    if (!declaration || !stateField || !incrementField || !modeField ||
        !constraintModeField) {
      emitError(location)
          << "randomize receiver has no object-local stream or mode state";
      return failure();
    }
    Type randomReferenceType = sim::ManagedRefType::get(
        function.getContext(), i64, objectType.getClassName());
    stateReference = sim::SimClassFieldRefOp::create(
        builder, location, randomReferenceType, receiver, stateField);
    Value incrementReference = sim::SimClassFieldRefOp::create(
        builder, location, randomReferenceType, receiver, incrementField);
    Value modeReference = sim::SimClassFieldRefOp::create(
        builder, location, randomReferenceType, receiver, modeField);
    Value constraintModeReference = sim::SimClassFieldRefOp::create(
        builder, location, randomReferenceType, receiver, constraintModeField);
    state =
        sim::SimManagedLoadOp::create(builder, location, i64, stateReference);
    increment = sim::SimManagedLoadOp::create(builder, location, i64,
                                              incrementReference);
    mode = sim::SimManagedLoadOp::create(builder, location, i64, modeReference);
    constraintMode = sim::SimManagedLoadOp::create(builder, location, i64,
                                                   constraintModeReference);
  }

  auto loadRandomState = [&]() -> Value {
    if (scopeRandomize)
      return sim::SimRandomStateOp::create(builder, location, randomContext)
          .getState();
    return sim::SimManagedLoadOp::create(builder, location, i64,
                                         stateReference);
  };
  auto storeRandomState = [&](Value nextState) {
    if (scopeRandomize) {
      sim::SimRandomSetStateOp::create(builder, location, randomContext,
                                       nextState, increment);
      return;
    }
    sim::SimManagedStoreOp::create(builder, location, nextState,
                                   stateReference);
  };

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
    auto field =
        guard ? guard.getAs<FlatSymbolRefAttr>("field") : FlatSymbolRefAttr{};
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
    auto rootDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
        function, rootConcreteType.getClassName());
    while (rootDeclaration &&
           !rootDeclaration->hasAttr("simulation.random_mode_field")) {
      if (!rootDeclaration.getBaseAttr())
        break;
      rootDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, rootDeclaration.getBaseAttr());
    }
    auto rootModeField =
        rootDeclaration ? rootDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                              "simulation.random_mode_field")
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
    uint64_t outerModeIndex = outerModeIndexAttr.getValue().getZExtValue();
    Value outerModeBit = arith::AndIOp::create(
        builder, location, mode, constant64(uint64_t{1} << outerModeIndex));
    Value outerEnabled =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              outerModeBit, constant64(0));
    Value rootNonNull = arith::XOrIOp::create(
        builder, location, rootNull,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    Value rootActive =
        arith::AndIOp::create(builder, location, outerEnabled, rootNonNull);
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
      Value currentModeReference =
          sim::SimClassFieldRefOp::create(builder, location, modeReferenceType,
                                          currentObject, currentModeField);
      Value currentMode = sim::SimManagedLoadOp::create(builder, location, i64,
                                                        currentModeReference);
      Value edgeModeBit =
          arith::AndIOp::create(builder, location, currentMode,
                                constant64(uint64_t{1} << element.modeIndex));
      Value edgeEnabled =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                edgeModeBit, constant64(0));
      Type edgeReferenceType =
          sim::ManagedRefType::get(function.getContext(), element.storageType,
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
      Value edgeActive =
          arith::AndIOp::create(builder, location, edgeEnabled, edgeNonNull);
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
    Value ancestorID =
        sim::SimClassIdOp::create(builder, location, activeObjects[aliasDepth]);
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
    for (const auto &read : readCaptures) {
      Value capture = values.lookup(read.getKey());
      if (!capture)
        capture = lvalues.lookup(read.getKey());
      if (capture)
        recordSensitivity(capture);
    }
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
      arguments.push_back(capture);
    }
    sim::SimClassDirectCallOp::create(builder, location, TypeRange{}, callee,
                                      hookReceiver, arguments);
    return emitTerminationPoll(location);
  };
  for (Attribute hookAttr : nestedHooks) {
    auto hook = dyn_cast<DictionaryAttr>(hookAttr);
    auto field =
        hook ? hook.getAs<FlatSymbolRefAttr>("field") : FlatSymbolRefAttr{};
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
    Value isNull = sim::SimManagedIsNullOp::create(builder, location,
                                                   builder.getI1Type(), object);
    uint64_t outerModeIndex = outerModeIndexAttr.getValue().getZExtValue();
    Value outerModeBit = arith::AndIOp::create(
        builder, location, mode, constant64(uint64_t{1} << outerModeIndex));
    Value outerEnabled =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              outerModeBit, constant64(0));
    Value nonNull = arith::XOrIOp::create(
        builder, location, isNull,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    Value enabled =
        arith::AndIOp::create(builder, location, outerEnabled, nonNull);
    if (object.getType() != concreteTypeAttr.getValue())
      object = sim::SimClassCastOp::create(builder, location,
                                           concreteTypeAttr.getValue(), object);
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
      auto currentType =
          cast<sim::ClassHandleType>(concreteTypeAttr.getValue());
      for (const ObjectPathElement &element : path) {
        sim::SimClassDeclOp currentDeclaration =
            lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                function, currentType.getClassName());
        while (currentDeclaration &&
               !currentDeclaration->hasAttr("simulation.random_mode_field")) {
          if (!currentDeclaration.getBaseAttr())
            break;
          currentDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, currentDeclaration.getBaseAttr());
        }
        auto currentModeField =
            currentDeclaration
                ? currentDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                      "simulation.random_mode_field")
                : FlatSymbolRefAttr{};
        if (!currentModeField) {
          emitError(location) << "nested hook path has no rand_mode field";
          return failure();
        }
        Type modeReferenceType = sim::ManagedRefType::get(
            function.getContext(), i64, currentType.getClassName());
        Value currentModeReference = sim::SimClassFieldRefOp::create(
            builder, location, modeReferenceType, object, currentModeField);
        Value currentMode = sim::SimManagedLoadOp::create(
            builder, location, i64, currentModeReference);
        Value edgeModeBit =
            arith::AndIOp::create(builder, location, currentMode,
                                  constant64(uint64_t{1} << element.modeIndex));
        Value edgeEnabled =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  edgeModeBit, constant64(0));
        Type edgeReferenceType =
            sim::ManagedRefType::get(function.getContext(), element.storageType,
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
        Value edgeActive =
            arith::AndIOp::create(builder, location, edgeEnabled, edgeNonNull);
        Block *edgePresentBlock = addBlock();
        cf::CondBranchOp::create(builder, location, edgeActive,
                                 edgePresentBlock, ValueRange{}, missingBlock,
                                 ValueRange{});
        setCurrent(edgePresentBlock);
        object = *loadedEdge;
        if (object.getType() != element.concreteType)
          object = sim::SimClassCastOp::create(builder, location,
                                               element.concreteType, object);
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
      Value sameID =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                objectID, previous.objectID);
      Value previousMatch =
          arith::AndIOp::create(builder, location, previous.enabled, sameID);
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
    auto field =
        nested ? nested.getAs<FlatSymbolRefAttr>("field") : FlatSymbolRefAttr{};
    auto concreteTypeAttr =
        nested ? nested.getAs<TypeAttr>("concrete_type") : TypeAttr{};
    auto storageTypeAttr =
        nested ? nested.getAs<TypeAttr>("storage_type") : TypeAttr{};
    auto globalIndices = nested
                             ? nested.getAs<DenseI64ArrayAttr>("global_indices")
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
        emitError(location) << "nested constraint-mode index is malformed";
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
    Value outerDisabled =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                              outerModeBit, constant64(0));
    Value rootInactive =
        arith::OrIOp::create(builder, location, isNull, outerDisabled);
    Block *disabledBlock = addBlock();
    Block *objectBlock = addBlock();
    Block *mergeBlock = addBlock();
    mergeBlock->addArgument(i64, location);
    mergeBlock->addArgument(builder.getI1Type(), location);
    cf::CondBranchOp::create(builder, location, rootInactive, disabledBlock,
                             ValueRange{}, objectBlock, ValueRange{});

    setCurrent(disabledBlock);
    Value disabledMode = arith::OrIOp::create(builder, location, constraintMode,
                                              constant64(globalMask));
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    cf::BranchOp::create(builder, location, mergeBlock,
                         ValueRange{disabledMode, trueValue});

    setCurrent(objectBlock);
    Value object = *loadedObject;
    if (object.getType() != concreteTypeAttr.getValue())
      object = sim::SimClassCastOp::create(builder, location,
                                           concreteTypeAttr.getValue(), object);
    auto concreteType = cast<sim::ClassHandleType>(concreteTypeAttr.getValue());
    for (const ObjectPathElement &element : path) {
      sim::SimClassDeclOp currentDeclaration =
          lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, concreteType.getClassName());
      while (currentDeclaration &&
             !currentDeclaration->hasAttr("simulation.random_mode_field")) {
        if (!currentDeclaration.getBaseAttr())
          break;
        currentDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, currentDeclaration.getBaseAttr());
      }
      auto currentModeField =
          currentDeclaration
              ? currentDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                    "simulation.random_mode_field")
              : FlatSymbolRefAttr{};
      if (!currentModeField) {
        emitError(location) << "nested constraint path has no rand_mode field";
        return failure();
      }
      Type modeReferenceType = sim::ManagedRefType::get(
          function.getContext(), i64, concreteType.getClassName());
      Value modeReference = sim::SimClassFieldRefOp::create(
          builder, location, modeReferenceType, object, currentModeField);
      Value objectMode =
          sim::SimManagedLoadOp::create(builder, location, i64, modeReference);
      Value edgeModeBit =
          arith::AndIOp::create(builder, location, objectMode,
                                constant64(uint64_t{1} << element.modeIndex));
      Value edgeDisabled =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                edgeModeBit, constant64(0));
      Type edgeReferenceType =
          sim::ManagedRefType::get(function.getContext(), element.storageType,
                                   concreteType.getClassName());
      Value edgeReference = sim::SimClassFieldRefOp::create(
          builder, location, edgeReferenceType, object, element.field);
      FailureOr<Value> loadedEdge = loadReference(edgeReference, location);
      if (failed(loadedEdge))
        return failure();
      Value edgeNull = sim::SimManagedIsNullOp::create(
          builder, location, builder.getI1Type(), *loadedEdge);
      Value edgeInactive =
          arith::OrIOp::create(builder, location, edgeDisabled, edgeNull);
      Block *nextObjectBlock = addBlock();
      cf::CondBranchOp::create(builder, location, edgeInactive, disabledBlock,
                               ValueRange{}, nextObjectBlock, ValueRange{});
      setCurrent(nextObjectBlock);
      object = *loadedEdge;
      if (object.getType() != element.concreteType)
        object = sim::SimClassCastOp::create(builder, location,
                                             element.concreteType, object);
      concreteType = cast<sim::ClassHandleType>(element.concreteType);
    }
    sim::SimClassDeclOp nestedDeclaration =
        lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, concreteType.getClassName());
    while (nestedDeclaration &&
           !nestedDeclaration->hasAttr("simulation.constraint_mode_field")) {
      if (!nestedDeclaration.getBaseAttr())
        break;
      nestedDeclaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, nestedDeclaration.getBaseAttr());
    }
    auto nestedModeField =
        nestedDeclaration ? nestedDeclaration->getAttrOfType<FlatSymbolRefAttr>(
                                "simulation.constraint_mode_field")
                          : FlatSymbolRefAttr{};
    if (!nestedModeField) {
      emitError(location) << "nested rand object has no constraint_mode field";
      return failure();
    }
    Type nestedModeReferenceType = sim::ManagedRefType::get(
        function.getContext(), i64, concreteType.getClassName());
    Value nestedModeReference = sim::SimClassFieldRefOp::create(
        builder, location, nestedModeReferenceType, object, nestedModeField);
    Value nestedMode = sim::SimManagedLoadOp::create(builder, location, i64,
                                                     nestedModeReference);
    Value mappedMode = arith::AndIOp::create(builder, location, constraintMode,
                                             constant64(~globalMask));
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
    Value inactiveMask =
        arith::SelectOp::create(builder, location, mergeBlock->getArgument(1),
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
      constraintMode = arith::AndIOp::create(builder, location, constraintMode,
                                             constant64(~bit));
      Value reference = sim::SimContextStorageOp::create(
          builder, location, referenceType, context,
          builder.getI64IntegerAttr(storage));
      Value staticMode =
          sim::SimRefLoadOp::create(builder, location, i64, reference);
      Value disabled =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                staticMode, constant64(0));
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
  constraintMode = arith::OrIOp::create(builder, location, constraintMode,
                                        nullNestedConstraintMask);
  unsigned assignmentWidth = std::max<uint64_t>(64, totalWidth);
  auto assignmentType =
      IntegerType::get(function.getContext(), assignmentWidth);
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
      uint64_t bit = uint64_t{1} << property.modeIndex;
      if (!property.randomModeStorage) {
        if (property.parentModeMask) {
          Value parentDisabled = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne,
              arith::AndIOp::create(builder, location, mode,
                                    constant64(property.parentModeMask)),
              constant64(0));
          Value selected =
              arith::SelectOp::create(builder, location, parentDisabled,
                                      constant64(bit), constant64(0));
          relevantMode =
              arith::OrIOp::create(builder, location, relevantMode, selected);
        }
        continue;
      }
      relevantMode = arith::AndIOp::create(builder, location, relevantMode,
                                           constant64(~bit));
      Value reference = sim::SimContextStorageOp::create(
          builder, location, referenceType, context,
          builder.getI64IntegerAttr(*property.randomModeStorage));
      Value staticMode =
          sim::SimRefLoadOp::create(builder, location, i64, reference);
      Value disabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(builder, location, staticMode,
                                constant64(property.staticModeMask)),
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
      constraintCount == 64 ? UINT64_MAX : (uint64_t{1} << constraintCount) - 1;
  Value relevantConstraintMode = arith::AndIOp::create(
      builder, location, constraintMode, constant64(constraintModeMask));
  Value allConstraintsEnabled =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                            relevantConstraintMode, constant64(0));

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
        chunk =
            arith::ExtUIOp::create(builder, location, assignmentType, chunk);
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
    assert(property.width <= 64);
    uint64_t result = 0;
    for (const PropertyDomain &domain : property.domains) {
      uint64_t mask =
          domain.width == 64 ? UINT64_MAX : (uint64_t{1} << domain.width) - 1;
      result |= mask << domain.offset;
    }
    return result;
  };
  auto propertyDomainCardinality = [&](const Property &property) {
    unsigned ordinaryBits = property.width;
    for (const PropertyDomain &domain : property.domains)
      ordinaryBits -= domain.width;
    if (ordinaryBits >= 64)
      return uint64_t{0};
    uint64_t cardinality = uint64_t{1} << ordinaryBits;
    for (const PropertyDomain &domain : property.domains) {
      if (domain.cardinality == 0 ||
          cardinality > UINT64_MAX / domain.cardinality)
        return uint64_t{0};
      cardinality *= domain.cardinality;
    }
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
              pattern.value.getZExtValue() |
              expandConstantBits(groupIndex - base,
                                 pattern.freeMask.getZExtValue());
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
    if (property.width > 64) {
      Type fieldType = builder.getIntegerType(property.width);
      auto resize = [&](Value bits, Type type) -> Value {
        if (bits.getType() == type)
          return bits;
        return arith::ExtUIOp::create(builder, location, type, bits);
      };
      auto expand = [&](Value ordinal, const APInt &outputMask,
                        Type type) -> Value {
        Value result = arith::ConstantOp::create(
            builder, location, type, builder.getIntegerAttr(type, 0));
        unsigned inputBit = 0;
        for (unsigned outputBit = 0; outputBit != outputMask.getBitWidth();
             ++outputBit) {
          if (!outputMask[outputBit])
            continue;
          Value bit = arith::AndIOp::create(
              builder, location,
              arith::ShRUIOp::create(builder, location, ordinal,
                                     constant64(inputBit++)),
              constant64(1));
          bit = resize(bit, type);
          if (outputBit)
            bit = arith::ShLIOp::create(builder, location, bit,
                                        resize(constant64(outputBit), type));
          result = arith::OrIOp::create(builder, location, result, bit);
        }
        return result;
      };
      // Clear disjoint group ranges in one mask. Group literals retain their
      // natural width and are extended in SSA, avoiding a parent-width APInt
      // for every leaf of a wide aggregate.
      APInt ordinaryMask = APInt::getAllOnes(property.width);
      for (const PropertyDomain &domain : property.domains)
        ordinaryMask.clearBits(domain.offset, domain.offset + domain.width);
      Value field = expand(index, ordinaryMask, fieldType);
      unsigned ordinaryBits = ordinaryMask.popcount();
      Value remaining = index;
      if (ordinaryBits)
        remaining = arith::ShRUIOp::create(builder, location, remaining,
                                           constant64(ordinaryBits));
      for (const PropertyDomain &domain : property.domains) {
        Value groupIndex = arith::RemUIOp::create(
            builder, location, remaining, constant64(domain.cardinality));
        remaining = arith::DivUIOp::create(builder, location, remaining,
                                           constant64(domain.cardinality));
        Type groupType = builder.getIntegerType(std::max(64u, domain.width));
        Value selected;
        uint64_t base = 0;
        for (const DomainPattern &pattern : domain.patterns) {
          Value ordinal = arith::SubIOp::create(builder, location, groupIndex,
                                                constant64(base));
          Value candidate = arith::OrIOp::create(
              builder, location,
              arith::ConstantOp::create(
                  builder, location, groupType,
                  builder.getIntegerAttr(
                      groupType,
                      pattern.value.zextOrTrunc(std::max(64u, domain.width)))),
              expand(ordinal, pattern.freeMask, groupType));
          if (!selected)
            selected = candidate;
          else {
            Value use = arith::CmpIOp::create(builder, location,
                                              arith::CmpIPredicate::uge,
                                              groupIndex, constant64(base));
            selected = arith::SelectOp::create(builder, location, use,
                                               candidate, selected);
          }
          base += pattern.cardinality;
        }
        selected = resize(selected, fieldType);
        if (domain.offset)
          selected = arith::ShLIOp::create(
              builder, location, selected,
              resize(constant64(domain.offset), fieldType));
        field = arith::OrIOp::create(builder, location, field, selected);
      }
      return field;
    }
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
          builder, location, constant64(last.value.getZExtValue()),
          expandStaticBits(arith::SubIOp::create(builder, location, groupIndex,
                                                 constant64(lastBase)),
                           last.freeMask.getZExtValue()));
      uint64_t end = lastBase;
      for (const DomainPattern &pattern :
           llvm::reverse(ArrayRef(domain.patterns).drop_back())) {
        end -= pattern.cardinality;
        Value candidate = arith::OrIOp::create(
            builder, location, constant64(pattern.value.getZExtValue()),
            expandStaticBits(arith::SubIOp::create(builder, location,
                                                   groupIndex, constant64(end)),
                             pattern.freeMask.getZExtValue()));
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
  auto toRandomScalar = [&](Value value,
                            Location valueLocation) -> FailureOr<Value> {
    if (!isa<sim::UnpackedArrayType>(value.getType()))
      return toPackedScalar(value, valueLocation);
    auto width = getIntegralRandomWidth(value.getType());
    if (!width || *width == 0)
      return failure();
    Type scalar = streamContainsFourState(value.getType())
                      ? Type(sim::LogicType::get(function.getContext(), *width))
                      : Type(builder.getIntegerType(*width));
    return convertFixedAggregateBitstream(value, scalar, scalar, valueLocation);
  };
  Value zeroAssignment = constantAssignment64(0);
  Value currentAssignment = zeroAssignment;
  Value mutableMask = zeroAssignment;
  SmallVector<Value> propertyEnabled;
  bool hasArrayElementModes =
      llvm::any_of(planned, [](const Property &property) {
        return static_cast<bool>(property.elementModeReference);
      });
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
                       arith::ConstantOp::create(builder, location,
                                                 builder.getI1Type(),
                                                 builder.getBoolAttr(false)),
                       constant64(0)});

        setCurrent(objectBlock);
        Value object = *loadedObject;
        if (object.getType() != property.nestedObjectType)
          object = sim::SimClassCastOp::create(
              builder, location, property.nestedObjectType, object);
        auto concreteType =
            cast<sim::ClassHandleType>(property.nestedObjectType);
        FlatSymbolRefAttr objectModeField = property.nestedModeField;
        for (const ObjectPathElement &element : property.nestedObjectPath) {
          Type modeReferenceType = sim::ManagedRefType::get(
              function.getContext(), i64, concreteType.getClassName());
          Value modeReference = sim::SimClassFieldRefOp::create(
              builder, location, modeReferenceType, object, objectModeField);
          Value objectMode = sim::SimManagedLoadOp::create(builder, location,
                                                           i64, modeReference);
          Value edgeModeBit = arith::AndIOp::create(
              builder, location, objectMode,
              constant64(uint64_t{1} << element.modeIndex));
          Value edgeEnabled =
              arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                    edgeModeBit, constant64(0));
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
          Value edgeActive = arith::AndIOp::create(builder, location,
                                                   edgeEnabled, edgeNonNull);
          Block *edgeBlock = addBlock();
          cf::CondBranchOp::create(builder, location, edgeActive, edgeBlock,
                                   ValueRange{}, nullBlock, ValueRange{});
          setCurrent(edgeBlock);
          object = *loadedEdge;
          if (object.getType() != element.concreteType)
            object = sim::SimClassCastOp::create(builder, location,
                                                 element.concreteType, object);
          concreteType = cast<sim::ClassHandleType>(element.concreteType);
          objectModeField = element.modeField;
        }
        Type nestedFieldType =
            property.isContainerSize ? property.containerType : property.type;
        Type fieldReferenceType =
            sim::ManagedRefType::get(function.getContext(), nestedFieldType,
                                     concreteType.getClassName());
        Value fieldReference = sim::SimClassFieldRefOp::create(
            builder, location, fieldReferenceType, object,
            property.nestedField);
        FailureOr<Value> value = loadReference(fieldReference, location);
        Type modeReferenceType = sim::ManagedRefType::get(
            function.getContext(), i64, concreteType.getClassName());
        Value childModeReference = sim::SimClassFieldRefOp::create(
            builder, location, modeReferenceType, object, objectModeField);
        Value childMode = sim::SimManagedLoadOp::create(builder, location, i64,
                                                        childModeReference);
        Value childModeBit = arith::AndIOp::create(
            builder, location, childMode,
            constant64(uint64_t{1} << property.nestedModeIndex));
        Value childEnabled =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  childModeBit, constant64(0));
        if (failed(value))
          return failure();
        if (property.isContainerSize) {
          Value size =
              sim::SimContainerSizeOp::create(builder, location, i64, *value);
          value =
              arith::TruncIOp::create(builder, location, property.type, size)
                  .getResult();
        }
        cf::BranchOp::create(
            builder, location, mergeBlock,
            ValueRange{*value, childEnabled,
                       sim::SimClassIdOp::create(builder, location, object)});
        setCurrent(mergeBlock);
        nestedEnabled = mergeBlock->getArgument(1);
        property.nestedObjectID = mergeBlock->getArgument(2);
        return mergeBlock->getArgument(0);
      }
      if (!property.isContainerSize)
        return loadProperty(property);
      FailureOr<Value> container = loadReference(property.reference, location);
      if (failed(container))
        return failure();
      Value size =
          sim::SimContainerSizeOp::create(builder, location, i64, *container);
      return arith::TruncIOp::create(builder, location, property.type, size)
          .getResult();
    }();
    if (failed(current))
      return failure();
    property.currentValue = *current;
    FailureOr<Value> scalar = [&]() -> FailureOr<Value> {
      if (isa<FloatType>(property.type)) {
        property.originalScalar = *current;
        auto rank = sim::SimRandomRealToRankOp::create(
            builder, location, builder.getIntegerType(property.width),
            builder.getI1Type(), *current);
        return rank.getRank();
      }
      return toRandomScalar(*current, location);
    }();
    if (succeeded(scalar) && !isa<FloatType>(property.type))
      property.originalScalar = *scalar;
    FailureOr<Value> extended =
        succeeded(scalar) ? convert(*scalar, assignmentType, property.isSigned,
                                    location, false)
                          : FailureOr<Value>(failure());
    if (failed(extended))
      return failure();
    APInt valueMask = APInt::getLowBitsSet(assignmentWidth, property.width);
    Value bits = arith::AndIOp::create(builder, location, *extended,
                                       constantAssignment(valueMask));

    Value propertyMode =
        arith::AndIOp::create(builder, location, relevantMode,
                              constant64(uint64_t{1} << property.modeIndex));
    Value enabled =
        checkerOnly
            ? arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false))
                  .getResult()
            : arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                    propertyMode, constant64(0))
                  .getResult();
    if (nestedEnabled)
      enabled =
          arith::AndIOp::create(builder, location, enabled, nestedEnabled);
    Value activeElementMask;
    if (property.elementModeReference) {
      FailureOr<Value> bitmap =
          loadReference(property.elementModeReference, location);
      if (failed(bitmap))
        return failure();
      property.disabledElements = *bitmap;
      auto bitmapType = cast<IntegerType>((*bitmap).getType());
      auto bitmapConstant = [&](APInt value) -> Value {
        return arith::ConstantOp::create(
            builder, location, bitmapType,
            builder.getIntegerAttr(bitmapType, value));
      };
      Value zero = bitmapConstant(APInt(bitmapType.getWidth(), 0));
      Value allDisabled =
          bitmapConstant(APInt::getAllOnes(bitmapType.getWidth()));
      Value anyActive = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, *bitmap, allDisabled);
      enabled = arith::AndIOp::create(builder, location, enabled, anyActive);
      Value allActive = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, *bitmap, zero);
      randomizationEnabled = arith::AndIOp::create(
          builder, location, randomizationEnabled, allActive);
      // Solver masks describe representation bits, while the persistent
      // array inventory has one bit per complete integral element. Spread
      // those bits with logarithmically many shifts, then fill each leaf's
      // representation. Do not emit an aggregate-width constant per element.
      Value activeBits =
          arith::XOrIOp::create(builder, location, *bitmap, allDisabled);
      FailureOr<Value> extendedModes =
          convert(activeBits, assignmentType, false, location);
      if (failed(extendedModes))
        return failure();
      activeElementMask = *extendedModes;
      if (property.elementWidth > 1) {
        uint64_t highestBlock =
            bitmapType.getWidth() > 1
                ? uint64_t{1} << llvm::Log2_64(bitmapType.getWidth() - 1)
                : 0;
        for (uint64_t block = highestBlock; block; block >>= 1) {
          uint64_t distance = uint64_t{property.elementWidth - 1} * block;
          APInt placement(assignmentWidth, 0);
          for (uint64_t leaf = 0; leaf < bitmapType.getWidth(); ++leaf)
            placement.setBit(leaf + uint64_t{property.elementWidth - 1} *
                                        (leaf & ~(block - 1)));
          Value shifted =
              arith::ShLIOp::create(builder, location, activeElementMask,
                                    constantAssignment64(distance));
          activeElementMask = arith::AndIOp::create(
              builder, location,
              arith::OrIOp::create(builder, location, activeElementMask,
                                   shifted),
              constantAssignment(placement));
        }
        for (uint64_t filled = 1; filled < property.elementWidth;) {
          uint64_t step =
              std::min(filled, uint64_t{property.elementWidth} - filled);
          activeElementMask = arith::OrIOp::create(
              builder, location, activeElementMask,
              arith::ShLIOp::create(builder, location, activeElementMask,
                                    constantAssignment64(step)));
          filled += step;
        }
      }
      if (currentOffset)
        activeElementMask =
            arith::ShLIOp::create(builder, location, activeElementMask,
                                  constantAssignment64(currentOffset));
    }
    if (property.isContainerSize && !property.hasUnconditionalSizeConstraint) {
      Value disabledSizeConstraints =
          arith::AndIOp::create(builder, location, relevantConstraintMode,
                                constant64(property.sizeConstraintMask));
      Value allSizeConstraintsDisabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, disabledSizeConstraints,
          constant64(property.sizeConstraintMask));
      enabled = arith::AndIOp::create(
          builder, location, enabled,
          arith::XOrIOp::create(
              builder, location, allSizeConstraintsDisabled,
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(true))));
    }
    propertyEnabled.push_back(enabled);
    if (property.isRandC &&
        (useCyclicPlan ||
         (checkerOnly && isa<sim::UnpackedArrayType>(property.type)))) {
      auto key = loadReference(property.randcKeyReference, location);
      auto position = loadReference(property.randcPositionReference, location);
      auto domain = loadReference(property.randcDomainReference, location);
      if (failed(key) || failed(position) || failed(domain))
        return failure();
      property.nextRandcKey = *key;
      property.nextRandcPosition = *position;
      property.nextRandcDomain = *domain;
    } else if (property.isRandC) {
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
        if (value.getType() != assignmentType)
          value =
              arith::ExtUIOp::create(builder, location, assignmentType, value);
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
        if (semanticValue.getType() != assignmentType)
          semanticValue = arith::ExtUIOp::create(builder, location,
                                                 assignmentType, semanticValue);
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
      FailureOr<Value> cyclic = convert(bits, property.type, false, location);
      if (failed(cyclic))
        return failure();
      property.currentValue = arith::SelectOp::create(
          builder, location, enabled, *cyclic, property.currentValue);
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
    if (activeElementMask)
      enabledMask = arith::AndIOp::create(builder, location, enabledMask,
                                          activeElementMask);
    mutableMask =
        arith::OrIOp::create(builder, location, mutableMask, enabledMask);
    currentOffset += property.width;
  }
  if (hasArrayElementModes) {
    Value anyActive = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    for (Value enabled : propertyEnabled)
      anyActive = arith::OrIOp::create(builder, location, anyActive, enabled);
    for (const ContainerProperty &property : plannedContainers) {
      if (property.inertClassHandles)
        continue;
      Value active = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq,
          arith::AndIOp::create(builder, location, relevantMode,
                                constant64(uint64_t{1} << property.modeIndex)),
          constant64(0));
      anyActive = arith::OrIOp::create(builder, location, anyActive, active);
    }
    allPropertiesDisabled = arith::XOrIOp::create(
        builder, location, anyActive,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
  }
  struct CyclicField {
    unsigned width, offset;
    Value enabled, key, position, domain, mask{};
  };
  SmallVector<CyclicField> cyclicFields;
  SmallVector<std::pair<unsigned, unsigned>> cyclicFieldRanges(planned.size());
  if (useCyclicPlan)
    for (auto [index, property] : llvm::enumerate(planned)) {
      if (!property.isRandC)
        continue;
      auto layout = getFixedRandomArrayLayout(property.type);
      unsigned count = layout ? layout->elementCount : 1;
      unsigned width = layout ? layout->elementWidth : property.width;
      unsigned first = cyclicFields.size();
      std::function<void(Type, Value, Value, Value)> visit = [&](Type type,
                                                                 Value key,
                                                                 Value position,
                                                                 Value domain) {
        if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
          for (unsigned ordinal = 0;
               ordinal != sim::getAggregateNumElements(array); ++ordinal) {
            auto extract = [&](Value value) -> Value {
              return sim::SimAggregateExtractOp::create(
                  builder, location,
                  sim::getAggregateElementType(value.getType(), ordinal), value,
                  builder.getI64IntegerAttr(ordinal));
            };
            visit(array.getElementType(), extract(key), extract(position),
                  extract(domain));
          }
          return;
        }
        // Logical bit streams place the first declared leaf at the high
        // end. The persistent mode bitmap follows low-to-high bit offsets.
        unsigned leaf = count - 1 - (cyclicFields.size() - first);
        Value enabled = propertyEnabled[index];
        if (property.disabledElements) {
          Type bitmapType = property.disabledElements.getType();
          Value shift = constant64(leaf);
          unsigned bitmapWidth = cast<IntegerType>(bitmapType).getWidth();
          if (bitmapWidth < 64)
            shift =
                arith::TruncIOp::create(builder, location, bitmapType, shift);
          else if (bitmapWidth > 64)
            shift =
                arith::ExtUIOp::create(builder, location, bitmapType, shift);
          Value disabled = arith::ShRUIOp::create(
              builder, location, property.disabledElements, shift);
          if (!disabled.getType().isInteger(1))
            disabled = arith::TruncIOp::create(builder, location,
                                               builder.getI1Type(), disabled);
          Value active = arith::XOrIOp::create(
              builder, location, disabled,
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(true)));
          enabled = arith::AndIOp::create(builder, location, enabled, active);
        }
        cyclicFields.push_back({width, propertyOffsets[index] + leaf * width,
                                enabled, key, position, domain});
      };
      visit(property.type, property.nextRandcKey, property.nextRandcPosition,
            property.nextRandcDomain);
      cyclicFieldRanges[index] = {first, cyclicFields.size()};
    }
  Value activeGuardBits = mutableMask;
  auto includeCyclicBits = [&](unsigned width, unsigned fieldOffset,
                               Value enabled) -> Value {
    // Each leaf uses bounded-width constants and the shared assignment zero.
    // Avoid allocating an aggregate-width literal for every array element.
    Value propertyMask =
        constant64(APInt::getLowBitsSet(64, width).getZExtValue());
    if (assignmentType != i64)
      propertyMask = arith::ExtUIOp::create(builder, location, assignmentType,
                                            propertyMask);
    if (fieldOffset) {
      Value offset = constant64(fieldOffset);
      if (assignmentType != i64)
        offset =
            arith::ExtUIOp::create(builder, location, assignmentType, offset);
      propertyMask =
          arith::ShLIOp::create(builder, location, propertyMask, offset);
    }
    activeGuardBits = arith::OrIOp::create(
        builder, location, activeGuardBits,
        arith::SelectOp::create(builder, location, enabled, propertyMask,
                                zeroAssignment));
    return propertyMask;
  };
  if (useCyclicPlan)
    for (CyclicField &field : cyclicFields)
      field.mask = includeCyclicBits(field.width, field.offset, field.enabled);
  else
    for (auto [index, property] : llvm::enumerate(planned))
      if (property.isRandC && !isa<sim::UnpackedArrayType>(property.type))
        includeCyclicBits(property.width, propertyOffsets[index],
                          propertyEnabled[index]);
  struct AliasEquality {
    uint32_t leftOffset;
    uint32_t rightOffset;
    unsigned width;
    Value active;
  };
  SmallVector<AliasEquality> aliasEqualities;
  auto sameObjectOccurrence = [](const Property &left, const Property &right) {
    if (left.nestedObjectRootField != right.nestedObjectRootField ||
        left.nestedObjectPath.size() != right.nestedObjectPath.size())
      return false;
    return llvm::equal(left.nestedObjectPath, right.nestedObjectPath,
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
      Value bothEnabled =
          arith::AndIOp::create(builder, location, propertyEnabled[leftIndex],
                                propertyEnabled[rightIndex]);
      Value sameID =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                left.nestedObjectID, right.nestedObjectID);
      aliasEqualities.push_back(
          {propertyOffsets[leftIndex], propertyOffsets[rightIndex], left.width,
           arith::AndIOp::create(builder, location, bothEnabled, sameID)});
    }
  }
  Value fixedAssignment = arith::AndIOp::create(
      builder, location, currentAssignment,
      arith::XOrIOp::create(builder, location, mutableMask, mask));

  bool hasSoftConstraint = false;
  for (auto [index, child] : llvm::enumerate(children)) {
    if (isReceiverChild(index))
      continue;
    child->walk([&](semantic::SVExpressionConstraintOp expression) {
      hasSoftConstraint |= expression.getIsSoft();
    });
  }
  if (constraintTemplate)
    constraintTemplate.getBody().walk(
        [&](sim::SimRandomSoftConstraintOp) { hasSoftConstraint = true; });

  struct SolveBeforeEdge {
    APInt beforeMask;
    APInt afterMask;
    uint32_t constraintBlock;
    bool functionPriority = false;

    bool operator==(const SolveBeforeEdge &other) const {
      return beforeMask == other.beforeMask && afterMask == other.afterMask &&
             constraintBlock == other.constraintBlock &&
             functionPriority == other.functionPriority;
    }
  };
  SmallVector<APInt> propertyMasks;
  uint64_t solvePropertyOffset = 0;
  for (const Property &property : planned) {
    propertyMasks.push_back(
        APInt::getLowBitsSet(assignmentWidth, property.width)
            .shl(solvePropertyOffset));
    solvePropertyOffset += property.width;
  }
  SmallVector<SolveBeforeEdge> solveBeforeEdges;
  llvm::DenseSet<std::tuple<APInt, APInt, uint32_t, uint8_t>>
      solveBeforeEdgeKeys;
  auto appendSolveBeforeEdge = [&](const SolveBeforeEdge &edge) {
    if (solveBeforeEdgeKeys
            .insert({edge.beforeMask, edge.afterMask, edge.constraintBlock,
                     static_cast<uint8_t>(edge.functionPriority)})
            .second)
      solveBeforeEdges.push_back(edge);
  };
  SmallVector<APInt> solveBeforeLayerMasks;
  APInt randomAssignmentMask =
      APInt::getLowBitsSet(assignmentWidth, totalWidth);
  bool hasSolveBefore = false;
  bool hasFunctionPriority = false;
  auto randomPropertyMask = [&](Operation *expression) -> FailureOr<APInt> {
    FailureOr<Type> type = getNormalizedSemanticType(expression);
    auto width = succeeded(type) ? sim::getPackedWidth(*type) : std::nullopt;
    if (!width || *width == 0) {
      emitError(getSemanticLocation(expression))
          << "solve before operands must be integral random values";
      return failure();
    }
    if (auto offset = expression->getAttrOfType<IntegerAttr>(
            randomVariableBitOffsetAttrName)) {
      const APInt &value = offset.getValue();
      if (value.isNegative() || value.getActiveBits() > 32 ||
          value.getZExtValue() > totalWidth ||
          *width > totalWidth - value.getZExtValue()) {
        emitError(getSemanticLocation(expression))
            << "solve before selected path is out of range";
        return failure();
      }
      APInt mask = APInt::getLowBitsSet(assignmentWidth, *width)
                       .shl(value.getZExtValue());
      for (auto [index, property] : llvm::enumerate(planned))
        if (property.isRandC && !(mask & propertyMasks[index]).isZero()) {
          emitError(getSemanticLocation(expression))
              << "solve before cannot order randc values";
          return failure();
        }
      return mask;
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
    unsigned index = indexAttr.getValue().getZExtValue();
    if (planned[index].isRandC) {
      emitError(getSemanticLocation(expression))
          << "solve before cannot order randc values";
      return failure();
    }
    return propertyMasks[index];
  };
  for (auto [index, root] : llvm::enumerate(children)) {
    if (isReceiverChild(index))
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
    if (Attribute order = root->getAttr(randomFunctionOrderAttrName)) {
      hasSolveBefore = true;
      hasFunctionPriority = true;
      SmallVector<APInt> pairs;
      if (auto dense = dyn_cast<DenseI64ArrayAttr>(order)) {
        for (int64_t value : dense.asArrayRef())
          pairs.emplace_back(assignmentWidth, static_cast<uint64_t>(value));
      } else if (auto array = dyn_cast<ArrayAttr>(order)) {
        for (Attribute value : array) {
          auto integer = dyn_cast<IntegerAttr>(value);
          if (!integer ||
              integer.getValue().getActiveBits() > assignmentWidth) {
            emitError(getSemanticLocation(root))
                << "constraint function ordering metadata is malformed";
            return failure();
          }
          pairs.push_back(integer.getValue().zextOrTrunc(assignmentWidth));
        }
      } else {
        emitError(getSemanticLocation(root))
            << "constraint function ordering metadata is malformed";
        return failure();
      }
      if (pairs.size() % 2 != 0) {
        emitError(getSemanticLocation(root))
            << "constraint function ordering metadata is malformed";
        return failure();
      }
      for (size_t pair = 0; pair != pairs.size(); pair += 2) {
        APInt beforeMask = pairs[pair];
        APInt afterMask = pairs[pair + 1];
        if (beforeMask.isZero() || afterMask.isZero() ||
            !(beforeMask & ~randomAssignmentMask).isZero() ||
            !(afterMask & ~randomAssignmentMask).isZero() ||
            !(beforeMask & afterMask).isZero()) {
          emitError(getSemanticLocation(root))
              << "constraint function ordering metadata is out of range";
          return failure();
        }
        hasSolveBefore = true;
        hasFunctionPriority = true;
        SolveBeforeEdge edge{beforeMask, afterMask, solveConstraintBlock, true};
        appendSolveBeforeEdge(edge);
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
          SmallVector<APInt> before;
          SmallVector<APInt> after;
          for (auto [operandIndex, operand] : llvm::enumerate(operands)) {
            FailureOr<APInt> property = randomPropertyMask(operand);
            if (failed(property))
              return WalkResult::interrupt();
            (operandIndex < solveCount ? before : after).push_back(*property);
          }
          for (const APInt &lhs : before)
            for (const APInt &rhs : after) {
              if (!(lhs & rhs).isZero()) {
                emitError(getSemanticLocation(solve))
                    << "solve before cannot order a property before itself";
                return WalkResult::interrupt();
              }
              SolveBeforeEdge edge{lhs, rhs, solveConstraintBlock};
              appendSolveBeforeEdge(edge);
            }
          return WalkResult::advance();
        });
    if (result.wasInterrupted())
      return failure();
  }
  if (hasSolveBefore) {
    // 18.5.9 defers partially ordered variables to their latest legal layer.
    // Overlapping selected bit paths cannot be sampled independently: keep
    // each transitive overlap class together so an edge to one part of a
    // packed slice cannot move its other bits into an earlier layer.
    SmallVector<APInt> nodes;
    llvm::DenseMap<APInt, unsigned> nodeIds;
    auto addNode = [&](const APInt &mask) {
      auto [found, inserted] = nodeIds.try_emplace(mask, nodes.size());
      if (inserted)
        nodes.push_back(mask);
      return found->second;
    };
    SmallVector<std::pair<unsigned, unsigned>> edges;
    for (const SolveBeforeEdge &edge : solveBeforeEdges) {
      unsigned before = addNode(edge.beforeMask);
      unsigned after = addNode(edge.afterMask);
      edges.emplace_back(before, after);
    }

    // Connect overlapping views through one owner per bit. A breadth-first
    // traversal then finds complete overlap classes without pairwise mask
    // comparisons or repeated merging. Work is linear in the lowered masks.
    SmallVector<unsigned> bitOwners(totalWidth, UINT_MAX);
    SmallVector<SmallVector<unsigned>> overlaps(nodes.size());
    for (auto [id, mask] : llvm::enumerate(nodes)) {
      for (unsigned word = 0; word != mask.getNumWords(); ++word) {
        uint64_t bits = mask.getRawData()[word];
        while (bits != 0) {
          unsigned bit = word * 64 + llvm::countr_zero(bits);
          unsigned &owner = bitOwners[bit];
          if (owner == UINT_MAX)
            owner = id;
          else {
            overlaps[id].push_back(owner);
            overlaps[owner].push_back(id);
          }
          bits &= bits - 1;
        }
      }
    }
    SmallVector<unsigned> components(nodes.size(), UINT_MAX);
    SmallVector<APInt> componentMasks;
    SmallVector<unsigned> worklist;
    for (unsigned node = 0; node != nodes.size(); ++node) {
      if (components[node] != UINT_MAX)
        continue;
      unsigned component = componentMasks.size();
      componentMasks.emplace_back(assignmentWidth, 0);
      components[node] = component;
      worklist.clear();
      worklist.push_back(node);
      for (size_t cursor = 0; cursor != worklist.size(); ++cursor) {
        unsigned member = worklist[cursor];
        componentMasks[component] |= nodes[member];
        for (unsigned adjacent : overlaps[member]) {
          if (components[adjacent] != UINT_MAX)
            continue;
          components[adjacent] = component;
          worklist.push_back(adjacent);
        }
      }
    }

    SmallVector<SmallVector<unsigned>> predecessors(componentMasks.size());
    SmallVector<unsigned> successors(componentMasks.size(), 0);
    SmallVector<unsigned> distance(componentMasks.size(), 0);
    for (auto [before, after] : edges) {
      predecessors[components[after]].push_back(components[before]);
      ++successors[components[before]];
    }
    // Peel sinks first so partially ordered fields are deferred to their
    // latest legal layer. Each edge is visited once, including parallel edges
    // from distinct constraint blocks and function-priority records.
    worklist.clear();
    for (unsigned component = 0; component != componentMasks.size();
         ++component)
      if (successors[component] == 0)
        worklist.push_back(component);
    unsigned maxDistance = 0;
    for (size_t cursor = 0; cursor != worklist.size(); ++cursor) {
      unsigned component = worklist[cursor];
      for (unsigned predecessor : predecessors[component]) {
        distance[predecessor] =
            std::max(distance[predecessor], distance[component] + 1);
        maxDistance = std::max(maxDistance, distance[predecessor]);
        if (--successors[predecessor] == 0)
          worklist.push_back(predecessor);
      }
    }
    if (worklist.size() != componentMasks.size()) {
      emitError(location) << "solve before ordering contains a cycle";
      return failure();
    }
    if (!componentMasks.empty()) {
      solveBeforeLayerMasks.assign(maxDistance + 1,
                                   APInt::getZero(assignmentWidth));
      for (unsigned component = 0; component != componentMasks.size();
           ++component)
        solveBeforeLayerMasks[maxDistance - distance[component]] |=
            componentMasks[component];
    }
    // Unspecified fields join the final ordered group.
    if (!solveBeforeLayerMasks.empty()) {
      APInt orderedMask = APInt::getZero(assignmentWidth);
      for (const APInt &layer : solveBeforeLayerMasks)
        orderedMask |= layer;
      solveBeforeLayerMasks.back() |= randomAssignmentMask & ~orderedMask;
    }
  }
#ifndef OBELISK_ENABLE_Z3
  if (hasFunctionPriority) {
    emitError(location) << "constraint function priority subdivision requires "
                           "compiler Z3 sampling plans";
    return failure();
  }
#endif
  if (hasFunctionPriority && hasRandC) {
    emitError(location)
        << "constraint function priorities with randc require staged cyclic "
           "histories";
    return failure();
  }
  if (hasFunctionPriority && !aliasEqualities.empty()) {
    emitError(location)
        << "constraint function priorities over aliased rand objects require "
           "identity-aware priority stages";
    return failure();
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
  llvm::DenseMap<Operation *, uint32_t> programExpressionCaptures;
  llvm::DenseMap<unsigned, std::pair<uint32_t, uint32_t>> realPropertyCaptures;
  llvm::DenseMap<uint32_t, unsigned> realPropertyOffsets;
  for (auto [index, property] : llvm::enumerate(planned))
    if (isa<FloatType>(property.type))
      realPropertyOffsets[propertyOffsets[index]] = index;
  bool hasRealProgram = hasRealProperties;
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
    std::optional<unsigned> width;
    if (succeeded(type)) {
      if (auto floating = dyn_cast<FloatType>(*type)) {
        width = floating.getWidth();
        hasRealProgram = true;
      } else
        width = getIntegralRandomWidth(*type);
    }
    if (!width || *width == 0) {
      emitError(getSemanticLocation(expression))
          << "random constraint values must have fixed integral bitstreams "
             "or IEEE real types";
      return failure();
    }
    return *width;
  };
  struct ExpressionDependencies {
    bool candidate;
    bool runtimeInput;
  };
  RandomArithmetic *arithmeticGraph = nullptr;
  llvm::DenseMap<Operation *, ExpressionDependencies> expressionDependencies;
  auto dependenciesForExpression = [&](Operation *expression) {
    SmallVector<std::pair<Operation *, bool>> worklist{{expression, false}};
    while (!worklist.empty()) {
      auto [node, ready] = worklist.pop_back_val();
      if (expressionDependencies.contains(node))
        continue;
      if (!ready) {
        worklist.emplace_back(node, true);
        if (arithmeticGraph && arithmeticGraph->isNode(node)) {
          for (Operation *child : arithmeticGraph->operands(node))
            if (!expressionDependencies.contains(child))
              worklist.emplace_back(child, false);
        } else {
          for (Region &region : node->getRegions())
            for (Block &block : region)
              for (Operation &child : block)
                if (!expressionDependencies.contains(&child))
                  worklist.emplace_back(&child, false);
        }
        continue;
      }
      ExpressionDependencies dependencies{
          node->hasAttr(randomVariableAttrName),
          !getConstantSpelling(node) &&
              !node->hasAttr(randomVariableAttrName) &&
              !node->hasAttr(randomVariableBitOffsetAttrName) &&
              isa<semantic::SVNamedValueExpressionOp,
                  semantic::SVHierarchicalValueExpressionOp,
                  semantic::SVMemberAccessExpressionOp,
                  semantic::SVCallExpressionOp>(node)};
      auto addChild = [&](Operation *child) {
        auto nested = expressionDependencies.find(child);
        assert(nested != expressionDependencies.end());
        dependencies.candidate |= nested->second.candidate;
        dependencies.runtimeInput |= nested->second.runtimeInput;
      };
      if (arithmeticGraph && arithmeticGraph->isNode(node)) {
        for (Operation *child : arithmeticGraph->operands(node))
          addChild(child);
      } else {
        for (Region &region : node->getRegions())
          for (Block &block : region)
            for (Operation &child : block)
              addChild(&child);
      }
      expressionDependencies[node] = dependencies;
    }
    return expressionDependencies.lookup(expression);
  };
  auto dependsOnCandidate = [&](Operation *expression) {
    return dependenciesForExpression(expression).candidate;
  };
#ifdef OBELISK_ENABLE_Z3
  constexpr bool rawDistWeights = true;
#else
  constexpr bool rawDistWeights = false;
#endif
  Operation *activeSoftDistSource = nullptr;
  struct DistRangePlan {
    uint64_t lower;
    uint64_t cardinality;
    uint64_t coefficient;
    uint64_t selectionCoefficient;
    uint32_t weightCapture;
    bool weightSigned;
    Value weight;
    uint64_t itemIdentity;
    bool perRange;
    uint32_t intervalFlags = 0;
    Value capturedLower;
    Value capturedUpper;
  };
  struct DistPlan {
    Operation *source;
    unsigned propertyIndex;
    uint32_t propertyOffset;
    uint32_t constraintBlock;
    SmallVector<DistRangePlan> ranges;
    Operation *softSource;
  };
  SmallVector<DistPlan> distPlans;
  // Expression weights share immutable program temporaries and export one
  // Boolean root per bit. The dummy capture retains their existing ABI width.
  SmallVector<Operation *> expressionWeightSources;
  SmallVector<std::optional<uint32_t>> expressionWeightSlots;
  SmallVector<std::optional<uint32_t>> expressionWeightScales;
  uint32_t activeProgramConstraintBlock =
      OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
  auto emitLiteral = [&](bool value) {
    instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, false, 0, value);
  };

  auto sliceTemplateValue = [&](Value value, uint64_t low, uint64_t width,
                                Location captureLocation) -> FailureOr<Value> {
    FailureOr<Value> scalar = toPackedScalar(value, captureLocation);
    auto scalarType = succeeded(scalar)
                          ? dyn_cast<IntegerType>((*scalar).getType())
                          : IntegerType{};
    if (!scalarType || width == 0 || low > scalarType.getWidth() ||
        width > scalarType.getWidth() - low) {
      emitError(captureLocation)
          << "random constraint template capture has an invalid packed slice";
      return failure();
    }
    Value sliced = *scalar;
    if (low != 0) {
      Type type = scalarType;
      Value amount = arith::ConstantOp::create(
          builder, captureLocation, type,
          builder.getIntegerAttr(type, APInt(scalarType.getWidth(), low)));
      sliced = arith::ShRUIOp::create(builder, captureLocation, sliced, amount);
    }
    if (width != scalarType.getWidth()) {
      Type type = IntegerType::get(function.getContext(), width);
      sliced = arith::TruncIOp::create(builder, captureLocation, type, sliced);
    }
    return sliced;
  };

  auto appendTemplateCapture =
      [&](Value value, uint64_t low, uint64_t width,
          Location captureLocation) -> FailureOr<uint32_t> {
    FailureOr<Value> sliced =
        sliceTemplateValue(value, low, width, captureLocation);
    if (failed(sliced))
      return failure();
    Type captureType =
        width <= 64 ? Type(i64)
                    : Type(IntegerType::get(function.getContext(), width));
    FailureOr<Value> captured =
        convert(*sliced, captureType, false, captureLocation, false);
    if (failed(captured))
      return failure();
    uint32_t index = static_cast<uint32_t>(programCaptures.size());
    programCaptures.push_back(*captured);
    return index;
  };

  auto lookupTemplateReference = [&](sim::SimRandomConstraintValueOp valueOp)
      -> FailureOr<sim::RandomValueReferenceAttr> {
    ArrayAttr references = constraintTemplate
                               ? constraintTemplate.getReferencesAttr()
                               : ArrayAttr{};
    uint64_t index = valueOp.getIndex();
    if (!references || index >= references.size()) {
      emitError(valueOp.getLoc())
          << "random constraint template reference is outside its inventory";
      return failure();
    }
    return cast<sim::RandomValueReferenceAttr>(references[index]);
  };

  auto templateVariableOffset =
      [&](sim::RandomValueReferenceAttr reference,
          Location referenceLocation) -> FailureOr<std::optional<uint32_t>> {
    if (reference.getKind() != sim::RandomValueReferenceKind::ObjectField)
      return std::optional<uint32_t>{};
    if (!reference.getPath().empty()) {
      emitError(referenceLocation)
          << "nested object paths are not composed by scalar random plans";
      return failure();
    }
    uint32_t offset = 0;
    for (const Property &property : planned) {
      if (property.field == reference.getTarget()) {
        if (reference.getLow() > property.width ||
            reference.getWidth() > property.width - reference.getLow()) {
          emitError(referenceLocation)
              << "random constraint template variable slice is invalid";
          return failure();
        }
        return std::optional<uint32_t>(
            offset + static_cast<uint32_t>(reference.getLow()));
      }
      offset += property.width;
    }
    return std::optional<uint32_t>{};
  };

  auto materializeTemplateReference =
      [&](sim::SimRandomConstraintValueOp valueOp,
          sim::RandomValueReferenceAttr reference) -> FailureOr<Value> {
    switch (reference.getKind()) {
    case sim::RandomValueReferenceKind::ObjectField: {
      FailureOr<std::optional<uint32_t>> variable =
          templateVariableOffset(reference, valueOp.getLoc());
      if (failed(variable))
        return failure();
      if (variable->has_value()) {
        emitError(valueOp.getLoc())
            << "active random variable cannot be materialized as state";
        return failure();
      }
      auto field = lookupNearestSymbolFrom<sim::SimClassFieldDeclOp>(
          function, reference.getTarget());
      if (!field || field.getIsStatic()) {
        emitError(valueOp.getLoc())
            << "random constraint template state field does not resolve";
        return failure();
      }
      Type referenceType = sim::ManagedRefType::get(
          function.getContext(), field.getType(), objectType.getClassName());
      Value fieldReference = sim::SimClassFieldRefOp::create(
          builder, valueOp.getLoc(), referenceType, receiver,
          reference.getTarget());
      FailureOr<Value> loaded = loadReference(fieldReference, valueOp.getLoc());
      if (failed(loaded))
        return failure();
      return sliceTemplateValue(*loaded, reference.getLow(),
                                reference.getWidth(), valueOp.getLoc());
    }
    case sim::RandomValueReferenceKind::Storage: {
      uint64_t storageID = reference.getStorage().getValue().getZExtValue();
      sim::SimStorageDeclOp storage;
      sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
      if (design)
        for (Operation &candidate : design.getBody().front())
          if (auto declaration = dyn_cast<sim::SimStorageDeclOp>(candidate);
              declaration && declaration.getId() == storageID) {
            storage = declaration;
            break;
          }
      if (!storage) {
        emitError(valueOp.getLoc())
            << "random constraint template storage does not resolve";
        return failure();
      }
      Type referenceType =
          sim::RefType::get(function.getContext(), storage.getType());
      Value storageReference = sim::SimContextStorageOp::create(
          builder, valueOp.getLoc(), referenceType,
          function.getBody().front().getArgument(0),
          builder.getI64IntegerAttr(storageID));
      Value loaded = sim::SimRefLoadOp::create(
          builder, valueOp.getLoc(), storage.getType(), storageReference);
      return sliceTemplateValue(loaded, reference.getLow(),
                                reference.getWidth(), valueOp.getLoc());
    }
    }
    llvm_unreachable("unknown random-value reference kind");
  };

  llvm::DenseMap<Value, bool> templateVariableDependencies;
  std::function<FailureOr<bool>(Value)> templateDependsOnVariable;
  templateDependsOnVariable = [&](Value value) -> FailureOr<bool> {
    if (auto found = templateVariableDependencies.find(value);
        found != templateVariableDependencies.end())
      return found->second;
    Operation *operation = value.getDefiningOp();
    if (!operation) {
      emitError(value.getLoc())
          << "random constraint template value has no defining operation";
      return failure();
    }
    bool dependent = false;
    if (auto valueOp = dyn_cast<sim::SimRandomConstraintValueOp>(operation)) {
      FailureOr<sim::RandomValueReferenceAttr> reference =
          lookupTemplateReference(valueOp);
      if (failed(reference))
        return failure();
      FailureOr<std::optional<uint32_t>> variable =
          templateVariableOffset(*reference, valueOp.getLoc());
      if (failed(variable))
        return failure();
      dependent = variable->has_value();
    } else {
      for (Value operand : operation->getOperands()) {
        FailureOr<bool> operandDependent = templateDependsOnVariable(operand);
        if (failed(operandDependent))
          return failure();
        dependent |= *operandDependent;
      }
    }
    templateVariableDependencies[value] = dependent;
    return dependent;
  };

  llvm::DenseMap<Value, Value> materializedTemplateValues;
  std::function<FailureOr<Value>(Value)> materializeTemplateValue;
  materializeTemplateValue = [&](Value value) -> FailureOr<Value> {
    if (Value materialized = materializedTemplateValues.lookup(value))
      return materialized;
    Operation *operation = value.getDefiningOp();
    auto result = dyn_cast<OpResult>(value);
    if (!operation || !result) {
      emitError(value.getLoc()) << "random constraint template state value is "
                                   "not an operation result";
      return failure();
    }
    if (auto valueOp = dyn_cast<sim::SimRandomConstraintValueOp>(operation)) {
      FailureOr<sim::RandomValueReferenceAttr> reference =
          lookupTemplateReference(valueOp);
      if (failed(reference))
        return failure();
      FailureOr<Value> materialized =
          materializeTemplateReference(valueOp, *reference);
      if (failed(materialized))
        return failure();
      materializedTemplateValues[value] = *materialized;
      return *materialized;
    }

    IRMapping mapping;
    for (Value operand : operation->getOperands()) {
      FailureOr<Value> materialized = materializeTemplateValue(operand);
      if (failed(materialized))
        return failure();
      mapping.map(operand, *materialized);
    }
    Operation *cloned = builder.clone(*operation, mapping);
    for (auto [original, replacement] :
         llvm::zip_equal(operation->getResults(), cloned->getResults()))
      materializedTemplateValues[original] = replacement;
    return materializedTemplateValues.lookup(value);
  };

  llvm::DenseMap<Value, uint32_t> materializedTemplateCaptures;

  auto emitTemplateReference =
      [&](sim::SimRandomConstraintValueOp valueOp) -> LogicalResult {
    FailureOr<sim::RandomValueReferenceAttr> reference =
        lookupTemplateReference(valueOp);
    if (failed(reference))
      return failure();
    FailureOr<std::optional<uint32_t>> variable =
        templateVariableOffset(*reference, valueOp.getLoc());
    if (failed(variable))
      return failure();
    if (!variable->has_value()) {
      emitError(valueOp.getLoc())
          << "random constraint template state escaped materialization";
      return failure();
    }
    instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, reference->getWidth(),
                false, **variable);
    return success();
  };

  uint64_t programTemporaryCount = 0;
  llvm::DenseMap<Value, uint32_t> emittedTemplateValues;
  std::function<LogicalResult(Value)> emitTemplateNode;
  std::function<LogicalResult(Value)> emitTemplateValue;
  emitTemplateValue = [&](Value value) -> LogicalResult {
    if (auto found = emittedTemplateValues.find(value);
        found != emittedTemplateValues.end()) {
      instruction(OBELISK_RT_RANDOM_LOAD_VALUE_V1,
                  cast<IntegerType>(value.getType()).getWidth(), false,
                  found->second);
      return success();
    }
    if (failed(emitTemplateNode(value)))
      return failure();
    // Template nodes are pure. Emit every shared candidate computation once.
    // Variable and capture leaves use fixed-size instructions; shared wide
    // literals also need a slot to avoid copying their word pool at each use.
    // The postfix program now grows with graph nodes and uses, instead of
    // recursively expanding a shared graph into an exponential tree.
    unsigned width = cast<IntegerType>(value.getType()).getWidth();
    bool computed =
        templateVariableDependencies.lookup(value) &&
        !isa<sim::SimRandomConstraintValueOp>(value.getDefiningOp());
    bool wideLiteral =
        width > 64 && isa<arith::ConstantOp>(value.getDefiningOp());
    if (!value.hasOneUse() && (computed || wideLiteral)) {
      if (programTemporaryCount >= UINT32_MAX) {
        emitError(value.getLoc())
            << "random constraint temporary limit exceeded";
        return failure();
      }
      uint32_t slot = static_cast<uint32_t>(programTemporaryCount++);
      emittedTemplateValues[value] = slot;
      instruction(OBELISK_RT_RANDOM_STORE_VALUE_V1, width, false, slot);
    }
    return success();
  };
  emitTemplateNode = [&](Value value) -> LogicalResult {
    Operation *operation = value.getDefiningOp();
    auto resultType = dyn_cast<IntegerType>(value.getType());
    if (!operation || !resultType) {
      emitError(value.getLoc())
          << "random constraint template value is not an integer dataflow node";
      return failure();
    }
    unsigned width = resultType.getWidth();
    if (auto constant = dyn_cast<arith::ConstantOp>(operation)) {
      auto integer = dyn_cast<IntegerAttr>(constant.getValue());
      if (!integer) {
        emitError(operation->getLoc())
            << "random constraint template constant is not integral";
        return failure();
      }
      literalInstruction(width, false, integer.getValue());
      return success();
    }
    FailureOr<bool> dependsOnVariable = templateDependsOnVariable(value);
    if (failed(dependsOnVariable))
      return failure();
    if (!*dependsOnVariable) {
      uint32_t captureIndex = 0;
      auto found = materializedTemplateCaptures.find(value);
      if (found != materializedTemplateCaptures.end()) {
        captureIndex = found->second;
      } else {
        FailureOr<Value> materialized = materializeTemplateValue(value);
        if (failed(materialized))
          return failure();
        FailureOr<uint32_t> appended =
            appendTemplateCapture(*materialized, 0, width, operation->getLoc());
        if (failed(appended))
          return failure();
        captureIndex = *appended;
        materializedTemplateCaptures[value] = captureIndex;
      }
      instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, width, false,
                  captureIndex);
      return success();
    }
    if (auto reference = dyn_cast<sim::SimRandomConstraintValueOp>(operation))
      return emitTemplateReference(reference);
    if (isa<arith::TruncIOp, arith::ExtUIOp, arith::ExtSIOp>(operation)) {
      if (operation->getNumOperands() != 1 ||
          failed(emitTemplateValue(operation->getOperand(0))))
        return failure();
      instruction(OBELISK_RT_RANDOM_CAST_V1, width,
                  isa<arith::ExtSIOp>(operation));
      return success();
    }
    if (auto compare = dyn_cast<arith::CmpIOp>(operation)) {
      if (failed(emitTemplateValue(compare.getLhs())) ||
          failed(emitTemplateValue(compare.getRhs())))
        return failure();
      uint8_t opcode = 0;
      bool signedComparison = false;
      switch (compare.getPredicate()) {
      case arith::CmpIPredicate::eq:
        opcode = OBELISK_RT_RANDOM_EQ_V1;
        break;
      case arith::CmpIPredicate::ne:
        opcode = OBELISK_RT_RANDOM_NE_V1;
        break;
      case arith::CmpIPredicate::slt:
        opcode = OBELISK_RT_RANDOM_LT_V1;
        signedComparison = true;
        break;
      case arith::CmpIPredicate::sle:
        opcode = OBELISK_RT_RANDOM_LE_V1;
        signedComparison = true;
        break;
      case arith::CmpIPredicate::sgt:
        opcode = OBELISK_RT_RANDOM_GT_V1;
        signedComparison = true;
        break;
      case arith::CmpIPredicate::sge:
        opcode = OBELISK_RT_RANDOM_GE_V1;
        signedComparison = true;
        break;
      case arith::CmpIPredicate::ult:
        opcode = OBELISK_RT_RANDOM_LT_V1;
        break;
      case arith::CmpIPredicate::ule:
        opcode = OBELISK_RT_RANDOM_LE_V1;
        break;
      case arith::CmpIPredicate::ugt:
        opcode = OBELISK_RT_RANDOM_GT_V1;
        break;
      case arith::CmpIPredicate::uge:
        opcode = OBELISK_RT_RANDOM_GE_V1;
        break;
      }
      instruction(opcode, width, signedComparison);
      return success();
    }
    if (auto select = dyn_cast<arith::SelectOp>(operation)) {
      if (failed(emitTemplateValue(select.getCondition())) ||
          failed(emitTemplateValue(select.getTrueValue())) ||
          failed(emitTemplateValue(select.getFalseValue())))
        return failure();
      instruction(OBELISK_RT_RANDOM_SELECT_V1, width);
      return success();
    }
    if (operation->getNumOperands() != 2 ||
        failed(emitTemplateValue(operation->getOperand(0))) ||
        failed(emitTemplateValue(operation->getOperand(1)))) {
      emitError(operation->getLoc())
          << "random constraint template operation is not encodable: "
          << operation->getName();
      return failure();
    }
    uint8_t opcode = 0;
    bool signedOperation = false;
    if (isa<arith::AddIOp>(operation))
      opcode = OBELISK_RT_RANDOM_ADD_V1;
    else if (isa<arith::SubIOp>(operation))
      opcode = OBELISK_RT_RANDOM_SUB_V1;
    else if (isa<arith::MulIOp>(operation))
      opcode = OBELISK_RT_RANDOM_MUL_V1;
    else if (isa<arith::DivUIOp>(operation))
      opcode = OBELISK_RT_RANDOM_DIV_V1;
    else if (isa<arith::DivSIOp>(operation)) {
      opcode = OBELISK_RT_RANDOM_DIV_V1;
      signedOperation = true;
    } else if (isa<arith::RemUIOp>(operation))
      opcode = OBELISK_RT_RANDOM_MOD_V1;
    else if (isa<arith::RemSIOp>(operation)) {
      opcode = OBELISK_RT_RANDOM_MOD_V1;
      signedOperation = true;
    } else if (isa<arith::AndIOp>(operation))
      opcode = OBELISK_RT_RANDOM_BIT_AND_V1;
    else if (isa<arith::OrIOp>(operation))
      opcode = OBELISK_RT_RANDOM_BIT_OR_V1;
    else if (isa<arith::XOrIOp>(operation))
      opcode = OBELISK_RT_RANDOM_BIT_XOR_V1;
    else if (isa<arith::ShLIOp>(operation))
      opcode = OBELISK_RT_RANDOM_SHIFT_LEFT_V1;
    else if (isa<arith::ShRUIOp>(operation))
      opcode = OBELISK_RT_RANDOM_SHIFT_RIGHT_V1;
    else if (isa<arith::ShRSIOp>(operation)) {
      opcode = OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1;
      signedOperation = true;
    } else {
      emitError(operation->getLoc())
          << "random constraint template operation is not encodable: "
          << operation->getName();
      return failure();
    }
    instruction(opcode, width, signedOperation);
    return success();
  };
  // Compiler Z3 receives canonical value bits and independent unknown planes.
  // The target samples Boolean plans; it never reconstructs SV arithmetic.
  SmallVector<uint64_t> arithmeticPropertyWidths;
  for (const Property &property : planned)
    arithmeticPropertyWidths.push_back(property.width);
  auto arithmeticBinding =
      [&](Operation *expression) -> FailureOr<std::optional<unsigned>> {
    auto path = getRandomConstraintPath(
        expression, arithmeticPropertyWidths,
        [&](Operation *source) -> std::optional<unsigned> {
          auto index =
              source->getAttrOfType<IntegerAttr>(randomVariableAttrName);
          if (!index || index.getValue().isNegative() ||
              index.getValue().getActiveBits() > 64 ||
              index.getValue().getZExtValue() >= planned.size())
            return std::nullopt;
          return index.getValue().getZExtValue();
        });
    if (failed(path))
      return failure();
    if (!*path || !(**path).precise || (**path).isState)
      return std::optional<unsigned>{};
    return std::optional<unsigned>(propertyOffsets[(**path).property] +
                                   (**path).offset);
  };
  RandomArithmetic arithmetic(builder.getContext(), expressionWidth,
                              dependsOnCandidate, arithmeticBinding,
                              /*frozenVariables=*/!scopeRandomize);
  arithmeticGraph = &arithmetic;
  bool arithmeticFailure = false;
  auto arithmeticValidity = [&](Operation *expression) -> Operation * {
    if (isa<semantic::SVDistExpressionOp>(expression))
      return nullptr;
    auto known = arithmetic.known(expression);
    if (failed(known)) {
      arithmeticFailure = true;
      return nullptr;
    }
    return *known;
  };
  struct SafeConstraintValue {
    Value value;
    Value valid;
    Value random;
    Value inputs;
    Value arguments;
    Value errors;
    // A state-selected arm or element has a live dependency mask rather than
    // the union of every syntactically referenced random property bit.
    bool dynamicInputs = false;
  };
  std::function<FailureOr<SafeConstraintValue>(Operation *)>
      safeConstraintValue;
  std::function<FailureOr<Value>(Operation *)> constraintCreationValid;
  std::function<LogicalResult(Operation *)> emitProgramExpression;
  std::function<LogicalResult(Operation *)> emitProgramNode;
  llvm::DenseMap<Operation *, uint32_t> emittedArithmeticValues;
  auto emitWeightValue = [&](uint32_t capture, Value value) -> LogicalResult {
    unsigned width = cast<IntegerType>(value.getType()).getWidth();
    if (capture >= expressionWeightSources.size() ||
        !expressionWeightSources[capture]) {
      instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, width, false, capture);
      return success();
    }
    if (expressionWeightSlots[capture]) {
      instruction(OBELISK_RT_RANDOM_LOAD_VALUE_V1, width, false,
                  *expressionWeightSlots[capture]);
      return success();
    }
    if (failed(emitProgramExpression(expressionWeightSources[capture])))
      return failure();
    instruction(OBELISK_RT_RANDOM_CAST_V1, width, false);
    if (expressionWeightScales[capture]) {
      uint32_t scale = *expressionWeightScales[capture];
      instruction(
          OBELISK_RT_RANDOM_PUSH_CAPTURE_V1,
          cast<IntegerType>(programCaptures[scale].getType()).getWidth(), false,
          scale);
      instruction(OBELISK_RT_RANDOM_CAST_V1, width, false);
      instruction(OBELISK_RT_RANDOM_MUL_V1, width, false);
    }
    if (programTemporaryCount >= UINT32_MAX)
      return failure();
    uint32_t slot = static_cast<uint32_t>(programTemporaryCount++);
    expressionWeightSlots[capture] = slot;
    instruction(OBELISK_RT_RANDOM_STORE_VALUE_V1, width, false, slot);
    return success();
  };
  emitProgramExpression = [&](Operation *expression) -> LogicalResult {
    if (arithmeticFailure)
      return failure();
    if (auto found = emittedArithmeticValues.find(expression);
        found != emittedArithmeticValues.end()) {
      auto width = expressionWidth(expression);
      if (failed(width))
        return failure();
      instruction(OBELISK_RT_RANDOM_LOAD_VALUE_V1, *width, false,
                  found->second);
      return success();
    }
    if (failed(emitProgramNode(expression)))
      return failure();
    if (arithmetic.isNode(expression) && arithmetic.isShared(expression) &&
        !expression->hasAttr(randomArithmeticPlaneAttr)) {
      auto width = expressionWidth(expression);
      if (failed(width))
        return failure();
      if (programTemporaryCount >= UINT32_MAX) {
        emitError(getSemanticLocation(expression))
            << "random constraint temporary limit exceeded";
        return failure();
      }
      uint32_t slot = static_cast<uint32_t>(programTemporaryCount++);
      emittedArithmeticValues[expression] = slot;
      instruction(OBELISK_RT_RANDOM_STORE_VALUE_V1, *width, false, slot);
    }
    return success();
  };
  emitProgramNode = [&](Operation *expression) -> LogicalResult {
    if (arithmeticFailure)
      return failure();
    if (!isa<semantic::SVDistExpressionOp>(expression) &&
        arithmetic.needed(expression)) {
      auto planes = arithmetic.get(expression);
      if (failed(planes))
        return failure();
      return emitProgramExpression(planes->value);
    }
    if (auto inputs = expression->getAttrOfType<IntegerAttr>(
            randomEvaluationInputsAttrName)) {
      auto nested = getChildren(expression);
      if (inputs.getValue().isNegative() ||
          inputs.getValue().getActiveBits() > 64 ||
          inputs.getValue().getZExtValue() + 1 != nested.size())
        return failure();
      FailureOr<unsigned> width = expressionWidth(expression);
      if (failed(width) || failed(emitProgramExpression(nested.back())))
        return failure();
      instruction(OBELISK_RT_RANDOM_CAST_V1, *width,
                  isSignedNode(nested.back()));
      return success();
    }
    if (auto dist = dyn_cast<semantic::SVDistExpressionOp>(expression)) {
      if (llvm::is_contained(
              llvm::map_range(distPlans,
                              [](const DistPlan &plan) { return plan.source; }),
              expression)) {
        emitError(getSemanticLocation(expression))
            << "distribution expression was encoded more than once";
        return failure();
      }
      SmallVector<Operation *> nested = getChildren(expression);
      auto itemCountAttr = expression->getAttrOfType<IntegerAttr>("item_count");
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
      if ((itemCount == 0 && !hasDefaultWeight.getValue()) ||
          itemCount > static_cast<uint64_t>(INT64_MAX) ||
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
      auto variable =
          target->getAttrOfType<IntegerAttr>(randomVariableAttrName);
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
        rawItems.push_back({value, weight, itemWeightKinds[index] != 0});
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
          FailureOr<ParsedConstant> value = constantValue(converted.front());
          FailureOr<unsigned> width = expressionWidth(constant);
          if (failed(value) || failed(width))
            return failure();
          bool sign = isSignedNode(converted.front());
          value->value = sign ? value->value.sextOrTrunc(*width)
                              : value->value.zextOrTrunc(*width);
          value->unknown = sign ? value->unknown.sextOrTrunc(*width)
                                : value->unknown.zextOrTrunc(*width);
          return *value;
        }
        if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(constant)) {
          SmallVector<Operation *> operand = getChildren(unary);
          if (operand.size() != 1)
            return failure();
          FailureOr<ParsedConstant> value = constantValue(operand.front());
          FailureOr<unsigned> width = expressionWidth(constant);
          if (failed(value) || failed(width))
            return failure();
          bool sign = isSignedNode(operand.front());
          value->value = sign ? value->value.sextOrTrunc(*width)
                              : value->value.zextOrTrunc(*width);
          value->unknown = sign ? value->unknown.sextOrTrunc(*width)
                                : value->unknown.zextOrTrunc(*width);
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
        FailureOr<unsigned> width = expressionWidth(constant);
        if (failed(width))
          return failure();
        return parseSVInteger(*spelling, *width, getSemanticLocation(constant));
      };
      auto contextualConstant =
          [&](Operation *endpoint) -> FailureOr<ParsedConstant> {
        FailureOr<ParsedConstant> value = constantValue(endpoint);
        if (failed(value))
          return failure();
        bool sign = isSignedNode(endpoint);
        value->value = sign ? value->value.sextOrTrunc(*comparisonWidth)
                            : value->value.zextOrTrunc(*comparisonWidth);
        value->unknown = sign ? value->unknown.sextOrTrunc(*comparisonWidth)
                              : value->unknown.zextOrTrunc(*comparisonWidth);
        return *value;
      };
      llvm::DenseMap<Operation *, std::pair<uint32_t, Value>> weights;
      std::optional<std::pair<uint32_t, Value>> implicitWeight;
      auto materializeWeight =
          [&](Operation *weight,
              bool &isSigned) -> FailureOr<std::pair<uint32_t, Value>> {
        if (weight) {
          if (auto found = weights.find(weight); found != weights.end()) {
            isSigned = false;
            return found->second;
          }
          if (dependsOnCandidate(weight)) {
            auto width = expressionWidth(weight);
            auto type = getNormalizedSemanticType(weight);
            if (!rawDistWeights || failed(width) || failed(type) ||
                !getIntegralRandomWidth(*type)) {
              emitError(getSemanticLocation(weight))
                  << "candidate dist weights require integral compiler Z3 "
                     "sampling plans";
              return failure();
            }
            Type weightType =
                builder.getIntegerType(std::max<unsigned>(64, *width));
            Value placeholder = arith::ConstantOp::create(
                builder, location, weightType,
                builder.getIntegerAttr(weightType, 0));
            uint32_t capture = static_cast<uint32_t>(programCaptures.size());
            programCaptures.push_back(placeholder);
            expressionWeightSources.resize(capture + 1, nullptr);
            expressionWeightSlots.resize(capture + 1);
            expressionWeightScales.resize(capture + 1);
            expressionWeightSources[capture] = weight;
            auto result = std::make_pair(capture, placeholder);
            weights[weight] = result;
            isSigned = false;
            return result;
          }
          FailureOr<SafeConstraintValue> safe = safeConstraintValue(weight);
          FailureOr<Value> lowered = succeeded(safe)
                                         ? FailureOr<Value>(safe->value)
                                         : FailureOr<Value>(failure());
          FailureOr<Value> scalar =
              succeeded(lowered)
                  ? toPackedScalar(*lowered, getSemanticLocation(weight))
                  : FailureOr<Value>(failure());
          if (failed(scalar) || !isa<IntegerType>((*scalar).getType())) {
            emitError(getSemanticLocation(weight))
                << "dist weights must be packed integral values";
            return failure();
          }
          // IEEE 1800-2023 18.5.3 interprets the self-determined weight
          // expression as unsigned, including signed negative bit patterns.
          if (!rawDistWeights &&
              cast<IntegerType>((*scalar).getType()).getWidth() > 64) {
            emitError(getSemanticLocation(weight))
                << "dist weights wider than 64 bits are not executable yet";
            return failure();
          }
          isSigned = false;
          Type weightType =
              rawDistWeights
                  ? builder.getIntegerType(std::max<unsigned>(
                        64, cast<IntegerType>((*scalar).getType()).getWidth()))
                  : i64;
          FailureOr<Value> extended =
              convert(*scalar, weightType, isSigned,
                      getSemanticLocation(weight), false);
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

      // Runtime endpoints retain their self-determined range size before
      // intersection with the rand property's domain or other constraints.
      // The footer carries clipped ordinal bounds and the exact original mass.
      auto staticEndpointInDomain = [&](Operation *endpoint) {
        FailureOr<ParsedConstant> parsed = contextualConstant(endpoint);
        if (failed(parsed))
          return false;
        if (!parsed->unknown.isZero())
          return true; // The constant path diagnoses unknown bits below.
        if (property.isSigned)
          return parsed->value.sge(APInt::getSignedMinValue(property.width)
                                       .sext(*comparisonWidth)) &&
                 parsed->value.sle(APInt::getSignedMaxValue(property.width)
                                       .sext(*comparisonWidth));
        return parsed->value.ule(
            APInt::getLowBitsSet(*comparisonWidth, property.width));
      };
      bool capturedIntervals = rawItems.empty() && defaultWeight;
      for (const RawDistItem &item : rawItems) {
        auto endpoints = isa<semantic::SVValueRangeExpressionOp>(item.value)
                             ? getChildren(item.value)
                             : SmallVector<Operation *>{item.value};
        if (endpoints.empty() || endpoints.size() > 2)
          return failure();
        for (Operation *endpoint : endpoints)
          capturedIntervals |= !staticEndpointInDomain(endpoint);
      }
      if (capturedIntervals) {
        if (!rawDistWeights) {
          emitError(getSemanticLocation(expression))
              << "dynamic or clipped dist intervals require compiler Z3 "
                 "sampling plans";
          return failure();
        }
        if (defaultWeight && defaultWeightKind.getInt() == 0) {
          emitError(getSemanticLocation(defaultWeight))
              << "a default dist specification must use the :/ operator";
          return failure();
        }
        auto orderedType = builder.getIntegerType(*comparisonWidth + 1);
        auto orderedConstant = [&](const APInt &bits) -> Value {
          return arith::ConstantOp::create(
              builder, location, orderedType,
              builder.getIntegerAttr(orderedType, bits));
        };
        APInt minimum = property.isSigned
                            ? APInt::getSignedMinValue(property.width)
                                  .sext(orderedType.getWidth())
                            : APInt(orderedType.getWidth(), 0);
        APInt maximum =
            property.isSigned
                ? APInt::getSignedMaxValue(property.width)
                      .sext(orderedType.getWidth())
                : APInt::getLowBitsSet(orderedType.getWidth(), property.width);
        Value domainMinimum = orderedConstant(minimum);
        Value domainMaximum = orderedConstant(maximum);
        Value one = orderedConstant(APInt(orderedType.getWidth(), 1));
        auto endpointValue = [&](Operation *endpoint) -> FailureOr<Value> {
          if (dependsOnCandidate(endpoint)) {
            emitError(getSemanticLocation(endpoint))
                << "dist endpoints that depend on randomized properties are "
                   "not executable yet";
            return failure();
          }
          FailureOr<SafeConstraintValue> safe = safeConstraintValue(endpoint);
          FailureOr<Value> scalar =
              succeeded(safe)
                  ? toRandomScalar(safe->value, getSemanticLocation(endpoint))
                  : FailureOr<Value>(failure());
          if (failed(scalar) || !isa<IntegerType>((*scalar).getType())) {
            emitError(getSemanticLocation(endpoint))
                << "dynamic dist endpoints must be two-state integral values";
            return failure();
          }
          FailureOr<Value> contextual = convert(
              *scalar, builder.getIntegerType(*comparisonWidth),
              isSignedNode(endpoint), getSemanticLocation(endpoint), false);
          return succeeded(contextual)
                     ? convert(*contextual, orderedType, property.isSigned,
                               getSemanticLocation(endpoint), false)
                     : FailureOr<Value>(failure());
        };
        auto capture = [&](Value value) {
          uint32_t index = static_cast<uint32_t>(programCaptures.size());
          programCaptures.push_back(value);
          return index;
        };
        DistPlan plan{expression, propertyIndex,
                      0,          activeProgramConstraintBlock,
                      {},         activeSoftDistSource};
        plan.propertyOffset = propertyOffsets[propertyIndex];
        SmallVector<std::pair<uint32_t, Value>> rawWeights;
        for (auto [identity, item] : llvm::enumerate(rawItems)) {
          auto endpoints = isa<semantic::SVValueRangeExpressionOp>(item.value)
                               ? getChildren(item.value)
                               : SmallVector<Operation *>{item.value};
          if (endpoints.size() !=
              (isa<semantic::SVValueRangeExpressionOp>(item.value) ? 2u : 1u))
            return failure();
          FailureOr<Value> first = endpointValue(endpoints.front());
          FailureOr<Value> second =
              endpoints.size() == 2 ? endpointValue(endpoints.back()) : first;
          if (failed(first) || failed(second))
            return failure();
          Value ascending = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::sle, *first, *second);
          Value lower = arith::SelectOp::create(builder, location, ascending,
                                                *first, *second);
          Value upper = arith::SelectOp::create(builder, location, ascending,
                                                *second, *first);
          Value cardinality = arith::AddIOp::create(
              builder, location,
              arith::SubIOp::create(builder, location, upper, lower), one);
          Value aboveMinimum = arith::CmpIOp::create(builder, location,
                                                     arith::CmpIPredicate::sge,
                                                     lower, domainMinimum);
          Value belowMaximum = arith::CmpIOp::create(builder, location,
                                                     arith::CmpIPredicate::sle,
                                                     upper, domainMaximum);
          Value clippedLower = arith::SelectOp::create(
              builder, location, aboveMinimum, lower, domainMinimum);
          Value clippedUpper = arith::SelectOp::create(
              builder, location, belowMaximum, upper, domainMaximum);
          Value nonempty = arith::CmpIOp::create(builder, location,
                                                 arith::CmpIPredicate::sle,
                                                 clippedLower, clippedUpper);
          Value biasedLower = arith::SubIOp::create(
              builder, location, clippedLower, domainMinimum);
          Value biasedUpper = arith::SubIOp::create(
              builder, location, clippedUpper, domainMinimum);
          biasedLower = arith::SelectOp::create(builder, location, nonempty,
                                                biasedLower, one);
          biasedUpper = arith::SelectOp::create(
              builder, location, nonempty, biasedUpper,
              orderedConstant(APInt(orderedType.getWidth(), 0)));
          FailureOr<Value> lower64 =
              convert(biasedLower, i64, false, location, false);
          FailureOr<Value> upper64 =
              convert(biasedUpper, i64, false, location, false);
          if (failed(lower64) || failed(upper64))
            return failure();
          uint32_t lowerCapture = capture(*lower64);
          uint32_t upperCapture = capture(*upper64);
          bool weightSigned = false;
          FailureOr<std::pair<uint32_t, Value>> weight =
              materializeWeight(item.weight, weightSigned);
          if (failed(weight))
            return failure();
          rawWeights.push_back(*weight);
          uint32_t massCapture = weight->first;
          Value mass = weight->second;
          if (!item.perRange) {
            auto massType = builder.getIntegerType(
                cast<IntegerType>(mass.getType()).getWidth() +
                orderedType.getWidth());
            FailureOr<Value> extendedWeight =
                convert(mass, massType, false, location, false);
            FailureOr<Value> extendedCardinality =
                convert(cardinality, massType, false, location, false);
            if (failed(extendedWeight) || failed(extendedCardinality))
              return failure();
            mass = arith::MulIOp::create(builder, location, *extendedWeight,
                                         *extendedCardinality);
            massCapture = capture(mass);
            if (weight->first < expressionWeightSources.size() &&
                expressionWeightSources[weight->first]) {
              uint32_t scale = capture(cardinality);
              expressionWeightSources.resize(massCapture + 1, nullptr);
              expressionWeightSlots.resize(massCapture + 1);
              expressionWeightScales.resize(massCapture + 1);
              expressionWeightSources[massCapture] = item.weight;
              expressionWeightScales[massCapture] = scale;
            }
          }
          plan.ranges.push_back({lowerCapture, upperCapture, 1, 1, massCapture,
                                 false, mass, identity, true,
                                 OBELISK_RT_RANDOM_DIST_CAPTURED_INTERVAL,
                                 *lower64, *upper64});
        }
        if (defaultWeight) {
          bool weightSigned = false;
          FailureOr<std::pair<uint32_t, Value>> weight =
              materializeWeight(defaultWeight, weightSigned);
          if (failed(weight))
            return failure();
          uint64_t cardinality =
              property.width == 64 ? 0 : uint64_t{1} << property.width;
          plan.ranges.push_back({0,
                                 cardinality,
                                 1,
                                 1,
                                 weight->first,
                                 false,
                                 weight->second,
                                 rawItems.size(),
                                 true,
                                 OBELISK_RT_RANDOM_DIST_DEFAULT_COMPLEMENT,
                                 {},
                                 {}});
        }
        auto emitMatch = [&](const DistRangePlan &range) -> LogicalResult {
          auto emitOrdinal = [&]() -> LogicalResult {
            if (failed(emitProgramExpression(nested.front())))
              return failure();
            instruction(OBELISK_RT_RANDOM_CAST_V1, property.width);
            instruction(OBELISK_RT_RANDOM_CAST_V1, 64);
            if (property.isSigned) {
              instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 64, false, 0,
                          uint64_t{1} << (property.width - 1));
              instruction(OBELISK_RT_RANDOM_BIT_XOR_V1, 64);
            }
            return success();
          };
          if (failed(emitOrdinal()))
            return failure();
          instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 64, false,
                      range.lower);
          instruction(OBELISK_RT_RANDOM_GE_V1, 1);
          if (failed(emitOrdinal()))
            return failure();
          instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 64, false,
                      range.cardinality);
          instruction(OBELISK_RT_RANDOM_LE_V1, 1);
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          return success();
        };
        auto emitPositive = [&](uint32_t index, Value weight) -> LogicalResult {
          unsigned width = cast<IntegerType>(weight.getType()).getWidth();
          if (failed(emitWeightValue(index, weight)))
            return failure();
          instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, width, false, 0, 0);
          instruction(OBELISK_RT_RANDOM_GT_V1, 1);
          if (index < expressionWeightSources.size() &&
              expressionWeightSources[index])
            if (Operation *valid =
                    arithmeticValidity(expressionWeightSources[index])) {
              if (failed(emitProgramExpression(valid)))
                return failure();
              instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
            }
          return success();
        };
        bool firstSupport = true;
        for (size_t index = 0; index != rawItems.size(); ++index) {
          const DistRangePlan &range = plan.ranges[index];
          if (failed(emitMatch(range)))
            return failure();
          if (failed(emitPositive(range.weightCapture, range.weight)))
            return failure();
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          // The unscaled weight capture is shared by all its source uses;
          // keeping it in the predicate also records its precise ABI width.
          if (range.weightCapture != rawWeights[index].first) {
            if (failed(emitPositive(rawWeights[index].first,
                                    rawWeights[index].second)))
              return failure();
            instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          }
          if (!firstSupport)
            instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
          firstSupport = false;
        }
        if (defaultWeight) {
          bool firstExplicit = true;
          for (size_t index = 0; index != rawItems.size(); ++index) {
            if (failed(emitMatch(plan.ranges[index])))
              return failure();
            if (!firstExplicit)
              instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
            firstExplicit = false;
          }
          if (firstExplicit)
            emitLiteral(false);
          instruction(OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1);
          if (failed(emitPositive(plan.ranges.back().weightCapture,
                                  plan.ranges.back().weight)))
            return failure();
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          if (!firstSupport)
            instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
          firstSupport = false;
        }
        if (firstSupport)
          emitLiteral(false);
        distPlans.push_back(std::move(plan));
        return success();
      }

      auto constantBits = [&](Operation *value) -> FailureOr<uint64_t> {
        FailureOr<ParsedConstant> parsed = contextualConstant(value);
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
          APInt maximum =
              APInt::getLowBitsSet(*comparisonWidth, property.width);
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
        uint64_t identity;
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
        if (!rawDistWeights && cardinality == 0 && item.perRange &&
            property.domains.empty()) {
          emitError(getSemanticLocation(item.value))
              << "a per-range dist weight cannot span the complete 64-bit "
                 "domain";
          return failure();
        }
        pending.push_back({lower, cardinality, item.weight, item.perRange,
                           cardinality, cardinality, false, pending.size()});
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
          if (merged.empty() || (merged.back().second != UINT64_MAX &&
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
        if (!defaultPerRange) {
          emitError(getSemanticLocation(defaultWeight))
              << "a default dist specification must use the :/ operator";
          return failure();
        }
        if (!rawDistWeights && defaultPerRange && property.domains.empty() &&
            !complement.empty() && defaultCardinality == 0) {
          emitError(getSemanticLocation(defaultWeight))
              << "a per-range default dist weight cannot cover the complete "
                 "64-bit domain";
          return failure();
        }
        for (auto interval : complement)
          pending.push_back(
              {interval.first, interval.second - interval.first + 1,
               defaultWeight, defaultPerRange, defaultCardinality,
               interval.second - interval.first + 1, true, rawItems.size()});
      }

      if (!rawDistWeights && !property.domains.empty()) {
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
        if (rawDistWeights || !range.perRange)
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

      DistPlan plan{expression, propertyIndex,
                    0,          activeProgramConstraintBlock,
                    {},         activeSoftDistSource};
      plan.propertyOffset = propertyOffsets[propertyIndex];
      for (const PendingRange &range : pending) {
        bool weightSigned = false;
        FailureOr<std::pair<uint32_t, Value>> weight =
            materializeWeight(range.weight, weightSigned);
        if (failed(weight))
          return failure();
        uint64_t coefficient = rawDistWeights ? 1
                               : range.perRange
                                   ? normalization / range.denominator
                                   : normalization;
        if (!rawDistWeights &&
            (range.selectionCardinality == 0 ||
             coefficient > UINT64_MAX / range.selectionCardinality)) {
          emitError(getSemanticLocation(expression))
              << "dist range selection mass exceeds 64 bits";
          return failure();
        }
        uint64_t selectionCoefficient =
            coefficient * range.selectionCardinality;
        plan.ranges.push_back({range.lower, range.cardinality, coefficient,
                               selectionCoefficient, weight->first,
                               weightSigned, weight->second, range.identity,
                               range.perRange});
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
        unsigned width =
            cast<IntegerType>(materialized->second.getType()).getWidth();
        if (failed(emitWeightValue(materialized->first, materialized->second)))
          return failure();
        instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, width, false, 0, 0);
        instruction(OBELISK_RT_RANDOM_GT_V1, 1, weightSigned);
        if (materialized->first < expressionWeightSources.size() &&
            expressionWeightSources[materialized->first])
          if (Operation *valid = arithmeticValidity(weight)) {
            if (failed(emitProgramExpression(valid)))
              return failure();
            instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          }
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
          if (failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[0])))
            return failure();
          instruction(OBELISK_RT_RANDOM_GE_V1, 1, isSignedNode(nested.front()));
          if (failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[1])))
            return failure();
          instruction(OBELISK_RT_RANDOM_LE_V1, 1, isSignedNode(nested.front()));
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          return success();
        }
        if (failed(emitProgramExpression(nested.front())) ||
            failed(emitProgramExpression(value)))
          return failure();
        instruction(OBELISK_RT_RANDOM_EQ_V1, 1, isSignedNode(nested.front()));
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
    if (expression->hasAttr(randomEvaluationErrorAttrName)) {
      // Creation validity carries ERROR separately. The solver must never
      // read an invalid slice, including one eliminated by a state guard.
      literalInstruction(*width, false, APInt::getZero(*width));
      return success();
    }
    auto normalizedType = getNormalizedSemanticType(expression);
    bool floating =
        succeeded(normalizedType) && isa<FloatType>(*normalizedType);
    auto emitRealProperty = [&](unsigned index) -> LogicalResult {
      if (index >= planned.size() || !isa<FloatType>(planned[index].type))
        return failure();
      auto found = realPropertyCaptures.find(index);
      if (found == realPropertyCaptures.end()) {
        unsigned precision = cast<FloatType>(planned[index].type).getWidth();
        Value payload = arith::BitcastOp::create(
            builder, location, builder.getIntegerType(precision),
            planned[index].currentValue);
        if (precision != 64)
          payload = arith::ExtUIOp::create(builder, location, i64, payload);
        uint32_t capture = static_cast<uint32_t>(programCaptures.size());
        programCaptures.push_back(payload);
        programCaptures.push_back(arith::ExtUIOp::create(
            builder, location, i64, propertyEnabled[index]));
        found =
            realPropertyCaptures.insert({index, {capture, capture + 1}}).first;
      }
      // Active candidates decode numeric coordinates. Inactive properties
      // retain their complete original IEEE payload, including NaN/infinity.
      instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 1, false,
                  found->second.second);
      instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, planned[index].width,
                  false, propertyOffsets[index]);
      instruction(*width == 32 ? OBELISK_RT_RANDOM_REAL32_FROM_RANK_V1
                               : OBELISK_RT_RANDOM_REAL64_FROM_RANK_V1,
                  *width);
      instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, *width, false,
                  found->second.first);
      instruction(*width == 32 ? OBELISK_RT_RANDOM_REAL32_BITS_V1
                               : OBELISK_RT_RANDOM_REAL64_BITS_V1,
                  *width);
      instruction(OBELISK_RT_RANDOM_SELECT_V1, *width);
      return success();
    };
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
      if (floating) {
        auto found = realPropertyOffsets.find(offsetValue.getZExtValue());
        return found == realPropertyOffsets.end()
                   ? failure()
                   : emitRealProperty(found->second);
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
      if (floating)
        return emitRealProperty(index);
      instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, planned[index].width,
                  planned[index].isSigned, propertyOffsets[index]);
      return success();
    }
    if (std::optional<StringRef> spelling = getConstantSpelling(expression)) {
      if (floating) {
        double value;
        if (spelling->getAsDouble(value)) {
          emitError(getSemanticLocation(expression))
              << "invalid IEEE real literal";
          return failure();
        }
        auto payload = builder.getFloatAttr(*normalizedType, value);
        literalInstruction(*width, false, payload.getValue().bitcastToAPInt());
        instruction(*width == 32 ? OBELISK_RT_RANDOM_REAL32_BITS_V1
                                 : OBELISK_RT_RANDOM_REAL64_BITS_V1,
                    *width);
        return success();
      }
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
    SmallVector<Operation *> nested = arithmetic.operands(expression);
    if (isa<semantic::SVMinTypMaxExpressionOp>(expression)) {
      auto selected = expression->getAttrOfType<IntegerAttr>("selected_index");
      if (nested.size() != 3 || !selected || selected.getValue().isNegative() ||
          selected.getValue().getActiveBits() > 64 ||
          selected.getValue().getZExtValue() >= nested.size()) {
        emitError(getSemanticLocation(expression))
            << "random min:typ:max expression has no valid selected branch";
        return failure();
      }
      return emitProgramExpression(nested[selected.getValue().getZExtValue()]);
    }
    auto captureExpression = [&]() -> LogicalResult {
      // Consult the bank before lowering the source. In particular, a shared
      // frozen-plane projection must not rematerialize its address, snapshot
      // and active-bit mask once for every graph edge that reads the capture.
      Operation *key = arithmetic.canonical(expression);
      if (auto found = programExpressionCaptures.find(key);
          found != programExpressionCaptures.end()) {
        instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, *width,
                    isSignedNode(expression), found->second);
        if (floating)
          instruction(*width == 32 ? OBELISK_RT_RANDOM_REAL32_BITS_V1
                                   : OBELISK_RT_RANDOM_REAL64_BITS_V1,
                      *width);
        return success();
      }
      FailureOr<SafeConstraintValue> safe = safeConstraintValue(expression);
      FailureOr<Value> value = succeeded(safe) ? FailureOr<Value>(safe->value)
                                               : FailureOr<Value>(failure());
      FailureOr<Value> scalar =
          succeeded(value)
              ? (floating
                     ? FailureOr<Value>(
                           arith::BitcastOp::create(
                               builder, getSemanticLocation(expression),
                               builder.getIntegerType(*width), *value)
                               .getResult())
                     : toRandomScalar(*value, getSemanticLocation(expression)))
              : FailureOr<Value>(failure());
      if (succeeded(scalar) && isa<sim::LogicType>((*scalar).getType()) &&
          *width == 1)
        scalar = truthValue(*scalar, getSemanticLocation(expression));
      if (failed(scalar) || !isa<IntegerType>((*scalar).getType())) {
        emitError(getSemanticLocation(expression))
            << "four-state and non-integral runtime constraint captures are "
               "not executable yet";
        return failure();
      }
      // The capture is a pre-solve parameter, even when a frozen symbol plane
      // originates in an active property. Repeated projections of the same
      // immutable state value must reuse one program input, rather than grow
      // the native capture buffer for every copied expression occurrence.
      // Preserve the scalar runtime ABI for captures that fit in one word.
      // Wider v2 captures retain their source width for compile-time-assisted
      // materialization (and for the wide runtime ABI added separately).
      Type captureType =
          *width <= 64 ? Type(i64)
                       : Type(IntegerType::get(function.getContext(), *width));
      FailureOr<Value> extended = convert(
          *scalar, captureType, false, getSemanticLocation(expression), false);
      if (failed(extended))
        return failure();
      uint32_t capture = static_cast<uint32_t>(programCaptures.size());
      programCaptures.push_back(*extended);
      programExpressionCaptures[key] = capture;
      instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, *width,
                  isSignedNode(expression), capture);
      if (floating)
        instruction(*width == 32 ? OBELISK_RT_RANDOM_REAL32_BITS_V1
                                 : OBELISK_RT_RANDOM_REAL64_BITS_V1,
                    *width);
      return success();
    };
    // Raw value/unknown projections are parameters even when their source
    // property participates in the solve. Frozen unknowns were masked with
    // the live active-bit inventory while materializing this capture.
    if (expression->hasAttr(randomArithmeticPlaneAttr))
      return captureExpression();
    if (expression->hasAttr(randomArithmeticRealFiniteAttr)) {
      if (nested.size() != 1 || failed(emitProgramExpression(nested.front())))
        return failure();
      instruction(OBELISK_RT_RANDOM_REAL_IS_FINITE_V1, 1);
      return success();
    }
    if (isa<semantic::SVConversionExpressionOp>(expression)) {
      // Keep constant conversions structural so Z3 can fold them, but evaluate
      // a conversion of runtime state once as a capture. This preserves the
      // conversion's exact signed extension/truncation while exposing a direct
      // capture endpoint to interval planning.
      auto dependencies = dependenciesForExpression(expression);
      if (!dependencies.candidate && dependencies.runtimeInput &&
          (!expression->hasAttr(randomArithmeticNodeAttr) ||
           expression->hasAttr(randomArithmeticPlaneAttr)))
        return captureExpression();
      if (nested.size() != 1 || failed(emitProgramExpression(nested.front())))
        return failure();
      auto sourceType = getNormalizedSemanticType(nested.front());
      if (!floating && succeeded(sourceType) && isa<FloatType>(*sourceType)) {
        instruction(OBELISK_RT_RANDOM_REAL_TO_INTEGER_V1, *width);
        return success();
      }
      instruction(floating ? OBELISK_RT_RANDOM_REAL_CAST_V1
                           : OBELISK_RT_RANDOM_CAST_V1,
                  *width, isSignedNode(nested.front()));
      return success();
    }
    // Compiler arithmetic nodes remain structural even when their operands
    // are constant or state-only. Only the original state leaves and raw
    // unknown-plane projections become parameters of the Z3 plan.
    if (!dependsOnCandidate(expression) &&
        !expression->hasAttr(randomArithmeticNodeAttr))
      return captureExpression();

    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(expression);
        call && call.getIsSystemCall() &&
        isRealRandomSystemFunction(call.getCalleeName())) {
      if (nested.size() != 1 || failed(emitProgramExpression(nested.front())))
        return failure();
      auto inputType = getNormalizedSemanticType(nested.front());
      if (failed(inputType))
        return failure();
      StringRef name = call.getCalleeName();
      bool realInput = isa<FloatType>(*inputType);
      if (name == "$itor") {
        if (realInput)
          instruction(OBELISK_RT_RANDOM_REAL_TO_INTEGER_V1, 32);
        instruction(OBELISK_RT_RANDOM_REAL_CAST_V1, 64,
                    realInput || isSignedNode(nested.front()));
      } else if (name == "$rtoi") {
        if (!realInput)
          instruction(OBELISK_RT_RANDOM_REAL_CAST_V1, 64,
                      isSignedNode(nested.front()));
        instruction(OBELISK_RT_RANDOM_REAL_TRUNCATE_INTEGER_V1, *width);
      } else if (name == "$bitstoreal" || name == "$bitstoshortreal") {
        unsigned precision = name == "$bitstoreal" ? 64 : 32;
        if (realInput)
          instruction(OBELISK_RT_RANDOM_REAL_TO_INTEGER_V1, precision);
        else
          instruction(OBELISK_RT_RANDOM_CAST_V1, precision,
                      isSignedNode(nested.front()));
        instruction(precision == 64 ? OBELISK_RT_RANDOM_REAL64_BITS_V1
                                    : OBELISK_RT_RANDOM_REAL32_BITS_V1,
                    precision);
      } else {
        unsigned precision = name == "$realtobits" ? 64 : 32;
        if (!realInput || cast<FloatType>(*inputType).getWidth() != precision)
          instruction(OBELISK_RT_RANDOM_REAL_CAST_V1, precision,
                      isSignedNode(nested.front()));
        instruction(OBELISK_RT_RANDOM_REAL_TO_BITS_V1, precision);
        if (*width != precision)
          instruction(OBELISK_RT_RANDOM_CAST_V1, *width);
      }
      return success();
    }

    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(expression);
        call && call.getIsSystemCall() &&
        isIntegralRandomSystemFunction(call.getCalleeName())) {
      StringRef name = call.getCalleeName();
      if ((name == "$countbits" && nested.size() < 2) ||
          (name != "$countbits" && nested.size() != 1)) {
        emitError(getSemanticLocation(expression))
            << "malformed integral constraint system function arguments";
        return failure();
      }
      FailureOr<unsigned> inputWidth = expressionWidth(nested.front());
      if (failed(inputWidth))
        return failure();
      if (name == "$signed" || name == "$unsigned") {
        if (failed(emitProgramExpression(nested.front())))
          return failure();
        instruction(OBELISK_RT_RANDOM_CAST_V1, *width, false);
        return success();
      }
      if (name == "$isunknown") {
        // Random variables contain two-state candidates. State-only calls
        // are captured above; mixed four-state state inputs need their
        // unknown plane rather than silently returning a false result.
        bool unknownState = false;
        nested.front()->walk([&](Operation *node) {
          if (!dependsOnCandidate(node)) {
            auto type = node->getAttrOfType<TypeAttr>("semantic_type");
            FailureOr<Type> normalized = type ? getNormalizedSemanticType(node)
                                              : FailureOr<Type>(failure());
            unknownState |=
                succeeded(normalized) && isa<sim::LogicType>(*normalized);
          }
        });
        if (unknownState) {
          emitError(getSemanticLocation(expression))
              << "$isunknown with mixed four-state state inputs requires "
                 "an unknown-plane constraint capture";
          return failure();
        }
        literalInstruction(*width, false, APInt(*width, 0));
        return success();
      }
      if (name == "$countbits") {
        // Each distinct control state contributes at most once. X and Z
        // controls cannot match a two-state candidate; controls are reduced
        // to their low bit as required by 20.9.
        auto emitControlSet = [&](bool state) -> LogicalResult {
          emitLiteral(false);
          for (Operation *control : ArrayRef(nested).drop_front()) {
            if (auto spelling = getConstantSpelling(control)) {
              FailureOr<ParsedConstant> parsed =
                  parseSVInteger(*spelling, 1, getSemanticLocation(control));
              if (failed(parsed))
                return failure();
              emitLiteral(parsed->unknown.isZero() &&
                          parsed->value.getBoolValue() == state);
            } else {
              if (failed(emitProgramExpression(control)))
                return failure();
              instruction(OBELISK_RT_RANDOM_CAST_V1, 1, false);
              emitLiteral(state);
              instruction(OBELISK_RT_RANDOM_EQ_V1, 1);
            }
            instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
          }
          instruction(OBELISK_RT_RANDOM_CAST_V1, *width, false);
          return success();
        };
        if (failed(emitProgramExpression(nested.front())))
          return failure();
        instruction(OBELISK_RT_RANDOM_COUNT_ONES_V1, *width);
        if (failed(emitControlSet(true)))
          return failure();
        instruction(OBELISK_RT_RANDOM_MUL_V1, *width);
        literalInstruction(*width, false, APInt(*width, *inputWidth));
        if (failed(emitProgramExpression(nested.front())))
          return failure();
        instruction(OBELISK_RT_RANDOM_COUNT_ONES_V1, *width);
        instruction(OBELISK_RT_RANDOM_SUB_V1, *width);
        if (failed(emitControlSet(false)))
          return failure();
        instruction(OBELISK_RT_RANDOM_MUL_V1, *width);
        instruction(OBELISK_RT_RANDOM_ADD_V1, *width);
        return success();
      }
      if (failed(emitProgramExpression(nested.front())))
        return failure();
      if (name == "$clog2") {
        instruction(OBELISK_RT_RANDOM_CLOG2_V1, *width);
        return success();
      }
      unsigned countWidth = name == "$countones" ? *width : 32;
      instruction(OBELISK_RT_RANDOM_COUNT_ONES_V1, countWidth);
      if (name != "$countones") {
        literalInstruction(countWidth, false, APInt(countWidth, 1));
        instruction(name == "$onehot" ? OBELISK_RT_RANDOM_EQ_V1
                                      : OBELISK_RT_RANDOM_LE_V1,
                    *width);
      }
      return success();
    }

    if (isa<semantic::SVMemberAccessExpressionOp>(expression)) {
      auto offset = expression->getAttrOfType<IntegerAttr>("packed_offset");
      if (nested.size() != 1) {
        emitError(getSemanticLocation(expression))
            << "random packed member has no valid fixed bit range";
        return failure();
      }
      FailureOr<unsigned> baseWidth = expressionWidth(nested.front());
      if (failed(baseWidth))
        return failure();
      if (!offset || offset.getValue().isNegative() ||
          offset.getValue().getActiveBits() > 32 ||
          offset.getValue().getZExtValue() > *baseWidth ||
          *width > *baseWidth - offset.getValue().getZExtValue()) {
        emitError(getSemanticLocation(expression))
            << "random packed member has no valid fixed bit range";
        return failure();
      }
      if (failed(emitProgramExpression(nested.front())))
        return failure();
      literalInstruction(*baseWidth, false,
                         APInt(*baseWidth, offset.getValue().getZExtValue()));
      instruction(OBELISK_RT_RANDOM_SHIFT_RIGHT_V1, *baseWidth);
      instruction(OBELISK_RT_RANDOM_CAST_V1, *width, false);
      return success();
    }

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
      using Binary = semantic::SVBinaryOperator;
      if (nested.size() != 2)
        return failure();
      bool signedBase = isSignedNode(nested[0]);
      if (binary.getOperatorKind() == Binary::Divide ||
          binary.getOperatorKind() == Binary::Mod) {
        // Undefined arithmetic was normalized above. Its value plane already
        // has a nonzero divisor; a known nonzero source needs no normalization.
        if (failed(emitProgramExpression(nested[0])) ||
            failed(emitProgramExpression(nested[1])))
          return failure();
        instruction(binary.getOperatorKind() == Binary::Divide
                        ? OBELISK_RT_RANDOM_DIV_V1
                        : OBELISK_RT_RANDOM_MOD_V1,
                    *width, signedBase);
        return success();
      }
      if (binary.getOperatorKind() == Binary::Power &&
          isSignedNode(nested[1])) {
        auto exponentWidth = expressionWidth(nested[1]);
        if (failed(exponentWidth))
          return failure();
        auto emitNegative = [&]() -> LogicalResult {
          if (failed(emitProgramExpression(nested[1])))
            return failure();
          literalInstruction(*exponentWidth, true, APInt(*exponentWidth, 0));
          instruction(OBELISK_RT_RANDOM_LT_V1, 1, true);
          return success();
        };
        auto emitBaseEquals = [&](const APInt &value) -> LogicalResult {
          if (failed(emitProgramExpression(nested[0])))
            return failure();
          instruction(OBELISK_RT_RANDOM_CAST_V1, *width, signedBase);
          literalInstruction(*width, false, value);
          instruction(OBELISK_RT_RANDOM_EQ_V1, 1);
          return success();
        };
        if (failed(emitNegative()) || failed(emitBaseEquals(APInt(*width, 1))))
          return failure();
        literalInstruction(*width, false, APInt(*width, 1));
        if (signedBase) {
          if (failed(emitBaseEquals(APInt::getAllOnes(*width))) ||
              failed(emitProgramExpression(nested[1])))
            return failure();
          literalInstruction(*exponentWidth, false, APInt(*exponentWidth, 1));
          instruction(OBELISK_RT_RANDOM_BIT_AND_V1, *exponentWidth);
          literalInstruction(*width, false, APInt::getAllOnes(*width));
          literalInstruction(*width, false, APInt(*width, 1));
          instruction(OBELISK_RT_RANDOM_SELECT_V1, *width);
          literalInstruction(*width, false, APInt(*width, 0));
          instruction(OBELISK_RT_RANDOM_SELECT_V1, *width);
        } else {
          literalInstruction(*width, false, APInt(*width, 0));
        }
        instruction(OBELISK_RT_RANDOM_SELECT_V1, *width);
        if (failed(emitProgramExpression(nested[0])) || failed(emitNegative()))
          return failure();
        literalInstruction(*exponentWidth, false, APInt(*exponentWidth, 0));
        if (failed(emitProgramExpression(nested[1])))
          return failure();
        instruction(OBELISK_RT_RANDOM_SELECT_V1, *exponentWidth);
        instruction(OBELISK_RT_RANDOM_POWER_V1, *width, signedBase);
        instruction(OBELISK_RT_RANDOM_SELECT_V1, *width);
        return success();
      }
      if (nested.size() != 2 || failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramExpression(nested[1])))
        return failure();
      uint8_t opcode = 0;
      bool signedOperation = isSignedNode(nested.front());
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
            combinedWidth > std::numeric_limits<unsigned>::max() - *inputWidth)
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
      if (nested.empty())
        return failure();
      if (nested.size() == 1) {
        emitLiteral(false);
        return success();
      }
      bool first = true;
      for (Operation *item : ArrayRef(nested).drop_front()) {
        if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item)) {
          SmallVector<Operation *> endpoints = getChildren(range);
          if (range.getRangeKind() != semantic::SVValueRangeKind::Simple ||
              endpoints.size() != 2)
            return failure();
          bool unboundedLow = isUnboundedEndpoint(endpoints[0]);
          bool unboundedHigh = isUnboundedEndpoint(endpoints[1]);
          if (unboundedLow && unboundedHigh)
            return failure();
          if (!unboundedLow) {
            if (failed(emitProgramExpression(nested.front())) ||
                failed(emitProgramExpression(endpoints[0])))
              return failure();
            instruction(OBELISK_RT_RANDOM_GE_V1, 1,
                        isSignedNode(nested.front()));
          }
          if (!unboundedHigh) {
            if (failed(emitProgramExpression(nested.front())) ||
                failed(emitProgramExpression(endpoints[1])))
              return failure();
            instruction(OBELISK_RT_RANDOM_LE_V1, 1,
                        isSignedNode(nested.front()));
          }
          if (!unboundedLow && !unboundedHigh)
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
  Operation *priorityHardTarget = nullptr;
  emitProgramConstraint = [&](Operation *constraint,
                              Operation *softTarget) -> LogicalResult {
    if (arithmeticFailure)
      return failure();
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
    if (isa<semantic::SVSolveBeforeConstraintOp,
            semantic::SVDisableSoftConstraintOp>(constraint)) {
      emitLiteral(true);
      return success();
    }
    if (auto expression =
            dyn_cast<semantic::SVExpressionConstraintOp>(constraint)) {
      bool selected = softTarget ? softTarget == constraint
                                 : !expression.getIsSoft() &&
                                       (!priorityHardTarget ||
                                        priorityHardTarget == constraint);
      if (nested.size() != 1)
        return failure();
      Operation *valid = arithmeticValidity(nested.front());
      bool safetyOnly =
          !softTarget && expression.getIsSoft() && valid &&
          (!priorityHardTarget || priorityHardTarget == constraint);
      if (!selected && !safetyOnly) {
        emitLiteral(true);
        return success();
      }
      if (safetyOnly)
        return emitProgramExpression(valid);
      auto equality = dyn_cast<semantic::SVBinaryExpressionOp>(nested.front());
      if (equality &&
          equality.getOperatorKind() == semantic::SVBinaryOperator::Equality &&
          arithmetic.needed(equality)) {
        // A true equality requires equal canonical value bits and a known
        // result. Keep its direct definition visible to compiler Z3; encoding
        // the three-valued comparison as one masked Boolean hides that shape.
        auto operands = getChildren(equality);
        if (operands.size() != 2)
          return failure();
        auto left = arithmetic.get(operands[0]);
        auto right = arithmetic.get(operands[1]);
        if (failed(left) || failed(right) ||
            failed(emitProgramExpression(left->value)) ||
            failed(emitProgramExpression(right->value)))
          return failure();
        instruction(OBELISK_RT_RANDOM_EQ_V1, 1, isSignedNode(operands[0]));
      } else if (failed(emitProgramExpression(nested.front())))
        return failure();
      if (valid) {
        if (failed(emitProgramExpression(valid)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
      }
      return success();
    }
    if (isa<semantic::SVImplicationConstraintOp>(constraint)) {
      if (nested.size() != 2 || failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramConstraint(nested[1], softTarget)))
        return failure();
      instruction(OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1, 1);
      if (!softTarget &&
          (!priorityHardTarget || priorityHardTarget == constraint))
        if (Operation *valid = arithmeticValidity(nested.front())) {
          if (failed(emitProgramExpression(valid)))
            return failure();
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        }
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
      if (!softTarget &&
          (!priorityHardTarget || priorityHardTarget == constraint))
        if (Operation *valid = arithmeticValidity(nested.front())) {
          if (failed(emitProgramExpression(valid)))
            return failure();
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        }
      return success();
    }
    if (isa<semantic::SVUniquenessConstraintOp>(constraint)) {
      if (softTarget ||
          (priorityHardTarget && priorityHardTarget != constraint)) {
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
      for (Operation *operand : nested)
        if (Operation *valid = arithmeticValidity(operand)) {
          if (failed(emitProgramExpression(valid)))
            return failure();
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        }
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
  Value programSavedEnclosingThis = enclosingThisObject;
  SmallVector<Value> programSavedCandidates =
      std::move(randomizeCandidateValues);
  llvm::scope_exit restoreProgramBindings([&] {
    thisObject = programSavedThis;
    enclosingThisObject = programSavedEnclosingThis;
    randomizeCandidateValues = std::move(programSavedCandidates);
  });
  // IEEE 1800-2017 18.7: a name an inline constraint does not resolve in the
  // randomize() with object class resolves in the scope containing the call,
  // so that scope's object stays reachable alongside the receiver.
  if (!scopeRandomize) {
    enclosingThisObject = programSavedThis;
    thisObject = receiver;
  }
  for (const Property &property : planned)
    randomizeCandidateValues.push_back(property.currentValue);
  bool emittedHard = false;
  bool emittedSoft = false;
  llvm::DenseMap<Operation *, uint64_t> softPriorities;
  llvm::DenseMap<Operation *, uint64_t> softDeclarationOrder;
  struct SoftDisable {
    Operation *source;
    uint64_t declarationOrder;
    uint32_t block;
    SmallVector<std::pair<Operation *, bool>> guards;
  };
  SmallVector<SoftDisable> softDisables;
  uint64_t nextSoftPriority = 0;
  uint64_t nextDeclarationOrder = 0;
  auto assignSoftPriorities = [&](bool inlineConstraints) {
    for (auto [index, root] : llvm::enumerate(children)) {
      if (isReceiverChild(index) ||
          root->hasAttr(randomConstraintBlockAttrName) == inlineConstraints)
        continue;
      SmallVector<Operation *> items = isa<semantic::SVConstraintListOp>(root)
                                           ? getChildren(root)
                                           : SmallVector<Operation *>{root};
      for (Operation *item : items)
        item->walk<WalkOrder::PreOrder>([&](Operation *nested) {
          if (auto expression =
                  dyn_cast<semantic::SVExpressionConstraintOp>(nested)) {
            if (expression.getIsSoft()) {
              softPriorities[expression] = nextSoftPriority++;
              softDeclarationOrder[expression] = nextDeclarationOrder++;
            }
          } else if (isa<semantic::SVDisableSoftConstraintOp>(nested)) {
            uint32_t block = OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
            if (auto index = root->getAttrOfType<IntegerAttr>(
                    randomConstraintBlockAttrName))
              block = static_cast<uint32_t>(index.getInt());
            SoftDisable disable{nested, nextDeclarationOrder++, block, {}};
            for (Operation *child = nested; child != root;) {
              Operation *parent = child->getParentOp();
              SmallVector<Operation *> branches = getChildren(parent);
              if (isa<semantic::SVImplicationConstraintOp>(parent)) {
                disable.guards.push_back({branches.front(), true});
              } else if (isa<semantic::SVConditionalConstraintOp>(parent)) {
                disable.guards.push_back(
                    {branches.front(), child == branches[1]});
              }
              child = parent;
            }
            softDisables.push_back(std::move(disable));
          }
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

  // Only references inside the soft expression count. An enclosing
  // implication/if guard does not make its variables targets of disable soft
  // (IEEE 1800-2023 18.5.13.2). Stop at a precise packed selection so a
  // reference to x[0] does not accidentally become a reference to all of x.
  struct SoftReference {
    uint64_t offset;
    uint64_t width;
    SymbolRefAttr symbol;
  };
  auto collectSoftReferences = [&](Operation *expression) {
    SmallVector<SoftReference> references;
    expression->walk<WalkOrder::PreOrder>([&](Operation *nested) -> WalkResult {
      if (auto inputs =
              nested->getAttrOfType<IntegerAttr>(randomGuardInputsAttrName)) {
        APInt mask = inputs.getValue().zextOrTrunc(assignmentWidth);
        while (!mask.isZero()) {
          unsigned offset = mask.countTrailingZeros();
          unsigned width = mask.lshr(offset).countTrailingOnes();
          references.push_back({offset, width, {}});
          mask.clearBits(offset, offset + width);
        }
        // The retained input inventory also carries direct non-rand symbol
        // references used by disable soft. Expanded body reads are marked
        // indirect separately below.
        return WalkResult::advance();
      }
      // Reads introduced by expanding a constraint function's body are
      // indirect references, not references written in the soft expression.
      if (nested->hasAttr(randomFunctionStateAttrName))
        return WalkResult::skip();
      if (auto offset = nested->getAttrOfType<IntegerAttr>(
              randomVariableBitOffsetAttrName)) {
        auto type = nested->getAttrOfType<TypeAttr>("semantic_type");
        std::optional<uint64_t> width =
            type ? getSemanticBitstreamWidth(type.getValue()) : std::nullopt;
        if (width) {
          references.push_back({offset.getValue().getZExtValue(), *width, {}});
          return WalkResult::skip();
        }
      }
      if (auto variable =
              nested->getAttrOfType<IntegerAttr>(randomVariableAttrName)) {
        uint64_t index = variable.getValue().getZExtValue();
        if (index < planned.size()) {
          uint64_t offset = propertyOffsets[index];
          references.push_back({offset, planned[index].width, {}});
          return WalkResult::skip();
        }
      }
      if (auto symbol =
              nested->getAttrOfType<SymbolRefAttr>("referenced_symbol"))
        references.push_back({0, 0, symbol});
      return WalkResult::advance();
    });
    return references;
  };
  struct PriorityInputRecord {
    uint32_t index;
    APInt references, arguments;
    uint32_t maskCapture = 0;
  };
  SmallVector<PriorityInputRecord> priorityInputRecords;
  // Summarize immutable subtrees once. Safe captures and plane projections
  // request these inventories at every node; walking each complete subtree
  // again would make a nested constraint quadratic in its lowered size.
  llvm::DenseMap<Operation *, APInt> referenceInventories;
  std::function<APInt(Operation *)> sourceReferences = [&](Operation *source) {
    if (auto found = referenceInventories.find(source);
        found != referenceInventories.end())
      return found->second;
    APInt inputs = APInt::getZero(assignmentWidth);
    bool descend = true;
    if (auto mask =
            source->getAttrOfType<IntegerAttr>(randomGuardInputsAttrName))
      inputs = mask.getValue().zextOrTrunc(assignmentWidth);
    else if (source->hasAttr(randomFunctionStateAttrName))
      descend = false;
    else {
      if (auto offset = source->getAttrOfType<IntegerAttr>(
              randomVariableBitOffsetAttrName)) {
        auto type = source->getAttrOfType<TypeAttr>("semantic_type");
        auto width =
            type ? getSemanticBitstreamWidth(type.getValue()) : std::nullopt;
        if (width) {
          inputs |= APInt::getLowBitsSet(assignmentWidth, *width)
                        .shl(offset.getValue().getZExtValue());
          descend = false;
        }
      }
      if (descend)
        if (auto variable =
                source->getAttrOfType<IntegerAttr>(randomVariableAttrName)) {
          uint64_t index = variable.getValue().getZExtValue();
          if (index < planned.size()) {
            inputs |=
                APInt::getLowBitsSet(assignmentWidth, planned[index].width)
                    .shl(propertyOffsets[index]);
            descend = false;
          }
        }
    }
    if (descend)
      for (Region &region : source->getRegions())
        for (Block &block : region)
          for (Operation &child : block)
            inputs |= sourceReferences(&child);
    referenceInventories[source] = inputs;
    return inputs;
  };
  llvm::DenseMap<Operation *, APInt> argumentInventories;
  std::function<APInt(Operation *)> sourceFunctionArguments =
      [&](Operation *source) {
        if (auto found = argumentInventories.find(source);
            found != argumentInventories.end())
          return found->second;
        APInt inputs = APInt::getZero(assignmentWidth);
        if (auto mask =
                source->getAttrOfType<IntegerAttr>(randomGuardInputsAttrName))
          inputs = mask.getValue().zextOrTrunc(assignmentWidth);
        else if (!source->hasAttr(randomFunctionStateAttrName))
          for (Region &region : source->getRegions())
            for (Block &block : region)
              for (Operation &child : block)
                inputs |= sourceFunctionArguments(&child);
        argumentInventories[source] = inputs;
        return inputs;
      };
  auto priorityInputs = [&](Operation *source) {
    if (auto inputs =
            source->getAttrOfType<IntegerAttr>(randomPriorityInputsAttrName))
      return inputs.getValue().zextOrTrunc(assignmentWidth);
    return sourceReferences(source);
  };
  auto priorityArguments = [&](Operation *source) {
    if (auto inputs =
            source->getAttrOfType<IntegerAttr>(randomPriorityArgumentsAttrName))
      return inputs.getValue().zextOrTrunc(assignmentWidth);
    return APInt::getZero(assignmentWidth);
  };

  // IEEE 1800-2023 18.5.12 evaluates guards before forming the relation.
  // An active rand reference is RANDOM even when a proposed value happens to
  // make its predicate true. Only state TRUE/FALSE can absorb an ERROR.
  auto guardConstant = [&](bool value) -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(value));
  };
  auto guardNot = [&](Value value) -> Value {
    if (matchPattern(value, m_One()))
      return guardConstant(false);
    if (matchPattern(value, m_Zero()))
      return guardConstant(true);
    return arith::XOrIOp::create(builder, location, value, guardConstant(true));
  };
  auto guardAnd = [&](Value lhs, Value rhs) -> Value {
    if (matchPattern(lhs, m_Zero()) || matchPattern(rhs, m_Zero()))
      return guardConstant(false);
    if (matchPattern(lhs, m_One()))
      return rhs;
    if (matchPattern(rhs, m_One()))
      return lhs;
    return arith::AndIOp::create(builder, location, lhs, rhs);
  };
  auto guardOr = [&](Value lhs, Value rhs) -> Value {
    if (matchPattern(lhs, m_One()) || matchPattern(rhs, m_One()))
      return guardConstant(true);
    if (matchPattern(lhs, m_Zero()))
      return rhs;
    if (matchPattern(rhs, m_Zero()))
      return lhs;
    return arith::OrIOp::create(builder, location, lhs, rhs);
  };
  auto unionGuardInputs = [&](Value lhs, Value rhs) -> Value {
    if (matchPattern(lhs, m_Zero()))
      return rhs;
    if (matchPattern(rhs, m_Zero()))
      return lhs;
    APInt left, right;
    if (matchPattern(lhs, m_ConstantInt(&left)) &&
        matchPattern(rhs, m_ConstantInt(&right)))
      return constantAssignment(left | right);
    return arith::OrIOp::create(builder, location, lhs, rhs);
  };
  auto selectGuardInputs = [&](Value condition, Value inputs) -> Value {
    if (matchPattern(inputs, m_Zero()))
      return inputs;
    if (matchPattern(condition, m_One()))
      return inputs;
    if (matchPattern(condition, m_Zero()))
      return constantAssignment64(0);
    return arith::SelectOp::create(builder, location, condition, inputs,
                                   constantAssignment64(0));
  };
  auto stateGuard = [&](const SafeConstraintValue &value,
                        bool positive) -> FailureOr<Value> {
    FailureOr<Value> predicate = truthValue(value.value, location);
    if (failed(predicate))
      return failure();
    return guardAnd(guardAnd(value.valid, guardNot(value.random)),
                    positive ? *predicate : guardNot(*predicate));
  };
  llvm::DenseMap<Operation *, SafeConstraintValue> safeStateValues;
  llvm::DenseMap<Operation *, SafeConstraintValue> safeRawStateValues;
  llvm::DenseMap<Operation *, SafeConstraintValue> safeArithmeticValues;
  llvm::DenseMap<Operation *, SafeConstraintValue> safeRawArithmeticValues;
  unsigned safeEvaluationDepth = 0;
  bool freezeSafeStateValues = true;
  bool preserveArithmeticSource = false;
  llvm::SmallDenseSet<Value> nonNullReceivers;
  for (Value object = receiver; object;) {
    nonNullReceivers.insert(object);
    auto cast = object.getDefiningOp<sim::SimClassCastOp>();
    object = cast ? cast.getObject() : Value{};
  }
  safeConstraintValue =
      [&](Operation *expression) -> FailureOr<SafeConstraintValue> {
    // Every recursive evaluation appends straight-line values and guarded
    // reads followed by their merge blocks. Shared plane values therefore
    // dominate later uses in this evaluation. Start a fresh bank for the next
    // root: candidate bindings and enclosing control flow may have changed.
    if (safeEvaluationDepth++ == 0) {
      safeArithmeticValues.clear();
      safeRawArithmeticValues.clear();
    }
    auto finishEvaluation = llvm::scope_exit([&] { --safeEvaluationDepth; });
    bool graphNode = arithmetic.isNode(expression);
    auto &graphValues = preserveArithmeticSource ? safeRawArithmeticValues
                                                 : safeArithmeticValues;
    if (graphNode)
      if (auto found = graphValues.find(expression); found != graphValues.end())
        return found->second;
    bool candidate = dependsOnCandidate(expression);
    // Plane construction retains immutable source expressions. Reuse their
    // pre-solve captures by structural identity, including beneath a case or
    // wildcard comparison that needs the original X/Z symbols. Keep those
    // raw captures separate from normalized arithmetic values.
    Operation *stateKey =
        candidate ? nullptr : arithmetic.canonical(expression);
    auto &stateValues =
        preserveArithmeticSource ? safeRawStateValues : safeStateValues;
    if (stateKey)
      if (auto found = stateValues.find(stateKey); found != stateValues.end())
        return found->second;
    FailureOr<Type> resultType = getNormalizedSemanticType(expression);
    if (failed(resultType))
      return failure();
    SmallVector<Operation *> operands = arithmetic.operands(expression);
    if (auto plane =
            expression->getAttrOfType<IntegerAttr>(randomArithmeticPlaneAttr)) {
      if (operands.size() != 1 || plane.getInt() < 0 ||
          plane.getInt() >
              static_cast<int32_t>(RandomArithmeticPlane::FrozenHighImpedance))
        return failure();
      auto kind = static_cast<RandomArithmeticPlane>(plane.getInt());
      bool frozen = kind == RandomArithmeticPlane::FrozenUnknown ||
                    kind == RandomArithmeticPlane::FrozenHighImpedance;
      bool saved = preserveArithmeticSource;
      preserveArithmeticSource = true;
      auto input = safeConstraintValue(operands.front());
      preserveArithmeticSource = saved;
      if (failed(input))
        return failure();
      Type bitsType = sim::getPackedScalarType(*resultType);
      Value source = input->value;
      unsigned precision = cast<IntegerType>(bitsType).getWidth();
      unsigned offset = 0;
      if (frozen) {
        APInt sourceMask = priorityInputs(operands.front());
        offset = sourceMask.countTrailingZeros();
        if (offset >= assignmentWidth || precision > assignmentWidth - offset ||
            sourceMask != APInt::getBitsSet(assignmentWidth, offset,
                                            offset + precision)) {
          emitError(getSemanticLocation(expression))
              << "frozen unknown-plane property binding is not a fixed bit "
                 "range";
          return failure();
        }
        auto propertyEnd = llvm::upper_bound(propertyOffsets, offset);
        if (propertyEnd == propertyOffsets.begin())
          return failure();
        unsigned property =
            std::distance(propertyOffsets.begin(), propertyEnd) - 1;
        unsigned localOffset = offset - propertyOffsets[property];
        if (localOffset >= planned[property].width ||
            precision > planned[property].width - localOffset)
          return failure();
        // Candidate checkers temporarily replace ordinary variable reads with
        // proposed bits. Frozen unknown symbols must always come from the
        // pre-solve snapshot, including inside those checkers.
        source = planned[property].originalScalar;
        if (!source)
          return failure();
        if (auto logic = dyn_cast<sim::LogicType>(source.getType());
            logic && (localOffset || logic.getWidth() != precision))
          source = sim::SimLogicExtractOp::create(
              builder, location,
              sim::LogicType::get(builder.getContext(), precision), source,
              builder.getI64IntegerAttr(localOffset));
      }
      auto scalar = toRandomScalar(source, location);
      if (failed(scalar))
        return failure();
      Value value;
      if (auto logic = dyn_cast<sim::LogicType>(scalar->getType())) {
        Value bits =
            sim::SimLogicToBitsOp::create(builder, location, bitsType, *scalar);
        if (kind == RandomArithmeticPlane::Value)
          value = bits;
        else if (kind == RandomArithmeticPlane::HighImpedance ||
                 kind == RandomArithmeticPlane::FrozenHighImpedance) {
          auto all =
              builder.getIntegerAttr(bitsType, APInt::getAllOnes(precision));
          Value highImpedance = sim::SimLogicConstantOp::create(
              builder, location, logic, all, all);
          Value difference = sim::SimLogicCaseDifferenceMaskOp::create(
              builder, location, bitsType, *scalar, highImpedance);
          value = arith::XOrIOp::create(
              builder, location, difference,
              arith::ConstantOp::create(builder, location, bitsType, all));
        } else {
          Value known =
              sim::SimLogicFromBitsOp::create(builder, location, logic, bits);
          value = sim::SimLogicCaseDifferenceMaskOp::create(
              builder, location, bitsType, *scalar, known);
        }
      } else {
        auto converted = convert(*scalar, bitsType, false, location);
        if (failed(converted))
          return failure();
        value =
            kind == RandomArithmeticPlane::Value
                ? *converted
                : arith::ConstantOp::create(builder, location, bitsType,
                                            builder.getIntegerAttr(bitsType, 0))
                      .getResult();
      }
      Value random = input->random;
      if (frozen) {
        Value active = activeGuardBits;
        if (offset)
          active = arith::ShRUIOp::create(builder, location, active,
                                          constantAssignment64(offset));
        auto selected = convert(active, bitsType, false, location);
        if (failed(selected))
          return failure();
        Value inactive = arith::XOrIOp::create(
            builder, location, *selected,
            arith::ConstantOp::create(
                builder, location, bitsType,
                builder.getIntegerAttr(bitsType,
                                       APInt::getAllOnes(precision))));
        value = arith::AndIOp::create(builder, location, value, inactive);
        random = guardConstant(false);
      }
      SafeConstraintValue safe{value,
                               input->errors,
                               random,
                               input->inputs,
                               input->arguments,
                               input->errors,
                               input->dynamicInputs};
      if (stateKey && freezeSafeStateValues)
        stateValues[stateKey] = safe;
      if (graphNode)
        graphValues[expression] = safe;
      return safe;
    }
    SmallVector<Operation *> inputs;
    for (Operation *operand : operands) {
      if (isa<semantic::SVNullLiteralOp>(operand)) {
        // Handle comparisons materialize null in the type of the other
        // operand. It has no independent runtime type or evaluation error.
        continue;
      } else if (isa<semantic::SVValueRangeExpressionOp>(operand)) {
        auto endpoints = getChildren(operand);
        for (Operation *endpoint : endpoints)
          if (!isUnboundedEndpoint(endpoint))
            inputs.push_back(endpoint);
      } else {
        inputs.push_back(operand);
      }
    }
    SmallVector<SafeConstraintValue> values;
    Value valid = guardConstant(true);
    Value errors = guardConstant(true);
    auto caseComparison = dyn_cast<semantic::SVBinaryExpressionOp>(expression);
    bool consumesSymbols =
        isa<semantic::SVInsideExpressionOp>(expression) ||
        (caseComparison &&
         (caseComparison.getOperatorKind() ==
              semantic::SVBinaryOperator::CaseEquality ||
          caseComparison.getOperatorKind() ==
              semantic::SVBinaryOperator::CaseInequality ||
          caseComparison.getOperatorKind() ==
              semantic::SVBinaryOperator::WildcardEquality ||
          caseComparison.getOperatorKind() ==
              semantic::SVBinaryOperator::WildcardInequality));
    {
      bool saved = preserveArithmeticSource;
      auto restore =
          llvm::scope_exit([&] { preserveArithmeticSource = saved; });
      preserveArithmeticSource |= consumesSymbols;
      for (auto [index, input] : llvm::enumerate(inputs)) {
        // X/Z in an address is a structural evaluation error, even if a
        // surrounding two-state cast consumes unknown data bits. Bounds
        // checks need the original index symbols, rather than canonical
        // arithmetic value bits that replace those symbols with zero.
        preserveArithmeticSource =
            saved || consumesSymbols ||
            (index != 0 &&
             isa<semantic::SVElementSelectExpressionOp,
                 semantic::SVRangeSelectExpressionOp>(expression));
        FailureOr<SafeConstraintValue> value = safeConstraintValue(input);
        if (failed(value))
          return failure();
        values.push_back(*value);
        valid = guardAnd(valid, value->valid);
        errors = guardAnd(errors, value->errors);
      }
    }
    if (consumesSymbols)
      valid = errors;
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(expression);
        call && call.getIsSystemCall() &&
        call.getCalleeName() == "$countbits" && !values.empty()) {
      // X/Z controls are legal constant symbol selectors, rather than
      // unknown data values. Their structural evaluation must still succeed.
      valid = guardAnd(values.front().valid, errors);
    }
    APInt inputMask = graphNode ? APInt::getZero(assignmentWidth)
                                : priorityInputs(expression);
    Value guardInputs = constantAssignment(inputMask);
    Value guardArguments =
        graphNode ? constantAssignment64(0)
                  : constantAssignment(sourceFunctionArguments(expression));
    bool dynamicInputs = false;
    if (graphNode) {
      for (const SafeConstraintValue &value : values) {
        guardInputs = unionGuardInputs(guardInputs, value.inputs);
        guardArguments = unionGuardInputs(guardArguments, value.arguments);
        dynamicInputs |= value.dynamicInputs;
      }
    } else if (llvm::any_of(
                   values,
                   [](const auto &value) { return value.dynamicInputs; }) &&
               !expression->hasAttr(randomVariableAttrName) &&
               !expression->hasAttr(randomVariableBitOffsetAttrName)) {
      dynamicInputs = true;
      APInt remainingInputs = inputMask;
      APInt remainingArguments = sourceFunctionArguments(expression);
      Value selectedInputs = constantAssignment64(0);
      Value selectedArguments = constantAssignment64(0);
      for (auto [input, value] : llvm::zip_equal(inputs, values)) {
        // Rebuild every child contribution: an unselected element may overlap
        // a different, independently referenced sibling's static bit range.
        remainingInputs &= ~priorityInputs(input);
        remainingArguments &= ~sourceFunctionArguments(input);
        selectedInputs = unionGuardInputs(selectedInputs, value.inputs);
        selectedArguments =
            unionGuardInputs(selectedArguments, value.arguments);
      }
      guardInputs =
          unionGuardInputs(constantAssignment(remainingInputs), selectedInputs);
      guardArguments = unionGuardInputs(constantAssignment(remainingArguments),
                                        selectedArguments);
    }
    Value random =
        matchPattern(guardInputs, m_Zero())
            ? guardConstant(false)
            : arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                    arith::AndIOp::create(builder, location,
                                                          activeGuardBits,
                                                          guardInputs),
                                    constantAssignment64(0))
                  .getResult();

    // A state-selected conditional evaluates only its selected arm. Retain
    // both arms while the condition is RANDOM, just as for an if constraint,
    // and drop inactive arm dependencies from implicit function ordering.
    if (auto conditional =
            dyn_cast<semantic::SVConditionalExpressionOp>(expression);
        conditional && conditional.getConditionCount() == 1 &&
        conditional.getConditionPatternFlags().size() == 1 &&
        conditional.getConditionPatternFlags().front() == 0 &&
        !values.empty() &&
        sim::getPackedScalarType(values[0].value.getType())) {
      if (values.size() != 3)
        return failure();
      if (expression->hasAttr(randomFunctionConditionalAttrName) &&
          !matchPattern(values[0].random, m_Zero()) &&
          (!matchPattern(values[1].errors, m_One()) ||
           !matchPattern(values[2].errors, m_One()))) {
        emitError(getSemanticLocation(expression))
            << "candidate-conditioned constraint function evaluation errors "
               "require staged function execution";
        return failure();
      }
      FailureOr<Value> trueGuard = stateGuard(values[0], true);
      FailureOr<Value> falseGuard = stateGuard(values[0], false);
      if (failed(trueGuard) || failed(falseGuard))
        return failure();
      Value yesEnabled = guardNot(*falseGuard);
      Value noEnabled = guardNot(*trueGuard);
      valid = guardAnd(values[0].valid,
                       guardAnd(guardOr(*falseGuard, values[1].valid),
                                guardOr(*trueGuard, values[2].valid)));
      errors = guardAnd(values[0].errors,
                        guardAnd(guardOr(*falseGuard, values[1].errors),
                                 guardOr(*trueGuard, values[2].errors)));
      random = guardAnd(
          valid, guardOr(values[0].random,
                         guardOr(guardAnd(yesEnabled, values[1].random),
                                 guardAnd(noEnabled, values[2].random))));
      guardInputs = unionGuardInputs(
          values[0].inputs,
          unionGuardInputs(selectGuardInputs(yesEnabled, values[1].inputs),
                           selectGuardInputs(noEnabled, values[2].inputs)));
      guardArguments = unionGuardInputs(
          values[0].arguments,
          unionGuardInputs(selectGuardInputs(yesEnabled, values[1].arguments),
                           selectGuardInputs(noEnabled, values[2].arguments)));
      dynamicInputs = true;
    }

    Value result;
    auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(expression);
    auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(expression);
    if (expression->hasAttr(randomEvaluationInputsAttrName)) {
      auto count = expression->getAttrOfType<IntegerAttr>(
          randomEvaluationInputsAttrName);
      if (count.getValue().isNegative() ||
          count.getValue().getActiveBits() > 64 ||
          count.getValue().getZExtValue() + 1 != values.size())
        return failure();
      FailureOr<Value> converted =
          convert(values.back().value, *resultType, isSignedNode(inputs.back()),
                  location);
      if (failed(converted))
        return failure();
      result = *converted;
      // Expanded function evaluation retains errors in every executed input.
      // An unused four-state value does not taint a known function result.
      valid = guardAnd(errors, values.back().valid);
    } else if (expression->hasAttr(randomArithmeticRealFiniteAttr)) {
      if (values.size() != 1 || !isa<FloatType>(values.front().value.getType()))
        return failure();
      unsigned precision =
          cast<FloatType>(values.front().value.getType()).getWidth();
      unsigned fraction = precision == 32 ? 23 : 52;
      uint64_t exponentMask = precision == 32 ? 255 : 2047;
      Type integer = builder.getIntegerType(precision);
      auto constant = [&](uint64_t value) {
        return arith::ConstantOp::create(
            builder, location, integer, builder.getIntegerAttr(integer, value));
      };
      Value payload = arith::BitcastOp::create(builder, location, integer,
                                               values.front().value);
      Value exponent = arith::AndIOp::create(
          builder, location,
          arith::ShRUIOp::create(builder, location, payload,
                                 constant(fraction)),
          constant(exponentMask));
      result =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                exponent, constant(exponentMask));
      valid = errors;
    } else if (binary && (binary.getOperatorKind() ==
                              semantic::SVBinaryOperator::LogicalAnd ||
                          binary.getOperatorKind() ==
                              semantic::SVBinaryOperator::LogicalOr)) {
      if (values.size() != 2)
        return failure();
      bool conjunction =
          binary.getOperatorKind() == semantic::SVBinaryOperator::LogicalAnd;
      FailureOr<Value> lhsAbsorbs = stateGuard(values[0], !conjunction);
      FailureOr<Value> rhsAbsorbs = stateGuard(values[1], !conjunction);
      FailureOr<Value> lhs = truthValue(values[0].value, location);
      FailureOr<Value> rhs = truthValue(values[1].value, location);
      if (failed(lhsAbsorbs) || failed(rhsAbsorbs) || failed(lhs) ||
          failed(rhs))
        return failure();
      Value absorbs = guardOr(*lhsAbsorbs, *rhsAbsorbs);
      valid = guardOr(absorbs, valid);
      errors = guardOr(absorbs, errors);
      random = guardAnd(
          guardNot(absorbs),
          guardAnd(valid, guardOr(values[0].random, values[1].random)));
      guardInputs = selectGuardInputs(
          guardNot(absorbs),
          unionGuardInputs(values[0].inputs, values[1].inputs));
      guardArguments = selectGuardInputs(
          guardNot(absorbs),
          unionGuardInputs(values[0].arguments, values[1].arguments));
      Value predicate =
          conjunction ? guardAnd(*lhs, *rhs) : guardOr(*lhs, *rhs);
      if (isa<sim::LogicType>(*resultType)) {
        FailureOr<Value> left = conditionalPredicate(values[0].value, location);
        FailureOr<Value> right =
            conditionalPredicate(values[1].value, location);
        if (failed(left) || failed(right))
          return failure();
        predicate = sim::SimLogicLogicalOp::create(
            builder, location, sim::LogicType::get(function.getContext(), 1),
            conjunction ? sim::LogicalKind::And : sim::LogicalKind::Or, *left,
            *right);
      }
      FailureOr<Value> converted =
          convert(predicate, *resultType, false, location);
      if (failed(converted))
        return failure();
      result = *converted;
    } else if (unary && unary.getOperatorKind() ==
                            semantic::SVUnaryOperator::LogicalNot) {
      if (values.size() != 1)
        return failure();
      FailureOr<Value> predicate = truthValue(values[0].value, location);
      if (failed(predicate))
        return failure();
      random = values[0].random;
      guardInputs = values[0].inputs;
      guardArguments = values[0].arguments;
      Value negated = guardNot(*predicate);
      if (isa<sim::LogicType>(*resultType)) {
        FailureOr<Value> logical =
            conditionalPredicate(values[0].value, location);
        if (failed(logical))
          return failure();
        negated = sim::SimLogicUnaryOp::create(
            builder, location, sim::LogicType::get(function.getContext(), 1),
            sim::UnaryKind::LogicalNot, *logical);
      }
      FailureOr<Value> converted =
          convert(negated, *resultType, false, location);
      if (failed(converted))
        return failure();
      result = *converted;
    } else {
      // Capture each address operand once. Runtime reads of a null member or
      // an invalid element occur only on the valid edge; the other edge carries
      // a typed dummy and ERROR, allowing the guard algebra to sift it safely.
      auto requireSafeRead = [&](Value condition) {
        valid = guardAnd(valid, condition);
        errors = guardAnd(errors, condition);
      };
      if (expression->hasAttr(randomEvaluationErrorAttrName)) {
        valid = guardConstant(false);
        errors = guardConstant(false);
      }
      if (isa<semantic::SVMemberAccessExpressionOp>(expression) &&
          !values.empty() && expression->hasAttr("simulation.class_field") &&
          isa<sim::ClassHandleType>(values.front().value.getType()) &&
          !nonNullReceivers.contains(values.front().value)) {
        Value isNull = sim::SimManagedIsNullOp::create(
            builder, location, builder.getI1Type(), values.front().value);
        requireSafeRead(guardNot(isNull));
      }
      if (isa<semantic::SVMemberAccessExpressionOp>(expression) &&
          !values.empty() && !dependsOnCandidate(inputs.front())) {
        Type aggregate = values.front().value.getType();
        bool tagged = (isa<sim::PackedUnionType>(aggregate) &&
                       cast<sim::PackedUnionType>(aggregate).getIsTagged()) ||
                      (isa<sim::UnpackedUnionType>(aggregate) &&
                       cast<sim::UnpackedUnionType>(aggregate).getIsTagged());
        if (tagged) {
          auto ordinal =
              expression->getAttrOfType<IntegerAttr>("field_ordinal");
          if (!ordinal || ordinal.getValue().getActiveBits() > 32)
            return failure();
          requireSafeRead(sim::SimUnionIsActiveOp::create(
              builder, location, builder.getI1Type(), values.front().value,
              ordinal.getValue().getZExtValue()));
        }
      }
      if (isa<semantic::SVElementSelectExpressionOp>(expression) &&
          values.size() == 2) {
        Type baseType = values[0].value.getType();
        Value left, right;
        if (auto array = dyn_cast<sim::UnpackedArrayType>(baseType)) {
          left = constant64(array.getLeft());
          right = constant64(array.getRight());
        } else if (auto array = dyn_cast<sim::PackedArrayType>(baseType)) {
          left = constant64(array.getLeft());
          right = constant64(array.getRight());
        } else if (isa<sim::DynamicArrayType, sim::QueueType>(baseType)) {
          left = constant64(0);
          Value size = sim::SimContainerSizeOp::create(builder, location, i64,
                                                       values[0].value);
          right = arith::SubIOp::create(builder, location, size, constant64(1));
        } else if (auto width = sim::getPackedWidth(baseType)) {
          auto source = inputs[0]->getAttrOfType<TypeAttr>("semantic_type");
          auto layout = source ? getSemanticSelectionLayout(source.getValue())
                               : std::nullopt;
          left = constant64(layout ? layout->left : *width - 1);
          right = constant64(layout ? layout->right : 0);
        }
        auto sourceType =
            inputs[1] ? inputs[1]->getAttrOfType<TypeAttr>("semantic_type")
                      : TypeAttr{};
        bool signedIndex =
            sourceType && isSignedSemanticType(sourceType.getValue());
        APInt constantIndex, leftBound, rightBound;
        if (left && right && matchPattern(left, m_ConstantInt(&leftBound)) &&
            matchPattern(right, m_ConstantInt(&rightBound)) &&
            matchPattern(values[1].value, m_ConstantInt(&constantIndex))) {
          // A known constant address against static bounds is decided here,
          // so the guard algebra sees an error-free read as such (11.5.1).
          int64_t low = std::min(leftBound.getSExtValue(),
                                 rightBound.getSExtValue());
          int64_t high = std::max(leftBound.getSExtValue(),
                                  rightBound.getSExtValue());
          std::optional<int64_t> address =
              signedIndex ? (constantIndex.getSignificantBits() <= 64
                                 ? std::optional(constantIndex.getSExtValue())
                                 : std::nullopt)
                          : (constantIndex.getActiveBits() <= 63
                                 ? std::optional(static_cast<int64_t>(
                                       constantIndex.getZExtValue()))
                                 : std::nullopt);
          requireSafeRead(
              guardConstant(address && *address >= low && *address <= high));
        } else if (left && right) {
          FailureOr<Value> index =
              toArrayIndex(values[1].value, signedIndex, location);
          if (failed(index))
            return failure();
          Value known = guardConstant(true);
          if (auto logic = dyn_cast<sim::LogicType>((*index).getType())) {
            Value bits = sim::SimLogicToBitsOp::create(
                builder, location, builder.getIntegerType(logic.getWidth()),
                *index);
            Value restored =
                sim::SimLogicFromBitsOp::create(builder, location, logic, bits);
            known = sim::SimLogicCompareOp::create(
                builder, location, builder.getI1Type(),
                sim::CompareKind::CaseEq, *index, restored);
            index = bits;
          }
          FailureOr<Value> lower =
              convert(left, (*index).getType(), true, location);
          FailureOr<Value> upper =
              convert(right, (*index).getType(), true, location);
          if (failed(lower) || failed(upper))
            return failure();
          Value ascending = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::sle, *lower, *upper);
          Value min = arith::SelectOp::create(builder, location, ascending,
                                              *lower, *upper);
          Value max = arith::SelectOp::create(builder, location, ascending,
                                              *upper, *lower);
          Value inRange = guardAnd(
              arith::CmpIOp::create(builder, location,
                                    arith::CmpIPredicate::sge, *index, min),
              arith::CmpIOp::create(builder, location,
                                    arith::CmpIPredicate::sle, *index, max));
          if (isa<sim::DynamicArrayType, sim::QueueType>(baseType))
            inRange = guardAnd(inRange,
                               arith::CmpIOp::create(builder, location,
                                                     arith::CmpIPredicate::sge,
                                                     right, constant64(0)));
          requireSafeRead(guardAnd(known, inRange));
        }
      }
      if (isa<semantic::SVRangeSelectExpressionOp>(expression) &&
          values.size() == 3 &&
          !expression->hasAttr(randomEvaluationErrorAttrName)) {
        auto sourceWidth = sim::getPackedWidth(values[0].value.getType());
        auto selectedWidth = sim::getPackedWidth(*resultType);
        if (sourceWidth && selectedWidth) {
          if (*selectedWidth > *sourceWidth) {
            requireSafeRead(guardConstant(false));
          } else {
            // The ordinary selection lowering already normalizes declared
            // coordinates, directions, element widths and indexed bases in
            // a widened arithmetic type. Reuse that exact address for bounds
            // validation, retaining X/Z from the original index operands.
            SmallVector<std::pair<Operation *, Value>> previous;
            for (unsigned index = 1; index < inputs.size(); ++index) {
              previous.push_back(
                  {inputs[index], expressionCaptures.lookup(inputs[index])});
              expressionCaptures[inputs[index]] = values[index].value;
            }
            auto restore = llvm::scope_exit([&] {
              for (auto [input, value] : llvm::reverse(previous)) {
                if (value)
                  expressionCaptures[input] = value;
                else
                  expressionCaptures.erase(input);
              }
            });
            auto address = lowerPackedSelectionAddress(
                expression, values[0].value.getType(), *resultType);
            if (failed(address))
              return failure();
            if (!address->constant) {
              Value low = address->dynamicLow;
              Value known = guardConstant(true);
              if (auto logic = dyn_cast<sim::LogicType>(low.getType())) {
                Value bits = sim::SimLogicToBitsOp::create(
                    builder, location, builder.getIntegerType(logic.getWidth()),
                    low);
                Value restored = sim::SimLogicFromBitsOp::create(
                    builder, location, logic, bits);
                known = sim::SimLogicCompareOp::create(
                    builder, location, builder.getI1Type(),
                    sim::CompareKind::CaseEq, low, restored);
                low = bits;
              }
              Value inRange = guardAnd(
                  arith::CmpIOp::create(builder, location,
                                        arith::CmpIPredicate::sge, low,
                                        constantLike(low, 0)),
                  arith::CmpIOp::create(
                      builder, location, arith::CmpIPredicate::sle, low,
                      constantLike(low, *sourceWidth - *selectedWidth)));
              requireSafeRead(guardAnd(known, inRange));
            }
          }
        }
      }
      if (binary && values.size() == 2 && !isa<FloatType>(*resultType) &&
          (binary.getOperatorKind() == semantic::SVBinaryOperator::Divide ||
           binary.getOperatorKind() == semantic::SVBinaryOperator::Mod)) {
        auto right = convert(values[1].value, *resultType,
                             isSignedNode(inputs[1]), location);
        auto nonzero = succeeded(right) ? truthValue(*right, location)
                                        : FailureOr<Value>(failure());
        if (failed(nonzero))
          return failure();
        valid = guardAnd(valid, guardOr(values[1].random, *nonzero));
      }
      if (binary && values.size() == 2 &&
          binary.getOperatorKind() == semantic::SVBinaryOperator::Power &&
          isSignedNode(inputs[1])) {
        auto base = convert(values[0].value, *resultType,
                            isSignedNode(inputs[0]), location);
        auto baseNonzero = succeeded(base) ? truthValue(*base, location)
                                           : FailureOr<Value>(failure());
        auto exponent = toPackedScalar(values[1].value, location);
        if (failed(baseNonzero) || failed(exponent))
          return failure();
        auto exponentWidth = sim::getPackedWidth(exponent->getType());
        auto signedType = builder.getIntegerType(*exponentWidth);
        auto signedExponent = convert(*exponent, signedType, false, location);
        if (failed(signedExponent))
          return failure();
        Value negative = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::slt, *signedExponent,
            arith::ConstantOp::create(builder, location, signedType,
                                      builder.getIntegerAttr(signedType, 0)));
        valid =
            guardAnd(valid, guardOr(guardOr(values[0].random, values[1].random),
                                    guardOr(*baseNonzero, guardNot(negative))));
      }
      SmallVector<std::pair<Operation *, Value>> previous;
      auto captureOperands = graphNode ? getChildren(expression) : inputs;
      for (auto [input, value] : llvm::zip_equal(captureOperands, values)) {
        previous.push_back({input, expressionCaptures.lookup(input)});
        expressionCaptures[input] = value.value;
      }
      llvm::scope_exit restore([&] {
        for (auto [input, value] : llvm::reverse(previous)) {
          if (value)
            expressionCaptures[input] = value;
          else
            expressionCaptures.erase(input);
        }
      });
      if (matchPattern(errors, m_One())) {
        FailureOr<Value> lowered = lowerExpression(expression);
        if (failed(lowered))
          return failure();
        result = *lowered;
      } else {
        Block *read = addBlock();
        Block *invalid = addBlock();
        Block *done = addBlock();
        result = done->addArgument(*resultType, location);
        cf::CondBranchOp::create(builder, location, errors, read, ValueRange{},
                                 invalid, ValueRange{});
        setCurrent(invalid);
        Value dummy = createDefaultValue(builder, location, *resultType);
        cf::BranchOp::create(builder, location, done, ValueRange{dummy});
        setCurrent(read);
        FailureOr<Value> lowered = lowerExpression(expression);
        if (failed(lowered))
          return failure();
        cf::BranchOp::create(builder, location, done, ValueRange{*lowered});
        setCurrent(done);
      }
    }
    // IEEE conversions consume integral X/Z bits as zero. Retain structural
    // evaluation errors, such as an invalid address, across that boundary.
    if (values.size() == 1) {
      auto inputType = getNormalizedSemanticType(inputs.front());
      auto realCall = dyn_cast<semantic::SVCallExpressionOp>(expression);
      if (succeeded(inputType) && !isa<FloatType>(*inputType) &&
          ((isa<semantic::SVConversionExpressionOp>(expression) &&
            isa<FloatType>(*resultType)) ||
           (realCall && realCall.getIsSystemCall() &&
            isRealRandomSystemFunction(realCall.getCalleeName()))))
        valid = errors;
    }
    // An X/Z value is an evaluation error while forming constraints. An active
    // random input is a relation to be solved; its current stored X bits do not
    // participate in creation. Frozen random fields follow state semantics.
    if (!expression->hasAttr(randomArithmeticNodeAttr))
      if (auto logic = dyn_cast<sim::LogicType>(result.getType())) {
        Type bitsType = builder.getIntegerType(logic.getWidth());
        Value bits =
            sim::SimLogicToBitsOp::create(builder, location, bitsType, result);
        Value restored =
            sim::SimLogicFromBitsOp::create(builder, location, logic, bits);
        Value known = sim::SimLogicCompareOp::create(
            builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
            result, restored);
        valid = guardAnd(valid, guardOr(random, known));
      }
    if (!preserveArithmeticSource &&
        !isa<semantic::SVDistExpressionOp>(expression) &&
        arithmetic.needed(expression)) {
      auto planes = arithmetic.get(expression);
      if (failed(planes))
        return failure();
      auto value = safeConstraintValue(planes->value);
      if (failed(value))
        return failure();
      auto converted = convert(value->value, *resultType,
                               isSignedNode(expression), location);
      if (failed(converted))
        return failure();
      result = *converted;
      errors = guardAnd(errors, value->errors);
      if (value->dynamicInputs) {
        random = value->random;
        guardInputs = value->inputs;
        guardArguments = value->arguments;
        dynamicInputs = true;
      }
      bool coercion = isa<semantic::SVConversionExpressionOp>(expression) &&
                      isa<IntegerType>(sim::getPackedScalarType(*resultType));
      Operation *knownExpression = arithmeticValidity(expression);
      if (arithmeticFailure)
        return failure();
      if (coercion || !knownExpression)
        valid = errors;
      else {
        auto known = safeConstraintValue(knownExpression);
        auto predicate = succeeded(known) ? truthValue(known->value, location)
                                          : FailureOr<Value>(failure());
        if (failed(predicate))
          return failure();
        if (!candidate)
          valid = guardAnd(errors, *predicate);
        else if (value->dynamicInputs)
          valid = guardAnd(errors, guardOr(random, *predicate));
        else {
          Value stateKnown = guardAnd(
              known->valid, guardAnd(guardNot(known->random), *predicate));
          valid = guardAnd(errors, guardOr(valid, stateKnown));
        }
      }
    }
    SafeConstraintValue safe{result,         valid,  random,       guardInputs,
                             guardArguments, errors, dynamicInputs};
    if (stateKey && freezeSafeStateValues)
      stateValues[stateKey] = safe;
    if (graphNode)
      graphValues[expression] = safe;
    return safe;
  };
  constraintCreationValid = [&](Operation *constraint) -> FailureOr<Value> {
    SmallVector<Operation *> nested = getChildren(constraint);
    if (isa<semantic::SVSolveBeforeConstraintOp,
            semantic::SVDisableSoftConstraintOp,
            semantic::SVForeachConstraintOp>(constraint))
      return guardConstant(true);
    if (auto expression =
            dyn_cast<semantic::SVExpressionConstraintOp>(constraint)) {
      if (nested.size() != 1)
        return failure();
      if (isa<semantic::SVDistExpressionOp>(nested.front())) {
        Value valid = guardConstant(true);
        for (Operation *item : getChildren(nested.front())) {
          auto inputs = isa<semantic::SVValueRangeExpressionOp>(item)
                            ? getChildren(item)
                            : SmallVector<Operation *>{item};
          for (Operation *input : inputs) {
            FailureOr<SafeConstraintValue> safe = safeConstraintValue(input);
            if (failed(safe))
              return failure();
            valid = guardAnd(valid, safe->valid);
          }
        }
        return valid;
      }
      FailureOr<SafeConstraintValue> safe = safeConstraintValue(nested.front());
      return succeeded(safe) ? FailureOr<Value>(safe->valid)
                             : FailureOr<Value>(failure());
    }
    if (isa<semantic::SVImplicationConstraintOp,
            semantic::SVConditionalConstraintOp>(constraint)) {
      bool implication = isa<semantic::SVImplicationConstraintOp>(constraint);
      auto conditional =
          dyn_cast<semantic::SVConditionalConstraintOp>(constraint);
      if (nested.size() != (implication || !conditional.getHasElse() ? 2u : 3u))
        return failure();
      FailureOr<SafeConstraintValue> guard =
          safeConstraintValue(nested.front());
      FailureOr<Value> body = constraintCreationValid(nested[1]);
      if (failed(guard) || failed(body))
        return failure();
      FailureOr<Value> falseGuard = stateGuard(*guard, false);
      if (failed(falseGuard))
        return failure();
      Value valid = guardAnd(guard->valid, guardOr(*falseGuard, *body));
      if (!implication && conditional.getHasElse()) {
        FailureOr<Value> other = constraintCreationValid(nested[2]);
        FailureOr<Value> trueGuard = stateGuard(*guard, true);
        if (failed(other) || failed(trueGuard))
          return failure();
        valid = guardAnd(valid, guardOr(*trueGuard, *other));
      }
      return valid;
    }
    Value valid = guardConstant(true);
    if (isa<semantic::SVUniquenessConstraintOp>(constraint)) {
      for (Operation *input : nested) {
        FailureOr<SafeConstraintValue> safe = safeConstraintValue(input);
        if (failed(safe))
          return failure();
        valid = guardAnd(valid, safe->valid);
      }
    } else {
      for (Operation *item : nested) {
        FailureOr<Value> next = constraintCreationValid(item);
        if (failed(next))
          return failure();
        valid = guardAnd(valid, *next);
      }
    }
    return valid;
  };
  struct CreationCheck {
    Value valid;
    uint32_t block;
  };
  SmallVector<CreationCheck> creationChecks;
  bool hasDynamicGuardInputs = false;
  auto recordPriorityInputs = [&](APInt inputs, APInt arguments,
                                  Operation *source =
                                      nullptr) -> LogicalResult {
    if (hasFunctionPriority) {
      uint32_t maskCapture = 0;
      if (source) {
        Value mask = constantAssignment(sourceReferences(source));
        Value argumentMask =
            constantAssignment(sourceFunctionArguments(source));
        Value enabled = guardConstant(true);
        for (Operation *child = source, *parent = child->getParentOp();
             parent && parent != op->getParentOp();
             child = parent, parent = parent->getParentOp()) {
          bool implication = isa<semantic::SVImplicationConstraintOp>(parent);
          if (!implication && !isa<semantic::SVConditionalConstraintOp>(parent))
            continue;
          SmallVector<Operation *> parts = getChildren(parent);
          FailureOr<SafeConstraintValue> guard =
              safeConstraintValue(parts.front());
          if (failed(guard))
            return failure();
          bool positive = implication || child == parts[1];
          FailureOr<Value> eliminated = stateGuard(*guard, !positive);
          if (failed(eliminated))
            return failure();
          enabled =
              guardAnd(enabled, guardAnd(guard->valid, guardNot(*eliminated)));
          mask = unionGuardInputs(mask, guard->inputs);
          argumentMask = unionGuardInputs(argumentMask, guard->arguments);
        }
        mask = unionGuardInputs(
            mask, constantAssignment(inputs & ~priorityInputs(source)));
        mask = selectGuardInputs(enabled, mask);
        argumentMask = unionGuardInputs(
            argumentMask,
            constantAssignment(arguments & ~priorityArguments(source)));
        argumentMask = selectGuardInputs(enabled, argumentMask);
        APInt constant, constantArguments;
        if (!matchPattern(mask, m_ConstantInt(&constant)) ||
            constant.zextOrTrunc(assignmentWidth) != inputs ||
            !matchPattern(argumentMask, m_ConstantInt(&constantArguments)) ||
            constantArguments.zextOrTrunc(assignmentWidth) != arguments) {
          maskCapture = static_cast<uint32_t>(programCaptures.size() + 1);
          programCaptures.push_back(mask);
          programCaptures.push_back(argumentMask);
          hasDynamicGuardInputs = true;
        }
      }
      priorityInputRecords.push_back(
          {static_cast<uint32_t>(programInstructions.size() - 1),
           std::move(inputs), std::move(arguments), maskCapture});
    }
    return success();
  };
  llvm::DenseMap<Operation *, SmallVector<const SoftDisable *>> softDisablers;
  for (const SoftDisable &disable : softDisables) {
    SmallVector<Operation *> targets = getChildren(disable.source);
    if (targets.size() != 1) {
      emitError(getSemanticLocation(disable.source))
          << "disable soft requires one variable target";
      return failure();
    }
    SmallVector<SoftReference> targetReferences =
        collectSoftReferences(targets.front());
    if (targetReferences.size() != 1) {
      emitError(getSemanticLocation(disable.source))
          << "disable soft requires a direct variable or fixed packed path";
      return failure();
    }
    const SoftReference &target = targetReferences.front();
    for (auto [soft, order] : softDeclarationOrder) {
      if (order >= disable.declarationOrder)
        continue;
      SmallVector<SoftReference> references = collectSoftReferences(soft);
      if (llvm::any_of(references, [&](const SoftReference &reference) {
            if (target.symbol || reference.symbol)
              return target.symbol && target.symbol == reference.symbol;
            return target.offset < reference.offset + reference.width &&
                   reference.offset < target.offset + target.width;
          }))
        softDisablers[soft].push_back(&disable);
    }
  }

  auto emitSoftDisablers = [&](Operation *soft) -> LogicalResult {
    for (const SoftDisable *disable : softDisablers.lookup(soft)) {
      if (disable->block == OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1)
        emitLiteral(true);
      else
        instruction(OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1, 1, false,
                    disable->block);
      for (auto [guard, positive] : disable->guards) {
        if (failed(emitProgramExpression(guard)))
          return failure();
        if (!positive)
          instruction(OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1);
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
      }
      instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
    }
    return success();
  };
  if (constraintTemplate) {
    for (auto [index, root] : llvm::enumerate(children)) {
      if (!isReceiverChild(index) && isa<semantic::SVConstraintListOp>(root)) {
        emitError(getSemanticLocation(root))
            << "randomize call mixes a reusable template with cloned "
               "constraints";
        return failure();
      }
    }
    for (Operation &templateOperation : constraintTemplate.getBody().front()) {
      if (auto hard =
              dyn_cast<sim::SimRandomHardConstraintOp>(templateOperation)) {
        if (failed(emitTemplateValue(hard.getPredicate())))
          return failure();
        instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false, hard.getBlock());
        emittedHard = true;
      } else if (auto soft = dyn_cast<sim::SimRandomSoftConstraintOp>(
                     templateOperation)) {
        if (failed(emitTemplateValue(soft.getPredicate())))
          return failure();
        instruction(OBELISK_RT_RANDOM_END_SOFT_V1, 1, false, soft.getBlock(),
                    soft.getPriority());
        emittedSoft = true;
      }
    }
  }
  for (auto [index, root] : llvm::enumerate(children)) {
    if (isReceiverChild(index))
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
      FailureOr<Value> valid = constraintCreationValid(item);
      if (failed(valid))
        return failure();
      if (!matchPattern(*valid, m_One())) {
        creationChecks.push_back({*valid, constraintBlock});
        uint32_t capture = static_cast<uint32_t>(programCaptures.size());
        programCaptures.push_back(
            arith::ExtUIOp::create(builder, location, i64, *valid));
        instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 1, false, capture);
        instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false, constraintBlock);
        if (failed(recordPriorityInputs(APInt::getZero(assignmentWidth),
                                        APInt::getZero(assignmentWidth))))
          return failure();
        emittedHard = true;
      }
      bool hasHard = false;
      SmallVector<Operation *> hardConstraints;
      SmallVector<semantic::SVExpressionConstraintOp> softConstraints;
      item->walk([&](Operation *nested) {
        if (auto expression =
                dyn_cast<semantic::SVExpressionConstraintOp>(nested)) {
          if (expression.getIsSoft()) {
            softConstraints.push_back(expression);
            auto operands = getChildren(nested);
            if (operands.size() == 1 && arithmeticValidity(operands.front())) {
              hasHard = true;
              hardConstraints.push_back(nested);
            }
          } else {
            hasHard = true;
            hardConstraints.push_back(nested);
          }
        } else if (isa<semantic::SVUniquenessConstraintOp>(nested)) {
          hasHard = true;
          hardConstraints.push_back(nested);
        } else if (isa<semantic::SVImplicationConstraintOp,
                       semantic::SVConditionalConstraintOp>(nested)) {
          auto operands = getChildren(nested);
          if (!operands.empty() && arithmeticValidity(operands.front())) {
            hasHard = true;
            hardConstraints.push_back(nested);
          }
        }
      });
      if (hasHard) {
        // Preserve each atomic source constraint inside a conditional set.
        // Its inherited guards remain in the emitted predicate and input mask.
        if (!hasFunctionPriority)
          hardConstraints = {item};
        for (Operation *hard : hardConstraints) {
          priorityHardTarget = hasFunctionPriority ? hard : nullptr;
          activeProgramConstraintBlock = constraintBlock;
          if (failed(emitProgramConstraint(item, /*softTarget=*/nullptr)))
            return failure();
          instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false, constraintBlock);
          if (failed(recordPriorityInputs(priorityInputs(hard),
                                          priorityArguments(hard), hard)))
            return failure();
        }
        priorityHardTarget = nullptr;
        emittedHard = true;
      }
      for (semantic::SVExpressionConstraintOp soft : softConstraints) {
        bool containsDist = false;
        soft->walk([&](semantic::SVDistExpressionOp) { containsDist = true; });
        if (containsDist && !rawDistWeights) {
          emitError(getSemanticLocation(soft))
              << "soft dist constraints are not executable yet";
          return failure();
        }
        activeProgramConstraintBlock = constraintBlock;
        activeSoftDistSource = soft;
        if (failed(emitProgramConstraint(item, soft)) ||
            failed(emitSoftDisablers(soft)))
          return failure();
        instruction(OBELISK_RT_RANDOM_END_SOFT_V1, 1, false, constraintBlock,
                    softPriorities.lookup(soft));
        APInt inputs = priorityInputs(soft);
        APInt arguments = priorityArguments(soft);
        for (const SoftDisable *disable : softDisablers.lookup(soft)) {
          inputs |= priorityInputs(disable->source);
          arguments |= priorityArguments(disable->source);
        }
        if (failed(recordPriorityInputs(std::move(inputs), std::move(arguments),
                                        soft)))
          return failure();
        emittedSoft = true;
        activeSoftDistSource = nullptr;
      }
    }
  }
  for (auto [index, property] : llvm::enumerate(planned)) {
    if (!isa<FloatType>(property.type))
      continue;
    instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, property.width, false,
                propertyOffsets[index]);
    instruction(OBELISK_RT_RANDOM_REAL_RANK_VALID_V1, 1);
    instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1);
    if (failed(recordPriorityInputs(propertyMasks[index],
                                    APInt::getZero(assignmentWidth))))
      return failure();
    emittedHard = true;
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
    if (failed(recordPriorityInputs(APInt::getZero(assignmentWidth),
                                    APInt::getZero(assignmentWidth))))
      return failure();
  }
  if (!rawDistWeights && !distPlans.empty() && hasSolveBefore) {
    emitError(location)
        << "dist combined with solve before is not executable yet";
    return failure();
  }
  for (auto [index, plan] : llvm::enumerate(distPlans))
    for (const DistPlan &other : ArrayRef(distPlans).drop_front(index + 1))
      if (!rawDistWeights && plan.propertyIndex == other.propertyIndex) {
        emitError(getSemanticLocation(other.source))
            << "multiple active dist constraints for one property are not "
               "executable yet";
        return failure();
      }
  if (rawDistWeights && !distPlans.empty() && hasFunctionPriority) {
    emitError(location)
        << "dist across independent constraint function priority stages is "
           "not executable yet";
    return failure();
  }
  if (rawDistWeights) {
    for (auto [group, plan] : llvm::enumerate(distPlans)) {
      const APInt &target = propertyMasks[plan.propertyIndex];
      // Removing a mode-controlled edge must not split the distributed field
      // into independently normalized pieces.
      for (const SolveBeforeEdge &edge : solveBeforeEdges)
        for (const APInt *endpoint : {&edge.beforeMask, &edge.afterMask}) {
          APInt overlap = *endpoint & target;
          if (!overlap.isZero() && overlap != target) {
            emitError(getSemanticLocation(plan.source))
                << "dist target split by selected solve before paths requires "
                   "a conditional weight measure across those paths";
            return failure();
          }
        }
      APInt predecessors = target;
      for (;;) {
        APInt previous = predecessors;
        for (const SolveBeforeEdge &edge : solveBeforeEdges)
          if ((edge.constraintBlock ==
                   OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
               edge.constraintBlock == plan.constraintBlock) &&
              !(edge.afterMask & predecessors).isZero())
            predecessors |= edge.beforeMask;
        if (predecessors == previous)
          break;
      }
      predecessors &= ~target;
      for (const DistRangePlan &range : plan.ranges)
        if (range.weightCapture < expressionWeightSources.size() &&
            expressionWeightSources[range.weightCapture] &&
            !(priorityInputs(expressionWeightSources[range.weightCapture]) &
              ~(predecessors | target))
                 .isZero()) {
          emitError(
              getSemanticLocation(expressionWeightSources[range.weightCapture]))
              << "candidate dist weight inputs must be the distributed target "
                 "or solved before that complete target";
          return failure();
        }
      auto emitGuard = [&](Operation *guard, bool positive) -> LogicalResult {
        if (!(priorityInputs(guard) & ~predecessors).isZero()) {
          emitError(getSemanticLocation(guard))
              << "candidate-dependent dist activation requires its inputs to "
                 "be solved before the complete distributed target";
          return failure();
        }
        if (failed(emitProgramExpression(guard)))
          return failure();
        if (!positive)
          instruction(OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1);
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        return success();
      };
      emitLiteral(true);
      for (Operation *child = plan.source, *parent = child->getParentOp();
           parent; child = parent, parent = parent->getParentOp()) {
        if (!isa<semantic::SVImplicationConstraintOp,
                 semantic::SVConditionalConstraintOp>(parent))
          continue;
        SmallVector<Operation *> branches = getChildren(parent);
        if (isa<semantic::SVImplicationConstraintOp>(parent)) {
          if (failed(emitGuard(branches.front(), true)))
            return failure();
        } else if (isa<semantic::SVConditionalConstraintOp>(parent)) {
          if (failed(emitGuard(branches.front(), child == branches[1])))
            return failure();
        }
      }
      if (plan.softSource)
        for (const SoftDisable *disable :
             softDisablers.lookup(plan.softSource)) {
          if (disable->block == OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1)
            emitLiteral(true);
          else
            instruction(OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1, 1, false,
                        disable->block);
          for (auto [guard, positive] : disable->guards)
            if (failed(emitGuard(guard, positive)))
              return failure();
          instruction(OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1);
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        }
      instruction(OBELISK_RT_RANDOM_END_DIST_GUARD_V1, 1, false, group);
    }
    for (auto [capture, source] : llvm::enumerate(expressionWeightSources)) {
      if (!source)
        continue;
      Value value = programCaptures[capture];
      if (failed(emitWeightValue(capture, value)))
        return failure();
      unsigned width = cast<IntegerType>(value.getType()).getWidth();
      for (unsigned bit = 0; bit != width; ++bit)
        instruction(OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1, width, false,
                    capture, bit);
    }
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
  if (hasRealProgram)
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_REAL;
#ifdef OBELISK_ENABLE_Z3
  programFlags |= OBELISK_RT_RANDOM_PROGRAM_SMT_COMPLETION;
  if (distPlans.empty())
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM;
  else
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED;
#endif
  if (hasSolveBefore)
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE;
  if (hasFunctionPriority)
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY;
  if (hasDynamicGuardInputs)
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_GUARD_INPUT_MASKS;
  if (!distPlans.empty())
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_DIST;
  bool hasFiniteDomains = llvm::any_of(planned, [](const Property &property) {
    return !property.domains.empty();
  });
  if (hasFiniteDomains)
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS;
  bool wideProgram =
      useCyclicPlan || totalWidth > 64 ||
      llvm::any_of(programInstructions,
                   [](const EncodedInstruction &op) { return op.width > 64; });
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
  llvm::DenseMap<const DomainPattern *, std::pair<uint32_t, uint32_t>>
      domainLiteralIndices;
  if (wideProgram) {
    for (const EncodedInstruction &encoded : programInstructions) {
      uint32_t auxiliary = 0;
      if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
        APInt literal =
            encoded.literal.value_or(APInt(encoded.width, encoded.immediate));
        uint64_t words = literal.getNumWords();
        if (literalWords.size() > UINT32_MAX ||
            words > UINT32_MAX - literalWords.size()) {
          emitError(location) << "wide random literal pool exceeds 32 bits";
          return failure();
        }
        auxiliary = static_cast<uint32_t>(literalWords.size());
        literalWords.append(literal.getRawData(), literal.getRawData() + words);
      } else if (encoded.opcode == OBELISK_RT_RANDOM_END_SOFT_V1 ||
                 encoded.opcode == OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1) {
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
    for (const Property &property : planned)
      for (const PropertyDomain &domain : property.domains) {
        if (domain.width <= 64)
          continue;
        for (const DomainPattern &pattern : domain.patterns) {
          uint64_t words = pattern.mask.getNumWords();
          if (literalWords.size() > UINT32_MAX ||
              words * 2 > UINT32_MAX - literalWords.size()) {
            emitError(location) << "wide random literal pool exceeds 32 bits";
            return failure();
          }
          uint32_t maskIndex = static_cast<uint32_t>(literalWords.size());
          literalWords.append(pattern.mask.getRawData(),
                              pattern.mask.getRawData() + words);
          uint32_t valueIndex = static_cast<uint32_t>(literalWords.size());
          literalWords.append(pattern.value.getRawData(),
                              pattern.value.getRawData() + words);
          domainLiteralIndices[&pattern] = {maskIndex, valueIndex};
        }
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
      for (const APInt *bits : {&edge.beforeMask, &edge.afterMask}) {
        if (wideProgram) {
          for (unsigned word = 0; word != bits->getNumWords(); ++word)
            append64(bits->getRawData()[word]);
        } else {
          append64(bits->getZExtValue());
        }
      }
      append32(edge.constraintBlock);
      append32(edge.functionPriority
                   ? OBELISK_RT_RANDOM_SOLVE_EDGE_FUNCTION_PRIORITY
                   : 0u);
    }
  }
  if (hasFunctionPriority) {
    append32(static_cast<uint32_t>(priorityInputRecords.size()));
    for (const auto &record : priorityInputRecords) {
      append32(record.index);
      append32(record.maskCapture);
      for (const APInt *inputs : {&record.references, &record.arguments})
        for (unsigned word = 0; word != inputs->getNumWords(); ++word)
          append64(inputs->getRawData()[word]);
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
        append16(rawDistWeights && plan.softSource
                     ? softPriorities.lookup(plan.softSource) + 1
                     : 0);
        append64(range.lower);
        append64(range.cardinality);
        append64(rawDistWeights ? range.itemIdentity : range.coefficient);
        append32(range.weightCapture);
        uint32_t flags =
            range.weightSigned ? OBELISK_RT_RANDOM_DIST_WEIGHT_SIGNED : 0;
        if (rawDistWeights) {
          flags |= OBELISK_RT_RANDOM_DIST_RAW_WEIGHT;
          if (range.perRange)
            flags |= OBELISK_RT_RANDOM_DIST_PER_RANGE;
          flags |= range.intervalFlags;
          if (range.weightCapture < expressionWeightSources.size() &&
              expressionWeightSources[range.weightCapture])
            flags |= OBELISK_RT_RANDOM_DIST_EXPRESSION_WEIGHT;
        }
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
          append32(domain.width);
          if (domain.width > 64) {
            append32(OBELISK_RT_RANDOM_DOMAIN_LITERAL_WORDS);
            auto [maskIndex, valueIndex] =
                domainLiteralIndices.lookup(&pattern);
            append64(maskIndex);
            append64(valueIndex);
          } else {
            append32(0);
            append64(pattern.mask.getZExtValue());
            append64(pattern.value.getZExtValue());
          }
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
  bool useForeachRankSampler =
      hasRuntimeForeachConstraint && completeFallbackDomain && !hasRandC &&
      !hasSoftConstraint && !hasRealProgram && !hasFunctionPriority &&
      !hasSolveBefore && distPlans.empty() && plannedContainers.empty();
  if (hasRuntimeForeachConstraint && hasFiniteDomains &&
      !useForeachRankSampler) {
    emitError(location)
        << "foreach constraints over finite semantic random domains require "
           "complete integral rank traversal without cyclic, soft, ordered "
           "or randomized-container stages";
    return failure();
  }
  if (!rawDistWeights && hasFiniteDomains && !distPlans.empty() &&
      !completeFallbackDomain) {
    emitError(location)
        << "dist over a finite semantic domain currently requires exhaustive "
           "fallback traversal of at most 2^20 aggregate assignments";
    return failure();
  }
  solver::RandomProgramAnalysis analysis;
  if (!hasRealProgram)
    analysis = solver::analyzeRandomProgram(
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
      proposalAssignments.push_back(assignment.zextOrTrunc(assignmentWidth));

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
      SmallVector<std::pair<APInt, SmallVector<APInt>>> groups;
      for (const APInt &row : rows) {
        APInt key = row & solveBeforeLayerMasks[layer];
        auto group = llvm::find_if(
            groups, [&](const auto &entry) { return entry.first == key; });
        if (group == groups.end()) {
          groups.emplace_back(key, SmallVector<APInt>{});
          group = std::prev(groups.end());
        }
        group->second.push_back(row);
      }
      llvm::sort(groups, [](const auto &lhs, const auto &rhs) {
        return lhs.first.ult(rhs.first);
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
      normalizedAssignments.push_back(assignment.zextOrTrunc(assignmentWidth));
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
    APInt propertyMask = APInt::getLowBitsSet(assignmentWidth, property.width)
                             .shl(propertyOffset);
    if (!property.domains.empty()) {
      if (!property.isRandC && property.width <= 64 && !validAssignmentTable &&
          (proposalAssignmentTableMask & propertyMask) != propertyMask)
        proposalFiniteDomains.push_back(
            {propertyOffset, &property, propertyDomainCardinality(property)});
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
        proposalDomains.push_back(
            {propertyOffset, property.width, lower, cardinality,
             (cardinality & (cardinality - 1)) == 0, found->isSigned});
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
    // This proposal optimization uses scalar intervals. Wider captures remain
    // complete Boolean-plan parameters; narrowing one here would alias indices
    // and incorrectly constrain otherwise legal state-valued relations.
    if (cast<IntegerType>(programCaptures[bound.captureIndex].getType())
            .getWidth() > 64)
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
  llvm::DenseMap<uint32_t, unsigned> aliasTargetCounts;
  for (const solver::RandomVariableAlias &alias : analysis.aliases)
    ++aliasTargetCounts[alias.targetOffset];
  for (const solver::RandomVariableAlias &alias : analysis.aliases) {
    if (!distPlans.empty())
      break;
    bool sourceIsTarget = aliasTargetCounts.contains(alias.sourceOffset);
    unsigned targetCount = aliasTargetCounts.lookup(alias.targetOffset);
    if (alias.targetOffset != alias.sourceOffset &&
        isPropertyField(alias.targetOffset, alias.width) &&
        isPropertyField(alias.sourceOffset, alias.width) && !sourceIsTarget &&
        targetCount == 1)
      proposalAliases.push_back(
          {alias.targetOffset, alias.sourceOffset, alias.width});
  }
  if (hasSolveBefore && !proposalAliases.empty()) {
    auto solveLayerForField = [&](uint32_t fieldOffset, unsigned fieldWidth) {
      APInt fieldMask =
          APInt::getLowBitsSet(assignmentWidth, fieldWidth).shl(fieldOffset);
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
    return (opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
            opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1) ||
           opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1 ||
           opcode == OBELISK_RT_RANDOM_CLOG2_V1;
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
      // The scalar proposal evaluator uses 64-bit intermediate values. A
      // small random assignment can still depend on much wider state indices;
      // retain such definitions in the complete compiler-generated plan.
      if (assignmentWidth <= 64 && encoded.width > 64) {
        supported = false;
        break;
      }
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
        if (encoded.operand >= programCaptures.size() ||
            (assignmentWidth <= 64 &&
             cast<IntegerType>(programCaptures[encoded.operand].getType())
                     .getWidth() > 64)) {
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
      assignment = arith::SelectOp::create(builder, location, selected,
                                           constantAssignment(tableAssignment),
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
              : arith::RemUIOp::create(
                    builder, location, rawAssignment,
                    constantAssignment64(proposalAssignments.size()))
                    .getResult();
      return selectAssignmentTable(proposalAssignments, index);
    }
    Value assignment = rawAssignment;
    if (sampledComponentAssignment)
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(
              builder, location, assignment,
              constantAssignment(~proposalAssignmentTableMask)),
          sampledComponentAssignment);
    for (const ProposalDomain &domain : proposalDomains) {
      Value fieldBits;
      if (domain.powerOfTwo) {
        fieldBits = rawAssignment;
        if (domain.offset != 0)
          fieldBits =
              arith::ShRUIOp::create(builder, location, fieldBits,
                                     constantAssignment64(domain.offset));
        fieldBits =
            arith::AndIOp::create(builder, location, fieldBits,
                                  constantAssignment64(domain.cardinality - 1));
      } else {
        fieldBits = sampledDomainIndices[sampledDomainIndex++];
      }
      if (domain.lower != 0)
        fieldBits =
            arith::AddIOp::create(builder, location, fieldBits,
                                  constantLike(fieldBits, domain.lower));
      if (domain.isSigned)
        fieldBits = arith::XOrIOp::create(
            builder, location, fieldBits,
            constantLike(fieldBits, uint64_t{1} << (domain.width - 1)));
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
      APInt fieldMask =
          APInt::getLowBitsSet(assignmentWidth, bound.width).shl(bound.offset);
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
        return arith::AndIOp::create(builder, location, bits,
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
          bits =
              arith::ShLIOp::create(builder, location, bits, constant64(shift));
          bits = arith::ShRSIOp::create(builder, location, bits,
                                        constant64(shift));
        }
        return maskDefinitionValue(bits, width);
      }
      if (input.width == width)
        return input.bits;
      auto outputType = IntegerType::get(function.getContext(), width);
      if (input.width > width)
        return arith::TruncIOp::create(builder, location, outputType,
                                       input.bits)
            .getResult();
      if (signExtend)
        return arith::ExtSIOp::create(builder, location, outputType, input.bits)
            .getResult();
      return arith::ExtUIOp::create(builder, location, outputType, input.bits)
          .getResult();
    };
    auto definitionTruth = [&](Value bits) {
      return arith::CmpIOp::create(
                 builder, location, arith::CmpIPredicate::ne, bits,
                 assignmentWidth <= 64 ? constant64(0) : constantLike(bits, 0))
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
          APInt literal =
              encoded.literal.value_or(APInt(encoded.width, encoded.immediate));
          stack.push_back(
              {definitionConstant(encoded.width, literal), encoded.width});
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
            bits = arith::SubIOp::create(builder, location,
                                         constantLike(bits, 0), bits);
          else if (encoded.opcode == OBELISK_RT_RANDOM_BIT_NOT_V1)
            bits = arith::XOrIOp::create(
                builder, location, bits,
                definitionConstant(encoded.width,
                                   APInt::getAllOnes(encoded.width)));
          else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_AND_V1 ||
                   encoded.opcode == OBELISK_RT_RANDOM_REDUCE_NAND_V1) {
            Value allOnes =
                definitionConstant(input.width, APInt::getAllOnes(input.width));
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
                       : arith::ConstantOp::create(builder, location,
                                                   builder.getI1Type(),
                                                   builder.getBoolAttr(false))
                             .getResult();
            for (unsigned bit = 0; bit != input.width; ++bit) {
              Value current = input.bits;
              if (bit != 0)
                current = arith::ShRUIOp::create(builder, location, current,
                                                 constantLike(current, bit));
              current = arith::AndIOp::create(builder, location, current,
                                              constantLike(current, 1));
              // IEEE 1800-2017 11.4.9: each step applies the operator between
              // the one-bit result of the prior step and the next bit of the
              // operand. The accumulator is a value of this program, which
              // holds every value in an i64 below 65 bits, so the folded bit
              // joins it in that representation rather than as a bare i1.
              bits = arith::XOrIOp::create(
                  builder, location, bits,
                  definitionBooleanBits(definitionTruth(current)));
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
          } else if (encoded.opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1 ||
                     encoded.opcode == OBELISK_RT_RANDOM_CLOG2_V1) {
            // Use the same unsigned bit-vector meaning as the SMT and
            // residual interpreters. Arithmetic shifts are unnecessary here.
            bits = definitionConstant(encoded.width, APInt(encoded.width, 0));
            for (unsigned bit = 0; bit != input.width; ++bit) {
              Value current = input.bits;
              if (encoded.opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1) {
                if (bit != 0)
                  current = arith::ShRUIOp::create(builder, location, current,
                                                   constantLike(current, bit));
                current = arith::AndIOp::create(builder, location, current,
                                                constantLike(current, 1));
                bits = arith::AddIOp::create(
                    builder, location, bits,
                    resizeDefinitionValue({maskDefinitionValue(current, 1), 1},
                                          encoded.width, false));
              } else {
                Value threshold = definitionConstant(
                    input.width, APInt::getOneBitSet(input.width, bit));
                Value above = arith::CmpIOp::create(builder, location,
                                                    arith::CmpIPredicate::ugt,
                                                    current, threshold);
                bits = arith::SelectOp::create(
                    builder, location, above,
                    definitionConstant(encoded.width,
                                       APInt(encoded.width, bit + 1)),
                    bits);
              }
            }
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
          Value left =
              resizeDefinitionValue(lhs, operationWidth, arithmeticRight);
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
                                                   left, constantLike(left, 0));
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
          Value bits =
              definitionConstant(encoded.width, APInt(encoded.width, 1));
          for (unsigned bit = 0; bit != rhs.width; ++bit) {
            Value exponentBit = rhs.bits;
            if (bit != 0)
              exponentBit =
                  arith::ShRUIOp::create(builder, location, exponentBit,
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
                arith::CmpIOp::create(
                    builder, location, arith::CmpIPredicate::eq, left,
                    definitionConstant(operandWidth,
                                       APInt::getSignedMinValue(operandWidth))),
                arith::CmpIOp::create(
                    builder, location, arith::CmpIPredicate::eq, right,
                    definitionConstant(operandWidth,
                                       APInt::getAllOnes(operandWidth))));
            Value safeRight = arith::SelectOp::create(
                builder, location, overflow, constantLike(right, 1), right);
            if (encoded.opcode == OBELISK_RT_RANDOM_DIV_V1) {
              Value quotient =
                  arith::DivSIOp::create(builder, location, left, safeRight);
              bits = arith::SelectOp::create(builder, location, overflow, left,
                                             quotient);
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
          bits = definitionBooleanBits(
              arith::CmpIOp::create(builder, location, predicate, left, right));
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
            Value leftFalse = arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::eq, lhs.bits,
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
        fieldBits = arith::ShLIOp::create(
            builder, location, fieldBits,
            constantAssignment64(definition.targetOffset));
      APInt targetMask = APInt::getLowBitsSet(assignmentWidth, definition.width)
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
          sourceBits =
              arith::ShRUIOp::create(builder, location, sourceBits,
                                     constantAssignment64(alias.sourceOffset));
        sourceBits = maskDefinitionValue(sourceBits, alias.width);
        aliasSources.emplace_back(alias.sourceOffset, alias.width, sourceBits);
      }
      if (sourceBits.getType() != assignmentType)
        sourceBits = arith::ExtUIOp::create(builder, location, assignmentType,
                                            sourceBits);
      if (alias.targetOffset != 0)
        sourceBits =
            arith::ShLIOp::create(builder, location, sourceBits,
                                  constantAssignment64(alias.targetOffset));
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
                                constantAssignment(~sampledDistMask)),
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
  bool exactProposal = !hasRuntimeForeachConstraint && analysis.proposalExact &&
                       softProposalExact && !hasRandC && distPlans.empty() &&
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
  // Mode changes can reach residuals even when the default proposal is exact.
  // Wide ordered metadata requires compiler-generated sampling plans.
  bool unsupportedWideOrder = false;
#ifndef OBELISK_ENABLE_Z3
  unsupportedWideOrder = hasSolveBefore;
#endif
  if (wideProgram &&
      (unsupportedWideOrder || (!rawDistWeights && !distPlans.empty()))) {
    emitError(location)
        << "wide residual random fallback does not yet support solve before "
           "or dist";
    return failure();
  }

  auto structuralProposalFollowsSolveOrder = [&]() {
    auto propertyIndexForField =
        [&](uint32_t fieldOffset,
            unsigned fieldWidth) -> std::optional<unsigned> {
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
      APInt orderedBits = APInt::getZero(assignmentWidth);
      for (auto [layerIndex, layerMask] :
           llvm::enumerate(solveBeforeLayerMasks)) {
        APInt bits = propertyMask & layerMask;
        if (bits.isZero())
          continue;
        orderedBits |= bits;
        if (selectedLayer && *selectedLayer != layerIndex)
          return false;
        selectedLayer = layerIndex;
      }
      // Generated structural definitions and aliases operate on whole
      // properties. A path-level order that covers only part of a property
      // must stay in the bit-mask-aware table or residual runtime paths.
      if (!orderedBits.isZero() && orderedBits != propertyMask)
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

  bool weightedRequiresRuntime = rawDistWeights && !distPlans.empty();
  bool solveBeforeRequiresRuntime = hasRealProgram || weightedRequiresRuntime ||
                                    hasFunctionPriority ||
                                    (hasSolveBefore && !softProposalExact);
  if (hasSolveBefore && !solveBeforeTableRoot &&
      analysis.satisfiability != solver::Satisfiability::Unsatisfiable) {
    APInt orderedPropertyMask = APInt::getZero(assignmentWidth);
    for (const APInt &layerMask : solveBeforeLayerMasks)
      orderedPropertyMask |= layerMask;

    APInt uncoveredOrderedMask =
        orderedPropertyMask & ~proposalAssignmentTableMask;
    if (!uncoveredOrderedMask.isZero()) {
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

#ifndef OBELISK_ENABLE_Z3
  if (solveBeforeRequiresRuntime && !completeFallbackDomain) {
    emitError(location)
        << "solve before residual fallback requires exhaustive traversal of "
           "at most 2^20 semantic assignments; compile-time planning could "
           "not preserve the ordered distribution";
    return failure();
  }
#endif

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

  // obelisk-sim-compile-random-plans appends the compiled sampling plan to
  // this residual program. All cyclic stages specialize the same relation;
  // reuse its attribute rather than copying the program for each field.
  StringAttr samplerProgram;
  auto getSamplerProgram = [&]() {
    if (!samplerProgram)
      samplerProgram = builder.getStringAttr(StringRef(
          reinterpret_cast<const char *>(program.data()), program.size()));
    return samplerProgram;
  };
  SmallVector<Value> cyclicMasks;
  SmallVector<Value> cyclicKeys;
  SmallVector<Value> cyclicPositions;
  SmallVector<Value> cyclicDomains;
  Block *cyclicPlanEntry = useCyclicPlan ? addBlock() : nullptr;
  if (useCyclicPlan) {
    cyclicKeys.resize(cyclicFields.size());
    cyclicPositions.resize(cyclicFields.size());
    cyclicDomains.resize(cyclicFields.size());
    Value remainingMask = activeGuardBits;
    Type referenceType = sim::RefType::get(function.getContext(), i64);
    for (auto [index, field] : llvm::enumerate(cyclicFields)) {
      // Tentative histories live in automatic storage. A later cyclic field
      // or ordinary solve may still fail, so none is published before commit.
      cyclicKeys[index] = sim::SimRefAllocOp::create(builder, location,
                                                     referenceType, field.key);
      cyclicPositions[index] = sim::SimRefAllocOp::create(
          builder, location, referenceType, field.position);
      cyclicDomains[index] = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(),
                            sim::StringType::get(function.getContext())),
          field.domain);
      Value inverseMask =
          arith::XOrIOp::create(builder, location, field.mask, mask);
      // The current field and all later random fields are free at this stage.
      cyclicMasks.push_back(remainingMask);
      remainingMask =
          arith::AndIOp::create(builder, location, remainingMask, inverseMask);
    }
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
  Value randomDraw = (hasRealProgram || useCyclicPlan) ? currentAssignment
                                                       : nextAssignment(state);
  Value start = arith::OrIOp::create(
      builder, location,
      arith::AndIOp::create(builder, location, randomDraw, mutableMask),
      fixedAssignment);
  Value modeStart = start;
  Value modeState = state;
  Block *modeSamplingDispatchBlock = addBlock();
  Block *plannedSamplingBlock = addBlock();
  Value usePlannedSampling = allConstraintsEnabled;
  if (weightedRequiresRuntime || (hasFiniteDomains && !distPlans.empty())) {
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
  if (hasArrayElementModes)
    usePlannedSampling = arith::AndIOp::create(
        builder, location, usePlannedSampling, randomizationEnabled);
  if (useCyclicPlan) {
    cf::BranchOp::create(builder, location, cyclicPlanEntry);
  } else
    cf::CondBranchOp::create(builder, location, usePlannedSampling,
                             plannedSamplingBlock, ValueRange{},
                             modeSamplingDispatchBlock, ValueRange{});
  setCurrent(plannedSamplingBlock);

  auto sampleBoundedIndex = [&](uint64_t cardinality, Value draw,
                                Value &streamState) -> Value {
    // Assignment draws may span several words; table cardinalities and the
    // rejection sampler use one uniformly random 64-bit word.
    if (draw.getType() != i64)
      draw = arith::TruncIOp::create(builder, location, i64, draw);
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
    distWeightsValid =
        distWeightsValid
            ? arith::AndIOp::create(builder, location, distWeightsValid, valid)
                  .getResult()
            : valid;
  };
  for (const DistPlan &plan : distPlans) {
    if (weightedRequiresRuntime)
      break;
    MaterializedDistPlan materialized{&plan, {}, constant64(0)};
    for (const DistRangePlan &range : plan.ranges) {
      if (range.weightSigned)
        requireValidDistWeights(
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::sge,
                                  range.weight, constant64(0)));
      Value zero =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                range.weight, constant64(0));
      Value safeWeight = arith::SelectOp::create(builder, location, zero,
                                                 constant64(1), range.weight);
      Value scaled =
          arith::MulIOp::create(builder, location, range.weight,
                                constant64(range.selectionCoefficient));
      Value recovered =
          arith::DivUIOp::create(builder, location, scaled, safeWeight);
      Value productValid = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, recovered,
          constant64(range.selectionCoefficient));
      productValid =
          arith::OrIOp::create(builder, location, zero, productValid);
      requireValidDistWeights(productValid);
      Value updated = arith::AddIOp::create(builder, location,
                                            materialized.totalWeight, scaled);
      Value sumValid =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                                updated, materialized.totalWeight);
      requireValidDistWeights(sumValid);
      materialized.totalWeight = updated;
      materialized.weights.push_back(scaled);
    }
    requireValidDistWeights(
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
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
      Value choice = sampleDynamicBoundedIndex(materialized.totalWeight,
                                               next64(state), state);
      const DistRangePlan *first = &materialized.plan->ranges.front();
      Value selectedLower = constant64(first->lower);
      Value selectedCardinality = constant64(first->cardinality);
      Value cumulative = materialized.weights.front();
      for (size_t index = 1; index != materialized.plan->ranges.size();
           ++index) {
        const DistRangePlan &range = materialized.plan->ranges[index];
        Value selected = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::uge, choice, cumulative);
        selectedLower =
            arith::SelectOp::create(builder, location, selected,
                                    constant64(range.lower), selectedLower);
        selectedCardinality = arith::SelectOp::create(
            builder, location, selected, constant64(range.cardinality),
            selectedCardinality);
        cumulative = arith::AddIOp::create(builder, location, cumulative,
                                           materialized.weights[index]);
      }
      Value field = arith::AddIOp::create(
          builder, location, selectedLower,
          sampleDynamicBoundedIndex(selectedCardinality, next64(state), state));
      const Property &property = planned[materialized.plan->propertyIndex];
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
        field =
            arith::ExtUIOp::create(builder, location, assignmentType, field);
      if (materialized.plan->propertyOffset != 0) {
        field = arith::ShLIOp::create(
            builder, location, field,
            constantAssignment64(materialized.plan->propertyOffset));
      }
      APInt fieldMask = APInt::getLowBitsSet(assignmentWidth, property.width)
                            .shl(materialized.plan->propertyOffset);
      sampledDistAssignment =
          arith::OrIOp::create(builder, location, sampledDistAssignment, field);
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
        Value index = sampleBoundedIndex(node.assignments.size(), draw, state);
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
      sampledIndices.push_back(
          sampleDynamicBoundedIndex(bound.cardinality, draw, streamState));
    }
    return sampledIndices;
  };
  SmallVector<Value> sampledDomainIndices = sampleProposalDomainIndices(state);
  storeRandomState(state);

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
  Block *foreachRankEntry =
      useForeachRankSampler && !checkerOnly ? addBlock() : nullptr;
  Block *singleRandCLoop = nullptr;
  Block *singleRandCAdvance = nullptr;
  Block *singleRandCCommit = nullptr;
  if (!useCyclicPlan && !checkerOnly && planned.size() == 1 &&
      planned.front().isRandC) {
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
    storeRandomState(invalidCaptureBoundsState);
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  }
  if (invalidDistWeightsBlock) {
    setCurrent(invalidDistWeightsBlock);
    storeRandomState(invalidDistWeightsState);
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  }

  auto materializeCandidates =
      [&](Value assignment) -> FailureOr<SmallVector<Value>> {
    SmallVector<Value> candidates;
    unsigned offset = 0;
    for (const Property &property : planned) {
      Value bits = assignment;
      if (offset != 0)
        bits = arith::ShRUIOp::create(builder, location, bits,
                                      constantAssignment64(offset));
      Type integerType =
          IntegerType::get(function.getContext(), property.width);
      if (property.width != assignmentWidth)
        bits = arith::TruncIOp::create(builder, location, integerType, bits);
      FailureOr<Value> converted = [&]() -> FailureOr<Value> {
        if (isa<FloatType>(property.type)) {
          Value value = sim::SimRandomRealFromRankOp::create(
              builder, location, property.type, bits);
          return arith::SelectOp::create(builder, location,
                                         propertyEnabled[candidates.size()],
                                         value, property.currentValue)
              .getResult();
        }
        return isa<sim::UnpackedArrayType>(property.type)
                   ? convertFixedBitstreamTarget(bits, property.type, location)
                   : convert(bits, property.type, false, location,
                             property.isSigned);
      }();
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
  // The captures above dominate every candidate checker. State values first
  // encountered inside a runtime foreach must remain local to that checker.
  freezeSafeStateValues = false;
  auto materializeConstraintCheck =
      [&](Value assignment) -> FailureOr<ConstraintCheck> {
    FailureOr<SmallVector<Value>> candidates =
        materializeCandidates(assignment);
    if (failed(candidates))
      return failure();
    Value savedThis = thisObject;
    Value savedEnclosingThis = enclosingThisObject;
    SmallVector<Value> savedCandidates = std::move(randomizeCandidateValues);
    llvm::scope_exit restoreBindings([&] {
      thisObject = savedThis;
      enclosingThisObject = savedEnclosingThis;
      randomizeCandidateValues = std::move(savedCandidates);
    });
    if (!scopeRandomize) {
      enclosingThisObject = savedThis;
      thisObject = receiver;
    }
    randomizeCandidateValues = *candidates;

    Value softSatisfied;
    if (hasSoftConstraint)
      softSatisfied = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));

    auto requireArithmeticValidity = [&](Operation *expression,
                                         Value predicate) -> FailureOr<Value> {
      Operation *validity = arithmeticValidity(expression);
      if (!validity)
        return predicate;
      auto value = safeConstraintValue(validity);
      auto valid = succeeded(value) ? truthValue(value->value, location)
                                    : FailureOr<Value>(failure());
      if (failed(valid))
        return failure();
      return guardAnd(predicate, *valid);
    };

    std::function<FailureOr<Value>(Operation *, Operation *)> lowerConstraint =
        [&](Operation *constraint, Operation *softTarget) -> FailureOr<Value> {
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
      if (isa<semantic::SVSolveBeforeConstraintOp,
              semantic::SVDisableSoftConstraintOp>(constraint))
        return arith::ConstantOp::create(
                   builder, getSemanticLocation(constraint),
                   builder.getI1Type(), builder.getBoolAttr(true))
            .getResult();
      if (auto expression =
              dyn_cast<semantic::SVExpressionConstraintOp>(constraint)) {
        bool selected =
            softTarget ? softTarget == constraint : !expression.getIsSoft();
        Operation *arithmeticValid =
            nested.size() == 1 ? arithmeticValidity(nested.front()) : nullptr;
        bool safetyOnly =
            !softTarget && expression.getIsSoft() && arithmeticValid;
        if (!selected && !safetyOnly)
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
          FailureOr<Value> scalar =
              toPackedScalar(randomizeCandidateValues[plan->propertyIndex],
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
          Value supported =
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false));
          Value mentioned = supported;
          SmallVector<Value> intervalMatches;
          for (const DistRangePlan &range : plan->ranges) {
            Value matches;
            if (range.intervalFlags &
                OBELISK_RT_RANDOM_DIST_DEFAULT_COMPLEMENT) {
              intervalMatches.push_back({});
              continue;
            } else if (range.intervalFlags &
                       OBELISK_RT_RANDOM_DIST_CAPTURED_INTERVAL) {
              Value atOrAbove = arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::uge, field,
                  range.capturedLower);
              Value atOrBelow = arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::ule, field,
                  range.capturedUpper);
              matches = arith::AndIOp::create(builder, location, atOrAbove,
                                              atOrBelow);
            } else if (range.cardinality == 0) {
              matches = arith::ConstantOp::create(builder, location,
                                                  builder.getI1Type(),
                                                  builder.getBoolAttr(true));
            } else {
              Value atOrAbove = arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::uge, field,
                  constant64(range.lower));
              Value distance = arith::SubIOp::create(builder, location, field,
                                                     constant64(range.lower));
              Value belowEnd = arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::ult, distance,
                  constant64(range.cardinality));
              matches =
                  arith::AndIOp::create(builder, location, atOrAbove, belowEnd);
            }
            intervalMatches.push_back(matches);
            mentioned =
                arith::OrIOp::create(builder, location, mentioned, matches);
          }
          for (auto [index, range] : llvm::enumerate(plan->ranges)) {
            Value matches = intervalMatches[index];
            if (range.intervalFlags & OBELISK_RT_RANDOM_DIST_DEFAULT_COMPLEMENT)
              matches = arith::XOrIOp::create(
                  builder, location, mentioned,
                  arith::ConstantOp::create(builder, location,
                                            builder.getI1Type(),
                                            builder.getBoolAttr(true)));
            arith::CmpIPredicate positivePredicate =
                range.weightSigned ? arith::CmpIPredicate::sgt
                                   : arith::CmpIPredicate::ugt;
            Value weight = range.weight;
            Value weightValid;
            if (range.weightCapture < expressionWeightSources.size() &&
                expressionWeightSources[range.weightCapture]) {
              auto safe = safeConstraintValue(
                  expressionWeightSources[range.weightCapture]);
              auto scalar = succeeded(safe)
                                ? toPackedScalar(safe->value, location)
                                : FailureOr<Value>(failure());
              auto converted = succeeded(scalar)
                                   ? convert(*scalar, weight.getType(), false,
                                             location, false)
                                   : FailureOr<Value>(failure());
              if (failed(converted))
                return failure();
              weight = *converted;
              weightValid = safe->valid;
              if (Operation *valid = arithmeticValidity(
                      expressionWeightSources[range.weightCapture])) {
                auto known = safeConstraintValue(valid);
                auto predicate = succeeded(known)
                                     ? truthValue(known->value, location)
                                     : FailureOr<Value>(failure());
                if (failed(predicate))
                  return failure();
                weightValid = guardAnd(weightValid, *predicate);
              }
              if (expressionWeightScales[range.weightCapture]) {
                auto scale =
                    convert(programCaptures
                                [*expressionWeightScales[range.weightCapture]],
                            weight.getType(), false, location, false);
                if (failed(scale))
                  return failure();
                weight =
                    arith::MulIOp::create(builder, location, weight, *scale);
              }
            }
            Value positive = arith::CmpIOp::create(
                builder, location, positivePredicate, weight,
                arith::ConstantOp::create(
                    builder, location, range.weight.getType(),
                    builder.getIntegerAttr(range.weight.getType(), 0)));
            if (weightValid)
              positive = guardAnd(positive, weightValid);
            Value active =
                arith::AndIOp::create(builder, location, matches, positive);
            supported =
                arith::OrIOp::create(builder, location, supported, active);
          }
          return supported;
        }
        FailureOr<SafeConstraintValue> value =
            safeConstraintValue(nested.front());
        if (failed(value))
          return failure();
        FailureOr<Value> predicate =
            truthValue(value->value, getSemanticLocation(constraint));
        if (failed(predicate))
          return failure();
        Value result = *predicate;
        if (arithmeticValid) {
          auto safe = safeConstraintValue(arithmeticValid);
          auto valid =
              succeeded(safe)
                  ? truthValue(safe->value, getSemanticLocation(constraint))
                  : FailureOr<Value>(failure());
          if (failed(valid))
            return failure();
          result = safetyOnly ? *valid : guardAnd(result, *valid);
        }
        if (softTarget)
          for (const SoftDisable *disable : softDisablers.lookup(softTarget)) {
            Value enabled = arith::ConstantOp::create(
                builder, location, builder.getI1Type(),
                builder.getBoolAttr(true));
            if (disable->block != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1) {
              Value selected = arith::AndIOp::create(
                  builder, location, relevantConstraintMode,
                  constant64(uint64_t{1} << disable->block));
              enabled = arith::CmpIOp::create(builder, location,
                                              arith::CmpIPredicate::eq,
                                              selected, constant64(0));
            }
            for (auto [guard, positive] : disable->guards) {
              FailureOr<SafeConstraintValue> value = safeConstraintValue(guard);
              if (failed(value))
                return failure();
              FailureOr<Value> condition =
                  truthValue(value->value, getSemanticLocation(guard));
              if (failed(condition))
                return failure();
              Value guardValue = *condition;
              if (!positive) {
                Value falseValue = arith::ConstantOp::create(
                    builder, location, builder.getI1Type(),
                    builder.getBoolAttr(false));
                guardValue = arith::CmpIOp::create(builder, location,
                                                   arith::CmpIPredicate::eq,
                                                   guardValue, falseValue);
              }
              enabled =
                  arith::AndIOp::create(builder, location, enabled, guardValue);
            }
            result = arith::OrIOp::create(builder, location, result, enabled);
          }
        return result;
      }
      if (isa<semantic::SVImplicationConstraintOp>(constraint)) {
        if (nested.size() != 2) {
          emitError(getSemanticLocation(constraint))
              << "implication constraint does not contain a predicate and body";
          return failure();
        }
        FailureOr<SafeConstraintValue> predicateValue =
            safeConstraintValue(nested.front());
        FailureOr<Value> body = lowerConstraint(nested.back(), softTarget);
        if (failed(predicateValue) || failed(body))
          return failure();
        FailureOr<Value> predicate = truthValue(
            predicateValue->value, getSemanticLocation(nested.front()));
        if (failed(predicate))
          return failure();
        Value falseValue = arith::ConstantOp::create(
            builder, getSemanticLocation(constraint), builder.getI1Type(),
            builder.getBoolAttr(false));
        Value notPredicate = arith::CmpIOp::create(
            builder, getSemanticLocation(constraint), arith::CmpIPredicate::eq,
            *predicate, falseValue);
        Value result = arith::OrIOp::create(
            builder, getSemanticLocation(constraint), notPredicate, *body);
        return softTarget ? FailureOr<Value>(result)
                          : requireArithmeticValidity(nested.front(), result);
      }
      if (auto conditional =
              dyn_cast<semantic::SVConditionalConstraintOp>(constraint)) {
        size_t expected = conditional.getHasElse() ? 3 : 2;
        if (nested.size() != expected) {
          emitError(getSemanticLocation(constraint))
              << "conditional constraint has inconsistent branch inventory";
          return failure();
        }
        FailureOr<SafeConstraintValue> predicateValue =
            safeConstraintValue(nested[0]);
        FailureOr<Value> thenValue = lowerConstraint(nested[1], softTarget);
        if (failed(predicateValue) || failed(thenValue))
          return failure();
        FailureOr<Value> predicate =
            truthValue(predicateValue->value, getSemanticLocation(nested[0]));
        if (failed(predicate))
          return failure();
        Value elseValue;
        if (conditional.getHasElse()) {
          FailureOr<Value> loweredElse = lowerConstraint(nested[2], softTarget);
          if (failed(loweredElse))
            return failure();
          elseValue = *loweredElse;
        } else {
          elseValue = arith::ConstantOp::create(
              builder, getSemanticLocation(constraint), builder.getI1Type(),
              builder.getBoolAttr(true));
        }
        Value result =
            arith::SelectOp::create(builder, getSemanticLocation(constraint),
                                    *predicate, *thenValue, elseValue);
        return softTarget ? FailureOr<Value>(result)
                          : requireArithmeticValidity(nested.front(), result);
      }
      if (isa<semantic::SVUniquenessConstraintOp>(constraint)) {
        if (softTarget)
          return arith::ConstantOp::create(
                     builder, getSemanticLocation(constraint),
                     builder.getI1Type(), builder.getBoolAttr(true))
              .getResult();
        SmallVector<Value> values;
        for (Operation *item : nested) {
          FailureOr<SafeConstraintValue> value = safeConstraintValue(item);
          if (failed(value))
            return failure();
          values.push_back(value->value);
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
        for (Operation *operand : nested) {
          auto valid = requireArithmeticValidity(operand, result);
          if (failed(valid))
            return failure();
          result = *valid;
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
            dimension ? dimension.getAs<BoolAttr>(foreach_metadata::hasIterator)
                      : BoolAttr{};
        auto hasStaticRange =
            dimension
                ? dimension.getAs<BoolAttr>(foreach_metadata::hasStaticRange)
                : BoolAttr{};
        auto iteratorPath =
            dimension
                ? dimension.getAs<StringAttr>(foreach_metadata::iteratorPath)
                : StringAttr{};
        auto semanticIteratorType =
            dimension
                ? dimension.getAs<TypeAttr>(foreach_metadata::iteratorType)
                : TypeAttr{};
        if (!dimension || !hasIterator || !hasIterator.getValue() ||
            !hasStaticRange || hasStaticRange.getValue() || !iteratorPath ||
            !semanticIteratorType) {
          emitError(getSemanticLocation(constraint))
              << "runtime foreach constraint metadata is malformed";
          return failure();
        }
        FailureOr<Type> iteratorType =
            normalizeSemanticType(semanticIteratorType.getValue(),
                                  getSemanticLocation(constraint), function);
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
          return arith::ConstantOp::create(builder, foreachLocation, indexType,
                                           builder.getI64IntegerAttr(value));
        };
        Value trueValue = arith::ConstantOp::create(builder, foreachLocation,
                                                    builder.getI1Type(),
                                                    builder.getBoolAttr(true));
        Value count = sim::SimContainerSizeOp::create(builder, foreachLocation,
                                                      indexType, *collection);
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
        Value more =
            arith::CmpIOp::create(builder, foreachLocation,
                                  arith::CmpIPredicate::ult, ordinal, count);
        cf::CondBranchOp::create(builder, foreachLocation, more, body,
                                 ValueRange{}, exit, ValueRange{accumulated});

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
        FailureOr<Value> valid = constraintCreationValid(nested.back());
        if (hadPrevious)
          values[iteratorPath.getValue()] = saved;
        else
          values.erase(iteratorPath.getValue());
        if (failed(item) || failed(valid))
          return failure();
        item = guardAnd(*item, *valid);
        Value nextResult =
            arith::AndIOp::create(builder, foreachLocation, accumulated, *item);
        Value nextOrdinal = arith::AddIOp::create(builder, foreachLocation,
                                                  ordinal, indexConstant(1));
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
    for (const CreationCheck &check : creationChecks) {
      Value valid = check.valid;
      if (check.block != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1) {
        Value selected =
            arith::AndIOp::create(builder, location, relevantConstraintMode,
                                  constant64(uint64_t{1} << check.block));
        Value disabled =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  selected, constant64(0));
        valid = guardOr(disabled, valid);
      }
      satisfied = guardAnd(satisfied, valid);
    }
    uint64_t domainPropertyOffset = 0;
    for (const Property &property : planned) {
      for (const PropertyDomain &domain : property.domains) {
        uint64_t offset = domainPropertyOffset + domain.offset;
        Type fieldType = builder.getIntegerType(domain.width);
        Value shift;
        if (offset != 0) {
          shift = constant64(offset);
          if (assignmentType != i64)
            shift = arith::ExtUIOp::create(builder, location, assignmentType,
                                           shift);
        }
        auto extractField = [&](Value bits) -> Value {
          if (shift)
            bits = arith::ShRUIOp::create(builder, location, bits, shift);
          if (bits.getType() != fieldType)
            bits = arith::TruncIOp::create(builder, location, fieldType, bits);
          return bits;
        };
        auto fieldConstant = [&](const APInt &bits) -> Value {
          return arith::ConstantOp::create(
              builder, location, fieldType,
              builder.getIntegerAttr(fieldType, bits));
        };
        // Keep each predicate at its domain width, including enum array
        // leaves, instead of copying parent-width pattern literals.
        Value field = extractField(assignment);
        Value member = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(false));
        for (const DomainPattern &pattern : domain.patterns) {
          Value masked = arith::AndIOp::create(builder, location, field,
                                               fieldConstant(pattern.mask));
          Value matches =
              arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                    masked, fieldConstant(pattern.value));
          member = arith::OrIOp::create(builder, location, member, matches);
        }
        Value mutableField = extractField(mutableMask);
        Value inactive = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, mutableField,
            fieldConstant(APInt(domain.width, 0)));
        Value accepted =
            arith::OrIOp::create(builder, location, member, inactive);
        satisfied =
            arith::AndIOp::create(builder, location, satisfied, accepted);
      }
      domainPropertyOffset += property.width;
    }
    if (constraintTemplate) {
      llvm::DenseMap<Value, Value> checkedTemplateValues;
      std::function<FailureOr<Value>(Value)> materializeTemplateCheckValue;
      materializeTemplateCheckValue = [&](Value value) -> FailureOr<Value> {
        if (Value checked = checkedTemplateValues.lookup(value))
          return checked;
        FailureOr<bool> dependsOnVariable = templateDependsOnVariable(value);
        if (failed(dependsOnVariable))
          return failure();
        if (!*dependsOnVariable) {
          // State subgraphs were captured before the sampling CFG was built,
          // so their materialized SSA values dominate every checker. Constants
          // that were encoded directly have no such binding and must be cloned
          // into this checker invocation rather than cached across sibling
          // blocks.
          if (Value materialized = materializedTemplateValues.lookup(value)) {
            checkedTemplateValues[value] = materialized;
            return materialized;
          }
        }

        Operation *operation = value.getDefiningOp();
        auto result = dyn_cast<OpResult>(value);
        if (!operation || !result) {
          emitError(value.getLoc())
              << "random constraint template checker value is not an operation "
                 "result";
          return failure();
        }
        if (auto valueOp =
                dyn_cast<sim::SimRandomConstraintValueOp>(operation)) {
          FailureOr<sim::RandomValueReferenceAttr> reference =
              lookupTemplateReference(valueOp);
          if (failed(reference))
            return failure();
          FailureOr<std::optional<uint32_t>> variable =
              templateVariableOffset(*reference, valueOp.getLoc());
          if (failed(variable))
            return failure();
          if (!variable->has_value()) {
            emitError(valueOp.getLoc())
                << "random constraint template state escaped checker "
                   "materialization";
            return failure();
          }
          for (auto [index, property] : llvm::enumerate(planned)) {
            if (property.field != reference->getTarget())
              continue;
            FailureOr<Value> checked = sliceTemplateValue(
                randomizeCandidateValues[index], reference->getLow(),
                reference->getWidth(), valueOp.getLoc());
            if (failed(checked))
              return failure();
            checkedTemplateValues[value] = *checked;
            return *checked;
          }
          emitError(valueOp.getLoc())
              << "random constraint template variable has no checker binding";
          return failure();
        }

        IRMapping mapping;
        for (Value operand : operation->getOperands()) {
          FailureOr<Value> checked = materializeTemplateCheckValue(operand);
          if (failed(checked))
            return failure();
          mapping.map(operand, *checked);
        }
        Operation *cloned = builder.clone(*operation, mapping);
        for (auto [original, replacement] :
             llvm::zip_equal(operation->getResults(), cloned->getResults()))
          checkedTemplateValues[original] = replacement;
        return checkedTemplateValues.lookup(value);
      };

      auto applyTemplateConstraintMode =
          [&](Value predicate, uint32_t block) -> FailureOr<Value> {
        if (block >= constraintCount) {
          emitError(location)
              << "random constraint template block index is malformed";
          return failure();
        }
        uint64_t blockBit = uint64_t{1} << block;
        Value selected = arith::AndIOp::create(
            builder, location, relevantConstraintMode, constant64(blockBit));
        Value disabled =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  selected, constant64(0));
        Value trueValue = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(true));
        return arith::SelectOp::create(builder, location, disabled, trueValue,
                                       predicate)
            .getResult();
      };

      for (Operation &templateOperation :
           constraintTemplate.getBody().front()) {
        if (auto hard =
                dyn_cast<sim::SimRandomHardConstraintOp>(templateOperation)) {
          FailureOr<Value> predicate =
              materializeTemplateCheckValue(hard.getPredicate());
          if (failed(predicate))
            return failure();
          predicate = applyTemplateConstraintMode(*predicate, hard.getBlock());
          if (failed(predicate))
            return failure();
          satisfied =
              arith::AndIOp::create(builder, location, satisfied, *predicate);
          continue;
        }
        if (auto soft =
                dyn_cast<sim::SimRandomSoftConstraintOp>(templateOperation)) {
          FailureOr<Value> predicate =
              materializeTemplateCheckValue(soft.getPredicate());
          if (failed(predicate))
            return failure();
          predicate = applyTemplateConstraintMode(*predicate, soft.getBlock());
          if (failed(predicate))
            return failure();
          softSatisfied = arith::AndIOp::create(builder, location,
                                                softSatisfied, *predicate);
        }
      }
    }
    for (auto [index, constraint] : llvm::enumerate(children)) {
      if (isReceiverChild(index))
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
          hasHard ? lowerConstraint(constraint, /*softTarget=*/nullptr)
                  : FailureOr<Value>(
                        arith::ConstantOp::create(
                            builder, getSemanticLocation(constraint),
                            builder.getI1Type(), builder.getBoolAttr(true))
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
            builder, location, relevantConstraintMode, constant64(blockBit));
        Value disabled =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  selected, constant64(0));
        Value trueValue = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(true));
        hard = arith::SelectOp::create(builder, location, disabled, trueValue,
                                       *hard)
                   .getResult();
        for (Value &soft : softValues)
          soft = arith::SelectOp::create(builder, location, disabled, trueValue,
                                         soft);
      }
      satisfied = arith::AndIOp::create(builder, location, satisfied, *hard);
      for (Value soft : softValues)
        softSatisfied =
            arith::AndIOp::create(builder, location, softSatisfied, soft);
    }
    thisObject = savedThis;
    enclosingThisObject = savedEnclosingThis;
    randomizeCandidateValues = std::move(savedCandidates);
    restoreBindings.release();
    Value preferred =
        hasSoftConstraint
            ? arith::AndIOp::create(builder, location, satisfied, softSatisfied)
                  .getResult()
            : satisfied;
    return ConstraintCheck{satisfied, preferred};
  };

  if (useCyclicPlan) {
    // IEEE 1800-2023 18.4.2: project each cyclic field before drawing ordinary
    // values. One shared operation specializes the same compiled relation at
    // every stage; accepted values, histories and RNG remain tentative.
    Type stringType = sim::StringType::get(function.getContext());
    Type i32 = builder.getI32Type();
    auto constant32 = [&](uint32_t value) -> Value {
      return arith::ConstantOp::create(builder, location, i32,
                                       builder.getI32IntegerAttr(value));
    };
    SmallVector<Block *> entries, accepted;
    SmallVector<int32_t> stages;
    for (unsigned stage = 0; stage != cyclicFields.size(); ++stage) {
      entries.push_back(addBlock());
      accepted.push_back(addBlock());
      stages.push_back(stage);
    }
    Block *probe = addBlock();
    Value probeAssignment = probe->addArgument(assignmentType, location);
    Value probeMask = probe->addArgument(assignmentType, location);
    Value probeState = probe->addArgument(i64, location);
    Value probeKey = probe->addArgument(i64, location);
    Value probePosition = probe->addArgument(i64, location);
    Value probeDomain = probe->addArgument(stringType, location);
    Value probeOffset = probe->addArgument(i32, location);
    Value probeWidth = probe->addArgument(i32, location);
    Value probeStage = probe->addArgument(i32, location);
    Block *probeAccepted = addBlock();
    Block *finalEntry = addBlock();
    Value finalCandidate = finalEntry->addArgument(assignmentType, location);
    Value finalInputState = finalEntry->addArgument(i64, location);
    Block *finalAccepted = addBlock();
    Value finalAssignment =
        finalAccepted->addArgument(assignmentType, location);
    Value finalState = finalAccepted->addArgument(i64, location);

    setCurrent(cyclicPlanEntry);
    if (analysis.satisfiability == solver::Satisfiability::Unsatisfiable)
      cf::CondBranchOp::create(builder, location, allConstraintsEnabled,
                               failedBlock, ValueRange{}, entries.front(),
                               ValueRange{currentAssignment, modeState});
    else
      cf::BranchOp::create(builder, location, entries.front(),
                           ValueRange{currentAssignment, modeState});

    for (auto [stage, field] : llvm::enumerate(cyclicFields)) {
      Value entryAssignment =
          entries[stage]->addArgument(assignmentType, location);
      Value entryState = entries[stage]->addArgument(i64, location);
      Value acceptedAssignment =
          accepted[stage]->addArgument(assignmentType, location);
      Value acceptedState = accepted[stage]->addArgument(i64, location);
      Value acceptedKey = accepted[stage]->addArgument(i64, location);
      Value acceptedPosition = accepted[stage]->addArgument(i64, location);
      Value acceptedDomain = accepted[stage]->addArgument(stringType, location);
      Block *next =
          stage + 1 == entries.size() ? finalEntry : entries[stage + 1];
      setCurrent(entries[stage]);
      // Inactive cyclic fields are frozen state and retain their histories.
      cf::CondBranchOp::create(
          builder, location, field.enabled, probe,
          ValueRange{entryAssignment, cyclicMasks[stage], entryState, field.key,
                     field.position, field.domain, constant32(field.offset),
                     constant32(field.width), constant32(stage)},
          next, ValueRange{entryAssignment, entryState});

      setCurrent(accepted[stage]);
      sim::SimRefStoreOp::create(builder, location, acceptedKey,
                                 cyclicKeys[stage]);
      sim::SimRefStoreOp::create(builder, location, acceptedPosition,
                                 cyclicPositions[stage]);
      sim::SimRefStoreOp::create(builder, location, acceptedDomain,
                                 cyclicDomains[stage]);
      cf::BranchOp::create(builder, location, next,
                           ValueRange{acceptedAssignment, acceptedState});
    }

    setCurrent(probe);
    auto cycle = sim::SimRandomCycleSampleOp::create(
        builder, location, function.getBody().front().getArgument(0),
        probeAssignment, probeMask, relevantConstraintMode, probeState,
        increment, probeKey, probePosition, probeDomain, probeOffset,
        probeWidth, programCaptures, getSamplerProgram());
    cf::CondBranchOp::create(builder, location, cycle.getSuccess(),
                             probeAccepted, ValueRange{}, failedBlock,
                             ValueRange{});

    setCurrent(probeAccepted);
    auto caseType = RankedTensorType::get({static_cast<int64_t>(stages.size())},
                                          builder.getI32Type());
    auto caseValues = DenseIntElementsAttr::get(caseType, ArrayRef(stages));
    SmallVector<Value> acceptedValues{
        cycle.getAssignment(), cycle.getNextRngState(), cycle.getNextKey(),
        cycle.getNextPosition(), cycle.getNextDomain()};
    SmallVector<ValueRange> acceptedOperands(stages.size(),
                                             ValueRange(acceptedValues));
    cf::SwitchOp::create(builder, location, probeStage, failedBlock,
                         ValueRange{}, caseValues, accepted, acceptedOperands);

    setCurrent(finalEntry);
    auto solve = sim::SimRandomSolveWideOp::create(
        builder, location, function.getBody().front().getArgument(0),
        finalCandidate, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), finalInputState, increment,
        programCaptures, getSamplerProgram());
    cf::CondBranchOp::create(
        builder, location, solve.getSuccess(), finalAccepted,
        ValueRange{solve.getAssignment(), solve.getNextRngState()}, failedBlock,
        ValueRange{});

    setCurrent(finalAccepted);
    storeRandomState(finalState);
    cf::BranchOp::create(builder, location, commit,
                         ValueRange{finalAssignment});
  }

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
    Value advancePosition = singleRandCAdvance->addArgument(i64, location);
    Value advanceRemaining = singleRandCAdvance->addArgument(i64, location);
    Value advanceFreshCycle =
        singleRandCAdvance->addArgument(builder.getI1Type(), location);
    Value commitCandidate =
        singleRandCCommit->addArgument(assignmentType, location);
    Value commitKey = singleRandCCommit->addArgument(i64, location);
    Value commitPosition = singleRandCCommit->addArgument(i64, location);

    setCurrent(singleRandCLoop);
    FailureOr<ConstraintCheck> randcCheck =
        materializeConstraintCheck(randcCandidate);
    if (failed(randcCheck))
      return failure();
    cf::CondBranchOp::create(
        builder, location, randcCheck->preferred, singleRandCCommit,
        ValueRange{randcCandidate, randcKey, randcPosition}, singleRandCAdvance,
        ValueRange{randcKey, randcPosition, randcRemaining, randcFreshCycle});

    setCurrent(singleRandCAdvance);
    Value nextRemaining = arith::SubIOp::create(
        builder, location, advanceRemaining, constant64(1));
    if (semanticCardinality == 1) {
      cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
    } else {
      Value exhausted =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                nextRemaining, constant64(0));
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
      Value needsRekey =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                entryPosition, constant64(0));
      cf::CondBranchOp::create(
          builder, location, needsRekey, rekeyBlock, ValueRange{}, cycleBlock,
          ValueRange{entryKey, entryPosition, entryRemaining, entryFreshCycle});

      setCurrent(rekeyBlock);
      Value rekeyState = loadRandomState();
      Value newKey = next64(rekeyState);
      storeRandomState(rekeyState);
      cf::BranchOp::create(
          builder, location, cycleBlock,
          ValueRange{newKey, entryPosition, constant64(semanticCardinality),
                     arith::ConstantOp::create(builder, location,
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
      if (semanticValue.getType() != assignmentType)
        semanticValue = arith::ExtUIOp::create(builder, location,
                                               assignmentType, semanticValue);
      cf::CondBranchOp::create(builder, location, valid, singleRandCLoop,
                               ValueRange{semanticValue, activeKey,
                                          cycle.getNextPosition(),
                                          activeRemaining, activeFreshCycle},
                               candidateEntry,
                               ValueRange{activeKey, cycle.getNextPosition(),
                                          activeRemaining, activeFreshCycle});
    }

    setCurrent(singleRandCCommit);
    FailureOr<SmallVector<Value>> candidates =
        materializeCandidates(commitCandidate);
    if (failed(candidates))
      return failure();
    if (failed(storeProperty(property, candidates->front())) ||
        failed(
            storeReference(property.randcKeyReference, commitKey, location)) ||
        failed(storeReference(property.randcPositionReference, commitPosition,
                              location)))
      return failure();
    cf::BranchOp::create(builder, location, postBlock);
  }

  if (foreachRankEntry) {
    // The compiler's semantic domains form a mixed-radix assignment space.
    // Visit every rank once and reservoir-sample the satisfying assignments.
    // Starting a cyclic scan at a random rank and taking its first solution
    // would weight solutions by the preceding gaps instead of uniformly.
    Value initialState = foreachRankEntry->addArgument(i64, location);
    Block *rankLoop = addBlock();
    Value rank = rankLoop->addArgument(i64, location);
    Value count = rankLoop->addArgument(i64, location);
    Value selected = rankLoop->addArgument(assignmentType, location);
    Value stream = rankLoop->addArgument(i64, location);
    Block *accepted = addBlock();
    Block *rankAdvance = addBlock();
    Value nextCount = rankAdvance->addArgument(i64, location);
    Value nextSelected = rankAdvance->addArgument(assignmentType, location);
    Value nextStream = rankAdvance->addArgument(i64, location);
    Block *finished = addBlock();
    Value finalCount = finished->addArgument(i64, location);
    Value finalSelected = finished->addArgument(assignmentType, location);
    Value finalStream = finished->addArgument(i64, location);
    Block *successful = addBlock();

    setCurrent(foreachRankEntry);
    cf::BranchOp::create(
        builder, location, rankLoop,
        ValueRange{constant64(0), constant64(0), zeroAssignment, initialState});
    setCurrent(rankLoop);
    Value remaining = rank;
    Value candidate = zeroAssignment;
    for (auto [index, property] : llvm::enumerate(planned)) {
      uint64_t cardinality = propertyDomainCardinality(property);
      Value fieldIndex = arith::RemUIOp::create(builder, location, remaining,
                                                constant64(cardinality));
      remaining = arith::DivUIOp::create(builder, location, remaining,
                                         constant64(cardinality));
      Value bits = property.domains.empty()
                       ? fieldIndex
                       : materializePropertyDomainIndex(property, fieldIndex);
      if (bits.getType() != assignmentType)
        bits = arith::ExtUIOp::create(builder, location, assignmentType, bits);
      if (propertyOffsets[index]) {
        Value shift = constant64(propertyOffsets[index]);
        if (assignmentType != i64)
          shift =
              arith::ExtUIOp::create(builder, location, assignmentType, shift);
        bits = arith::ShLIOp::create(builder, location, bits, shift);
      }
      candidate = arith::OrIOp::create(builder, location, candidate, bits);
    }
    candidate = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, candidate, mutableMask),
        fixedAssignment);
    FailureOr<ConstraintCheck> check = materializeConstraintCheck(candidate);
    if (failed(check))
      return failure();
    cf::CondBranchOp::create(builder, location, check->hard, accepted,
                             ValueRange{}, rankAdvance,
                             ValueRange{count, selected, stream});

    setCurrent(accepted);
    Value acceptedCount =
        arith::AddIOp::create(builder, location, count, constant64(1));
    Value acceptedState = stream;
    Value draw = next64(acceptedState);
    Value choice =
        sampleDynamicBoundedIndex(acceptedCount, draw, acceptedState);
    Value replace = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, choice, constant64(0));
    Value acceptedValue = arith::SelectOp::create(builder, location, replace,
                                                  candidate, selected);
    cf::BranchOp::create(
        builder, location, rankAdvance,
        ValueRange{acceptedCount, acceptedValue, acceptedState});

    setCurrent(rankAdvance);
    Value nextRank =
        arith::AddIOp::create(builder, location, rank, constant64(1));
    Value exhausted =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              nextRank, constant64(fallbackAttempts));
    cf::CondBranchOp::create(
        builder, location, exhausted, finished,
        ValueRange{nextCount, nextSelected, nextStream}, rankLoop,
        ValueRange{nextRank, nextCount, nextSelected, nextStream});

    setCurrent(finished);
    Value found = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, finalCount, constant64(0));
    cf::CondBranchOp::create(builder, location, found, successful, ValueRange{},
                             failedBlock, ValueRange{});
    setCurrent(successful);
    storeRandomState(finalStream);
    cf::BranchOp::create(builder, location, commit, ValueRange{finalSelected});
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
    next = arith::AddIOp::create(builder, location, counter,
                                 constantAssignment64(1));
    nextSampledDomainIndices.assign(loopSampledDomainIndices.begin(),
                                    loopSampledDomainIndices.end());
  } else {
    Value retryState = loadRandomState();
    next = nextAssignment(retryState);
    nextSampledDomainIndices = sampleProposalDomainIndices(retryState);
    storeRandomState(retryState);
  }
  next = arith::OrIOp::create(
      builder, location,
      arith::AndIOp::create(builder, location, next, mutableMask),
      fixedAssignment);
  Value nextAttempt =
      arith::AddIOp::create(builder, location, attempt, constant64(1));
  Value exhausted = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::uge, nextAttempt,
      constant64(hasRuntimeForeachConstraint ? fallbackAttempts : 64));
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
    Value fallbackState = loadRandomState();
    auto fallback = sim::SimRandomSolveWideOp::create(
        builder, location, function.getBody().front().getArgument(0),
        fallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), fallbackState, increment, programCaptures,
        getSamplerProgram());
    storeRandomState(fallback.getNextRngState());
    cf::CondBranchOp::create(builder, location, fallback.getSuccess(), commit,
                             ValueRange{fallback.getAssignment()}, failedBlock,
                             ValueRange{});
  } else {
    Value fallbackState = loadRandomState();
    auto fallback = sim::SimRandomSolveOp::create(
        builder, location, function.getBody().front().getArgument(0),
        fallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), fallbackState, increment, programCaptures,
        getSamplerProgram());
    storeRandomState(fallback.getNextRngState());
    cf::CondBranchOp::create(builder, location, fallback.getSuccess(), commit,
                             ValueRange{fallback.getAssignment()}, failedBlock,
                             ValueRange{});
  }

  setCurrent(modeSamplingDispatchBlock);
  storeRandomState(modeState);
  if (foreachRankEntry)
    cf::BranchOp::create(builder, location, foreachRankEntry,
                         ValueRange{modeState});
  else if (!hasRuntimeForeachConstraint &&
           (hasRealProgram || weightedRequiresRuntime || hasSolveBefore ||
            hasFiniteDomains))
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
                                     constantAssignment64(1));
  } else {
    Value modeRetryState = loadRandomState();
    modeNext = nextAssignment(modeRetryState);
    storeRandomState(modeRetryState);
  }
  modeNext = arith::OrIOp::create(
      builder, location,
      arith::AndIOp::create(builder, location, modeNext, mutableMask),
      fixedAssignment);
  Value modeNextAttempt =
      arith::AddIOp::create(builder, location, modeAttempt, constant64(1));
  Value modeExhausted = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::uge, modeNextAttempt,
      constant64(hasRuntimeForeachConstraint ? fallbackAttempts : 64));
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
    Value fallbackState = loadRandomState();
    auto modeFallback = sim::SimRandomSolveWideOp::create(
        builder, location, function.getBody().front().getArgument(0),
        modeFallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), fallbackState, increment, programCaptures,
        getSamplerProgram());
    storeRandomState(modeFallback.getNextRngState());
    cf::CondBranchOp::create(builder, location, modeFallback.getSuccess(),
                             commit, ValueRange{modeFallback.getAssignment()},
                             failedBlock, ValueRange{});
  } else {
    Value fallbackState = loadRandomState();
    auto modeFallback = sim::SimRandomSolveOp::create(
        builder, location, function.getBody().front().getArgument(0),
        modeFallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), fallbackState, increment, programCaptures,
        getSamplerProgram());
    storeRandomState(modeFallback.getNextRngState());
    cf::CondBranchOp::create(builder, location, modeFallback.getSuccess(),
                             commit, ValueRange{modeFallback.getAssignment()},
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
  else if (foreachRankEntry)
    cf::BranchOp::create(builder, location, foreachRankEntry,
                         ValueRange{state});
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
  for (auto [index, entry] :
       llvm::enumerate(llvm::zip_equal(planned, *committed, propertyEnabled))) {
    auto [property, candidate, enabled] = entry;
    Block *store = addBlock();
    Block *nextProperty = addBlock();
    cf::CondBranchOp::create(builder, location, enabled, store, ValueRange{},
                             nextProperty, ValueRange{});
    setCurrent(store);
    if (property.disabledElements &&
        isa<sim::LogicType>(property.originalScalar.getType())) {
      // Select entire four-state leaves. Ordinary bitwise masking would turn
      // preserved Z symbols into X and violate the inactive-value contract.
      FailureOr<Value> scalar = toRandomScalar(candidate, location);
      if (failed(scalar))
        return failure();
      auto bitmapType = cast<IntegerType>(property.disabledElements.getType());
      Type leafType =
          sim::LogicType::get(function.getContext(), property.elementWidth);
      SmallVector<Value> leaves;
      for (unsigned leaf = bitmapType.getWidth(); leaf != 0; --leaf) {
        Value oldLeaf = sim::SimLogicExtractOp::create(
            builder, location, leafType, property.originalScalar,
            builder.getI64IntegerAttr(uint64_t{leaf - 1} *
                                      property.elementWidth));
        Value newLeaf = sim::SimLogicExtractOp::create(
            builder, location, leafType, *scalar,
            builder.getI64IntegerAttr(uint64_t{leaf - 1} *
                                      property.elementWidth));
        Value bit = arith::ConstantOp::create(
            builder, location, bitmapType,
            builder.getIntegerAttr(
                bitmapType,
                APInt::getOneBitSet(bitmapType.getWidth(), leaf - 1)));
        Value zero = arith::ConstantOp::create(
            builder, location, bitmapType,
            builder.getIntegerAttr(bitmapType,
                                   APInt(bitmapType.getWidth(), 0)));
        Value active = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq,
            arith::AndIOp::create(builder, location, property.disabledElements,
                                  bit),
            zero);
        leaves.push_back(arith::SelectOp::create(builder, location, active,
                                                 newLeaf, oldLeaf));
      }
      Value preserved = leaves.size() == 1
                            ? leaves.front()
                            : sim::SimLogicConcatOp::create(
                                  builder, location,
                                  property.originalScalar.getType(), leaves)
                                  .getResult();
      FailureOr<Value> array =
          convertFixedBitstreamTarget(preserved, property.type, location);
      if (failed(array))
        return failure();
      candidate = *array;
    }
    if (property.nestedObjectReference) {
      FailureOr<Value> loadedObject =
          loadReference(property.nestedObjectReference, location);
      if (failed(loadedObject))
        return failure();
      Value object = *loadedObject;
      if (object.getType() != property.nestedObjectType)
        object = sim::SimClassCastOp::create(builder, location,
                                             property.nestedObjectType, object);
      auto concreteType = cast<sim::ClassHandleType>(property.nestedObjectType);
      for (const ObjectPathElement &element : property.nestedObjectPath) {
        Type edgeReferenceType =
            sim::ManagedRefType::get(function.getContext(), element.storageType,
                                     concreteType.getClassName());
        Value edgeReference = sim::SimClassFieldRefOp::create(
            builder, location, edgeReferenceType, object, element.field);
        FailureOr<Value> loadedEdge = loadReference(edgeReference, location);
        if (failed(loadedEdge))
          return failure();
        object = *loadedEdge;
        if (object.getType() != element.concreteType)
          object = sim::SimClassCastOp::create(builder, location,
                                               element.concreteType, object);
        concreteType = cast<sim::ClassHandleType>(element.concreteType);
      }
      Type nestedFieldType =
          property.isContainerSize ? property.containerType : property.type;
      Type fieldReferenceType = sim::ManagedRefType::get(
          function.getContext(), nestedFieldType, concreteType.getClassName());
      Value fieldReference = sim::SimClassFieldRefOp::create(
          builder, location, fieldReferenceType, object, property.nestedField);
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
    } else if (failed(storeProperty(property, candidate)))
      return failure();
    if (property.isRandC) {
      Value key = property.nextRandcKey;
      Value position = property.nextRandcPosition;
      if (useCyclicPlan) {
        auto readHistory = [&](Type type, ArrayRef<Value> references) -> Value {
          unsigned cursor = cyclicFieldRanges[index].first;
          std::function<Value(Type)> build = [&](Type current) -> Value {
            if (auto array = dyn_cast<sim::UnpackedArrayType>(current)) {
              SmallVector<Value> elements;
              for (unsigned ordinal = 0;
                   ordinal != sim::getAggregateNumElements(array); ++ordinal)
                elements.push_back(build(array.getElementType()));
              return sim::SimAggregateConstructOp::create(builder, location,
                                                          current, elements);
            }
            return sim::SimRefLoadOp::create(builder, location, current,
                                             references[cursor++]);
          };
          Value result = build(type);
          assert(cursor == cyclicFieldRanges[index].second &&
                 "cyclic history shape mismatch");
          return result;
        };
        key = readHistory(getReferenceElementType(property.randcKeyReference),
                          cyclicKeys);
        position = readHistory(
            getReferenceElementType(property.randcPositionReference),
            cyclicPositions);
        Value domain =
            readHistory(getReferenceElementType(property.randcDomainReference),
                        cyclicDomains);
        if (failed(storeReference(property.randcDomainReference, domain,
                                  location)))
          return failure();
      }
      if (failed(storeReference(property.randcKeyReference, key, location)) ||
          failed(storeReference(property.randcPositionReference, position,
                                location)))
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
  for (auto [containerIndex, property] : llvm::enumerate(plannedContainers)) {
    if (property.inertClassHandles)
      continue;
    Block *entryBlock = addBlock();
    Block *header = addBlock();
    Block *body = addBlock();
    Block *nextProperty = addBlock();
    Value propertyMode =
        arith::AndIOp::create(builder, location, relevantMode,
                              constant64(uint64_t{1} << property.modeIndex));
    Value enabled =
        checkerOnly
            ? arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false))
                  .getResult()
            : arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                    propertyMode, constant64(0))
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
        object = sim::SimClassCastOp::create(builder, location,
                                             property.nestedObjectType, object);
      auto concreteType = cast<sim::ClassHandleType>(property.nestedObjectType);
      FlatSymbolRefAttr objectModeField = property.nestedModeField;
      for (const ObjectPathElement &element : property.nestedObjectPath) {
        Type modeReferenceType = sim::ManagedRefType::get(
            function.getContext(), i64, concreteType.getClassName());
        Value modeReference = sim::SimClassFieldRefOp::create(
            builder, location, modeReferenceType, object, objectModeField);
        Value objectMode = sim::SimManagedLoadOp::create(builder, location, i64,
                                                         modeReference);
        Value edgeModeBit =
            arith::AndIOp::create(builder, location, objectMode,
                                  constant64(uint64_t{1} << element.modeIndex));
        Value edgeEnabled =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  edgeModeBit, constant64(0));
        Type edgeReferenceType =
            sim::ManagedRefType::get(function.getContext(), element.storageType,
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
        Value edgeActive =
            arith::AndIOp::create(builder, location, edgeEnabled, edgeNonNull);
        Block *edgeBlock = addBlock();
        cf::CondBranchOp::create(builder, location, edgeActive, edgeBlock,
                                 ValueRange{}, inactiveBlock, ValueRange{});
        setCurrent(edgeBlock);
        object = *loadedEdge;
        if (object.getType() != element.concreteType)
          object = sim::SimClassCastOp::create(builder, location,
                                               element.concreteType, object);
        concreteType = cast<sim::ClassHandleType>(element.concreteType);
        objectModeField = element.modeField;
      }
      Type modeReferenceType = sim::ManagedRefType::get(
          function.getContext(), i64, concreteType.getClassName());
      Value childModeReference = sim::SimClassFieldRefOp::create(
          builder, location, modeReferenceType, object, objectModeField);
      Value childMode = sim::SimManagedLoadOp::create(builder, location, i64,
                                                      childModeReference);
      Value childModeBit = arith::AndIOp::create(
          builder, location, childMode,
          constant64(uint64_t{1} << property.nestedModeIndex));
      Value childEnabled =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                childModeBit, constant64(0));
      Block *childActiveBlock = addBlock();
      cf::CondBranchOp::create(builder, location, childEnabled,
                               childActiveBlock, ValueRange{}, inactiveBlock,
                               ValueRange{});
      setCurrent(childActiveBlock);
      Type fieldReferenceType = sim::ManagedRefType::get(
          function.getContext(), property.type, concreteType.getClassName());
      Value containerReference = sim::SimClassFieldRefOp::create(
          builder, location, fieldReferenceType, object, property.nestedField);
      container = sim::SimManagedLoadOp::create(
          builder, location, property.type, containerReference);
      randomizeBlock = childActiveBlock;
      if (needsIdentity) {
        Block *resolvedBlock = addBlock();
        resolvedBlock->addArgument(property.type, location);
        resolvedBlock->addArgument(i64, location);
        resolvedBlock->addArgument(builder.getI1Type(), location);
        Value objectID = sim::SimClassIdOp::create(builder, location, object);
        cf::BranchOp::create(
            builder, location, resolvedBlock,
            ValueRange{container, objectID,
                       arith::ConstantOp::create(builder, location,
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
                       arith::ConstantOp::create(builder, location,
                                                 builder.getI1Type(),
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
          Value sameID =
              arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                    objectID, previous.objectID);
          Value previousMatch =
              arith::AndIOp::create(builder, location, previous.active, sameID);
          alreadyVisited = arith::OrIOp::create(builder, location,
                                                alreadyVisited, previousMatch);
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
    Value size =
        sim::SimContainerSizeOp::create(builder, location, i64, container);
    Value containerState = loadRandomState();
    header->addArgument(i64, location);
    header->addArgument(i64, location);
    cf::BranchOp::create(builder, location, header,
                         ValueRange{constant64(0), containerState});

    setCurrent(header);
    Value index = header->getArgument(0);
    Value loopState = header->getArgument(1);
    Value more = arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::ult, index, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{},
                             nextProperty, ValueRange{});

    setCurrent(body);
    Value nextState = loopState;
    Value bits = next64(nextState);
    Type scalarType =
        IntegerType::get(function.getContext(), property.elementWidth);
    if (property.elementWidth != 64)
      bits = arith::TruncIOp::create(builder, location, scalarType, bits);
    FailureOr<Value> element =
        convert(bits, property.elementType, false, location, false);
    if (failed(element))
      return failure();
    sim::SimContainerWriteOp::create(builder, location, container, index,
                                     *element);
    storeRandomState(nextState);
    Value nextIndex =
        arith::AddIOp::create(builder, location, index, constant64(1));
    cf::BranchOp::create(builder, location, header,
                         ValueRange{nextIndex, nextState});
    setCurrent(nextProperty);
  }
  cf::BranchOp::create(builder, location, postBlock);

  setCurrent(postBlock);
  if (failed(callLifecycleHook(
          randomPostHookAttrName, randomPostHookOwnerAttrName,
          randomPostHookCapturesAttrName, randomPostHookReadCapturesAttrName)))
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
