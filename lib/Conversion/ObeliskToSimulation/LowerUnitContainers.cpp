//===- LowerUnitContainers.cpp - Lower container methods ---------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

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
  StringRef name = op.getCalleeName();
  bool mutatesReceiver = name == "delete" || name == "reverse" ||
                         name == "shuffle" || name == "sort" ||
                         name == "rsort" || name == "push_back" ||
                         name == "push_front" || name == "pop_front" ||
                         name == "pop_back" || name == "insert";
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
  if (auto array = dyn_cast<sim::DynamicArrayType>(receiverType))
    elementType = array.getElementType();
  else if (auto queue = dyn_cast<sim::QueueType>(receiverType))
    elementType = queue.getElementType();
  else
    return failure();

  if (name == "size") {
    if (withClause || children.size() != 1)
      return emitError(location) << "size does not accept arguments or a "
                                    "with clause",
             failure();
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    return failed(resultType) ? FailureOr<Value>(failure())
                              : convert(size, *resultType, false, location);
  }
  if (name == "push_back") {
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
  if (name == "push_front") {
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
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    sim::SimQueueInsertOp::create(builder, location, *receiver, zero,
                                  *converted);
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  if (name == "pop_front") {
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
  if (name == "pop_back") {
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
  if (name == "insert") {
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
  if (name == "delete") {
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
  auto sourceIndex = [&](Value ordinal) -> Value {
    if (!iteratorKeys)
      return ordinal;
    auto keys = cast<sim::QueueType>(iteratorKeys.getType());
    return sim::SimContainerReadOp::create(
        builder, location, keys.getElementType(), iteratorKeys, ordinal);
  };
  auto evaluateClause = [&](StringRef path, Value element,
                            Value index) -> FailureOr<Value> {
    bindIterator(path, element, sourceIndex(index));
    return clause ? lowerExpression(clause) : FailureOr<Value>(element);
  };

  if (name == "sum" || name == "product" || name == "and" || name == "or" ||
      name == "xor") {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    FailureOr<StringRef> path = iteratorPath();
    if (failed(resultType) || failed(path))
      return failure();
    SavedIterator saved = saveIterator(*path);
    Value initial;
    if (auto integer = dyn_cast<IntegerType>(*resultType)) {
      APInt identity(integer.getWidth(), name == "product" ? 1 : 0);
      if (name == "and")
        identity.setAllBits();
      initial =
          arith::ConstantOp::create(builder, location, integer,
                                    builder.getIntegerAttr(integer, identity));
    } else if (auto logic = dyn_cast<sim::LogicType>(*resultType)) {
      APInt identity(logic.getWidth(), name == "product" ? 1 : 0);
      if (name == "and")
        identity.setAllBits();
      Type plane = builder.getIntegerType(logic.getWidth());
      initial = sim::SimLogicConstantOp::create(
          builder, location, logic, builder.getIntegerAttr(plane, identity),
          builder.getIntegerAttr(plane, 0));
    } else if (isa<FloatType>(*resultType) &&
               (name == "sum" || name == "product")) {
      initial = arith::ConstantOp::create(
          builder, location, *resultType,
          builder.getFloatAttr(*resultType, name == "product" ? 1.0 : 0.0));
    } else {
      emitError(location) << "array reduction " << name
                          << " requires an arithmetic or packed result";
      return failure();
    }
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
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
    Value element = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, index);
    FailureOr<Value> term = evaluateClause(*path, element, index);
    if (failed(term))
      return failure();
    FailureOr<Value> converted =
        convert(*term, *resultType,
                isSignedNode(clause ? clause : receiverNode), location);
    if (failed(converted))
      return failure();
    Value nextAccumulator;
    if (isa<IntegerType>(*resultType)) {
      if (name == "sum")
        nextAccumulator =
            arith::AddIOp::create(builder, location, accumulator, *converted);
      else if (name == "product")
        nextAccumulator =
            arith::MulIOp::create(builder, location, accumulator, *converted);
      else if (name == "and")
        nextAccumulator =
            arith::AndIOp::create(builder, location, accumulator, *converted);
      else if (name == "or")
        nextAccumulator =
            arith::OrIOp::create(builder, location, accumulator, *converted);
      else
        nextAccumulator =
            arith::XOrIOp::create(builder, location, accumulator, *converted);
    } else if (isa<FloatType>(*resultType)) {
      nextAccumulator =
          name == "sum" ? Value(arith::AddFOp::create(builder, location,
                                                      accumulator, *converted))
                        : Value(arith::MulFOp::create(builder, location,
                                                      accumulator, *converted));
    } else {
      sim::BinaryKind kind = name == "sum"       ? sim::BinaryKind::Add
                             : name == "product" ? sim::BinaryKind::Mul
                             : name == "and"     ? sim::BinaryKind::And
                             : name == "or"      ? sim::BinaryKind::Or
                                                 : sim::BinaryKind::Xor;
      nextAccumulator = sim::SimLogicBinaryOp::create(
          builder, location, *resultType, kind, accumulator, *converted);
    }
    Value next =
        arith::AddIOp::create(builder, location, index, indexConstant(1));
    cf::BranchOp::create(builder, location, header,
                         ValueRange{next, nextAccumulator});
    restoreIterator(saved);
    setCurrent(exit);
    return exit->getArgument(0);
  }

  if (name == "reverse") {
    if (withClause)
      return emitError(location) << "reverse does not accept a with clause",
             failure();
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
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

  if (name == "shuffle") {
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

  bool locator = name == "find" || name == "find_index" ||
                 name == "find_first" || name == "find_first_index" ||
                 name == "find_last" || name == "find_last_index";
  if (locator) {
    if (!withClause)
      return emitError(location) << name << " requires a with clause",
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
        builder.getDenseI32ArrayAttr(descriptor->traceKinds), 2, bound);
    SavedIterator saved = saveIterator(*path);
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
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
    Value element = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, index);
    FailureOr<Value> predicate = evaluateClause(*path, element, index);
    FailureOr<Value> truth = succeeded(predicate)
                                 ? truthValue(*predicate, location)
                                 : FailureOr<Value>(failure());
    if (failed(truth))
      return failure();
    cf::CondBranchOp::create(builder, location, *truth, append, ValueRange{},
                             step, ValueRange{index});
    setCurrent(append);
    bool indexResult = name.contains("index");
    Value appended = element;
    if (indexResult) {
      FailureOr<Value> converted = convert(
          sourceIndex(index), queue.getElementType(), true, location, true);
      if (failed(converted))
        return failure();
      appended = *converted;
    }
    if (name.starts_with("find_last"))
      sim::SimContainerDeleteOp::create(builder, location, result);
    Value outputIndex = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), result);
    sim::SimContainerWriteOp::create(builder, location, result, outputIndex,
                                     appended);
    if (name.starts_with("find_first"))
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

  if (name == "sort" || name == "rsort") {
    FailureOr<StringRef> path = iteratorPath();
    FailureOr<Type> keyType = clause ? getNormalizedSemanticType(clause)
                                     : FailureOr<Type>(elementType);
    if (failed(path) || failed(keyType))
      return failure();
    SavedIterator saved = saveIterator(*path);
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
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
        builder.getDenseI32ArrayAttr(keyDescriptor->traceKinds), 1, 0);
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
        convert(*evaluatedKey, *keyType,
                isSignedNode(clause ? clause : receiverNode), location);
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
        orderedCompare(convertedRight, convertedLeft, *keyType, name == "sort",
                       isSignedNode(clause ? clause : receiverNode));
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

  if (name == "map") {
    if (!withClause)
      return emitError(location) << "map requires a with clause", failure();
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    Type resultElement;
    uint32_t resultKind = 0;
    uint64_t bound = 0;
    if (succeeded(resultType)) {
      if (auto array = dyn_cast<sim::DynamicArrayType>(*resultType)) {
        resultElement = array.getElementType();
        resultKind = 1;
      } else if (auto queue = dyn_cast<sim::QueueType>(*resultType)) {
        resultElement = queue.getElementType();
        resultKind = 2;
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
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    Value allocationSize = resultKind == 1 ? size : indexConstant(0);
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
    Value element = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, index);
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

  if (name == "min" || name == "max") {
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
        builder.getDenseI32ArrayAttr(descriptor->traceKinds), 2, bound);
    SavedIterator saved = saveIterator(*path);
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
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
    Value first = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, indexConstant(0));
    FailureOr<Value> firstKey = evaluateClause(*path, first, indexConstant(0));
    if (failed(firstKey))
      return failure();
    FailureOr<Value> convertedFirst =
        convert(*firstKey, *keyType,
                isSignedNode(clause ? clause : receiverNode), location);
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
    Value candidate = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, index);
    FailureOr<Value> candidateKey = evaluateClause(*path, candidate, index);
    if (failed(candidateKey))
      return failure();
    FailureOr<Value> convertedKey =
        convert(*candidateKey, *keyType,
                isSignedNode(clause ? clause : receiverNode), location);
    if (failed(convertedKey))
      return failure();
    FailureOr<Value> preferred =
        orderedCompare(*convertedKey, bestKey, *keyType, name == "min",
                       isSignedNode(clause ? clause : receiverNode));
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

  if (name == "unique" || name == "unique_index") {
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
        builder.getDenseI32ArrayAttr(resultDescriptor->traceKinds), 2,
        resultBound);
    Type keyQueueType = sim::QueueType::get(function.getContext(), *keyType, 0);
    Value keys = sim::SimContainerCreateOp::create(
        builder, location, keyQueueType, indexConstant(0),
        keyDescriptor->typeID, keyDescriptor->kind, keyDescriptor->flags,
        keyDescriptor->valueSize, keyDescriptor->alignment,
        keyDescriptor->bitWidth,
        builder.getDenseI64ArrayAttr(keyDescriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(keyDescriptor->traceKinds), 2, UINT64_MAX);
    SavedIterator saved = saveIterator(*path);
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
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
    Value candidate = sim::SimContainerReadOp::create(
        builder, location, elementType, *receiver, inputIndex);
    FailureOr<Value> candidateKey =
        evaluateClause(*path, candidate, inputIndex);
    if (failed(candidateKey))
      return failure();
    FailureOr<Value> convertedKey =
        convert(*candidateKey, *keyType,
                isSignedNode(clause ? clause : receiverNode), location);
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
    if (name == "unique_index") {
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

  return emitError(location) << "unsupported dynamic-array method " << name,
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
  StringRef name = op.getCalleeName();

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

  if (name == "size" || name == "num") {
    if (children.size() != 1)
      return emitError(location) << name << " takes no arguments", failure();
    FailureOr<Value> receiver = receiverValue();
    if (failed(receiver))
      return failure();
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    return result(size);
  }
  if (name == "exists") {
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
  if (name == "delete") {
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
  if (name == "first" || name == "last" || name == "next" || name == "prev") {
    if (children.size() != 2)
      return emitError(location) << name << " takes one key output argument",
             failure();
    FailureOr<Value> receiver = receiverValue();
    FailureOr<Value> destination = lowerExpression(children[1], true);
    if (failed(receiver) || failed(destination))
      return failure();
    Value inputKey;
    bool endpoint = name == "first" || name == "last";
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
    int32_t direction = name == "first" || name == "next" ? 1 : -1;
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

  bool expressionMethod = name == "sum" || name == "product" || name == "and" ||
                          name == "or" || name == "xor" || name == "find" ||
                          name == "find_index" || name == "find_first" ||
                          name == "find_first_index" || name == "find_last" ||
                          name == "find_last_index" || name == "min" ||
                          name == "max" || name == "unique" ||
                          name == "unique_index" || name == "map";
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
          builder.getDenseI32ArrayAttr(descriptor.traceKinds), 2, UINT64_MAX);
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

    if (name == "map") {
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

  return emitError(location) << "unsupported associative-array method " << name,
         failure();
}

} // namespace obelisk::simlowering
