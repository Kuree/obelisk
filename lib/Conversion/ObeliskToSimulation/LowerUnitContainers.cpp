//===- LowerUnitContainers.cpp - Lower container methods ---------------===//

#include "LowerUnit.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "llvm/ADT/StringSwitch.h"

#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

namespace {

enum class ArrayMethod {
  Unknown,
  Size,
  Num,
  Exists,
  Delete,
  First,
  Last,
  Next,
  Prev,
  Insert,
  PopBack,
  PopFront,
  PushBack,
  PushFront,
  Reverse,
  Shuffle,
  Sort,
  RSort,
  Sum,
  Product,
  And,
  Or,
  Xor,
  Find,
  FindIndex,
  FindFirst,
  FindFirstIndex,
  FindLast,
  FindLastIndex,
  Map,
  Min,
  Max,
  Unique,
  UniqueIndex,
};

ArrayMethod classifyArrayMethod(StringRef name) {
  return llvm::StringSwitch<ArrayMethod>(name)
      .Case("size", ArrayMethod::Size)
      .Case("num", ArrayMethod::Num)
      .Case("exists", ArrayMethod::Exists)
      .Case("delete", ArrayMethod::Delete)
      .Case("first", ArrayMethod::First)
      .Case("last", ArrayMethod::Last)
      .Case("next", ArrayMethod::Next)
      .Case("prev", ArrayMethod::Prev)
      .Case("insert", ArrayMethod::Insert)
      .Case("pop_back", ArrayMethod::PopBack)
      .Case("pop_front", ArrayMethod::PopFront)
      .Case("push_back", ArrayMethod::PushBack)
      .Case("push_front", ArrayMethod::PushFront)
      .Case("reverse", ArrayMethod::Reverse)
      .Case("shuffle", ArrayMethod::Shuffle)
      .Case("sort", ArrayMethod::Sort)
      .Case("rsort", ArrayMethod::RSort)
      .Case("sum", ArrayMethod::Sum)
      .Case("product", ArrayMethod::Product)
      .Case("and", ArrayMethod::And)
      .Case("or", ArrayMethod::Or)
      .Case("xor", ArrayMethod::Xor)
      .Case("find", ArrayMethod::Find)
      .Case("find_index", ArrayMethod::FindIndex)
      .Case("find_first", ArrayMethod::FindFirst)
      .Case("find_first_index", ArrayMethod::FindFirstIndex)
      .Case("find_last", ArrayMethod::FindLast)
      .Case("find_last_index", ArrayMethod::FindLastIndex)
      .Case("map", ArrayMethod::Map)
      .Case("min", ArrayMethod::Min)
      .Case("max", ArrayMethod::Max)
      .Case("unique", ArrayMethod::Unique)
      .Case("unique_index", ArrayMethod::UniqueIndex)
      .Default(ArrayMethod::Unknown);
}

} // namespace

