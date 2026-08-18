//===- LowerUnitControlFlow.cpp - Lower loops, blocks, and forks -------===//

#include "LowerUnit.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <functional>
#include <limits>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

LogicalResult UnitLowering::lowerWhile(Operation *op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2) {
    unsupported(op) << " (while loop arity)";
    return failure();
  }
  Block *conditionBlock = addBlock();
  Block *bodyBlock = addBlock();
  Block *exitBlock = addBlock();
  emitBranch(conditionBlock);
  setCurrent(conditionBlock);
  FailureOr<Value> conditionValue = lowerExpression(children[0]);
  if (failed(conditionValue))
    return failure();
  FailureOr<Value> condition = truthValue(*conditionValue, location);
  if (failed(condition))
    return failure();
  cf::CondBranchOp::create(builder, location, *condition, bodyBlock,
                           ValueRange{}, exitBlock, ValueRange{});
  loopTargets.push_back({exitBlock, conditionBlock, {}, controlScopes.size()});
  setCurrent(bodyBlock);
  if (failed(lowerStatement(children[1])))
    return failure();
  emitBranch(conditionBlock);
  loopTargets.pop_back();
  setCurrent(exitBlock);
  return success();
}

LogicalResult UnitLowering::lowerDoWhile(Operation *op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  // The semantic inventory is condition followed by body, even though the
  // body appears first in source.
  if (children.size() != 2) {
    unsupported(op) << " (do-while loop arity)";
    return failure();
  }
  Block *bodyBlock = addBlock();
  Block *conditionBlock = addBlock();
  Block *exitBlock = addBlock();
  emitBranch(bodyBlock);

  loopTargets.push_back({exitBlock, conditionBlock, {}, controlScopes.size()});
  setCurrent(bodyBlock);
  if (failed(lowerStatement(children[1])))
    return failure();
  emitBranch(conditionBlock);

  setCurrent(conditionBlock);
  FailureOr<Value> conditionValue = lowerExpression(children[0]);
  if (failed(conditionValue))
    return failure();
  FailureOr<Value> condition = truthValue(*conditionValue, location);
  if (failed(condition))
    return failure();
  cf::CondBranchOp::create(builder, location, *condition, bodyBlock,
                           ValueRange{}, exitBlock, ValueRange{});
  loopTargets.pop_back();
  setCurrent(exitBlock);
  return success();
}

LogicalResult UnitLowering::lowerFor(semantic::SVForLoopStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  uint64_t initializerCount = op.getInitializerCount();
  uint64_t stepCount = op.getStepCount();
  size_t conditionCount = op.getHasCondition() ? 1 : 0;
  if (initializerCount > children.size() ||
      conditionCount > children.size() - initializerCount ||
      stepCount > children.size() - initializerCount - conditionCount ||
      children.size() - initializerCount - conditionCount - stepCount != 1) {
    op.emitError("malformed for-loop child inventory");
    return failure();
  }
  size_t initializerSize = static_cast<size_t>(initializerCount);
  size_t stepSize = static_cast<size_t>(stepCount);

  ArrayRef<Operation *> inventory(children);
  ArrayRef<Operation *> initializers = inventory.take_front(initializerSize);
  Operation *condition =
      op.getHasCondition() ? children[initializerSize] : nullptr;
  ArrayRef<Operation *> steps =
      inventory.slice(initializerSize + conditionCount, stepSize);
  Operation *body = children.back();

  // SystemVerilog evaluates expression initializers once, in source order,
  // before the first condition check. Loop-variable declarations are separate
  // declaration statements in the enclosing semantic block and have already
  // been lowered before this node.
  for (Operation *initializer : initializers)
    if (failed(lowerExpression(initializer)))
      return failure();

  Block *conditionBlock = addBlock();
  Block *bodyBlock = addBlock();
  Block *stepBlock = addBlock();
  Block *exitBlock = addBlock();
  emitBranch(conditionBlock);

  setCurrent(conditionBlock);
  if (condition) {
    FailureOr<Value> conditionValue = lowerExpression(condition);
    if (failed(conditionValue))
      return failure();
    FailureOr<Value> truth = truthValue(*conditionValue, location);
    if (failed(truth))
      return failure();
    cf::CondBranchOp::create(builder, location, *truth, bodyBlock, ValueRange{},
                             exitBlock, ValueRange{});
  } else {
    cf::BranchOp::create(builder, location, bodyBlock);
  }

  loopTargets.push_back({exitBlock, stepBlock, {}, controlScopes.size()});
  setCurrent(bodyBlock);
  if (failed(lowerStatement(body)))
    return failure();
  emitBranch(stepBlock);

  setCurrent(stepBlock);
  for (Operation *step : steps)
    if (failed(lowerExpression(step)))
      return failure();
  emitBranch(conditionBlock);
  loopTargets.pop_back();
  setCurrent(exitBlock);
  return success();
}

LogicalResult UnitLowering::lowerForever(Operation *op) {
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 1) {
    unsupported(op) << " (forever loop arity)";
    return failure();
  }
  Block *bodyBlock = addBlock();
  Block *exitBlock = addBlock();
  emitBranch(bodyBlock);

  loopTargets.push_back({exitBlock, bodyBlock, {}, controlScopes.size()});
  setCurrent(bodyBlock);
  if (failed(lowerStatement(children.front())))
    return failure();
  emitBranch(bodyBlock);
  loopTargets.pop_back();
  setCurrent(exitBlock);
  return success();
}

LogicalResult
UnitLowering::lowerForeach(semantic::SVForeachLoopStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2) {
    unsupported(op) << " (expected array expression and body)";
    return failure();
  }

  if (foreach_metadata::hasRuntimeDimension(op.getLoopDimensions())) {
    struct RuntimeDimension {
      bool hasIterator;
      bool runtime;
      int64_t left;
      int64_t right;
      std::string iteratorPath;
      Type iteratorType;
    };
    SmallVector<RuntimeDimension> dimensions;
    for (Attribute attribute : op.getLoopDimensions()) {
      auto dimension = dyn_cast<DictionaryAttr>(attribute);
      auto hasIterator =
          dimension ? dimension.getAs<BoolAttr>(foreach_metadata::hasIterator)
                    : BoolAttr{};
      auto hasRange =
          dimension
              ? dimension.getAs<BoolAttr>(foreach_metadata::hasStaticRange)
              : BoolAttr{};
      if (!dimension || !hasIterator || !hasRange) {
        emitError(location) << "malformed runtime foreach metadata";
        return failure();
      }
      RuntimeDimension lowered{
          hasIterator.getValue(), !hasRange.getValue(), 0, 0, {}, {}};
      if (hasRange.getValue()) {
        auto left = dimension.getAs<IntegerAttr>(foreach_metadata::left);
        auto right = dimension.getAs<IntegerAttr>(foreach_metadata::right);
        if (!left || !right) {
          emitError(location) << "static foreach range metadata is missing";
          return failure();
        }
        lowered.left = left.getInt();
        lowered.right = right.getInt();
      }
      if (hasIterator.getValue()) {
        auto path = dimension.getAs<StringAttr>(foreach_metadata::iteratorPath);
        auto semanticIteratorType =
            dimension.getAs<TypeAttr>(foreach_metadata::iteratorType);
        if (!path || !semanticIteratorType) {
          emitError(location) << "runtime foreach iterator metadata is missing";
          return failure();
        }
        FailureOr<Type> iteratorType =
            normalizeSemanticType(semanticIteratorType.getValue(), location);
        if (failed(iteratorType))
          return failure();
        lowered.iteratorPath = path.getValue().str();
        lowered.iteratorType = *iteratorType;
      }
      dimensions.push_back(std::move(lowered));
    }
    if (dimensions.empty())
      return emitError(location) << "foreach statement has no dimensions",
             failure();

    FailureOr<Value> collection = lowerExpression(children[0]);
    if (failed(collection))
      return failure();
    Block *exit = addBlock();
    Type indexType = builder.getI64Type();
    auto constant = [&](uint64_t value) -> Value {
      return arith::ConstantOp::create(builder, location, indexType,
                                       builder.getI64IntegerAttr(value));
    };

    std::function<LogicalResult(unsigned, Value, Block *, ValueRange)>
        emitDimension = [&](unsigned dimensionIndex, Value currentCollection,
                            Block *parentStep,
                            ValueRange parentStepOperands) -> LogicalResult {
      if (dimensionIndex == dimensions.size()) {
        if (!parentStep) {
          emitError(location) << "runtime foreach has no iterated dimension";
          return failure();
        }
        loopTargets.push_back({exit, parentStep,
                               SmallVector<Value>(parentStepOperands.begin(),
                                                  parentStepOperands.end()),
                               controlScopes.size()});
        LogicalResult status = lowerStatement(children[1]);
        loopTargets.pop_back();
        return status;
      }
      RuntimeDimension &dimension = dimensions[dimensionIndex];
      bool traverseOmitted =
          !dimension.hasIterator &&
          llvm::any_of(
              ArrayRef(dimensions).drop_front(dimensionIndex + 1),
              [](const RuntimeDimension &next) { return next.runtime; });
      if (!dimension.hasIterator) {
        // A terminal omitted dimension contributes no loop. An omitted outer
        // dimension still traverses the collection so a later runtime
        // dimension can be selected, but it does not create an iterator
        // binding.
        if (!traverseOmitted)
          return emitDimension(dimensionIndex + 1, currentCollection,
                               parentStep, parentStepOperands);
      }

      if (dimension.runtime &&
          isa<sim::AssocArrayType>(currentCollection.getType())) {
        auto associative =
            cast<sim::AssocArrayType>(currentCollection.getType());
        Type keyType = associative.getKeyType();
        Value initialKey = createDefaultValue(builder, location, keyType);
        FailureOr<std::pair<Value, Value>> first =
            traverseAssoc(currentCollection, initialKey, 1, true, location);
        if (failed(first))
          return failure();
        Block *header = addBlock();
        header->addArgument(keyType, location);
        header->addArgument(builder.getI1Type(), location);
        Block *body = addBlock();
        Block *step = addBlock();
        step->addArgument(keyType, location);
        Block *localExit = !parentStep ? exit : addBlock();
        cf::BranchOp::create(builder, location, header,
                             ValueRange{first->first, first->second});
        setCurrent(header);
        Value key = header->getArgument(0);
        Value valid = header->getArgument(1);
        cf::CondBranchOp::create(builder, location, valid, body, ValueRange{},
                                 localExit, ValueRange{});

        setCurrent(body);
        bool hadPrevious = false;
        Value saved;
        if (dimension.hasIterator) {
          auto previous = values.find(dimension.iteratorPath);
          hadPrevious = previous != values.end();
          saved = hadPrevious ? previous->second : Value{};
          FailureOr<Value> iterator =
              convert(key, dimension.iteratorType, associative.getSignedKey(),
                      location, true);
          if (failed(iterator))
            return failure();
          values[dimension.iteratorPath] = *iterator;
        }

        Value nestedCollection = currentCollection;
        if (dimensionIndex + 1 != dimensions.size())
          nestedCollection = sim::SimAssocReadOp::create(
              builder, location, associative.getElementType(),
              currentCollection, key);
        if (failed(emitDimension(dimensionIndex + 1, nestedCollection, step,
                                 ValueRange{key})))
          return failure();
        if (dimension.hasIterator) {
          if (hadPrevious)
            values[dimension.iteratorPath] = saved;
          else
            values.erase(dimension.iteratorPath);
        }
        if (current->empty() ||
            !current->back().hasTrait<OpTrait::IsTerminator>())
          cf::BranchOp::create(builder, location, step, ValueRange{key});

        setCurrent(step);
        Value previousKey = step->getArgument(0);
        FailureOr<std::pair<Value, Value>> next =
            traverseAssoc(currentCollection, previousKey, 1, false, location);
        if (failed(next))
          return failure();
        cf::BranchOp::create(builder, location, header,
                             ValueRange{next->first, next->second});
        setCurrent(localExit);
        if (parentStep)
          cf::BranchOp::create(builder, location, parentStep,
                               parentStepOperands);
        return success();
      }

      Value count;
      if (dimension.runtime) {
        if (isa<sim::DynamicArrayType, sim::QueueType>(
                currentCollection.getType())) {
          count = sim::SimContainerSizeOp::create(builder, location, indexType,
                                                  currentCollection);
        } else if (isa<sim::StringType>(currentCollection.getType())) {
          count = sim::SimStringLengthOp::create(builder, location, indexType,
                                                 currentCollection);
        } else {
          emitError(location)
              << "runtime foreach dimension is not a sequential container";
          return failure();
        }
      } else {
        APInt left(65, static_cast<uint64_t>(dimension.left), true);
        APInt right(65, static_cast<uint64_t>(dimension.right), true);
        APInt distance = left.sge(right) ? left - right : right - left;
        ++distance;
        if (distance.getActiveBits() > 64) {
          emitError(location) << "foreach dimension range is too large";
          return failure();
        }
        count = constant(distance.getZExtValue());
      }

      Block *header = addBlock();
      header->addArgument(indexType, location);
      Block *body = addBlock();
      Block *step = addBlock();
      Block *localExit = !parentStep ? exit : addBlock();
      cf::BranchOp::create(builder, location, header, ValueRange{constant(0)});
      setCurrent(header);
      Value ordinal = header->getArgument(0);
      Value more = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, ordinal, count);
      cf::CondBranchOp::create(builder, location, more, body, ValueRange{},
                               localExit, ValueRange{});

      setCurrent(body);
      Value sourceIndex = ordinal;
      if (!dimension.runtime) {
        Value left = arith::ConstantOp::create(
            builder, location, indexType,
            builder.getIntegerAttr(
                indexType, APInt(64, static_cast<uint64_t>(dimension.left))));
        sourceIndex =
            dimension.left <= dimension.right
                ? Value(arith::AddIOp::create(builder, location, left, ordinal))
                : Value(
                      arith::SubIOp::create(builder, location, left, ordinal));
      }
      auto previous = values.end();
      bool hadPrevious = false;
      Value saved;
      if (dimension.hasIterator) {
        previous = values.find(dimension.iteratorPath);
        hadPrevious = previous != values.end();
        saved = hadPrevious ? previous->second : Value{};
        FailureOr<Value> iterator =
            convert(sourceIndex, dimension.iteratorType, true, location, true);
        if (failed(iterator))
          return failure();
        values[dimension.iteratorPath] = *iterator;
      }

      Value nestedCollection = currentCollection;
      if (dimensionIndex + 1 != dimensions.size() &&
          isa<sim::DynamicArrayType, sim::QueueType>(
              currentCollection.getType())) {
        Type nestedType =
            isa<sim::DynamicArrayType>(currentCollection.getType())
                ? cast<sim::DynamicArrayType>(currentCollection.getType())
                      .getElementType()
                : cast<sim::QueueType>(currentCollection.getType())
                      .getElementType();
        nestedCollection = sim::SimContainerReadOp::create(
            builder, location, nestedType, currentCollection, sourceIndex);
      } else if (dimensionIndex + 1 != dimensions.size()) {
        if (auto fixed =
                dyn_cast<sim::UnpackedArrayType>(currentCollection.getType()))
          nestedCollection = sim::SimArrayDynExtractOp::create(
              builder, location, fixed.getElementType(), currentCollection,
              sourceIndex);
      }
      if (failed(emitDimension(dimensionIndex + 1, nestedCollection, step,
                               ValueRange{})))
        return failure();
      if (dimension.hasIterator) {
        if (hadPrevious)
          values[dimension.iteratorPath] = saved;
        else
          values.erase(dimension.iteratorPath);
      }
      emitBranch(step);

      setCurrent(step);
      Value next =
          arith::AddIOp::create(builder, location, ordinal, constant(1));
      cf::BranchOp::create(builder, location, header, ValueRange{next});
      setCurrent(localExit);
      if (parentStep)
        cf::BranchOp::create(builder, location, parentStep, parentStepOperands);
      return success();
    };

    if (failed(emitDimension(0, *collection, nullptr, ValueRange{})))
      return failure();
    setCurrent(exit);
    return success();
  }

  struct Dimension {
    int64_t left;
    int64_t right;
    uint64_t size;
    uint64_t stride;
    std::string iteratorPath;
    Type iteratorType;
  };
  SmallVector<Dimension> dimensions;
  for (Attribute attribute : op.getLoopDimensions()) {
    auto dimension = dyn_cast<DictionaryAttr>(attribute);
    auto hasIterator =
        dimension ? dimension.getAs<BoolAttr>(foreach_metadata::hasIterator)
                  : BoolAttr{};
    if (!hasIterator) {
      emitError(location) << "malformed foreach dimension metadata";
      return failure();
    }
    // An omitted iterator skips that dimension.
    if (!hasIterator.getValue())
      continue;
    auto hasRange = dimension.getAs<BoolAttr>(foreach_metadata::hasStaticRange);
    auto left = dimension.getAs<IntegerAttr>(foreach_metadata::left);
    auto right = dimension.getAs<IntegerAttr>(foreach_metadata::right);
    auto path = dimension.getAs<StringAttr>(foreach_metadata::iteratorPath);
    auto semanticIteratorType =
        dimension.getAs<TypeAttr>(foreach_metadata::iteratorType);
    if (!hasRange || !hasRange.getValue()) {
      emitError(location)
          << "runtime-sized foreach dimension survived simulation "
             "preparation";
      return failure();
    }
    if (!left || !right || !path || !semanticIteratorType) {
      emitError(location) << "malformed foreach dimension metadata";
      return failure();
    }
    FailureOr<Type> iteratorType =
        normalizeSemanticType(semanticIteratorType.getValue(), location);
    if (failed(iteratorType))
      return failure();

    int64_t leftValue = left.getInt();
    int64_t rightValue = right.getInt();
    llvm::APInt wideLeft(64, static_cast<uint64_t>(leftValue), true);
    llvm::APInt wideRight(64, static_cast<uint64_t>(rightValue), true);
    wideLeft = wideLeft.sext(65);
    wideRight = wideRight.sext(65);
    llvm::APInt distance =
        wideLeft.sge(wideRight) ? wideLeft - wideRight : wideRight - wideLeft;
    ++distance;
    if (distance.getActiveBits() > 64) {
      emitError(location) << "foreach dimension range is too large";
      return failure();
    }
    dimensions.push_back({leftValue, rightValue, distance.getZExtValue(), 0,
                          path.getValue().str(), *iteratorType});
  }
  if (dimensions.empty()) {
    emitError(location) << "foreach statement has no iterator";
    return failure();
  }

  uint64_t iterationCount = 1;
  for (Dimension &dimension : llvm::reverse(dimensions)) {
    dimension.stride = iterationCount;
    if (dimension.size != 0 &&
        iterationCount >
            std::numeric_limits<uint64_t>::max() / dimension.size) {
      emitError(location) << "foreach iteration space is too large";
      return failure();
    }
    iterationCount *= dimension.size;
  }

  Type indexType = builder.getI64Type();
  auto indexConstant = [&](uint64_t value) -> Value {
    return arith::ConstantOp::create(
        builder, location, indexType,
        builder.getIntegerAttr(indexType, llvm::APInt(64, value)));
  };
  Block *header = addBlock();
  header->addArgument(indexType, location);
  Block *body = addBlock();
  Block *step = addBlock();
  step->addArgument(indexType, location);
  Block *exit = addBlock();
  cf::BranchOp::create(builder, location, header, ValueRange{indexConstant(0)});

  setCurrent(header);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, header->getArgument(0),
      indexConstant(iterationCount));
  cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                           ValueRange{});

  setCurrent(body);
  struct SavedBinding {
    std::string path;
    Value value;
    bool existed;
  };
  SmallVector<SavedBinding> savedBindings;
  savedBindings.reserve(dimensions.size());
  for (const Dimension &dimension : dimensions) {
    auto previous = values.find(dimension.iteratorPath);
    savedBindings.push_back(
        {dimension.iteratorPath,
         previous == values.end() ? Value{} : previous->second,
         previous != values.end()});

    Value position = header->getArgument(0);
    if (dimension.stride != 1)
      position = arith::DivUIOp::create(builder, location, position,
                                        indexConstant(dimension.stride));
    position = arith::RemUIOp::create(builder, location, position,
                                      indexConstant(dimension.size));
    Value leftValue =
        arith::ConstantOp::create(builder, location, indexType,
                                  builder.getI64IntegerAttr(dimension.left));
    Value index = dimension.left <= dimension.right
                      ? Value(arith::AddIOp::create(builder, location,
                                                    leftValue, position))
                      : Value(arith::SubIOp::create(builder, location,
                                                    leftValue, position));
    FailureOr<Value> converted =
        convert(index, dimension.iteratorType, true, location, true);
    if (failed(converted))
      return failure();
    values[dimension.iteratorPath] = *converted;
  }

  loopTargets.push_back(
      {exit, step, {header->getArgument(0)}, controlScopes.size()});
  if (failed(lowerStatement(children[1])))
    return failure();
  if (current->empty() || !current->back().hasTrait<OpTrait::IsTerminator>())
    cf::BranchOp::create(builder, location, step,
                         ValueRange{header->getArgument(0)});
  loopTargets.pop_back();
  for (const SavedBinding &binding : savedBindings) {
    if (binding.existed)
      values[binding.path] = binding.value;
    else
      values.erase(binding.path);
  }

  setCurrent(step);
  Value next = arith::AddIOp::create(builder, location, step->getArgument(0),
                                     indexConstant(1));
  cf::BranchOp::create(builder, location, header, ValueRange{next});
  setCurrent(exit);
  return success();
}