FailureOr<Value> UnitLowering::lowerArrayMethod(semantic::SVCallExpressionOp op,
                                                Value receiverOverride,
                                                Value iteratorKeys) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  bool withClause = op.getHasIteratorExpression();
  if ((withClause && children.size() != 2u) ||
      (!withClause && children.empty())) {
    emitError(location) << "malformed array-method expression inventory";
    return failure();
  }
  Operation *receiverNode = withClause ? children.back() : children.front();
  Operation *clause = withClause ? children.front() : nullptr;
  StringRef methodName = op.getCalleeName();
  ArrayMethod method = classifyArrayMethod(methodName);
  bool mutatesReceiver =
      method == ArrayMethod::Delete || method == ArrayMethod::Reverse ||
      method == ArrayMethod::Shuffle || method == ArrayMethod::Sort ||
      method == ArrayMethod::RSort || method == ArrayMethod::PushBack ||
      method == ArrayMethod::PushFront || method == ArrayMethod::PopFront ||
      method == ArrayMethod::PopBack || method == ArrayMethod::Insert;
  FailureOr<Type> semanticReceiverType =
      getNormalizedSemanticType(receiverNode);
  if (failed(semanticReceiverType))
    return failure();
  Type sourceElementType;
  if (auto sourceType =
          receiverNode->getAttrOfType<TypeAttr>("semantic_type")) {
    Type type = sourceType.getValue();
    if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type))
      sourceElementType = array.getElementType();
    else if (auto array = dyn_cast<semantic::UnpackedArrayType>(type))
      sourceElementType = array.getElementType();
    else if (auto array = dyn_cast<semantic::DynArrayType>(type))
      sourceElementType = array.getElementType();
    else if (auto array = dyn_cast<semantic::AssocArrayType>(type))
      sourceElementType = array.getElementType();
    else if (auto queue = dyn_cast<semantic::QueueType>(type))
      sourceElementType = queue.getElementType();
  }
  bool elementSigned =
      sourceElementType && isSignedSemanticType(sourceElementType);
  bool fixedReceiver = isa<sim::UnpackedArrayType>(*semanticReceiverType);
  if (fixedReceiver && mutatesReceiver) {
    emitError(location) << "fixed-array ordering methods are not executable "
                           "yet: "
                        << methodName;
    return failure();
  }
  FailureOr<Value> receiver;
  if (receiverOverride) {
    receiver = receiverOverride;
  } else if (mutatesReceiver) {
    FailureOr<Value> reference = lowerExpression(receiverNode, true);
    FailureOr<Value> loaded = succeeded(reference)
                                  ? loadReference(*reference, location)
                                  : FailureOr<Value>(failure());
    if (failed(reference) || failed(loaded))
      return failure();
    Value updated = cloneSequentialValue(*loaded, location);
    FailureOr<Value> allocated = ensureSequentialContainer(updated, location);
    if (failed(allocated))
      return failure();
    updated = *allocated;
    if (isa<sim::RefType>((*reference).getType()))
      sim::SimRefStoreOp::create(builder, location, updated, *reference);
    else if (isa<sim::ManagedRefType>((*reference).getType()))
      sim::SimManagedStoreOp::create(builder, location, updated, *reference);
    else if (isa<sim::ArgumentRefType>((*reference).getType()))
      sim::SimArgumentRefStoreOp::create(builder, location, updated,
                                         *reference);
    else
      return failure();
    receiver = updated;
  } else {
    receiver = lowerExpression(receiverNode);
  }
  if (failed(receiver))
    return failure();
  Type receiverType = (*receiver).getType();
  Type elementType;
  sim::UnpackedArrayType fixedArray;
  if (auto array = dyn_cast<sim::DynamicArrayType>(receiverType))
    elementType = array.getElementType();
  else if (auto queue = dyn_cast<sim::QueueType>(receiverType))
    elementType = queue.getElementType();
  else if ((fixedArray = dyn_cast<sim::UnpackedArrayType>(receiverType)))
    elementType = fixedArray.getElementType();
  else
    return failure();

  if (method == ArrayMethod::Size) {
    if (withClause || children.size() != 1)
      return emitError(location) << "size does not accept arguments or a "
                                    "with clause",
             failure();
    Value size = fixedArray
                     ? Value(arith::ConstantOp::create(
                           builder, location, builder.getI64Type(),
                           builder.getI64IntegerAttr(
                               sim::getAggregateNumElements(fixedArray))))
                     : Value(sim::SimContainerSizeOp::create(
                           builder, location, builder.getI64Type(), *receiver));
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    return failed(resultType) ? FailureOr<Value>(failure())
                              : convert(size, *resultType, false, location);
  }
  if (method == ArrayMethod::PushBack) {
    if (withClause || children.size() != 2)
      return emitError(location)
                 << "push_back requires exactly one value argument",
             failure();
    auto queue = dyn_cast<sim::QueueType>(receiverType);
    if (!queue)
      return emitError(location) << "push_back requires a queue receiver",
             failure();
    FailureOr<Value> value = lowerExpression(children[1]);
    FailureOr<Value> converted =
        succeeded(value)
            ? convert(*value, elementType, isSignedNode(children[1]), location)
            : FailureOr<Value>(failure());
    if (failed(converted))
      return failure();
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    sim::SimContainerWriteOp::create(builder, location, *receiver, size,
                                     *converted);
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  if (method == ArrayMethod::PushFront) {
    if (withClause || children.size() != 2)
      return emitError(location)
                 << "push_front requires exactly one value argument",
             failure();
    auto queue = dyn_cast<sim::QueueType>(receiverType);
    if (!queue)
      return emitError(location) << "push_front requires a queue receiver",
             failure();
    FailureOr<Value> value = lowerExpression(children[1]);
    FailureOr<Value> converted =
        succeeded(value)
            ? convert(*value, elementType, isSignedNode(children[1]), location)
            : FailureOr<Value>(failure());
    if (failed(converted))
      return failure();
    if (queue.getBound()) {
      Value size = sim::SimContainerSizeOp::create(
          builder, location, builder.getI64Type(), *receiver);
      Value capacity = arith::ConstantOp::create(
          builder, location, builder.getI64Type(),
          builder.getI64IntegerAttr(static_cast<uint64_t>(queue.getBound()) +
                                    1));
      Value full = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::uge, size, capacity);
      Block *trim = addBlock();
      Block *insert = addBlock();
      cf::CondBranchOp::create(builder, location, full, trim, ValueRange{},
                               insert, ValueRange{});
      setCurrent(trim);
      Value one =
          arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                    builder.getI64IntegerAttr(1));
      Value last = arith::SubIOp::create(builder, location, size, one);
      sim::SimQueueDeleteOp::create(builder, location, *receiver, last);
      cf::BranchOp::create(builder, location, insert);
      setCurrent(insert);
    }
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    sim::SimQueueInsertOp::create(builder, location, *receiver, zero,
                                  *converted);
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  if (method == ArrayMethod::PopFront) {
    if (withClause || children.size() != 1)
      return emitError(location) << "pop_front does not accept arguments",
             failure();
    auto queue = dyn_cast<sim::QueueType>(receiverType);
    if (!queue)
      return emitError(location) << "pop_front requires a queue receiver",
             failure();
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    Value value = sim::SimContainerReadOp::create(builder, location,
                                                  elementType, *receiver, zero);
    sim::SimQueueDeleteOp::create(builder, location, *receiver, zero);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    return failed(resultType)
               ? FailureOr<Value>(failure())
               : convert(value, *resultType, isSignedNode(op), location);
  }
  if (method == ArrayMethod::PopBack) {
    if (withClause || children.size() != 1)
      return emitError(location) << "pop_back does not accept arguments",
             failure();
    auto queue = dyn_cast<sim::QueueType>(receiverType);
    if (!queue)
      return emitError(location) << "pop_back requires a queue receiver",
             failure();
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
    Value index = arith::SubIOp::create(builder, location, size, one);
    Value value = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, index);
    sim::SimQueueDeleteOp::create(builder, location, *receiver, index);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    return failed(resultType)
               ? FailureOr<Value>(failure())
               : convert(value, *resultType, isSignedNode(op), location);
  }
  if (method == ArrayMethod::Insert) {
    if (withClause || children.size() != 3)
      return emitError(location)
                 << "insert requires exactly an index and a value",
             failure();
    auto queue = dyn_cast<sim::QueueType>(receiverType);
    if (!queue)
      return emitError(location) << "insert requires a queue receiver",
             failure();
    FailureOr<Value> index = lowerExpression(children[1]);
    FailureOr<Value> value = lowerExpression(children[2]);
    FailureOr<Value> convertedIndex =
        succeeded(index) ? convert(*index, builder.getI64Type(),
                                   isSignedNode(children[1]), location)
                         : FailureOr<Value>(failure());
    FailureOr<Value> convertedValue =
        succeeded(value)
            ? convert(*value, elementType, isSignedNode(children[2]), location)
            : FailureOr<Value>(failure());
    if (failed(convertedIndex) || failed(convertedValue))
      return failure();
    sim::SimQueueInsertOp::create(builder, location, *receiver, *convertedIndex,
                                  *convertedValue);
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  if (method == ArrayMethod::Delete) {
    if (withClause || children.size() > 2)
      return emitError(location)
                 << "delete accepts at most one index and no with clause",
             failure();
    if (children.size() == 1) {
      sim::SimContainerDeleteOp::create(builder, location, *receiver);
    } else {
      auto queue = dyn_cast<sim::QueueType>(receiverType);
      if (!queue)
        return emitError(location)
                   << "indexed delete requires a queue receiver",
               failure();
      FailureOr<Value> index = lowerExpression(children[1]);
      FailureOr<Value> converted =
          succeeded(index) ? convert(*index, builder.getI64Type(),
                                     isSignedNode(children[1]), location)
                           : FailureOr<Value>(failure());
      if (failed(converted))
        return failure();
      sim::SimQueueDeleteOp::create(builder, location, *receiver, *converted);
    }
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }

  auto iteratorPath = [&]() -> FailureOr<StringRef> {
    if (!withClause)
      return StringRef{};
    auto path = op->getAttrOfType<StringAttr>("iterator_variable_path");
    if (!path) {
      emitError(location)
          << "array method with clause has no iterator-variable path";
      return failure();
    }
    return path.getValue();
  };
  auto bindIterator = [&](StringRef path, Value element, Value index) {
    if (path.empty())
      return;
    values[path] = element;
    iteratorIndices[path] = index;
  };
  struct SavedIterator {
    std::string path;
    Value value;
    Value index;
    bool hadValue = false;
    bool hadIndex = false;
  };
  auto saveIterator = [&](StringRef path) {
    SavedIterator saved;
    saved.path = path.str();
    if (auto found = values.find(path); found != values.end()) {
      saved.value = found->second;
      saved.hadValue = true;
    }
    if (auto found = iteratorIndices.find(path);
        found != iteratorIndices.end()) {
      saved.index = found->second;
      saved.hadIndex = true;
    }
    return saved;
  };
  auto restoreIterator = [&](const SavedIterator &saved) {
    if (saved.path.empty())
      return;
    if (saved.hadValue)
      values[saved.path] = saved.value;
    else
      values.erase(saved.path);
    if (saved.hadIndex)
      iteratorIndices[saved.path] = saved.index;
    else
      iteratorIndices.erase(saved.path);
  };
  auto indexConstant = [&](uint64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                     builder.getI64IntegerAttr(value));
  };
  auto inputSize = [&]() -> Value {
    if (fixedArray)
      return indexConstant(sim::getAggregateNumElements(fixedArray));
    return sim::SimContainerSizeOp::create(builder, location,
                                           builder.getI64Type(), *receiver);
  };
  auto fixedSourceIndex = [&](Value ordinal) -> Value {
    Value left = arith::ConstantOp::create(
        builder, location, builder.getI64Type(),
        builder.getI64IntegerAttr(fixedArray.getLeft()));
    return fixedArray.getLeft() <= fixedArray.getRight()
               ? Value(arith::AddIOp::create(builder, location, left, ordinal))
               : Value(arith::SubIOp::create(builder, location, left, ordinal));
  };
  auto sourceIndex = [&](Value ordinal) -> Value {
    if (iteratorKeys) {
      auto keys = cast<sim::QueueType>(iteratorKeys.getType());
      return sim::SimContainerReadOp::create(
          builder, location, keys.getElementType(), iteratorKeys, ordinal);
    }
    return fixedArray ? fixedSourceIndex(ordinal) : ordinal;
  };
  auto readInput = [&](Value ordinal) -> Value {
    if (fixedArray)
      return sim::SimArrayDynExtractOp::create(
          builder, location, elementType, *receiver, fixedSourceIndex(ordinal));
    return sim::SimContainerReadOp::create(builder, location, elementType,
                                           *receiver, ordinal);
  };
  auto keyIsSigned = [&]() {
    return clause ? isSignedNode(clause) : elementSigned;
  };
  auto evaluateClause = [&](StringRef path, Value element,
                            Value index) -> FailureOr<Value> {
    bindIterator(path, element, sourceIndex(index));
    return clause ? lowerExpression(clause) : FailureOr<Value>(element);
  };

  if (method == ArrayMethod::Sum || method == ArrayMethod::Product ||
      method == ArrayMethod::And || method == ArrayMethod::Or ||
      method == ArrayMethod::Xor) {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    FailureOr<StringRef> path = iteratorPath();
    if (failed(resultType) || failed(path))
      return failure();
    SavedIterator saved = saveIterator(*path);
    Value initial;
    if (auto integer = dyn_cast<IntegerType>(*resultType)) {
      APInt identity(integer.getWidth(),
                     method == ArrayMethod::Product ? 1 : 0);
      if (method == ArrayMethod::And)
        identity.setAllBits();
      initial =
          arith::ConstantOp::create(builder, location, integer,
                                    builder.getIntegerAttr(integer, identity));
    } else if (auto logic = dyn_cast<sim::LogicType>(*resultType)) {
      APInt identity(logic.getWidth(), method == ArrayMethod::Product ? 1 : 0);
      if (method == ArrayMethod::And)
        identity.setAllBits();
      Type plane = builder.getIntegerType(logic.getWidth());
      initial = sim::SimLogicConstantOp::create(
          builder, location, logic, builder.getIntegerAttr(plane, identity),
          builder.getIntegerAttr(plane, 0));
    } else if (isa<FloatType>(*resultType) &&
               (method == ArrayMethod::Sum || method == ArrayMethod::Product)) {
      initial = arith::ConstantOp::create(
          builder, location, *resultType,
          builder.getFloatAttr(*resultType,
                               method == ArrayMethod::Product ? 1.0 : 0.0));
    } else {
      emitError(location) << "array reduction " << methodName
                          << " requires an arithmetic or packed result";
      return failure();
    }
    auto combine = [&](Value accumulator, Value term) -> Value {
      if (isa<IntegerType>(*resultType)) {
        if (method == ArrayMethod::Sum)
          return arith::AddIOp::create(builder, location, accumulator, term);
        if (method == ArrayMethod::Product)
          return arith::MulIOp::create(builder, location, accumulator, term);
        if (method == ArrayMethod::And)
          return arith::AndIOp::create(builder, location, accumulator, term);
        if (method == ArrayMethod::Or)
          return arith::OrIOp::create(builder, location, accumulator, term);
        return arith::XOrIOp::create(builder, location, accumulator, term);
      }
      if (isa<FloatType>(*resultType))
        return method == ArrayMethod::Sum
                   ? Value(arith::AddFOp::create(builder, location, accumulator,
                                                 term))
                   : Value(arith::MulFOp::create(builder, location, accumulator,
                                                 term));
      sim::BinaryKind kind = method == ArrayMethod::Sum ? sim::BinaryKind::Add
                             : method == ArrayMethod::Product
                                 ? sim::BinaryKind::Mul
                             : method == ArrayMethod::And ? sim::BinaryKind::And
                             : method == ArrayMethod::Or  ? sim::BinaryKind::Or
                                                         : sim::BinaryKind::Xor;
      return sim::SimLogicBinaryOp::create(builder, location, *resultType, kind,
                                           accumulator, term);
    };

    // Fixed arrays have a statically known extent.  Express their reduction
    // as ordinary aggregate extracts and SSA operations so the canonicalizer
    // can fold a constant aggregate without teaching lowering about constant
    // payloads.
    if (fixedArray) {
      Value accumulator = initial;
      unsigned count = sim::getAggregateNumElements(fixedArray);
      for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
        Value element = sim::SimAggregateExtractOp::create(
            builder, location, elementType, *receiver, ordinal);
        Value index = indexConstant(ordinal);
        FailureOr<Value> term = evaluateClause(*path, element, index);
        FailureOr<Value> converted =
            succeeded(term)
                ? convert(*term, *resultType, keyIsSigned(), location)
                : FailureOr<Value>(failure());
        if (failed(converted))
          return failure();
        accumulator = combine(accumulator, *converted);
      }
      restoreIterator(saved);
      return accumulator;
    }
    Value size = inputSize();
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    header->addArgument(*resultType, location);
    Block *body = addBlock();
    Block *exit = addBlock();
    exit->addArgument(*resultType, location);
    cf::BranchOp::create(builder, location, header,
                         ValueRange{indexConstant(0), initial});
    setCurrent(header);
    Value index = header->getArgument(0);
    Value accumulator = header->getArgument(1);
    Value more = arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::ult, index, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{accumulator});
    setCurrent(body);
    Value element = readInput(index);
    FailureOr<Value> term = evaluateClause(*path, element, index);
    if (failed(term))
      return failure();
    FailureOr<Value> converted =
        convert(*term, *resultType, keyIsSigned(), location);
    if (failed(converted))
      return failure();
    Value nextAccumulator = combine(accumulator, *converted);
    Value next =
        arith::AddIOp::create(builder, location, index, indexConstant(1));
    cf::BranchOp::create(builder, location, header,
                         ValueRange{next, nextAccumulator});
    restoreIterator(saved);
    setCurrent(exit);
    return exit->getArgument(0);
  }

  if (method == ArrayMethod::Reverse) {
    if (withClause)
      return emitError(location) << "reverse does not accept a with clause",
             failure();
    Value size = inputSize();
    Value two = indexConstant(2);
    Value half = arith::DivUIOp::create(builder, location, size, two);
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, header,
                         ValueRange{indexConstant(0)});
    setCurrent(header);
    Value leftIndex = header->getArgument(0);
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, leftIndex, half);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{});
    setCurrent(body);
    Value last =
        arith::SubIOp::create(builder, location, size, indexConstant(1));
    Value rightIndex =
        arith::SubIOp::create(builder, location, last, leftIndex);
    Value left = sim::SimContainerReadOp::create(builder, location, elementType,
                                                 *receiver, leftIndex);
    Value right = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, rightIndex);
    sim::SimContainerWriteOp::create(builder, location, *receiver, leftIndex,
                                     right);
    sim::SimContainerWriteOp::create(builder, location, *receiver, rightIndex,
                                     left);
    Value next =
        arith::AddIOp::create(builder, location, leftIndex, indexConstant(1));
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    setCurrent(exit);
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }

  if (method == ArrayMethod::Shuffle) {
    if (withClause)
      return emitError(location) << "shuffle does not accept a with clause",
             failure();
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, header, ValueRange{size});
    setCurrent(header);
    Value count = header->getArgument(0);
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ugt, count, indexConstant(1));
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{});
    setCurrent(body);
    Value last =
        arith::SubIOp::create(builder, location, count, indexConstant(1));
    Value context = function.getBody().front().getArgument(0);
    Value random = sim::SimRandomBoundedOp::create(
        builder, location, builder.getI64Type(), context, count);
    Value left = sim::SimContainerReadOp::create(builder, location, elementType,
                                                 *receiver, last);
    Value right = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, random);
    sim::SimContainerWriteOp::create(builder, location, *receiver, last, right);
    sim::SimContainerWriteOp::create(builder, location, *receiver, random,
                                     left);
    cf::BranchOp::create(builder, location, header, ValueRange{last});
    setCurrent(exit);
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }

  bool locator =
      method == ArrayMethod::Find || method == ArrayMethod::FindIndex ||
      method == ArrayMethod::FindFirst ||
      method == ArrayMethod::FindFirstIndex ||
      method == ArrayMethod::FindLast || method == ArrayMethod::FindLastIndex;
  if (locator) {
    if (!withClause)
      return emitError(location) << methodName << " requires a with clause",
             failure();
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    auto queue = succeeded(resultType) ? dyn_cast<sim::QueueType>(*resultType)
                                       : sim::QueueType{};
    FailureOr<StringRef> path = iteratorPath();
    if (!queue || failed(path))
      return failure();
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(queue.getElementType(), location);
    if (failed(descriptor))
      return failure();
    uint64_t bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
    Value result = sim::SimContainerCreateOp::create(
        builder, location, *resultType, indexConstant(0), descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds),
        OBELISK_RT_CONTAINER_QUEUE, bound);
    SavedIterator saved = saveIterator(*path);
    Value size = inputSize();
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *append = addBlock();
    Block *step = addBlock();
    step->addArgument(builder.getI64Type(), location);
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, header,
                         ValueRange{indexConstant(0)});
    setCurrent(header);
    Value index = header->getArgument(0);
    Value more = arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::ult, index, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{});
    setCurrent(body);
    Value element = readInput(index);
    FailureOr<Value> predicate = evaluateClause(*path, element, index);
    FailureOr<Value> truth = succeeded(predicate)
                                 ? truthValue(*predicate, location)
                                 : FailureOr<Value>(failure());
    if (failed(truth))
      return failure();
    cf::CondBranchOp::create(builder, location, *truth, append, ValueRange{},
                             step, ValueRange{index});
    setCurrent(append);
    bool indexResult = method == ArrayMethod::FindIndex ||
                       method == ArrayMethod::FindFirstIndex ||
                       method == ArrayMethod::FindLastIndex;
    Value appended = element;
    if (indexResult) {
      FailureOr<Value> converted = convert(
          sourceIndex(index), queue.getElementType(), true, location, true);
      if (failed(converted))
        return failure();
      appended = *converted;
    }
    if (method == ArrayMethod::FindLast || method == ArrayMethod::FindLastIndex)
      sim::SimContainerDeleteOp::create(builder, location, result);
    Value outputIndex = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), result);
    sim::SimContainerWriteOp::create(builder, location, result, outputIndex,
                                     appended);
    if (method == ArrayMethod::FindFirst ||
        method == ArrayMethod::FindFirstIndex)
      cf::BranchOp::create(builder, location, exit);
    else
      cf::BranchOp::create(builder, location, step, ValueRange{index});
    setCurrent(step);
    Value next = arith::AddIOp::create(builder, location, step->getArgument(0),
                                       indexConstant(1));
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    restoreIterator(saved);
    setCurrent(exit);
    return result;
  }

  auto orderedCompare = [&](Value left, Value right, Type type, bool less,
                            bool isSigned) -> FailureOr<Value> {
    if (Type scalarType = sim::getPackedScalarType(type);
        scalarType && scalarType != type) {
      FailureOr<Value> leftScalar = toPackedScalar(left, location);
      FailureOr<Value> rightScalar = toPackedScalar(right, location);
      if (failed(leftScalar) || failed(rightScalar))
        return failure();
      if (isa<IntegerType>(scalarType))
        return arith::CmpIOp::create(
                   builder, location,
                   less ? (isSigned ? arith::CmpIPredicate::slt
                                    : arith::CmpIPredicate::ult)
                        : (isSigned ? arith::CmpIPredicate::sgt
                                    : arith::CmpIPredicate::ugt),
                   *leftScalar, *rightScalar)
            .getResult();
      Value compared = sim::SimLogicCompareOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          less ? (isSigned ? sim::CompareKind::SLT : sim::CompareKind::ULT)
               : (isSigned ? sim::CompareKind::SGT : sim::CompareKind::UGT),
          *leftScalar, *rightScalar);
      return sim::SimLogicIsTrueOp::create(builder, location,
                                           builder.getI1Type(), compared)
          .getResult();
    }
    if (isa<IntegerType>(type))
      return arith::CmpIOp::create(builder, location,
                                   less
                                       ? (isSigned ? arith::CmpIPredicate::slt
                                                   : arith::CmpIPredicate::ult)
                                       : (isSigned ? arith::CmpIPredicate::sgt
                                                   : arith::CmpIPredicate::ugt),
                                   left, right)
          .getResult();
    if (isa<FloatType>(type))
      return arith::CmpFOp::create(builder, location,
                                   less ? arith::CmpFPredicate::OLT
                                        : arith::CmpFPredicate::OGT,
                                   left, right)
          .getResult();
    if (isa<sim::LogicType>(type)) {
      Value compared = sim::SimLogicCompareOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          less ? (isSigned ? sim::CompareKind::SLT : sim::CompareKind::ULT)
               : (isSigned ? sim::CompareKind::SGT : sim::CompareKind::UGT),
          left, right);
      return sim::SimLogicIsTrueOp::create(builder, location,
                                           builder.getI1Type(), compared)
          .getResult();
    }
    if (isa<sim::StringType>(type)) {
      Value compared = sim::SimStringCompareOp::create(
          builder, location, builder.getI32Type(), left, right,
          builder.getBoolAttr(false));
      Value zero =
          arith::ConstantOp::create(builder, location, builder.getI32Type(),
                                    builder.getI32IntegerAttr(0));
      return arith::CmpIOp::create(builder, location,
                                   less ? arith::CmpIPredicate::slt
                                        : arith::CmpIPredicate::sgt,
                                   compared, zero)
          .getResult();
    }
    emitError(location) << "array ordering key is not orderable: " << type;
    return failure();
  };

  if (method == ArrayMethod::Sort || method == ArrayMethod::RSort) {
    FailureOr<StringRef> path = iteratorPath();
    FailureOr<Type> keyType = clause ? getNormalizedSemanticType(clause)
                                     : FailureOr<Type>(elementType);
    if (failed(path) || failed(keyType))
      return failure();
    SavedIterator saved = saveIterator(*path);
    Value size = inputSize();
    FailureOr<ContainerElementDescriptor> keyDescriptor =
        describeContainerElement(*keyType, location);
    if (failed(keyDescriptor))
      return failure();
    Type keyContainerType =
        sim::DynamicArrayType::get(function.getContext(), *keyType);
    Value keys = sim::SimContainerCreateOp::create(
        builder, location, keyContainerType, size, keyDescriptor->typeID,
        keyDescriptor->kind, keyDescriptor->flags, keyDescriptor->valueSize,
        keyDescriptor->alignment, keyDescriptor->bitWidth,
        builder.getDenseI64ArrayAttr(keyDescriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(keyDescriptor->traceKinds),
        OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 0);
    Block *keyHeader = addBlock();
    keyHeader->addArgument(builder.getI64Type(), location);
    Block *keyBody = addBlock();
    Block *keysReady = addBlock();
    cf::BranchOp::create(builder, location, keyHeader,
                         ValueRange{indexConstant(0)});
    setCurrent(keyHeader);
    Value keyIndex = keyHeader->getArgument(0);
    Value needsKey = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, keyIndex, size);
    cf::CondBranchOp::create(builder, location, needsKey, keyBody, ValueRange{},
                             keysReady, ValueRange{});
    setCurrent(keyBody);
    Value keyElement = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, keyIndex);
    FailureOr<Value> evaluatedKey = evaluateClause(*path, keyElement, keyIndex);
    if (failed(evaluatedKey))
      return failure();
    FailureOr<Value> convertedKey =
        convert(*evaluatedKey, *keyType, keyIsSigned(), location);
    if (failed(convertedKey))
      return failure();
    sim::SimContainerWriteOp::create(builder, location, keys, keyIndex,
                                     *convertedKey);
    Value nextKey =
        arith::AddIOp::create(builder, location, keyIndex, indexConstant(1));
    cf::BranchOp::create(builder, location, keyHeader, ValueRange{nextKey});
    restoreIterator(saved);
    setCurrent(keysReady);
    Block *outerHeader = addBlock();
    outerHeader->addArgument(builder.getI64Type(), location);
    Block *innerInit = addBlock();
    Block *innerHeader = addBlock();
    innerHeader->addArgument(builder.getI64Type(), location);
    innerHeader->addArgument(builder.getI64Type(), location);
    Block *innerBody = addBlock();
    Block *swap = addBlock();
    swap->addArgument(elementType, location);
    swap->addArgument(elementType, location);
    swap->addArgument(*keyType, location);
    swap->addArgument(*keyType, location);
    swap->addArgument(builder.getI64Type(), location);
    Block *innerStep = addBlock();
    innerStep->addArgument(builder.getI64Type(), location);
    innerStep->addArgument(builder.getI64Type(), location);
    Block *outerStep = addBlock();
    outerStep->addArgument(builder.getI64Type(), location);
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, outerHeader,
                         ValueRange{indexConstant(0)});
    setCurrent(outerHeader);
    Value pass = outerHeader->getArgument(0);
    Value anotherPass = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, pass, size);
    cf::CondBranchOp::create(builder, location, anotherPass, innerInit,
                             ValueRange{}, exit, ValueRange{});
    setCurrent(innerInit);
    Value remaining = arith::SubIOp::create(builder, location, size, pass);
    Value limit =
        arith::SubIOp::create(builder, location, remaining, indexConstant(1));
    cf::BranchOp::create(builder, location, innerHeader,
                         ValueRange{indexConstant(0), limit});
    setCurrent(innerHeader);
    Value index = innerHeader->getArgument(0);
    Value innerLimit = innerHeader->getArgument(1);
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, innerLimit);
    cf::CondBranchOp::create(builder, location, more, innerBody, ValueRange{},
                             outerStep, ValueRange{pass});
    setCurrent(innerBody);
    Value rightIndex =
        arith::AddIOp::create(builder, location, index, indexConstant(1));
    Value left = sim::SimContainerReadOp::create(builder, location, elementType,
                                                 *receiver, index);
    Value right = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, rightIndex);
    Value convertedLeft = sim::SimContainerReadOp::create(
        builder, location, *keyType, keys, index);
    Value convertedRight = sim::SimContainerReadOp::create(
        builder, location, *keyType, keys, rightIndex);
    FailureOr<Value> outOfOrder =
        orderedCompare(convertedRight, convertedLeft, *keyType,
                       method == ArrayMethod::Sort, keyIsSigned());
    if (failed(outOfOrder))
      return failure();
    cf::CondBranchOp::create(
        builder, location, *outOfOrder, swap,
        ValueRange{left, right, convertedLeft, convertedRight, index},
        innerStep, ValueRange{index, innerLimit});
    setCurrent(swap);
    Value swapIndex = swap->getArgument(4);
    Value swapRightIndex =
        arith::AddIOp::create(builder, location, swapIndex, indexConstant(1));
    sim::SimContainerWriteOp::create(builder, location, *receiver, swapIndex,
                                     swap->getArgument(1));
    sim::SimContainerWriteOp::create(builder, location, *receiver,
                                     swapRightIndex, swap->getArgument(0));
    sim::SimContainerWriteOp::create(builder, location, keys, swapIndex,
                                     swap->getArgument(3));
    sim::SimContainerWriteOp::create(builder, location, keys, swapRightIndex,
                                     swap->getArgument(2));
    cf::BranchOp::create(builder, location, innerStep,
                         ValueRange{swapIndex, innerLimit});
    setCurrent(innerStep);
    Value nextIndex = arith::AddIOp::create(
        builder, location, innerStep->getArgument(0), indexConstant(1));
    cf::BranchOp::create(builder, location, innerHeader,
                         ValueRange{nextIndex, innerStep->getArgument(1)});
    setCurrent(outerStep);
    Value nextPass = arith::AddIOp::create(
        builder, location, outerStep->getArgument(0), indexConstant(1));
    cf::BranchOp::create(builder, location, outerHeader, ValueRange{nextPass});
    setCurrent(exit);
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }

  if (method == ArrayMethod::Map) {
    if (!withClause)
      return emitError(location) << "map requires a with clause", failure();
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    Type resultElement;
    uint32_t resultKind = 0;
    uint64_t bound = 0;
    if (succeeded(resultType)) {
      if (auto array = dyn_cast<sim::DynamicArrayType>(*resultType)) {
        resultElement = array.getElementType();
        resultKind = OBELISK_RT_CONTAINER_DYNAMIC_ARRAY;
      } else if (auto queue = dyn_cast<sim::QueueType>(*resultType)) {
        resultElement = queue.getElementType();
        resultKind = OBELISK_RT_CONTAINER_QUEUE;
        bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
      }
    }
    FailureOr<StringRef> path = iteratorPath();
    if (!resultElement || failed(path))
      return failure();
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(resultElement, location);
    if (failed(descriptor))
      return failure();
    Value size = inputSize();
    Value allocationSize = resultKind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY
                               ? size
                               : indexConstant(0);
    Value result = sim::SimContainerCreateOp::create(
        builder, location, *resultType, allocationSize, descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds), resultKind,
        bound);
    SavedIterator saved = saveIterator(*path);
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    Block *body = addBlock();
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, header,
                         ValueRange{indexConstant(0)});
    setCurrent(header);
    Value index = header->getArgument(0);
    Value more = arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::ult, index, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                             ValueRange{});
    setCurrent(body);
    Value element = readInput(index);
    FailureOr<Value> mapped = evaluateClause(*path, element, index);
    if (failed(mapped))
      return failure();
    FailureOr<Value> converted =
        convert(*mapped, resultElement, isSignedNode(clause), location);
    if (failed(converted))
      return failure();
    sim::SimContainerWriteOp::create(builder, location, result, index,
                                     *converted);
    Value next =
        arith::AddIOp::create(builder, location, index, indexConstant(1));
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    restoreIterator(saved);
    setCurrent(exit);
    return result;
  }

  if (method == ArrayMethod::Min || method == ArrayMethod::Max) {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    auto queue = succeeded(resultType) ? dyn_cast<sim::QueueType>(*resultType)
                                       : sim::QueueType{};
    FailureOr<StringRef> path = iteratorPath();
    if (!queue || failed(path))
      return failure();
    FailureOr<Type> keyType = clause ? getNormalizedSemanticType(clause)
                                     : FailureOr<Type>(elementType);
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(queue.getElementType(), location);
    if (failed(keyType) || failed(descriptor))
      return failure();
    uint64_t bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
    Value result = sim::SimContainerCreateOp::create(
        builder, location, *resultType, indexConstant(0), descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds),
        OBELISK_RT_CONTAINER_QUEUE, bound);
    SavedIterator saved = saveIterator(*path);

    // Keep fixed-array selection in SSA form.  Static aggregate extracts are
    // canonicalized through aggregate.construct, after which the normal arith
    // folders reduce comparisons and selects for constant arrays.
    if (fixedArray) {
      unsigned count = sim::getAggregateNumElements(fixedArray);
      assert(count && "a fixed unpacked array cannot be empty");
      Value first = sim::SimAggregateExtractOp::create(
          builder, location, elementType, *receiver, 0);
      FailureOr<Value> firstKey =
          evaluateClause(*path, first, indexConstant(0));
      FailureOr<Value> convertedFirst =
          succeeded(firstKey)
              ? convert(*firstKey, *keyType, keyIsSigned(), location)
              : FailureOr<Value>(failure());
      if (failed(convertedFirst))
        return failure();
      Value best = first;
      Value bestKey = *convertedFirst;
      for (unsigned ordinal = 1; ordinal < count; ++ordinal) {
        Value candidate = sim::SimAggregateExtractOp::create(
            builder, location, elementType, *receiver, ordinal);
        FailureOr<Value> candidateKey =
            evaluateClause(*path, candidate, indexConstant(ordinal));
        FailureOr<Value> convertedKey =
            succeeded(candidateKey)
                ? convert(*candidateKey, *keyType, keyIsSigned(), location)
                : FailureOr<Value>(failure());
        if (failed(convertedKey))
          return failure();
        FailureOr<Value> preferred =
            orderedCompare(*convertedKey, bestKey, *keyType,
                           method == ArrayMethod::Min, keyIsSigned());
        if (failed(preferred))
          return failure();
        best = arith::SelectOp::create(builder, location, *preferred, candidate,
                                       best);
        bestKey = arith::SelectOp::create(builder, location, *preferred,
                                          *convertedKey, bestKey);
      }
      sim::SimContainerWriteOp::create(builder, location, result,
                                       indexConstant(0), best);
      restoreIterator(saved);
      return result;
    }
    Value size = inputSize();
    Value nonempty = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, size, indexConstant(0));
    Block *initialize = addBlock();
    Block *header = addBlock();
    header->addArgument(builder.getI64Type(), location);
    header->addArgument(elementType, location);
    header->addArgument(*keyType, location);
    Block *body = addBlock();
    Block *finish = addBlock();
    finish->addArgument(elementType, location);
    Block *exit = addBlock();
    cf::CondBranchOp::create(builder, location, nonempty, initialize,
                             ValueRange{}, exit, ValueRange{});
    setCurrent(initialize);
    Value first = readInput(indexConstant(0));
    FailureOr<Value> firstKey = evaluateClause(*path, first, indexConstant(0));
    if (failed(firstKey))
      return failure();
    FailureOr<Value> convertedFirst =
        convert(*firstKey, *keyType, keyIsSigned(), location);
    if (failed(convertedFirst))
      return failure();
    cf::BranchOp::create(builder, location, header,
                         ValueRange{indexConstant(1), first, *convertedFirst});
    setCurrent(header);
    Value index = header->getArgument(0);
    Value best = header->getArgument(1);
    Value bestKey = header->getArgument(2);
    Value more = arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::ult, index, size);
    cf::CondBranchOp::create(builder, location, more, body, ValueRange{},
                             finish, ValueRange{best});
    setCurrent(body);
    Value candidate = readInput(index);
    FailureOr<Value> candidateKey = evaluateClause(*path, candidate, index);
    if (failed(candidateKey))
      return failure();
    FailureOr<Value> convertedKey =
        convert(*candidateKey, *keyType, keyIsSigned(), location);
    if (failed(convertedKey))
      return failure();
    FailureOr<Value> preferred =
        orderedCompare(*convertedKey, bestKey, *keyType,
                       method == ArrayMethod::Min, keyIsSigned());
    if (failed(preferred))
      return failure();
    Value nextBest =
        arith::SelectOp::create(builder, location, *preferred, candidate, best);
    Value nextKey = arith::SelectOp::create(builder, location, *preferred,
                                            *convertedKey, bestKey);
    Value next =
        arith::AddIOp::create(builder, location, index, indexConstant(1));
    cf::BranchOp::create(builder, location, header,
                         ValueRange{next, nextBest, nextKey});
    setCurrent(finish);
    sim::SimContainerWriteOp::create(builder, location, result,
                                     indexConstant(0), finish->getArgument(0));
    cf::BranchOp::create(builder, location, exit);
    restoreIterator(saved);
    setCurrent(exit);
    return result;
  }

  if (method == ArrayMethod::Unique || method == ArrayMethod::UniqueIndex) {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    auto resultQueue = succeeded(resultType)
                           ? dyn_cast<sim::QueueType>(*resultType)
                           : sim::QueueType{};
    FailureOr<StringRef> path = iteratorPath();
    FailureOr<Type> keyType = clause ? getNormalizedSemanticType(clause)
                                     : FailureOr<Type>(elementType);
    if (!resultQueue || failed(path) || failed(keyType))
      return failure();
    FailureOr<ContainerElementDescriptor> resultDescriptor =
        describeContainerElement(resultQueue.getElementType(), location);
    FailureOr<ContainerElementDescriptor> keyDescriptor =
        describeContainerElement(*keyType, location);
    if (failed(resultDescriptor) || failed(keyDescriptor))
      return failure();
    uint64_t resultBound =
        resultQueue.getBound() ? resultQueue.getBound() : UINT64_MAX;
    Value result = sim::SimContainerCreateOp::create(
        builder, location, *resultType, indexConstant(0),
        resultDescriptor->typeID, resultDescriptor->kind,
        resultDescriptor->flags, resultDescriptor->valueSize,
        resultDescriptor->alignment, resultDescriptor->bitWidth,
        builder.getDenseI64ArrayAttr(resultDescriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(resultDescriptor->traceKinds),
        OBELISK_RT_CONTAINER_QUEUE, resultBound);
    Type keyQueueType = sim::QueueType::get(function.getContext(), *keyType, 0);
    Value keys = sim::SimContainerCreateOp::create(
        builder, location, keyQueueType, indexConstant(0),
        keyDescriptor->typeID, keyDescriptor->kind, keyDescriptor->flags,
        keyDescriptor->valueSize, keyDescriptor->alignment,
        keyDescriptor->bitWidth,
        builder.getDenseI64ArrayAttr(keyDescriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(keyDescriptor->traceKinds),
        OBELISK_RT_CONTAINER_QUEUE, UINT64_MAX);
    SavedIterator saved = saveIterator(*path);
    Value size = inputSize();
    Block *outerHeader = addBlock();
    outerHeader->addArgument(builder.getI64Type(), location);
    Block *outerBody = addBlock();
    Block *innerHeader = addBlock();
    innerHeader->addArgument(builder.getI64Type(), location);
    innerHeader->addArgument(elementType, location);
    innerHeader->addArgument(*keyType, location);
    innerHeader->addArgument(builder.getI64Type(), location);
    Block *innerBody = addBlock();
    Block *append = addBlock();
    append->addArgument(elementType, location);
    append->addArgument(*keyType, location);
    append->addArgument(builder.getI64Type(), location);
    Block *outerStep = addBlock();
    outerStep->addArgument(builder.getI64Type(), location);
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, outerHeader,
                         ValueRange{indexConstant(0)});
    setCurrent(outerHeader);
    Value inputIndex = outerHeader->getArgument(0);
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, inputIndex, size);
    cf::CondBranchOp::create(builder, location, more, outerBody, ValueRange{},
                             exit, ValueRange{});
    setCurrent(outerBody);
    Value candidate = readInput(inputIndex);
    FailureOr<Value> candidateKey =
        evaluateClause(*path, candidate, inputIndex);
    if (failed(candidateKey))
      return failure();
    FailureOr<Value> convertedKey =
        convert(*candidateKey, *keyType, keyIsSigned(), location);
    if (failed(convertedKey))
      return failure();
    cf::BranchOp::create(
        builder, location, innerHeader,
        ValueRange{indexConstant(0), candidate, *convertedKey, inputIndex});
    setCurrent(innerHeader);
    Value keyIndex = innerHeader->getArgument(0);
    Value outputSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), keys);
    Value search = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, keyIndex, outputSize);
    cf::CondBranchOp::create(
        builder, location, search, innerBody, ValueRange{}, append,
        ValueRange{innerHeader->getArgument(1), innerHeader->getArgument(2),
                   innerHeader->getArgument(3)});
    setCurrent(innerBody);
    Value existingKey = sim::SimContainerReadOp::create(
        builder, location, *keyType, keys, keyIndex);
    FailureOr<Value> equal = conditionalEqual(
        existingKey, innerHeader->getArgument(2), *keyType, location);
    if (failed(equal))
      return failure();
    Block *innerNext = addBlock();
    cf::CondBranchOp::create(builder, location, *equal, outerStep,
                             ValueRange{innerHeader->getArgument(3)}, innerNext,
                             ValueRange{});
    setCurrent(innerNext);
    Value nextKeyIndex =
        arith::AddIOp::create(builder, location, keyIndex, indexConstant(1));
    cf::BranchOp::create(builder, location, innerHeader,
                         ValueRange{nextKeyIndex, innerHeader->getArgument(1),
                                    innerHeader->getArgument(2),
                                    innerHeader->getArgument(3)});
    setCurrent(append);
    Value resultValue = append->getArgument(0);
    if (method == ArrayMethod::UniqueIndex) {
      FailureOr<Value> convertedIndex =
          convert(sourceIndex(append->getArgument(2)),
                  resultQueue.getElementType(), true, location, true);
      if (failed(convertedIndex))
        return failure();
      resultValue = *convertedIndex;
    }
    Value appendIndex = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), result);
    sim::SimContainerWriteOp::create(builder, location, result, appendIndex,
                                     resultValue);
    sim::SimContainerWriteOp::create(builder, location, keys, appendIndex,
                                     append->getArgument(1));
    cf::BranchOp::create(builder, location, outerStep,
                         ValueRange{append->getArgument(2)});
    setCurrent(outerStep);
    Value nextInput = arith::AddIOp::create(
        builder, location, outerStep->getArgument(0), indexConstant(1));
    cf::BranchOp::create(builder, location, outerHeader, ValueRange{nextInput});
    restoreIterator(saved);
    setCurrent(exit);
    return result;
  }

  return emitError(location)
             << "unsupported dynamic-array method " << methodName,
         failure();
}