LogicalResult UnitLowering::lowerRepeat(Operation *op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2) {
    unsupported(op) << " (repeat loop inventory)";
    return failure();
  }
  FailureOr<Value> count = lowerExpression(children[0]);
  if (failed(count))
    return failure();
  FailureOr<Value> scalar = toPackedScalar(*count, location);
  if (failed(scalar))
    return failure();
  Type countType = builder.getI64Type();
  FailureOr<Value> normalized =
      convert(*scalar, countType, isSignedNode(children[0]), location);
  if (failed(normalized))
    return failure();

  Block *header = addBlock();
  header->addArgument(countType, location);
  Block *body = addBlock();
  Block *step = addBlock();
  step->addArgument(countType, location);
  Block *exit = addBlock();
  cf::BranchOp::create(builder, location, header, ValueRange{*normalized});

  setCurrent(header);
  Value zero = arith::ConstantOp::create(builder, location, countType,
                                         builder.getI64IntegerAttr(0));
  Value more =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::sgt,
                            header->getArgument(0), zero);
  cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                           ValueRange{});

  loopTargets.push_back(
      {exit, step, {header->getArgument(0)}, controlScopes.size()});
  setCurrent(body);
  if (failed(lowerStatement(children[1])))
    return failure();
  if (current->empty() || !current->back().hasTrait<OpTrait::IsTerminator>())
    cf::BranchOp::create(builder, location, step,
                         ValueRange{header->getArgument(0)});

  setCurrent(step);
  Value one = arith::ConstantOp::create(builder, location, countType,
                                        builder.getI64IntegerAttr(1));
  Value remaining =
      arith::SubIOp::create(builder, location, step->getArgument(0), one);
  cf::BranchOp::create(builder, location, header, ValueRange{remaining});
  loopTargets.pop_back();
  setCurrent(exit);
  return success();
}