FailureOr<Value>
UnitLowering::lowerAssociativeArrayMethod(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.empty()) {
    emitError(location)
        << "malformed associative-array method argument inventory";
    return failure();
  }
  bool withClause = op.getHasIteratorExpression();
  Operation *receiverNode = withClause ? children.back() : children.front();
  FailureOr<Type> semanticReceiverType =
      getNormalizedSemanticType(receiverNode);
  if (failed(semanticReceiverType))
    return failure();
  auto arrayType = dyn_cast<sim::AssocArrayType>(*semanticReceiverType);
  if (!arrayType)
    return failure();
  StringRef methodName = op.getCalleeName();
  ArrayMethod method = classifyArrayMethod(methodName);

  auto result = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType))
      return failure();
    return convert(value, *resultType, false, location);
  };
  auto lowerKey = [&](Operation *node) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(node);
    if (failed(value))
      return failure();
    return convert(*value, arrayType.getKeyType(), isSignedNode(node),
                   getSemanticLocation(node), arrayType.getSignedKey());
  };
  auto receiverValue = [&]() -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(children.front());
    if (failed(value))
      return failure();
    return ensureAssocArray(*value, location);
  };

  if (method == ArrayMethod::Size || method == ArrayMethod::Num) {
    if (children.size() != 1)
      return emitError(location) << methodName << " takes no arguments",
             failure();
    FailureOr<Value> receiver = receiverValue();
    if (failed(receiver))
      return failure();
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    return result(size);
  }
  if (method == ArrayMethod::Exists) {
    if (children.size() != 2)
      return emitError(location) << "exists takes one key argument", failure();
    FailureOr<Value> receiver = receiverValue();
    FailureOr<Value> key = lowerKey(children[1]);
    if (failed(receiver) || failed(key))
      return failure();
    Value exists = sim::SimAssocExistsOp::create(
        builder, location, builder.getI1Type(), *receiver, *key);
    return result(exists);
  }
  if (method == ArrayMethod::Delete) {
    if (children.size() > 2)
      return emitError(location) << "delete takes at most one key argument",
             failure();
    FailureOr<Value> reference = lowerExpression(children.front(), true);
    FailureOr<Value> loaded = succeeded(reference)
                                  ? loadReference(*reference, location)
                                  : FailureOr<Value>(failure());
    if (failed(reference) || failed(loaded))
      return failure();
    Value updated = cloneSequentialValue(*loaded, location);
    FailureOr<Value> allocated = ensureAssocArray(updated, location);
    if (failed(allocated))
      return failure();
    updated = *allocated;
    if (children.size() == 1) {
      sim::SimContainerDeleteOp::create(builder, location, updated);
    } else {
      FailureOr<Value> key = lowerKey(children[1]);
      if (failed(key))
        return failure();
      sim::SimAssocDeleteOp::create(builder, location, updated, *key);
    }
    if (failed(storeReference(*reference, updated, location)))
      return failure();
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  bool traversalMethod =
      method == ArrayMethod::First || method == ArrayMethod::Last ||
      method == ArrayMethod::Next || method == ArrayMethod::Prev;
  if (traversalMethod) {
    if (children.size() != 2)
      return emitError(location)
                 << methodName << " takes one key output argument",
             failure();
    FailureOr<Value> receiver = receiverValue();
    FailureOr<Value> destination = lowerExpression(children[1], true);
    if (failed(receiver) || failed(destination))
      return failure();
    Value inputKey;
    bool endpoint = method == ArrayMethod::First || method == ArrayMethod::Last;
    if (endpoint)
      inputKey = createDefaultValue(builder, location, arrayType.getKeyType());
    else {
      FailureOr<Value> current = loadReference(*destination, location);
      if (failed(current))
        return failure();
      FailureOr<Value> converted =
          convert(*current, arrayType.getKeyType(), isSignedNode(children[1]),
                  location, arrayType.getSignedKey());
      if (failed(converted))
        return failure();
      inputKey = *converted;
    }
    int32_t direction =
        method == ArrayMethod::First || method == ArrayMethod::Next ? 1 : -1;
    FailureOr<std::pair<Value, Value>> traversed =
        traverseAssoc(*receiver, inputKey, direction, endpoint, location);
    if (failed(traversed))
      return failure();
    Block *store = addBlock();
    Block *done = addBlock();
    cf::CondBranchOp::create(builder, location, traversed->second, store,
                             ValueRange{}, done, ValueRange{});
    setCurrent(store);
    FailureOr<Type> destinationType = getNormalizedSemanticType(children[1]);
    if (failed(destinationType))
      return failure();
    FailureOr<Value> converted =
        convert(traversed->first, *destinationType, arrayType.getSignedKey(),
                location, isSignedNode(children[1]));
    if (failed(converted) ||
        failed(storeReference(*destination, *converted, location)))
      return failure();
    cf::BranchOp::create(builder, location, done);
    setCurrent(done);
    return result(traversed->second);
  }

  bool expressionMethod =
      method == ArrayMethod::Sum || method == ArrayMethod::Product ||
      method == ArrayMethod::And || method == ArrayMethod::Or ||
      method == ArrayMethod::Xor || method == ArrayMethod::Find ||
      method == ArrayMethod::FindIndex || method == ArrayMethod::FindFirst ||
      method == ArrayMethod::FindFirstIndex ||
      method == ArrayMethod::FindLast || method == ArrayMethod::FindLastIndex ||
      method == ArrayMethod::Min || method == ArrayMethod::Max ||
      method == ArrayMethod::Unique || method == ArrayMethod::UniqueIndex ||
      method == ArrayMethod::Map;
  if (expressionMethod) {
    if (children.size() != (withClause ? 2u : 1u)) {
      emitError(location)
          << "malformed associative-array expression-method inventory";
      return failure();
    }
    FailureOr<Value> receiver = lowerExpression(receiverNode);
    if (failed(receiver))
      return failure();
    receiver = ensureAssocArray(*receiver, location);
    if (failed(receiver))
      return failure();
    Type valueQueueType = sim::QueueType::get(function.getContext(),
                                              arrayType.getElementType(), 0);
    Type keyQueueType =
        sim::QueueType::get(function.getContext(), arrayType.getKeyType(), 0);
    FailureOr<ContainerElementDescriptor> valueDescriptor =
        describeContainerElement(arrayType.getElementType(), location);
    FailureOr<ContainerElementDescriptor> keyDescriptor =
        describeContainerElement(arrayType.getKeyType(), location);
    if (failed(valueDescriptor) || failed(keyDescriptor))
      return failure();
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    auto createQueue = [&](Type type,
                           const ContainerElementDescriptor &descriptor) {
      return sim::SimContainerCreateOp::create(
          builder, location, type, zero, descriptor.typeID, descriptor.kind,
          descriptor.flags, descriptor.valueSize, descriptor.alignment,
          descriptor.bitWidth,
          builder.getDenseI64ArrayAttr(descriptor.traceOffsets),
          builder.getDenseI32ArrayAttr(descriptor.traceKinds),
          OBELISK_RT_CONTAINER_QUEUE, UINT64_MAX);
    };
    Value orderedValues = createQueue(valueQueueType, *valueDescriptor);
    Value orderedKeys = createQueue(keyQueueType, *keyDescriptor);
    Value defaultKey =
        createDefaultValue(builder, location, arrayType.getKeyType());
    FailureOr<std::pair<Value, Value>> first =
        traverseAssoc(*receiver, defaultKey, 1, true, location);
    if (failed(first))
      return failure();
    Block *header = addBlock();
    header->addArgument(arrayType.getKeyType(), location);
    header->addArgument(builder.getI1Type(), location);
    Block *body = addBlock();
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, header,
                         ValueRange{first->first, first->second});
    setCurrent(header);
    Value key = header->getArgument(0);
    cf::CondBranchOp::create(builder, location, header->getArgument(1), body,
                             ValueRange{}, exit, ValueRange{});
    setCurrent(body);
    Value value = sim::SimAssocReadOp::create(
        builder, location, arrayType.getElementType(), *receiver, key);
    Value ordinal = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), orderedValues);
    sim::SimContainerWriteOp::create(builder, location, orderedValues, ordinal,
                                     value);
    sim::SimContainerWriteOp::create(builder, location, orderedKeys, ordinal,
                                     key);
    FailureOr<std::pair<Value, Value>> next =
        traverseAssoc(*receiver, key, 1, false, location);
    if (failed(next))
      return failure();
    cf::BranchOp::create(builder, location, header,
                         ValueRange{next->first, next->second});
    setCurrent(exit);

    if (method == ArrayMethod::Map) {
      if (!withClause)
        return emitError(location) << "map requires a with clause", failure();
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      auto resultArray = succeeded(resultType)
                             ? dyn_cast<sim::AssocArrayType>(*resultType)
                             : sim::AssocArrayType{};
      if (!resultArray || resultArray.getKeyType() != arrayType.getKeyType())
        return failure();
      FailureOr<Value> mappedArray = createAssocArray(resultArray, location);
      if (failed(mappedArray))
        return failure();
      auto iteratorPath =
          op->getAttrOfType<StringAttr>("iterator_variable_path");
      if (!iteratorPath)
        return emitError(location)
                   << "map with clause has no iterator-variable path",
               failure();
      StringRef path = iteratorPath.getValue();
      Value savedValue = values.lookup(path);
      Value savedIndex = iteratorIndices.lookup(path);
      Value count = sim::SimContainerSizeOp::create(
          builder, location, builder.getI64Type(), orderedValues);
      Block *mapHeader = addBlock();
      mapHeader->addArgument(builder.getI64Type(), location);
      Block *mapBody = addBlock();
      Block *mapExit = addBlock();
      cf::BranchOp::create(builder, location, mapHeader, ValueRange{zero});
      setCurrent(mapHeader);
      Value index = mapHeader->getArgument(0);
      Value more = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, index, count);
      cf::CondBranchOp::create(builder, location, more, mapBody, ValueRange{},
                               mapExit, ValueRange{});
      setCurrent(mapBody);
      Value item = sim::SimContainerReadOp::create(
          builder, location, arrayType.getElementType(), orderedValues, index);
      Value itemKey = sim::SimContainerReadOp::create(
          builder, location, arrayType.getKeyType(), orderedKeys, index);
      values[path] = item;
      iteratorIndices[path] = itemKey;
      FailureOr<Value> mapped = lowerExpression(children.front());
      if (failed(mapped))
        return failure();
      FailureOr<Value> converted =
          convert(*mapped, resultArray.getElementType(),
                  isSignedNode(children.front()), location);
      if (failed(converted))
        return failure();
      sim::SimAssocWriteOp::create(builder, location, *mappedArray, itemKey,
                                   *converted);
      Value one =
          arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                    builder.getI64IntegerAttr(1));
      Value following = arith::AddIOp::create(builder, location, index, one);
      cf::BranchOp::create(builder, location, mapHeader, ValueRange{following});
      if (savedValue)
        values[path] = savedValue;
      else
        values.erase(path);
      if (savedIndex)
        iteratorIndices[path] = savedIndex;
      else
        iteratorIndices.erase(path);
      setCurrent(mapExit);
      return *mappedArray;
    }
    return lowerArrayMethod(op, orderedValues, orderedKeys);
  }

  return emitError(location)
             << "unsupported associative-array method " << methodName,
         failure();
}

} // namespace obelisk::simlowering