LogicalResult
UnitLowering::lowerVariableDeclaration(semantic::SVVariableDeclStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef path = op.getReferencedPath();
  Value initial = localDefaults.lookup(path);
  bool aggregateMemberInitializers =
      op->hasAttr("obelisk_sim.aggregate_member_initializers");
  auto initializeAggregateMembers = [&](Value destination) -> LogicalResult {
    auto referenceType = dyn_cast<sim::RefType>(destination.getType());
    if (!referenceType) {
      emitError(location)
          << "aggregate member initializer destination is not a reference";
      return failure();
    }
    Type aggregateType = referenceType.getElementType();
    for (Operation *child : children) {
      auto ordinalAttr = child->getAttrOfType<IntegerAttr>(
          "obelisk_sim.initialize_subelement");
      if (!ordinalAttr) {
        emitError(getSemanticLocation(child))
            << "aggregate member initializer has no field ordinal";
        return failure();
      }
      int64_t ordinal = ordinalAttr.getInt();
      if (ordinal < 0 || static_cast<uint64_t>(ordinal) >=
                             sim::getAggregateNumElements(aggregateType)) {
        emitError(getSemanticLocation(child))
            << "aggregate member initializer ordinal " << ordinal
            << " is out of range for " << aggregateType;
        return failure();
      }
      Type fieldType =
          sim::getAggregateElementType(aggregateType, unsigned(ordinal));
      FailureOr<Value> lowered = lowerExpression(child);
      if (failed(lowered))
        return failure();
      FailureOr<Value> converted = convert(
          *lowered, fieldType, isSignedNode(child), getSemanticLocation(child));
      if (failed(converted))
        return failure();
      Value fieldReference = sim::SimRefSubelementOp::create(
          builder, getSemanticLocation(child),
          sim::RefType::get(function.getContext(), fieldType), destination,
          builder.getDenseI64ArrayAttr({ordinal}));
      sim::SimRefStoreOp::create(builder, getSemanticLocation(child),
                                 *converted, fieldReference);
    }
    return success();
  };
  if (automaticLocals.contains(path)) {
    if (!initial) {
      emitError(location)
          << "automatic variable declaration has no frozen binding type";
      return failure();
    }
    if (!children.empty() && !aggregateMemberInitializers) {
      FailureOr<Value> lowered = lowerExpression(children.front());
      if (failed(lowered))
        return failure();
      FailureOr<Value> converted =
          convert(*lowered, initial.getType(), isSignedNode(children.front()),
                  location);
      if (failed(converted))
        return failure();
      initial = *converted;
    }
    Value destination = sim::SimRefAllocOp::create(
        builder, location,
        sim::RefType::get(function.getContext(), initial.getType()), initial);
    // An automatic declaration executes on each entry to this statement.
    // Keep that reset explicit so SSA promotion does not mistake the
    // allocator's default value for a once-per-function initialization when
    // this block is reentered after a suspension or through a loop backedge.
    sim::SimRefStoreOp::create(builder, location, initial, destination);
    values[path] = destination;
    lvalues[path] = destination;
    if (aggregateMemberInitializers)
      return initializeAggregateMembers(destination);
    return success();
  }
  // A named event of static lifetime is a design object whose cell already
  // exists (IEEE 1800-2017 6.21 and 15.5). Its declaration statement only
  // brings the name into scope; there is nothing to allocate or initialize,
  // and the event handle is bound rather than referenced.
  Value bound = values.lookup(path);
  if (children.empty() && bound && isa<sim::EventType>(bound.getType()))
    return success();
  Value destination = lvalues.lookup(path);
  if (!destination || !isa<sim::RefType>(destination.getType())) {
    emitError(location) << "variable declaration has no reference binding";
    return failure();
  }
  // Descriptor-backed static locals are initialized once by the root
  // initialization phase. The first direct activation claims a stable site;
  // later and concurrent activations skip the initializer.
  if (!initial && children.empty())
    return success();
  if (!initial) {
    auto siteIDAttr =
        op->getAttrOfType<IntegerAttr>("obelisk_sim.static_site_id");
    if (!siteIDAttr || !siteIDAttr.getValue().isStrictlyPositive()) {
      emitError(location)
          << "static declaration has no prepared initialization site ID";
      return failure();
    }
    uint64_t siteID = siteIDAttr.getValue().getZExtValue();
    Value first = sim::SimStaticOnceOp::create(
        builder, location, builder.getI64IntegerAttr(siteID));
    Block *initialize = addBlock();
    Block *continuation = addBlock();
    cf::CondBranchOp::create(builder, location, first, initialize, ValueRange{},
                             continuation, ValueRange{});
    setCurrent(initialize);
    if (aggregateMemberInitializers) {
      if (failed(initializeAggregateMembers(destination)))
        return failure();
    } else {
      FailureOr<Value> lowered = lowerExpression(children.front());
      if (failed(lowered))
        return failure();
      FailureOr<Value> converted = convert(
          *lowered, cast<sim::RefType>(destination.getType()).getElementType(),
          isSignedNode(children.front()), location);
      if (failed(converted))
        return failure();
      sim::SimRefStoreOp::create(builder, location, *converted, destination);
    }
    cf::BranchOp::create(builder, location, continuation);
    setCurrent(continuation);
    return success();
  }
  if (aggregateMemberInitializers)
    return initializeAggregateMembers(destination);
  if (!children.empty()) {
    FailureOr<Value> lowered = lowerExpression(children.front());
    if (failed(lowered))
      return failure();
    FailureOr<Value> converted = convert(
        *lowered, cast<sim::RefType>(destination.getType()).getElementType(),
        isSignedNode(children.front()), location);
    if (failed(converted))
      return failure();
    initial = *converted;
  }
  if (initial)
    sim::SimRefStoreOp::create(builder, location, initial, destination);
  return success();
}

FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>>
UnitLowering::outlineForkBranch(Operation *branch, uint64_t forkNode,
                                unsigned branchIndex, bool captureReferences) {
  auto design = function->getParentOfType<sim::SimDesignOp>();
  if (!design)
    return function.emitError("fork outlining requires a simulation design"),
           failure();

  Location location = getSemanticLocation(branch);
  MLIRContext *context = function.getContext();
  SmallVector<Type> inputs;
  SmallVector<Value> captures;
  SmallVector<DictionaryAttr> argumentAttrs;
  SmallVector<Attribute> bindings;

  Value processContext = function.getBody().front().getArgument(0);
  inputs.push_back(processContext.getType());
  captures.push_back(processContext);
  argumentAttrs.push_back(captureMetadata(builder, sim::CaptureKind::Context));

  llvm::StringSet<> capturedPaths;
  llvm::StringSet<> branchDeclarations;
  auto addCapture = [&](StringRef path) {
    if (!capturedPaths.insert(path).second)
      return;
    Value capture =
        captureReferences ? lvalues.lookup(path) : values.lookup(path);
    if (!capture)
      capture = captureReferences ? values.lookup(path) : lvalues.lookup(path);
    if (!capture)
      return;
    unsigned argument = inputs.size();
    inputs.push_back(capture.getType());
    captures.push_back(capture);
    // A captured entry argument may still name concrete design storage. Keep
    // its descriptor/view metadata on the outlined ABI so sampled reads and
    // effect analysis can recover the same canonical state range. Falling
    // back to Formal is appropriate only for lexical SSA values and values
    // produced inside the parent body.
    DictionaryAttr metadata;
    if (auto argument = dyn_cast<BlockArgument>(capture);
        argument && argument.getOwner() == &function.getBody().front())
      metadata = function.getArgAttrDict(argument.getArgNumber());
    if (!metadata)
      metadata = captureMetadata(builder, sim::CaptureKind::Formal);
    if (!isStaticallyAllocatedOverrideTarget(capture)) {
      SmallVector<NamedAttribute> entries(metadata.begin(), metadata.end());
      if (!metadata.contains("obelisk_sim.automatic_reference_capture"))
        entries.push_back(builder.getNamedAttr(
            "obelisk_sim.automatic_reference_capture", builder.getUnitAttr()));
      metadata = builder.getDictionaryAttr(entries);
    }
    argumentAttrs.push_back(metadata);
    bindings.push_back(sim::ArgumentBindingAttr::get(
        context, builder.getStringAttr(path), argument,
        sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{},
        /*copyIn=*/true));
  };
  ArrayAttr parentBindings =
      function->getAttrOfType<ArrayAttr>(bindingsAttrName);
  llvm::StringSet<> thisBindingPaths;
  if (parentBindings && thisObject)
    for (Attribute attribute : parentBindings) {
      auto argument = dyn_cast<sim::ArgumentBindingAttr>(attribute);
      if (argument && argument.getArgument() < function.getNumArguments() &&
          function.getArgument(argument.getArgument()) == thisObject)
        thisBindingPaths.insert(argument.getPath().getValue());
    }
  llvm::StringSet<> referencedPaths;
  bool branchUsesThis = false;
  branch->walk([&](Operation *nested) {
    StringRef path;
    if (auto declaration =
            dyn_cast<semantic::SVVariableDeclStatementOp>(nested)) {
      path = declaration.getReferencedPath();
      if (!path.empty())
        branchDeclarations.insert(path);
    } else if (auto named =
                   dyn_cast<semantic::SVNamedValueExpressionOp>(nested)) {
      path = named.getReferencedPath();
      branchUsesThis |= named->hasAttr("obelisk_sim.class_field");
    } else if (auto hierarchical =
                   dyn_cast<semantic::SVHierarchicalValueExpressionOp>(nested))
      path = hierarchical.getReferencedPath();
    else if (auto member =
                 dyn_cast<semantic::SVMemberAccessExpressionOp>(nested)) {
      if (member->hasAttr(staticClassPropertyAttrName))
        path = member.getReferencedPath();
    } else if (auto instance =
                   dyn_cast<semantic::SVAssertionInstanceExpressionOp>(
                       nested)) {
      auto type = instance->getAttrOfType<TypeAttr>("semantic_type");
      if (type && isa<semantic::SequenceType>(type.getValue()))
        path = instance.getReferencedPath();
    }
    if (!path.empty())
      referencedPaths.insert(path);
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(nested);
        call && call->hasAttr("obelisk_sim.class_instance")) {
      auto formals = call->getAttrOfType<ArrayAttr>(calleeFormalsAttrName);
      bool superCall = call->hasAttr("obelisk_sim.class_super");
      branchUsesThis |=
          superCall || (formals && getChildren(call).size() == formals.size());
    }
    if (auto callCaptures =
            nested->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
      for (Attribute capture : callCaptures)
        referencedPaths.insert(cast<StringAttr>(capture).getValue());
    if (auto observerCaptures =
            nested->getAttrOfType<ArrayAttr>(observerCapturesAttrName))
      for (Attribute capture : observerCaptures) {
        StringRef path = cast<StringAttr>(capture).getValue();
        referencedPaths.insert(path);
        branchUsesThis |= thisBindingPaths.contains(path);
      }
  });
  std::optional<unsigned> outlinedThisArgument;
  if (branchUsesThis && thisObject) {
    outlinedThisArgument = inputs.size();
    inputs.push_back(thisObject.getType());
    captures.push_back(thisObject);
    argumentAttrs.push_back(captureMetadata(builder, sim::CaptureKind::Formal));
    // Preserve the receiver's frozen path as an alias of the distinguished
    // `this` argument. Observer and callee capture inventories resolve by
    // path, while ordinary implicit member accesses use thisArgument.
    for (const auto &entry : thisBindingPaths)
      if (capturedPaths.insert(entry.getKey()).second)
        bindings.push_back(sim::ArgumentBindingAttr::get(
            context, builder.getStringAttr(entry.getKey()),
            *outlinedThisArgument, sim::UnitArgumentKind::Direct,
            /*copyOut=*/false, IntegerAttr{}, /*copyIn=*/true));
  }
  if (parentBindings)
    for (Attribute attribute : parentBindings) {
      StringRef path = sim::getUnitBindingPath(attribute);
      if (path.empty())
        continue;
      // Only pass bindings actually referenced by the branch or a direct
      // callee. This also excludes pattern variables from unrelated matches.
      if (!referencedPaths.contains(path))
        continue;
      // An automatic declaration nested in this branch belongs to the child
      // process activation. Preserve its frozen type and lifetime contract so
      // the child allocates it at the declaration. Capturing a parent
      // reference here would incorrectly share storage between activations.
      if (auto local = dyn_cast<sim::LocalBindingAttr>(attribute);
          local && local.getAutomatic() && branchDeclarations.contains(path)) {
        if (capturedPaths.insert(path).second)
          bindings.push_back(attribute);
        continue;
      }
      // Elaborated constants are immutable compile-time facts, not runtime
      // ABI captures. Preserve their typed binding on the outlined child.
      if (isa<sim::ConstantBindingAttr>(attribute)) {
        if (capturedPaths.insert(path).second)
          bindings.push_back(attribute);
        continue;
      }
      // Descriptor-backed storage is available from the process context in
      // every outlined child. Preserve the binding instead of threading a
      // reference through the spawn ABI.
      if (isa<sim::DescriptorBindingAttr>(attribute)) {
        if (capturedPaths.insert(path).second)
          bindings.push_back(attribute);
        continue;
      }
      addCapture(path);
    }

  // Foreach iterators and other lexical SSA bindings have no frozen function
  // binding entry. Capture any such path referenced by the branch explicitly.
  SmallVector<StringRef> lexicalPaths;
  lexicalPaths.reserve(referencedPaths.size());
  for (const auto &entry : referencedPaths)
    if (!capturedPaths.contains(entry.getKey()))
      lexicalPaths.push_back(entry.getKey());
  llvm::sort(lexicalPaths);
  for (StringRef path : lexicalPaths)
    addCapture(path);

  uint64_t ordinal = nextForkOrdinal++;
  std::string symbol = (function.getSymName() + ".fork." + Twine(forkNode) +
                        "." + Twine(ordinal) + "." + Twine(branchIndex))
                           .str();
  uint64_t parentID = function.getCodeUnitId().value_or(0);
  uint64_t scopeID = 0;
  std::string parentHierarchy = function.getSymName().str();
  for (sim::SimCodeUnitDeclOp declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>()) {
    if (declaration.getId() != parentID)
      continue;
    scopeID = declaration.getScopeId();
    parentHierarchy = declaration.getHierarchicalName().str();
    break;
  }
  std::string hierarchy = (Twine(parentHierarchy) + ".$fork." +
                           Twine(forkNode) + "." + Twine(branchIndex))
                              .str();
  auto codeUnitIDAttr =
      branch->getAttrOfType<IntegerAttr>("obelisk_sim.fork_code_unit_id");
  if (!codeUnitIDAttr || !codeUnitIDAttr.getValue().isStrictlyPositive())
    return emitError(location) << "fork branch has no prepared code-unit ID",
           failure();
  uint64_t codeUnitID = codeUnitIDAttr.getValue().getZExtValue();

  OpBuilder outlineBuilder(function);
  outlineBuilder.setInsertionPoint(function);
  sim::SimCodeUnitDeclOp::create(outlineBuilder, location, codeUnitID, scopeID,
                                 sim::EntryKind::Fork,
                                 outlineBuilder.getStringAttr(hierarchy),
                                 outlineBuilder.getStringAttr("fork branch"),
                                 outlineBuilder.getUnitAttr());

  SmallVector<NamedAttribute> attributes{
      outlineBuilder.getNamedAttr(bindingsAttrName,
                                  outlineBuilder.getArrayAttr(bindings)),
      outlineBuilder.getNamedAttr("code_unit_id",
                                  outlineBuilder.getI64IntegerAttr(codeUnitID)),
      outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
  };
  if (outlinedThisArgument)
    attributes.push_back(outlineBuilder.getNamedAttr(
        sim::metadata::thisArgument,
        outlineBuilder.getI32IntegerAttr(*outlinedThisArgument)));
  SmallVector<Attribute> inheritedControls;
  llvm::StringMap<uint64_t> inherited = inheritedControlIDs;
  for (const ControlScope &scope : controlScopes)
    inherited[scope.path] = scope.targetID;
  SmallVector<StringRef> inheritedPaths;
  inheritedPaths.reserve(inherited.size());
  for (const auto &entry : inherited)
    inheritedPaths.push_back(entry.getKey());
  llvm::sort(inheritedPaths);
  for (StringRef path : inheritedPaths)
    inheritedControls.push_back(outlineBuilder.getDictionaryAttr({
        outlineBuilder.getNamedAttr("path", outlineBuilder.getStringAttr(path)),
        outlineBuilder.getNamedAttr(
            "id", outlineBuilder.getI64IntegerAttr(inherited.lookup(path))),
    }));
  if (!inheritedControls.empty())
    attributes.push_back(outlineBuilder.getNamedAttr(
        "inherited_controls", outlineBuilder.getArrayAttr(inheritedControls)));
  const StringRef inheritedAttributes[] = {
      delayScaleAttrName, delayQuantumAttrName, "home_region", "domain"};
  for (StringRef name : inheritedAttributes)
    if (Attribute attribute = function->getAttr(name))
      attributes.push_back(outlineBuilder.getNamedAttr(name, attribute));
  attributes.push_back(
      outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                  outlineBuilder.getStringAttr(hierarchy)));

  auto outlined =
      sim::SimFuncOp::create(outlineBuilder, location, symbol,
                             FunctionType::get(context, inputs, TypeRange{}),
                             sim::EntryKind::Fork, attributes, argumentAttrs);
  SymbolTable::setSymbolVisibility(outlined, SymbolTable::Visibility::Private);

  OpBuilder bodyBuilder = OpBuilder::atBlockEnd(&outlined.getBody().front());
  Operation *root = bodyBuilder.clone(*branch);
  UnitLowering nested(outlined);
  if (failed(nested.lower({root}))) {
    outlined.erase();
    return failure();
  }
  root->erase();
  outlined->setAttr(sim::metadata::lowered, builder.getUnitAttr());
  return std::make_pair(outlined, std::move(captures));
}

FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>>
UnitLowering::outlinePostponedDisplay(semantic::SVCallExpressionOp call,
                                      StringRef immediateName,
                                      bool persistent) {
  uint64_t ordinal = nextPostponedOrdinal++;
  uint64_t node = call->getAttrOfType<IntegerAttr>("node_id")
                      ? call->getAttrOfType<IntegerAttr>("node_id")
                            .getValue()
                            .getZExtValue()
                      : ordinal;
  std::string identity = (function.getSymName() + ".$postponed." + Twine(node) +
                          "." + Twine(ordinal))
                             .str();

  Attribute previousForkID = call->getAttr("obelisk_sim.fork_code_unit_id");
  StringAttr previousName = call.getCalleeNameAttr();
  call->setAttr("obelisk_sim.fork_code_unit_id",
                builder.getI64IntegerAttr(stableCodeUnitID(identity)));
  call->setAttr("callee_name", builder.getStringAttr(immediateName));
  FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> outlined =
      outlineForkBranch(call, node, static_cast<unsigned>(ordinal),
                        /*captureReferences=*/true);
  call->setAttr("callee_name", previousName);
  if (previousForkID)
    call->setAttr("obelisk_sim.fork_code_unit_id", previousForkID);
  else
    call->removeAttr("obelisk_sim.fork_code_unit_id");
  if (failed(outlined))
    return failure();

  sim::SimFuncOp callback = outlined->first;
  callback->setAttr("home_region",
                    sim::EventRegionAttr::get(function.getContext(),
                                              sim::EventRegion::Postponed));
  callback->setAttr(
      "domain", sim::ExecutionDomainAttr::get(function.getContext(),
                                              sim::ExecutionDomain::Design));
  if (!persistent)
    return outlined;

  Block &entry = callback.getBody().front();
  Block *loop = entry.splitBlock(entry.begin());
  SmallVector<sim::SimReturnOp> returns;
  callback.walk([&](sim::SimReturnOp op) { returns.push_back(op); });

  Block *dispatch = new Block;
  callback.getBody().getBlocks().insert(loop->getIterator(), dispatch);
  Block *stale = new Block;
  callback.getBody().push_back(stale);
  OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
  cf::BranchOp::create(entryBuilder, callback.getLoc(), dispatch);
  OpBuilder dispatchBuilder = OpBuilder::atBlockEnd(dispatch);
  Value current = sim::SimMonitorCurrentOp::create(
      dispatchBuilder, callback.getLoc(), dispatchBuilder.getI1Type());
  cf::CondBranchOp::create(dispatchBuilder, callback.getLoc(), current, loop,
                           stale);

  SmallVector<Value> watched;
  for (BlockArgument argument : entry.getArguments().drop_front())
    if (isa<sim::RefType, sim::NetType>(argument.getType()))
      watched.push_back(argument);
  for (sim::SimReturnOp returnOp : returns) {
    OpBuilder waitBuilder(returnOp);
    if (watched.empty()) {
      sim::SimSuspendForeverOp::create(
          waitBuilder, returnOp.getLoc(), ValueRange{},
          sim::ContinuationSiteAttr{},
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Postponed),
          dispatch);
    } else if (watched.size() == 1) {
      sim::SimSuspendChangeOp::create(
          waitBuilder, returnOp.getLoc(), watched.front(), ValueRange{},
          sim::ContinuationSiteAttr{},
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Postponed),
          dispatch);
    } else {
      SmallVector<int32_t> edges(watched.size(),
                                 static_cast<int32_t>(sim::EdgeKind::Change));
      sim::SimSuspendAnyOp::create(
          waitBuilder, returnOp.getLoc(), watched,
          waitBuilder.getDenseI32ArrayAttr(edges), sim::ContinuationSiteAttr{},
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Postponed),
          dispatch);
    }
    returnOp.erase();
  }
  OpBuilder staleBuilder = OpBuilder::atBlockEnd(stale);
  sim::SimReturnOp::create(staleBuilder, callback.getLoc(), ValueRange{});
  return outlined;
}

LogicalResult UnitLowering::lowerFork(semantic::SVBlockStatementOp op) {
  Location location = getSemanticLocation(op);
  if (function.getEntryKind() == sim::EntryKind::Function &&
      op.getBlockKind() != semantic::SVStatementBlockKind::JoinNone) {
    emitError(location) << "a fork in a zero-time function must use join_none";
    return failure();
  }
  SmallVector<Operation *> contents = getChildren(op);
  SmallVector<Operation *> branches;
  if (contents.size() == 1 &&
      isa<semantic::SVStatementListOp>(contents.front()))
    branches = getChildren(contents.front());
  else
    branches = contents;

  // Declarations in the fork block are initialized in lexical order before
  // any child starts. Slang places them before the branch statements.
  while (!branches.empty() &&
         isa<semantic::SVVariableDeclStatementOp>(branches.front())) {
    if (failed(lowerVariableDeclaration(
            cast<semantic::SVVariableDeclStatementOp>(branches.front()))))
      return failure();
    branches.erase(branches.begin());
  }

  uint64_t forkNode =
      op->getAttrOfType<IntegerAttr>("node_id")
          ? op->getAttrOfType<IntegerAttr>("node_id").getValue().getZExtValue()
          : nextForkOrdinal;
  SmallVector<Value> processes;
  for (auto [index, branch] : llvm::enumerate(branches)) {
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> outlined =
        outlineForkBranch(branch, forkNode, index);
    if (failed(outlined))
      return failure();
    processes.push_back(sim::SimSpawnOp::create(
                            builder, location, outlined->first.getSymNameAttr(),
                            outlined->second, ArrayAttr{}, ArrayAttr{})
                            .getProcess());
  }

  semantic::SVStatementBlockKind kind = op.getBlockKind();
  if (kind == semantic::SVStatementBlockKind::JoinNone || processes.empty())
    return success();
  Block *continuation = addBlock();
  sim::JoinKind joinKind = kind == semantic::SVStatementBlockKind::JoinAny
                               ? sim::JoinKind::Any
                               : sim::JoinKind::All;
  sim::SimSuspendJoinOp::create(builder, location, joinKind, processes,
                                processes.size(), sim::ContinuationSiteAttr{},
                                sim::EventRegionAttr{}, continuation);
  setCurrent(continuation);
  return success();
}

LogicalResult UnitLowering::lowerBlock(semantic::SVBlockStatementOp op) {
  auto path = op.getBlockPathAttr();
  SmallVector<Operation *> contents = getChildren(op);
  auto lowerContents = [&]() {
    // A statement block is a lexical scope. Keep bindings introduced by the
    // block (and bindings that shadow an outer declaration) from leaking into
    // the lowering of following statements. The values themselves remain in
    // SSA; only name resolution returns to the enclosing scope.
    llvm::StringMap<Value> enclosingValues = values;
    llvm::StringMap<Value> enclosingLValues = lvalues;
    llvm::scope_exit restoreBindings([&] {
      values = std::move(enclosingValues);
      lvalues = std::move(enclosingLValues);
    });
    if (op.getBlockKind() == semantic::SVStatementBlockKind::Sequential)
      return lowerSequence(contents);
    return lowerFork(op);
  };
  if (!path)
    return lowerContents();

  // Slang represents a labeled concurrent assertion as a synthetic named
  // statement block. The label names the assertion instance for assertion
  // control; it is not a procedural `begin : name` activation and must not
  // remain entered while the assertion monitor waits forever on its clock.
  if (op.getBlockKind() == semantic::SVStatementBlockKind::Sequential &&
      contents.size() == 1 &&
      isa<semantic::SVConcurrentAssertionStatementOp>(contents.front()))
    return lowerContents();

  // A label on an immediate assertion names that assertion, not an enclosing
  // procedural block. Preserve the prepared target identity on the assertion
  // so a later `disable label` can cancel its pending deferred report without
  // entering a dynamic control activation around the assertion statement.
  if (op.getBlockKind() == semantic::SVStatementBlockKind::Sequential &&
      contents.size() == 1 &&
      isa<semantic::SVImmediateAssertionStatementOp>(contents.front())) {
    auto targetID =
        op->getAttrOfType<IntegerAttr>("obelisk_sim.control_target_id");
    if (!targetID || !targetID.getValue().isStrictlyPositive())
      return emitError(getSemanticLocation(op))
                 << "labeled assertion has no prepared control ID",
             failure();
    contents.front()->setAttr("obelisk_sim.assertion_control_target_id",
                              targetID);
    return lowerContents();
  }

  Location location = getSemanticLocation(op);
  auto targetIDAttr =
      op->getAttrOfType<IntegerAttr>("obelisk_sim.control_target_id");
  if (!targetIDAttr || !targetIDAttr.getValue().isStrictlyPositive()) {
    emitError(location) << "named block has no prepared control ID";
    return failure();
  }
  uint64_t targetID = targetIDAttr.getValue().getZExtValue();
  Value activation = sim::SimControlEnterOp::create(
      builder, location, builder.getI64IntegerAttr(targetID));
  Block *exit = addBlock();
  controlScopes.push_back({path.getValue().str(), targetID, activation, exit});
  LogicalResult result = lowerContents();
  controlScopes.pop_back();
  if (failed(result))
    return failure();
  if (current->empty() || !current->back().hasTrait<OpTrait::IsTerminator>()) {
    sim::SimControlLeaveOp::create(builder, location, activation);
    cf::BranchOp::create(builder, location, exit);
  }
  setCurrent(exit);
  return success();
}

LogicalResult UnitLowering::lowerDisable(semantic::SVDisableStatementOp op) {
  Location location = getSemanticLocation(op);
  auto path = op.getTargetPathAttr();
  if (!path) {
    unsupported(op) << " (unresolved disable target)";
    return failure();
  }
  auto targetIDAttr =
      op->getAttrOfType<IntegerAttr>("obelisk_sim.control_target_id");
  if (!targetIDAttr || !targetIDAttr.getValue().isStrictlyPositive()) {
    emitError(location) << "disable has no prepared control ID";
    return failure();
  }
  uint64_t targetID = targetIDAttr.getValue().getZExtValue();
  bool hierarchical = op.getIsHierarchical();
  auto assertion = assertionControlIDs.find(path.getValue());
  if (assertion != assertionControlIDs.end() && assertion->second == targetID) {
    sim::SimControlDisableOp::create(
        builder, location, builder.getI64IntegerAttr(targetID), Value{},
        builder.getBoolAttr(hierarchical));
    return success();
  }
  for (const ControlScope &scope : llvm::reverse(controlScopes)) {
    if (scope.path == path.getValue()) {
      sim::SimControlDisableOp::create(
          builder, location, builder.getI64IntegerAttr(targetID),
          hierarchical ? Value{} : scope.activation,
          builder.getBoolAttr(hierarchical));
      cf::BranchOp::create(builder, location, scope.exit);
      setCurrent(addBlock());
      return success();
    }
  }

  // A statement block in another repeating procedural activation needs a
  // resumable exit continuation in that activation. Cancelling the complete
  // logical process would suppress its next iteration. Initial/final targets
  // can be cancelled outright, and inherited fork controls use the return
  // path below, so reject only this genuinely unsafe boundary.
  if (op->hasAttr(
          "obelisk_sim.nonlocal_repeating_statement_block_target")) {
    emitError(location)
        << "disable of a nonlocal statement block is not executable yet";
    return failure();
  }

  if (inheritedControlIDs.contains(path.getValue())) {
    sim::SimControlDisableOp::create(
        builder, location, builder.getI64IntegerAttr(targetID), Value{},
        builder.getBoolAttr(hierarchical));
    sim::SimReturnOp::create(builder, location, ValueRange{});
    setCurrent(addBlock());
    return success();
  }

  // A resolved target outside the lexical activation stack is hierarchical:
  // disable every live activation of that exact elaborated block identity.
  sim::SimControlDisableOp::create(builder, location,
                                   builder.getI64IntegerAttr(targetID), Value{},
                                   builder.getBoolAttr(true));
  return success();
}

} // namespace obelisk::simlowering
