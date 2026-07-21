//===- LowerUnit.cpp - Lower one frozen code unit to SSA and CF ---------===//
//
// Rewrites the semantic statement and expression tree cloned into one
// `obelisk_sim.func` into an SSA CFG. The unit is isolated and every non-local
// resource is already an entry argument, so this pass never consults the
// design or any sibling unit and can run on all units concurrently.
//
//===----------------------------------------------------------------------===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"

#include <limits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMLOWERUNITPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

/// Spelling of a semantic integer literal, whichever literal node it is.
static std::optional<StringRef> getConstantSpelling(Operation *op) {
  if (auto literal = dyn_cast<semantic::SVIntegerLiteralOp>(op))
    return literal.getConstantValue();
  if (auto literal = dyn_cast<semantic::SVUnbasedUnsizedIntegerLiteralOp>(op))
    return literal.getConstantValue();
  return std::nullopt;
}

static bool isIntegerLiteral(Operation *op) {
  return isa<semantic::SVIntegerLiteralOp>(op);
}

/// True when an expression denotes storage rather than a computed value, so a
/// suspension can watch it directly.
static bool isAddressableExpression(Operation *op) {
  if (isa<semantic::SVNamedValueExpressionOp>(op))
    return true;
  if (!isa<semantic::SVElementSelectExpressionOp,
           semantic::SVRangeSelectExpressionOp>(op))
    return false;
  SmallVector<Operation *> children = getChildren(op);
  return !children.empty() && isAddressableExpression(children.front());
}

class UnitLowering {
public:
  explicit UnitLowering(sim::SimFuncOp function);

  LogicalResult lower(ArrayRef<Operation *> roots);

private:
  FailureOr<Value> lowerExpression(Operation *op, bool lvalue = false);
  FailureOr<Value> lowerNamedValue(semantic::SVNamedValueExpressionOp op,
                                   bool lvalue);
  FailureOr<Value> lowerLiteral(Operation *op);
  FailureOr<Value> lowerConcatenation(Operation *op);
  FailureOr<Value> lowerReplication(Operation *op);
  FailureOr<Value> lowerSelection(Operation *op, bool lvalue);
  FailureOr<Value> lowerAssignment(semantic::SVAssignmentExpressionOp op);
  FailureOr<Value> lowerUnary(semantic::SVUnaryExpressionOp op);
  FailureOr<Value> lowerBinary(semantic::SVBinaryExpressionOp op);
  FailureOr<Value> lowerCall(semantic::SVCallExpressionOp op);

  LogicalResult lowerStatement(Operation *op);
  LogicalResult lowerSequence(ArrayRef<Operation *> operations);
  LogicalResult lowerConditional(semantic::SVConditionalStatementOp op);
  LogicalResult lowerCase(semantic::SVCaseStatementOp op);
  LogicalResult lowerWhile(Operation *op);
  LogicalResult lowerFor(Operation *op);
  LogicalResult
  lowerVariableDeclaration(semantic::SVVariableDeclStatementOp op);
  LogicalResult lowerTiming(Operation *control, Operation *statement);

  FailureOr<Value> convert(Value value, Type targetType, bool sourceSigned,
                           Location location);
  FailureOr<Value> truthValue(Value value, Location location);
  FailureOr<Value> toLogic(Value value, Location location);
  LogicalResult emitFunctionReturn(Location location,
                                   std::optional<Value> explicitResult,
                                   bool resultSigned = false);
  Block *addBlock();
  void setCurrent(Block *block);
  void emitBranch(Block *destination);
  InFlightDiagnostic unsupported(Operation *op);

  /// Signedness of a semantic node's own type, used to pick the signed or
  /// unsigned form of a normalized operation.
  static bool isSignedNode(Operation *op) {
    auto type = op->getAttrOfType<TypeAttr>("semantic_type");
    return type && isSignedSemanticType(type.getValue());
  }

  sim::SimFuncOp function;
  OpBuilder builder;
  Block *current;
  llvm::StringMap<Value> values;
  llvm::StringMap<Value> lvalues;
  llvm::StringMap<Value> localDefaults;
  llvm::SetVector<Value> sensitivity;
  std::string returnPath;
  SmallVector<std::string> copyOutPaths;
  SmallVector<std::pair<Block *, Block *>> loopTargets;
};

UnitLowering::UnitLowering(sim::SimFuncOp function)
    : function(function), builder(function.getContext()),
      current(&function.getBody().front()) {
  builder.setInsertionPointToStart(current);
  auto bindings = function->getAttrOfType<ArrayAttr>(bindingsAttrName);
  if (!bindings)
    return;
  for (Attribute attr : bindings) {
    auto dictionary = cast<DictionaryAttr>(attr);
    StringRef path = dictionary.getAs<StringAttr>("path").getValue();
    if (auto argument = dictionary.getAs<IntegerAttr>("argument")) {
      Value value = function.getBody().front().getArgument(
          argument.getValue().getZExtValue());
      if (dictionary.contains("formal_local")) {
        Value local = sim::SimRefAllocOp::create(
            builder, function.getLoc(),
            sim::RefType::get(function.getContext(), value.getType()), value);
        values[path] = local;
        lvalues[path] = local;
        if (dictionary.contains("copy_out"))
          copyOutPaths.push_back(path.str());
        continue;
      }
      if (dictionary.contains("lvalue_only")) {
        lvalues[path] = value;
        continue;
      }
      values[path] = value;
      if (isa<sim::RefType, sim::NetType, sim::DriverType>(value.getType()))
        lvalues.try_emplace(path, value);
      continue;
    }
    auto typeAttr = dictionary.getAs<TypeAttr>("local_type");
    if (!typeAttr)
      continue;
    Type type = typeAttr.getValue();
    Value initial = createDefaultValue(builder, function.getLoc(), type);
    if (!initial)
      continue;
    Value local = sim::SimRefAllocOp::create(
        builder, function.getLoc(),
        sim::RefType::get(function.getContext(), type), initial);
    values[path] = local;
    lvalues[path] = local;
    localDefaults[path] = initial;
    if (dictionary.contains("is_return"))
      returnPath = path.str();
  }
}

Block *UnitLowering::addBlock() {
  Block *block = new Block();
  function.getBody().push_back(block);
  return block;
}

void UnitLowering::setCurrent(Block *block) {
  current = block;
  builder.setInsertionPointToEnd(block);
}

void UnitLowering::emitBranch(Block *destination) {
  if (current->empty() || !current->back().hasTrait<OpTrait::IsTerminator>())
    cf::BranchOp::create(builder, function.getLoc(), destination);
}

InFlightDiagnostic UnitLowering::unsupported(Operation *op) {
  return emitError(getSemanticLocation(op))
         << "unsupported semantic node in the first simulation slice: "
         << op->getName();
}

//===----------------------------------------------------------------------===//
// Normalized value conversions
//===----------------------------------------------------------------------===//

FailureOr<Value> UnitLowering::convert(Value value, Type targetType,
                                       bool sourceSigned, Location location) {
  if (value.getType() == targetType)
    return value;
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

FailureOr<Value> UnitLowering::truthValue(Value value, Location location) {
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
  if (function.getEntryKind() != sim::EntryKind::Function) {
    if (explicitResult) {
      emitError(location) << "non-function entry cannot return a value";
      return failure();
    }
    sim::SimReturnOp::create(builder, location, ValueRange{});
    return success();
  }

  TypeRange resultTypes = function.getFunctionType().getResults();
  if (resultTypes.empty()) {
    function.emitError("function signature has no primary result");
    return failure();
  }
  SmallVector<Value> results;
  if (explicitResult) {
    FailureOr<Value> converted =
        convert(*explicitResult, resultTypes.front(), resultSigned, location);
    if (failed(converted))
      return failure();
    results.push_back(*converted);
  } else {
    Value returnStorage = values.lookup(returnPath);
    if (returnStorage && isa<sim::RefType>(returnStorage.getType()))
      results.push_back(sim::SimRefLoadOp::create(
          builder, location,
          cast<sim::RefType>(returnStorage.getType()).getElementType(),
          returnStorage));
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

  if (resultTypes.size() != copyOutPaths.size() + 1) {
    function.emitError("function copy-out result inventory is inconsistent");
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
    if (value.getType() != resultTypes[index + 1]) {
      function.emitError("copy-out formal type does not match its result");
      return failure();
    }
    results.push_back(value);
  }
  sim::SimReturnOp::create(builder, location, results);
  return success();
}

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

FailureOr<Value>
UnitLowering::lowerNamedValue(semantic::SVNamedValueExpressionOp op,
                              bool lvalue) {
  Location location = getSemanticLocation(op);
  StringRef path = op.getReferencedPath();
  Value value = lvalue ? lvalues.lookup(path) : values.lookup(path);
  if (!value) {
    emitError(location) << "named value has no frozen unit-local binding: "
                        << path;
    return failure();
  }
  if (lvalue)
    return value;

  // Only immutable process captures describe design sensitivity. An automatic
  // local is an implementation detail and must remain eligible for promotion
  // instead of escaping through a suspend operation.
  auto recordSensitivity = [&] {
    if (auto argument = dyn_cast<BlockArgument>(value);
        argument && argument.getOwner() == &function.getBody().front())
      sensitivity.insert(value);
  };
  if (auto ref = dyn_cast<sim::RefType>(value.getType())) {
    recordSensitivity();
    return sim::SimRefLoadOp::create(builder, location, ref.getElementType(),
                                     value)
        .getResult();
  }
  if (auto net = dyn_cast<sim::NetType>(value.getType())) {
    recordSensitivity();
    return sim::SimNetReadOp::create(builder, location, net.getElementType(),
                                     value)
        .getResult();
  }
  if (isa<sim::DriverType>(value.getType())) {
    emitError(location) << "driver handle cannot be used as an rvalue";
    return failure();
  }
  return value;
}

FailureOr<Value> UnitLowering::lowerLiteral(Operation *op) {
  Location location = getSemanticLocation(op);
  FailureOr<Type> type = getNormalizedSemanticType(op);
  if (failed(type))
    return failure();
  std::optional<unsigned> width = sim::getPackedWidth(*type);
  std::optional<StringRef> spelling = getConstantSpelling(op);
  if (!width || !spelling) {
    unsupported(op) << " (integer literal representation)";
    return failure();
  }
  FailureOr<ParsedConstant> parsed =
      parseSVInteger(*spelling, *width, location);
  if (failed(parsed))
    return failure();
  if (auto integer = dyn_cast<IntegerType>(*type))
    return arith::ConstantOp::create(
               builder, location, integer,
               builder.getIntegerAttr(integer, parsed->value))
        .getResult();
  auto planeType = IntegerType::get(op->getContext(), *width);
  return sim::SimLogicConstantOp::create(
             builder, location, *type,
             builder.getIntegerAttr(planeType, parsed->value),
             builder.getIntegerAttr(planeType, parsed->unknown))
      .getResult();
}

FailureOr<Value> UnitLowering::lowerConcatenation(Operation *op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.empty()) {
    unsupported(op) << " (empty concatenation)";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  SmallVector<Value> inputs;
  for (Operation *child : children) {
    FailureOr<Value> input = lowerExpression(child);
    if (failed(input))
      return failure();
    inputs.push_back(*input);
  }
  if (auto resultLogic = dyn_cast<sim::LogicType>(*resultType)) {
    SmallVector<Value> logicInputs;
    for (Value input : inputs) {
      FailureOr<Value> logic = toLogic(input, location);
      if (failed(logic))
        return failure();
      logicInputs.push_back(*logic);
    }
    return sim::SimLogicConcatOp::create(builder, location, resultLogic,
                                         logicInputs)
        .getResult();
  }
  auto resultInteger = cast<IntegerType>(*resultType);
  Value combined =
      arith::ConstantOp::create(builder, location, resultInteger,
                                builder.getIntegerAttr(resultInteger, 0));
  unsigned trailingWidth = resultInteger.getWidth();
  for (Value input : inputs) {
    auto inputInteger = cast<IntegerType>(input.getType());
    trailingWidth -= inputInteger.getWidth();
    FailureOr<Value> extended = convert(input, resultInteger, false, location);
    if (failed(extended))
      return failure();
    Value shifted = *extended;
    if (trailingWidth) {
      Value amount = arith::ConstantOp::create(
          builder, location, resultInteger,
          builder.getIntegerAttr(resultInteger, trailingWidth));
      shifted = arith::ShLIOp::create(builder, location, shifted, amount);
    }
    combined = arith::OrIOp::create(builder, location, combined, shifted);
  }
  return combined;
}

FailureOr<Value> UnitLowering::lowerReplication(Operation *op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2 || !isIntegerLiteral(children.front())) {
    unsupported(op) << " (nonconstant replication count)";
    return failure();
  }
  std::optional<StringRef> spelling = getConstantSpelling(children.front());
  if (!spelling) {
    unsupported(op) << " (nonconstant replication count)";
    return failure();
  }
  FailureOr<ParsedConstant> count = parseSVInteger(*spelling, 64, location);
  FailureOr<Value> input = lowerExpression(children[1]);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(count) || failed(input) || failed(resultType))
    return failure();
  if (!count->unknown.isZero() || count->value.isZero()) {
    emitError(location) << "replication count must be a known positive value";
    return failure();
  }
  uint64_t repetitions = count->value.getZExtValue();
  if (auto resultLogic = dyn_cast<sim::LogicType>(*resultType)) {
    FailureOr<Value> logicInput = toLogic(*input, location);
    if (failed(logicInput))
      return failure();
    return sim::SimLogicReplicateOp::create(
               builder, location, resultLogic, *logicInput,
               builder.getI64IntegerAttr(repetitions))
        .getResult();
  }
  auto resultInteger = cast<IntegerType>(*resultType);
  auto inputInteger = cast<IntegerType>((*input).getType());
  Value combined =
      arith::ConstantOp::create(builder, location, resultInteger,
                                builder.getIntegerAttr(resultInteger, 0));
  FailureOr<Value> extended = convert(*input, resultInteger, false, location);
  if (failed(extended))
    return failure();
  for (uint64_t index = 0; index < repetitions; ++index) {
    unsigned shift = inputInteger.getWidth() * (repetitions - index - 1);
    Value piece = *extended;
    if (shift) {
      Value amount = arith::ConstantOp::create(
          builder, location, resultInteger,
          builder.getIntegerAttr(resultInteger, shift));
      piece = arith::ShLIOp::create(builder, location, piece, amount);
    }
    combined = arith::OrIOp::create(builder, location, combined, piece);
  }
  return combined;
}

FailureOr<Value> UnitLowering::lowerSelection(Operation *op, bool lvalue) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  bool element = isa<semantic::SVElementSelectExpressionOp>(op);
  if (children.size() != (element ? 2u : 3u)) {
    unsupported(op) << " (selection arity)";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  FailureOr<Value> input = lowerExpression(children.front(), lvalue);
  if (failed(resultType) || failed(input))
    return failure();
  std::optional<unsigned> resultWidth = sim::getPackedWidth(*resultType);
  if (!resultWidth) {
    unsupported(op) << " (selection result type)";
    return failure();
  }
  auto sourceTypeAttr =
      children.front()->getAttrOfType<TypeAttr>("semantic_type");
  auto sourceRange =
      sourceTypeAttr
          ? dyn_cast<semantic::RangedPackedArrayType>(sourceTypeAttr.getValue())
          : semantic::RangedPackedArrayType{};
  int64_t sourceRight = sourceRange ? sourceRange.getRight() : 0;
  bool descending = !sourceRange || sourceRange.getLeft() >= sourceRight;
  semantic::SVRangeSelectionKind selectionKind =
      element
          ? semantic::SVRangeSelectionKind::Simple
          : cast<semantic::SVRangeSelectExpressionOp>(op).getSelectionKind();

  Type sourceValueType = (*input).getType();
  if (auto reference = dyn_cast<sim::RefType>(sourceValueType))
    sourceValueType = reference.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>(sourceValueType))
    sourceValueType = driver.getElementType();
  std::optional<unsigned> sourceWidth = sim::getPackedWidth(sourceValueType);
  if (!sourceWidth) {
    unsupported(op) << " (selection input type)";
    return failure();
  }

  // Selection offsets are signless bitvectors in the target IR, so normalize
  // source indices in a type wide enough that signed values, declared bounds,
  // and indexed-part adjustments cannot wrap back into the valid source
  // range. Two extra bits cover an unsigned index plus any signed 64-bit
  // boundary. Logic resizing retains the unknown plane.
  auto getIndexArithmeticType = [&](Type type) -> FailureOr<Type> {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || *width > std::numeric_limits<unsigned>::max() - 2) {
      emitError(location) << "selection index is too wide to normalize";
      return failure();
    }
    unsigned arithmeticWidth = std::max(*width, 64u) + 2;
    if (isa<sim::LogicType>(type))
      return sim::LogicType::get(function.getContext(), arithmeticWidth);
    if (isa<IntegerType>(type))
      return IntegerType::get(function.getContext(), arithmeticWidth);
    emitError(location) << "selection index is not a packed value: " << type;
    return failure();
  };
  auto createKnownIndex = [&](Type type, const APInt &value) -> Value {
    unsigned width = *sim::getPackedWidth(type);
    APInt resized = value.sextOrTrunc(width);
    auto planeType = IntegerType::get(function.getContext(), width);
    if (isa<IntegerType>(type))
      return arith::ConstantOp::create(
          builder, location, type, builder.getIntegerAttr(planeType, resized));
    return sim::SimLogicConstantOp::create(
        builder, location, type, builder.getIntegerAttr(planeType, resized),
        builder.getIntegerAttr(planeType, 0));
  };
  auto widenIndex = [&](Value value, Operation *source) -> FailureOr<Value> {
    FailureOr<Type> arithmeticType = getIndexArithmeticType(value.getType());
    if (failed(arithmeticType))
      return failure();
    return convert(value, *arithmeticType, isSignedNode(source), location);
  };
  auto subtract = [&](Value lhs, Value rhs) -> Value {
    if (isa<IntegerType>(lhs.getType()))
      return arith::SubIOp::create(builder, location, lhs, rhs);
    return sim::SimLogicBinaryOp::create(builder, location, lhs.getType(),
                                         sim::BinaryKind::Sub, lhs, rhs);
  };

  // Known source bounds and literals are 64-bit, and 66 signed bits hold the
  // exact difference of any unsigned 64-bit index and signed 64-bit boundary.
  constexpr unsigned constantOffsetWidth = 66;
  auto sourceOffset = [&](const APInt &index) -> APInt {
    APInt boundary(constantOffsetWidth, static_cast<uint64_t>(sourceRight),
                   true);
    return descending ? index - boundary : boundary - index;
  };
  auto extendLiteral = [&](const ParsedConstant &literal, Operation *source,
                           Type sourceType) -> APInt {
    APInt value = literal.value;
    if (std::optional<unsigned> sourceWidth = sim::getPackedWidth(sourceType);
        sourceWidth && *sourceWidth < value.getBitWidth())
      value = value.trunc(*sourceWidth);
    return isSignedNode(source) ? value.sextOrTrunc(constantOffsetWidth)
                                : value.zextOrTrunc(constantOffsetWidth);
  };

  bool literalIndex = isIntegerLiteral(children[1]);
  bool constant = false;
  uint64_t lowBit = 0;
  Value dynamicLow;
  if (literalIndex) {
    FailureOr<Type> firstIndexType = getNormalizedSemanticType(children[1]);
    if (failed(firstIndexType))
      return failure();
    FailureOr<ParsedConstant> first =
        parseSVInteger(*getConstantSpelling(children[1]), 64, location);
    if (failed(first))
      return failure();
    std::optional<APInt> knownLow;
    if (first->unknown.isZero())
      knownLow =
          sourceOffset(extendLiteral(*first, children[1], *firstIndexType));
    if (knownLow && !element) {
      if (!isIntegerLiteral(children[2])) {
        unsupported(op) << " (mixed constant and dynamic selection bounds)";
        return failure();
      }
      FailureOr<ParsedConstant> second =
          parseSVInteger(*getConstantSpelling(children[2]), 64, location);
      if (failed(second))
        return failure();
      if (second->unknown.isZero()) {
        FailureOr<Type> secondIndexType =
            getNormalizedSemanticType(children[2]);
        if (failed(secondIndexType))
          return failure();
        APInt secondLow =
            sourceOffset(extendLiteral(*second, children[2], *secondIndexType));
        if (secondLow.slt(*knownLow))
          knownLow = secondLow;
      } else {
        knownLow.reset();
      }
    }
    bool inRange =
        knownLow && !knownLow->isNegative() && *resultWidth <= *sourceWidth &&
        knownLow->ule(
            APInt(constantOffsetWidth,
                  static_cast<uint64_t>(*sourceWidth - *resultWidth)));
    if (inRange) {
      constant = true;
      lowBit = knownLow->getZExtValue();
    } else if (knownLow) {
      // A statically out-of-range selection still uses the dynamic operation:
      // its contract preserves valid positions and supplies the appropriate
      // X/zero fallback for invalid positions.
      FailureOr<Type> arithmeticType = getIndexArithmeticType(*firstIndexType);
      if (failed(arithmeticType))
        return failure();
      dynamicLow = createKnownIndex(*arithmeticType, *knownLow);
    }
  }
  if (!constant && !dynamicLow) {
    // Element selects use the same dynamic operation as indexed part-selects.
    // Only a simple part-select with dynamic bounds remains unsupported.
    if (!element && selectionKind == semantic::SVRangeSelectionKind::Simple) {
      unsupported(op) << " (dynamic simple range selection)";
      return failure();
    }
    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> widened = widenIndex(*index, children[1]);
    if (failed(widened))
      return failure();
    dynamicLow = *widened;
    unsigned arithmeticWidth = *sim::getPackedWidth(dynamicLow.getType());
    auto createKnownOffset = [&](int64_t value) -> Value {
      return createKnownIndex(
          dynamicLow.getType(),
          APInt(arithmeticWidth, static_cast<uint64_t>(value), true));
    };
    if (sourceRight != 0 || !descending) {
      Value boundary = createKnownOffset(sourceRight);
      dynamicLow = descending ? subtract(dynamicLow, boundary)
                              : subtract(boundary, dynamicLow);
    }
    bool baseNamesHighBit =
        (descending &&
         selectionKind == semantic::SVRangeSelectionKind::IndexedDown) ||
        (!descending &&
         selectionKind == semantic::SVRangeSelectionKind::IndexedUp);
    if (baseNamesHighBit && *resultWidth > 1) {
      Value adjustment = createKnownOffset(*resultWidth - 1);
      dynamicLow = subtract(dynamicLow, adjustment);
    }
  }

  if (isa<sim::RefType>((*input).getType())) {
    Type selected = sim::RefType::get(function.getContext(), *resultType);
    if (constant)
      return sim::SimRefExtractOp::create(builder, location, selected, *input,
                                          builder.getI64IntegerAttr(lowBit))
          .getResult();
    return sim::SimRefDynExtractOp::create(builder, location, selected, *input,
                                           dynamicLow)
        .getResult();
  }
  if (isa<sim::DriverType>((*input).getType())) {
    Type selected = sim::DriverType::get(function.getContext(), *resultType);
    if (constant)
      return sim::SimDriverExtractOp::create(builder, location, selected,
                                             *input,
                                             builder.getI64IntegerAttr(lowBit))
          .getResult();
    return sim::SimDriverDynExtractOp::create(builder, location, selected,
                                              *input, dynamicLow)
        .getResult();
  }
  if (isa<sim::LogicType>((*input).getType())) {
    auto selected = cast<sim::LogicType>(*resultType);
    if (constant)
      return sim::SimLogicExtractOp::create(builder, location, selected, *input,
                                            builder.getI64IntegerAttr(lowBit))
          .getResult();
    return sim::SimLogicDynExtractOp::create(builder, location, selected,
                                             *input, dynamicLow)
        .getResult();
  }
  auto selected = cast<IntegerType>(*resultType);
  auto inputInteger = cast<IntegerType>((*input).getType());
  if (!constant)
    return sim::SimBitsDynExtractOp::create(builder, location, selected, *input,
                                            dynamicLow)
        .getResult();
  Value amount =
      arith::ConstantOp::create(builder, location, inputInteger,
                                builder.getIntegerAttr(inputInteger, lowBit));
  Value shifted = arith::ShRUIOp::create(builder, location, *input, amount);
  if (selected == inputInteger)
    return shifted;
  return arith::TruncIOp::create(builder, location, selected, shifted)
      .getResult();
}

FailureOr<Value>
UnitLowering::lowerAssignment(semantic::SVAssignmentExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op.getOperatorKind()) {
    unsupported(op) << " (compound assignment)";
    return failure();
  }
  if (children.size() < 2) {
    unsupported(op) << " (assignment arity)";
    return failure();
  }
  FailureOr<Value> destination = lowerExpression(children[0], true);
  FailureOr<Value> rhs = lowerExpression(children[1]);
  if (failed(destination) || failed(rhs))
    return failure();
  Type elementType;
  if (auto ref = dyn_cast<sim::RefType>((*destination).getType()))
    elementType = ref.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>((*destination).getType()))
    elementType = driver.getElementType();
  else {
    emitError(location) << "assignment destination is not a ref or driver";
    return failure();
  }
  FailureOr<Value> value =
      convert(*rhs, elementType, isSignedNode(children[1]), location);
  if (failed(value))
    return failure();
  bool nonblocking =
      op.getAssignmentKind() == semantic::SVAssignmentKind::Nonblocking;
  if (isa<sim::RefType>((*destination).getType())) {
    if (nonblocking)
      sim::SimNBAEnqueueOp::create(builder, location, *value, *destination,
                                   Value{}, sim::NBASiteAttr{});
    else
      sim::SimRefStoreOp::create(builder, location, *value, *destination);
  } else {
    if (nonblocking) {
      emitError(location) << "nonblocking assignment cannot target a driver";
      return failure();
    }
    sim::SimDriverDriveOp::create(builder, location, *destination, *value);
  }
  return *value;
}

FailureOr<Value> UnitLowering::lowerUnary(semantic::SVUnaryExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 1) {
    unsupported(op) << " (unary arity)";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();

  semantic::SVUnaryOperator kind = op.getOperatorKind();
  bool increment = kind == semantic::SVUnaryOperator::Preincrement ||
                   kind == semantic::SVUnaryOperator::Postincrement;
  bool decrement = kind == semantic::SVUnaryOperator::Predecrement ||
                   kind == semantic::SVUnaryOperator::Postdecrement;
  if (increment || decrement) {
    FailureOr<Value> destination = lowerExpression(children.front(), true);
    if (failed(destination))
      return failure();
    if (!isa<sim::RefType>((*destination).getType())) {
      emitError(location) << "increment and decrement require a variable "
                             "reference";
      return failure();
    }
    auto reference = cast<sim::RefType>((*destination).getType());
    Value oldValue = sim::SimRefLoadOp::create(
        builder, location, reference.getElementType(), *destination);
    Value one;
    Value newValue;
    if (auto logic = dyn_cast<sim::LogicType>(oldValue.getType())) {
      auto planeType =
          IntegerType::get(function.getContext(), logic.getWidth());
      one = sim::SimLogicConstantOp::create(
          builder, location, logic, builder.getIntegerAttr(planeType, 1),
          builder.getIntegerAttr(planeType, 0));
      newValue = sim::SimLogicBinaryOp::create(builder, location, logic,
                                               increment ? sim::BinaryKind::Add
                                                         : sim::BinaryKind::Sub,
                                               oldValue, one);
    } else {
      auto integer = cast<IntegerType>(oldValue.getType());
      one = arith::ConstantOp::create(builder, location, integer,
                                      builder.getIntegerAttr(integer, 1));
      newValue =
          increment
              ? Value(arith::AddIOp::create(builder, location, oldValue, one))
              : Value(arith::SubIOp::create(builder, location, oldValue, one));
    }
    sim::SimRefStoreOp::create(builder, location, newValue, *destination);
    bool post = kind == semantic::SVUnaryOperator::Postincrement ||
                kind == semantic::SVUnaryOperator::Postdecrement;
    return convert(post ? oldValue : newValue, *resultType,
                   isSignedNode(children.front()), location);
  }

  FailureOr<Value> input = lowerExpression(children.front());
  if (failed(input))
    return failure();
  bool reduction = kind >= semantic::SVUnaryOperator::BitwiseAnd &&
                   kind <= semantic::SVUnaryOperator::BitwiseXnor;
  if (reduction) {
    sim::ReductionKind reductionKind;
    switch (kind) {
    case semantic::SVUnaryOperator::BitwiseAnd:
      reductionKind = sim::ReductionKind::And;
      break;
    case semantic::SVUnaryOperator::BitwiseOr:
      reductionKind = sim::ReductionKind::Or;
      break;
    case semantic::SVUnaryOperator::BitwiseXor:
      reductionKind = sim::ReductionKind::Xor;
      break;
    case semantic::SVUnaryOperator::BitwiseNand:
      reductionKind = sim::ReductionKind::Nand;
      break;
    case semantic::SVUnaryOperator::BitwiseNor:
      reductionKind = sim::ReductionKind::Nor;
      break;
    case semantic::SVUnaryOperator::BitwiseXnor:
      reductionKind = sim::ReductionKind::Xnor;
      break;
    default:
      llvm_unreachable("not a reduction operator");
    }
    if (isa<sim::LogicType>((*input).getType())) {
      Value value = sim::SimLogicReductionOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          reductionKind, *input);
      return convert(value, *resultType, false, location);
    }

    auto integer = cast<IntegerType>((*input).getType());
    Value reduced;
    bool invert = reductionKind == sim::ReductionKind::Nand ||
                  reductionKind == sim::ReductionKind::Nor ||
                  reductionKind == sim::ReductionKind::Xnor;
    if (reductionKind == sim::ReductionKind::And ||
        reductionKind == sim::ReductionKind::Nand) {
      Value ones = arith::ConstantOp::create(
          builder, location, integer,
          builder.getIntegerAttr(integer,
                                 APInt::getAllOnes(integer.getWidth())));
      reduced = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq, *input, ones);
    } else if (reductionKind == sim::ReductionKind::Or ||
               reductionKind == sim::ReductionKind::Nor) {
      Value zero = arith::ConstantOp::create(
          builder, location, integer, builder.getIntegerAttr(integer, 0));
      reduced = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::ne, *input, zero);
    } else {
      Value folded = *input;
      for (uint64_t shift = 1; shift < integer.getWidth(); shift <<= 1) {
        Value amount = arith::ConstantOp::create(
            builder, location, integer, builder.getIntegerAttr(integer, shift));
        Value shifted =
            arith::ShRUIOp::create(builder, location, folded, amount);
        folded = arith::XOrIOp::create(builder, location, folded, shifted);
      }
      reduced = integer.getWidth() == 1
                    ? folded
                    : Value(arith::TruncIOp::create(
                          builder, location, builder.getI1Type(), folded));
    }
    if (invert) {
      Value one = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      reduced = arith::XOrIOp::create(builder, location, reduced, one);
    }
    return convert(reduced, *resultType, false, location);
  }

  if (isa<sim::LogicType>((*input).getType())) {
    sim::UnaryKind normalized;
    switch (kind) {
    case semantic::SVUnaryOperator::Plus:
      normalized = sim::UnaryKind::Plus;
      break;
    case semantic::SVUnaryOperator::Minus:
      normalized = sim::UnaryKind::Negate;
      break;
    case semantic::SVUnaryOperator::BitwiseNot:
      normalized = sim::UnaryKind::BitNot;
      break;
    case semantic::SVUnaryOperator::LogicalNot:
      normalized = sim::UnaryKind::LogicalNot;
      break;
    default:
      unsupported(op) << " (unary operator)";
      return failure();
    }
    Type unaryResult = normalized == sim::UnaryKind::LogicalNot
                           ? *resultType
                           : (*input).getType();
    Value value = sim::SimLogicUnaryOp::create(builder, location, unaryResult,
                                               normalized, *input)
                      .getResult();
    return convert(value, *resultType, false, location);
  }

  Value value;
  switch (kind) {
  case semantic::SVUnaryOperator::Plus:
    value = *input;
    break;
  case semantic::SVUnaryOperator::Minus:
    value = arith::SubIOp::create(
        builder, location,
        arith::ConstantOp::create(
            builder, location, (*input).getType(),
            builder.getIntegerAttr((*input).getType(), 0)),
        *input);
    break;
  case semantic::SVUnaryOperator::BitwiseNot:
    value = arith::XOrIOp::create(
        builder, location, *input,
        arith::ConstantOp::create(
            builder, location, (*input).getType(),
            builder.getIntegerAttr((*input).getType(), -1)));
    break;
  case semantic::SVUnaryOperator::LogicalNot: {
    FailureOr<Value> truth = truthValue(*input, location);
    if (failed(truth))
      return failure();
    value = arith::XOrIOp::create(
        builder, location, *truth,
        arith::ConstantOp::create(builder, location, builder.getIntegerType(1),
                                  builder.getBoolAttr(true)));
    break;
  }
  default:
    unsupported(op) << " (integer unary operator)";
    return failure();
  }
  return convert(value, *resultType, false, location);
}

FailureOr<Value> UnitLowering::lowerBinary(semantic::SVBinaryExpressionOp op) {
  using Binary = semantic::SVBinaryOperator;
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2) {
    unsupported(op) << " (binary arity)";
    return failure();
  }
  FailureOr<Value> lhs = lowerExpression(children[0]);
  FailureOr<Value> rhs = lowerExpression(children[1]);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(lhs) || failed(rhs) || failed(resultType))
    return failure();
  Binary kind = op.getOperatorKind();
  bool signedOp = isSignedNode(children.front());

  if (kind == Binary::LogicalAnd || kind == Binary::LogicalOr) {
    if (isa<sim::LogicType>((*lhs).getType()) ||
        isa<sim::LogicType>((*rhs).getType())) {
      FailureOr<Value> logicLhs = toLogic(*lhs, location);
      FailureOr<Value> logicRhs = toLogic(*rhs, location);
      if (failed(logicLhs) || failed(logicRhs))
        return failure();
      Value logical = sim::SimLogicLogicalOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          kind == Binary::LogicalAnd ? sim::LogicalKind::And
                                     : sim::LogicalKind::Or,
          *logicLhs, *logicRhs);
      return convert(logical, *resultType, false, location);
    }
    FailureOr<Value> lhsTruth = truthValue(*lhs, location);
    FailureOr<Value> rhsTruth = truthValue(*rhs, location);
    if (failed(lhsTruth) || failed(rhsTruth))
      return failure();
    Value logical = kind == Binary::LogicalAnd
                        ? Value(arith::AndIOp::create(builder, location,
                                                      *lhsTruth, *rhsTruth))
                        : Value(arith::OrIOp::create(builder, location,
                                                     *lhsTruth, *rhsTruth));
    return convert(logical, *resultType, false, location);
  }

  if (isa<sim::LogicType>((*lhs).getType())) {
    std::optional<sim::CompareKind> compare;
    switch (kind) {
    case Binary::Equality:
      compare = sim::CompareKind::Eq;
      break;
    case Binary::Inequality:
      compare = sim::CompareKind::Ne;
      break;
    case Binary::CaseEquality:
      compare = sim::CompareKind::CaseEq;
      break;
    case Binary::CaseInequality:
      compare = sim::CompareKind::CaseNe;
      break;
    case Binary::GreaterThanEqual:
      compare = signedOp ? sim::CompareKind::SGE : sim::CompareKind::UGE;
      break;
    case Binary::GreaterThan:
      compare = signedOp ? sim::CompareKind::SGT : sim::CompareKind::UGT;
      break;
    case Binary::LessThanEqual:
      compare = signedOp ? sim::CompareKind::SLE : sim::CompareKind::ULE;
      break;
    case Binary::LessThan:
      compare = signedOp ? sim::CompareKind::SLT : sim::CompareKind::ULT;
      break;
    case Binary::WildcardEquality:
    case Binary::WildcardInequality:
      unsupported(op) << " (wildcard comparison)";
      return failure();
    default:
      break;
    }
    if (compare)
      return sim::SimLogicCompareOp::create(builder, location, *resultType,
                                            *compare, *lhs, *rhs)
          .getResult();

    std::optional<sim::ShiftKind> shift;
    switch (kind) {
    case Binary::LogicalShiftLeft:
    case Binary::ArithmeticShiftLeft:
      shift = sim::ShiftKind::Left;
      break;
    case Binary::LogicalShiftRight:
      shift = sim::ShiftKind::Right;
      break;
    case Binary::ArithmeticShiftRight:
      shift = sim::ShiftKind::RightArith;
      break;
    default:
      break;
    }
    if (shift) {
      Value shifted = sim::SimLogicShiftOp::create(
          builder, location, (*lhs).getType(), *shift, *lhs, *rhs);
      return convert(shifted, *resultType, signedOp, location);
    }

    sim::BinaryKind binary;
    switch (kind) {
    case Binary::Add:
      binary = sim::BinaryKind::Add;
      break;
    case Binary::Subtract:
      binary = sim::BinaryKind::Sub;
      break;
    case Binary::Multiply:
      binary = sim::BinaryKind::Mul;
      break;
    case Binary::Divide:
      binary = signedOp ? sim::BinaryKind::SDiv : sim::BinaryKind::UDiv;
      break;
    case Binary::Mod:
      binary = signedOp ? sim::BinaryKind::SMod : sim::BinaryKind::UMod;
      break;
    case Binary::BinaryAnd:
      binary = sim::BinaryKind::And;
      break;
    case Binary::BinaryOr:
      binary = sim::BinaryKind::Or;
      break;
    case Binary::BinaryXor:
      binary = sim::BinaryKind::Xor;
      break;
    case Binary::BinaryXnor:
      binary = sim::BinaryKind::Xnor;
      break;
    default:
      unsupported(op) << " (four-state binary operator)";
      return failure();
    }
    return sim::SimLogicBinaryOp::create(builder, location, *resultType, binary,
                                         *lhs, *rhs)
        .getResult();
  }

  std::optional<arith::CmpIPredicate> predicate;
  switch (kind) {
  case Binary::Equality:
  case Binary::CaseEquality:
    predicate = arith::CmpIPredicate::eq;
    break;
  case Binary::Inequality:
  case Binary::CaseInequality:
    predicate = arith::CmpIPredicate::ne;
    break;
  case Binary::GreaterThanEqual:
    predicate =
        signedOp ? arith::CmpIPredicate::sge : arith::CmpIPredicate::uge;
    break;
  case Binary::GreaterThan:
    predicate =
        signedOp ? arith::CmpIPredicate::sgt : arith::CmpIPredicate::ugt;
    break;
  case Binary::LessThanEqual:
    predicate =
        signedOp ? arith::CmpIPredicate::sle : arith::CmpIPredicate::ule;
    break;
  case Binary::LessThan:
    predicate =
        signedOp ? arith::CmpIPredicate::slt : arith::CmpIPredicate::ult;
    break;
  default:
    break;
  }
  if (predicate) {
    Value compared =
        arith::CmpIOp::create(builder, location, *predicate, *lhs, *rhs);
    return convert(compared, *resultType, false, location);
  }

  Value value;
  switch (kind) {
  case Binary::Add:
    value = arith::AddIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::Subtract:
    value = arith::SubIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::Multiply:
    value = arith::MulIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::Divide:
    value = signedOp
                ? Value(arith::DivSIOp::create(builder, location, *lhs, *rhs))
                : Value(arith::DivUIOp::create(builder, location, *lhs, *rhs));
    break;
  case Binary::Mod:
    value = signedOp
                ? Value(arith::RemSIOp::create(builder, location, *lhs, *rhs))
                : Value(arith::RemUIOp::create(builder, location, *lhs, *rhs));
    break;
  case Binary::BinaryAnd:
    value = arith::AndIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::BinaryOr:
    value = arith::OrIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::BinaryXor:
    value = arith::XOrIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::LogicalShiftLeft:
  case Binary::ArithmeticShiftLeft:
  case Binary::LogicalShiftRight:
  case Binary::ArithmeticShiftRight: {
    FailureOr<Value> amount = convert(*rhs, (*lhs).getType(), false, location);
    if (failed(amount))
      return failure();
    if (kind == Binary::LogicalShiftLeft || kind == Binary::ArithmeticShiftLeft)
      value = arith::ShLIOp::create(builder, location, *lhs, *amount);
    else if (kind == Binary::LogicalShiftRight)
      value = arith::ShRUIOp::create(builder, location, *lhs, *amount);
    else
      value = arith::ShRSIOp::create(builder, location, *lhs, *amount);
    break;
  }
  default:
    unsupported(op) << " (two-state binary operator)";
    return failure();
  }
  return convert(value, *resultType, signedOp, location);
}

FailureOr<Value> UnitLowering::lowerCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  auto callee = op->getAttrOfType<FlatSymbolRefAttr>(calleeAttrName);
  if (!callee) {
    unsupported(op) << " (indirect or system call)";
    return failure();
  }
  SmallVector<Value> operands{function.getBody().front().getArgument(0)};
  auto formals = op->getAttrOfType<ArrayAttr>(calleeFormalsAttrName);
  if (!formals || formals.size() != children.size()) {
    emitError(location)
        << "direct call has no complete frozen formal inventory";
    return failure();
  }
  struct CopyOut {
    Value destination;
    Type formalType;
    bool formalSigned;
  };
  SmallVector<CopyOut> copyOuts;
  for (auto [child, formalAttr] : llvm::zip_equal(children, formals)) {
    auto formal = cast<DictionaryAttr>(formalAttr);
    auto direction = static_cast<semantic::SVArgumentDirection>(
        formal.getAs<IntegerAttr>("direction").getInt());
    Type formalType = formal.getAs<TypeAttr>("type").getValue();
    bool formalSigned = formal.getAs<BoolAttr>("is_signed").getValue();
    bool isInput = direction == semantic::SVArgumentDirection::In;

    Operation *actual = child;
    if (!isInput)
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(child)) {
        SmallVector<Operation *> outputChildren = getChildren(assignment);
        if (outputChildren.size() == 2 &&
            isa<semantic::SVEmptyArgumentExpressionOp>(outputChildren[1]))
          actual = outputChildren.front();
      }

    if (isInput) {
      FailureOr<Value> argument = lowerExpression(actual);
      if (failed(argument))
        return failure();
      FailureOr<Value> converted =
          convert(*argument, formalType, isSignedNode(actual), location);
      if (failed(converted))
        return failure();
      operands.push_back(*converted);
      continue;
    }

    FailureOr<Value> destination = lowerExpression(actual, true);
    if (failed(destination))
      return failure();
    auto ref = dyn_cast<sim::RefType>((*destination).getType());
    if (!ref) {
      emitError(location)
          << "output, inout, and ref actuals must be variable references";
      return failure();
    }
    if (direction == semantic::SVArgumentDirection::Ref) {
      if (ref.getElementType() != formalType) {
        emitError(location)
            << "ref actual type must exactly match the formal type";
        return failure();
      }
      if (isa<BlockArgument>(*destination))
        sensitivity.insert(*destination);
      operands.push_back(*destination);
      continue;
    }

    Value initial;
    if (direction == semantic::SVArgumentDirection::Out) {
      initial = createDefaultValue(builder, location, formalType);
      if (!initial) {
        emitError(location)
            << "cannot materialize an output-formal default for type "
            << formalType;
        return failure();
      }
    } else {
      Value loaded = sim::SimRefLoadOp::create(
          builder, location, ref.getElementType(), *destination);
      FailureOr<Value> converted =
          convert(loaded, formalType, isSignedNode(actual), location);
      if (failed(converted))
        return failure();
      initial = *converted;
      if (isa<BlockArgument>(*destination))
        sensitivity.insert(*destination);
    }
    operands.push_back(initial);
    copyOuts.push_back({*destination, formalType, formalSigned});
  }

  if (auto captures = op->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
    for (Attribute captureAttr : captures) {
      StringRef path = cast<StringAttr>(captureAttr).getValue();
      Value capture = values.lookup(path);
      if (!capture) {
        emitError(location)
            << "direct callee capture has no frozen local binding: " << path;
        return failure();
      }
      if (isa<sim::RefType, sim::NetType>(capture.getType()) &&
          isa<BlockArgument>(capture))
        sensitivity.insert(capture);
      operands.push_back(capture);
    }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  SmallVector<Type> callResultTypes{*resultType};
  for (const CopyOut &copyOut : copyOuts)
    callResultTypes.push_back(copyOut.formalType);
  auto call = sim::SimCallOp::create(builder, location, callResultTypes, callee,
                                     operands, ArrayAttr{}, ArrayAttr{});
  for (auto [index, copyOut] : llvm::enumerate(copyOuts)) {
    auto destinationType =
        cast<sim::RefType>(copyOut.destination.getType()).getElementType();
    FailureOr<Value> converted =
        convert(call.getResult(index + 1), destinationType,
                copyOut.formalSigned, location);
    if (failed(converted))
      return failure();
    sim::SimRefStoreOp::create(builder, location, *converted,
                               copyOut.destination);
  }
  return call.getResults().front();
}

FailureOr<Value> UnitLowering::lowerExpression(Operation *op, bool lvalue) {
  if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(op))
    return lowerNamedValue(named, lvalue);
  if (isa<semantic::SVIntegerLiteralOp,
          semantic::SVUnbasedUnsizedIntegerLiteralOp>(op))
    return lowerLiteral(op);
  if (isa<semantic::SVConversionExpressionOp>(op)) {
    SmallVector<Operation *> children = getChildren(op);
    if (children.size() != 1) {
      unsupported(op) << " (conversion arity)";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    FailureOr<Type> target = getNormalizedSemanticType(op);
    if (failed(input) || failed(target))
      return failure();
    return convert(*input, *target, isSignedNode(children.front()),
                   getSemanticLocation(op));
  }
  if (isa<semantic::SVConcatenationExpressionOp>(op))
    return lowerConcatenation(op);
  if (isa<semantic::SVReplicationExpressionOp>(op))
    return lowerReplication(op);
  if (isa<semantic::SVRangeSelectExpressionOp,
          semantic::SVElementSelectExpressionOp>(op))
    return lowerSelection(op, lvalue);
  if (auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(op))
    return lowerAssignment(assignment);
  if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(op))
    return lowerUnary(unary);
  if (auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(op))
    return lowerBinary(binary);
  if (auto call = dyn_cast<semantic::SVCallExpressionOp>(op))
    return lowerCall(call);

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

LogicalResult UnitLowering::lowerTiming(Operation *control,
                                        Operation *statement) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  Block *continuation = addBlock();

  if (isa<semantic::SVDelayControlOp>(control)) {
    if (children.size() != 1) {
      unsupported(control) << " (delay arity)";
      return failure();
    }
    auto scaleAttr = function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
    if (!scaleAttr) {
      function.emitError("code unit has no frozen delay scale");
      return failure();
    }
    Value delay;
    if (isIntegerLiteral(children.front())) {
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(*getConstantSpelling(children.front()), 64, location);
      if (failed(parsed))
        return failure();
      if (!parsed->unknown.isZero()) {
        emitError(location) << "delay must be a known nonnegative constant";
        return failure();
      }
      APInt scaled = parsed->value.zextOrTrunc(128) *
                     APInt(128, scaleAttr.getValue().getZExtValue());
      if (scaled.ugt(APInt(128, static_cast<uint64_t>(
                                    std::numeric_limits<int64_t>::max())))) {
        emitError(location) << "scaled delay exceeds the simulation time range";
        return failure();
      }
      delay = sim::SimTimeConstantOp::create(
          builder, location, sim::TimeType::get(function.getContext()),
          builder.getI64IntegerAttr(scaled.getZExtValue()));
    } else {
      FailureOr<Value> amount = lowerExpression(children.front());
      if (failed(amount))
        return failure();
      delay = sim::SimTimeScaleOp::create(
          builder, location, sim::TimeType::get(function.getContext()), *amount,
          scaleAttr, builder.getBoolAttr(isSignedNode(children.front())));
    }
    sim::SimSuspendDelayOp::create(builder, location, delay,
                                   sim::TimingSiteAttr{}, ValueRange{},
                                   sim::ContinuationSiteAttr{}, continuation);
  } else if (isa<semantic::SVSignalEventControlOp,
                 semantic::SVEventListControlOp>(control)) {
    SmallVector<Operation *> events =
        isa<semantic::SVSignalEventControlOp>(control)
            ? SmallVector<Operation *>{control}
            : getChildren(control);
    SmallVector<Value> watched;
    SmallVector<int32_t> edges;
    for (Operation *eventOp : events) {
      auto event = dyn_cast<semantic::SVSignalEventControlOp>(eventOp);
      if (!event) {
        unsupported(eventOp) << " (event-list member)";
        return failure();
      }
      SmallVector<Operation *> eventChildren = getChildren(event);
      if (eventChildren.empty()) {
        unsupported(event) << " (missing event expression)";
        return failure();
      }
      if (event.getHasIff()) {
        unsupported(event) << " (event iff condition)";
        return failure();
      }
      if (!isAddressableExpression(eventChildren.front())) {
        unsupported(event) << " (computed edge expression)";
        return failure();
      }
      FailureOr<Value> handle = lowerExpression(eventChildren.front(), true);
      if (failed(handle))
        return failure();
      watched.push_back(*handle);
      edges.push_back(static_cast<int32_t>(event.getEdgeKind()));
    }
    if (watched.empty()) {
      unsupported(control) << " (empty event list)";
      return failure();
    }
    if (watched.size() == 1) {
      auto edge = static_cast<sim::EdgeKind>(edges.front());
      if (edge == sim::EdgeKind::Change)
        sim::SimSuspendChangeOp::create(
            builder, location, watched.front(), ValueRange{},
            sim::ContinuationSiteAttr{}, continuation);
      else
        sim::SimSuspendEdgeOp::create(builder, location, edge, watched.front(),
                                      ValueRange{}, sim::ContinuationSiteAttr{},
                                      continuation);
    } else {
      sim::SimSuspendAnyOp::create(builder, location, watched,
                                   builder.getDenseI32ArrayAttr(edges),
                                   sim::ContinuationSiteAttr{}, continuation);
    }
  } else {
    unsupported(control) << " (timing control)";
    return failure();
  }
  setCurrent(continuation);
  return lowerStatement(statement);
}

LogicalResult
UnitLowering::lowerConditional(semantic::SVConditionalStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op.getConditionCount() != 1 || children.size() < 2) {
    unsupported(op) << " (only one condition is currently representable)";
    return failure();
  }
  FailureOr<Value> conditionValue = lowerExpression(children[0]);
  if (failed(conditionValue))
    return failure();
  FailureOr<Value> condition = truthValue(*conditionValue, location);
  if (failed(condition))
    return failure();
  Block *thenBlock = addBlock();
  Block *elseBlock = addBlock();
  Block *mergeBlock = addBlock();
  cf::CondBranchOp::create(builder, location, *condition, thenBlock,
                           ValueRange{}, elseBlock, ValueRange{});
  setCurrent(thenBlock);
  if (failed(lowerStatement(children[1])))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(elseBlock);
  if (children.size() >= 3 && failed(lowerStatement(children[2])))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

LogicalResult UnitLowering::lowerCase(semantic::SVCaseStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op.getCheckKind() != semantic::SVUniquePriorityCheck::None ||
      op.getConditionKind() != semantic::SVCaseCondition::Normal ||
      children.empty()) {
    unsupported(op) << " (wildcard, inside, and pattern cases)";
    return failure();
  }
  size_t itemCount = op.getItemCount();
  bool hasDefault = op.getHasDefault();
  ArrayRef<int64_t> labelCounts = op.getItemLabelCounts();
  if (labelCounts.size() != itemCount) {
    unsupported(op) << " (missing case item boundaries)";
    return failure();
  }

  // The importer emits the selector, then every item's label expressions in
  // item order, then every item body followed by the default body. Split the
  // inventory by those counts rather than by inspecting each child's kind.
  int64_t totalLabels = 0;
  for (int64_t count : labelCounts) {
    if (count <= 0) {
      unsupported(op) << " (invalid case item boundaries)";
      return failure();
    }
    totalLabels += count;
  }
  size_t statementCount = itemCount + (hasDefault ? 1 : 0);
  if (children.size() !=
      1 + static_cast<size_t>(totalLabels) + statementCount) {
    unsupported(op) << " (malformed case item inventory)";
    return failure();
  }
  ArrayRef<Operation *> labels =
      ArrayRef<Operation *>(children).slice(1, totalLabels);
  ArrayRef<Operation *> statements =
      ArrayRef<Operation *>(children).take_back(statementCount);
  FailureOr<Value> selector = lowerExpression(children.front());
  if (failed(selector))
    return failure();
  Block *mergeBlock = addBlock();
  size_t nextLabel = 0;
  for (size_t item = 0; item < itemCount; ++item) {
    size_t labelCount = static_cast<size_t>(labelCounts[item]);
    ArrayRef<Operation *> itemLabels = labels.slice(nextLabel, labelCount);
    nextLabel += labelCount;
    Value matched;
    for (Operation *label : itemLabels) {
      Location labelLocation = getSemanticLocation(label);
      FailureOr<Value> candidate = lowerExpression(label);
      if (failed(candidate))
        return failure();
      FailureOr<Value> normalized =
          convert(*candidate, (*selector).getType(), false, labelLocation);
      if (failed(normalized))
        return failure();
      // A `case` label matches X and Z bits exactly, which is the four-state
      // case-equality predicate rather than ordinary equality.
      Value equal;
      if (isa<sim::LogicType>((*selector).getType()))
        equal = sim::SimLogicCompareOp::create(
            builder, labelLocation, builder.getI1Type(),
            sim::CompareKind::CaseEq, *selector, *normalized);
      else
        equal = arith::CmpIOp::create(builder, labelLocation,
                                      arith::CmpIPredicate::eq, *selector,
                                      *normalized);
      matched = matched ? Value(arith::OrIOp::create(builder, labelLocation,
                                                     matched, equal))
                        : equal;
    }
    Block *itemBlock = addBlock();
    Block *nextBlock = addBlock();
    cf::CondBranchOp::create(builder, location, matched, itemBlock,
                             ValueRange{}, nextBlock, ValueRange{});
    setCurrent(itemBlock);
    if (failed(lowerStatement(statements[item])))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(nextBlock);
  }
  if (hasDefault && failed(lowerStatement(statements.back())))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

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
  loopTargets.push_back({exitBlock, conditionBlock});
  setCurrent(bodyBlock);
  if (failed(lowerStatement(children[1])))
    return failure();
  emitBranch(conditionBlock);
  loopTargets.pop_back();
  setCurrent(exitBlock);
  return success();
}

LogicalResult UnitLowering::lowerFor(Operation *op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  // The semantic importer emits declaration initializers immediately before
  // the loop. The loop inventory is condition, step, then body.
  if (children.size() != 3) {
    unsupported(op) << " (expected condition, step, and body)";
    return failure();
  }
  Block *conditionBlock = addBlock();
  Block *bodyBlock = addBlock();
  Block *stepBlock = addBlock();
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

  loopTargets.push_back({exitBlock, stepBlock});
  setCurrent(bodyBlock);
  if (failed(lowerStatement(children[2])))
    return failure();
  emitBranch(stepBlock);

  setCurrent(stepBlock);
  if (failed(lowerStatement(children[1])))
    return failure();
  emitBranch(conditionBlock);
  loopTargets.pop_back();
  setCurrent(exitBlock);
  return success();
}

LogicalResult
UnitLowering::lowerVariableDeclaration(semantic::SVVariableDeclStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef path = op.getReferencedPath();
  Value destination = lvalues.lookup(path);
  if (!destination || !isa<sim::RefType>(destination.getType())) {
    emitError(location) << "variable declaration has no reference binding";
    return failure();
  }
  Value initial = localDefaults.lookup(path);
  if (!children.empty()) {
    if (!localDefaults.count(path)) {
      unsupported(op) << " (static local initializer)";
      return failure();
    }
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

LogicalResult UnitLowering::lowerStatement(Operation *op) {
  SmallVector<Operation *> children = getChildren(op);
  Location location = getSemanticLocation(op);
  builder.setInsertionPointToEnd(current);

  if (isa<semantic::SVEmptyStatementOp>(op))
    return success();
  if (isa<semantic::SVExpressionStatementOp>(op)) {
    if (children.size() != 1) {
      unsupported(op) << " (expression statement arity)";
      return failure();
    }
    return success(succeeded(lowerExpression(children.front())));
  }
  if (isa<semantic::SVBlockStatementOp, semantic::SVStatementListOp>(op))
    return lowerSequence(children);
  if (isa<semantic::SVTimedStatementOp>(op)) {
    if (children.size() != 2) {
      unsupported(op) << " (timed statement arity)";
      return failure();
    }
    return lowerTiming(children[0], children[1]);
  }
  if (auto conditional = dyn_cast<semantic::SVConditionalStatementOp>(op))
    return lowerConditional(conditional);
  if (auto caseStatement = dyn_cast<semantic::SVCaseStatementOp>(op))
    return lowerCase(caseStatement);
  if (isa<semantic::SVWhileLoopStatementOp>(op))
    return lowerWhile(op);
  if (isa<semantic::SVForLoopStatementOp>(op))
    return lowerFor(op);
  if (isa<semantic::SVBreakStatementOp>(op)) {
    if (loopTargets.empty()) {
      emitError(location) << "break is not nested in a loop";
      return failure();
    }
    cf::BranchOp::create(builder, location, loopTargets.back().first);
    setCurrent(addBlock());
    return success();
  }
  if (isa<semantic::SVContinueStatementOp>(op)) {
    if (loopTargets.empty()) {
      emitError(location) << "continue is not nested in a loop";
      return failure();
    }
    cf::BranchOp::create(builder, location, loopTargets.back().second);
    setCurrent(addBlock());
    return success();
  }
  if (isa<semantic::SVReturnStatementOp>(op)) {
    std::optional<Value> result;
    bool resultSigned = false;
    if (!children.empty()) {
      FailureOr<Value> value = lowerExpression(children.front());
      if (failed(value))
        return failure();
      result = *value;
      resultSigned = isSignedNode(children.front());
    }
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

  // An expression used directly as a statement, or an unrecognized node, for
  // which lowerExpression emits the same diagnostic.
  return success(succeeded(lowerExpression(op)));
}

LogicalResult UnitLowering::lower(ArrayRef<Operation *> roots) {
  setCurrent(&function.getBody().front());
  sim::EntryKind entryKind = function.getEntryKind();
  bool loopsForever = entryKind == sim::EntryKind::Always ||
                      entryKind == sim::EntryKind::AlwaysComb ||
                      entryKind == sim::EntryKind::AlwaysFF ||
                      entryKind == sim::EntryKind::AlwaysLatch ||
                      entryKind == sim::EntryKind::Continuous;
  Block *loopHeader = nullptr;
  if (loopsForever) {
    loopHeader = addBlock();
    emitBranch(loopHeader);
    setCurrent(loopHeader);
  }
  if (failed(lowerSequence(roots)))
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
    if (entryKind == sim::EntryKind::Function)
      return emitFunctionReturn(function.getLoc(), std::nullopt);
    sim::SimReturnOp::create(builder, function.getLoc(), ValueRange{});
    return success();
  }

  // An implicitly sensitive process waits on everything it read; an explicitly
  // timed `always` block re-enters its own timing control instead.
  if (entryKind != sim::EntryKind::AlwaysComb &&
      entryKind != sim::EntryKind::AlwaysLatch &&
      entryKind != sim::EntryKind::Continuous) {
    cf::BranchOp::create(builder, function.getLoc(), loopHeader);
    return success();
  }
  if (sensitivity.empty()) {
    function.emitError("combinational process has no sensitivity capture");
    return failure();
  }
  if (sensitivity.size() == 1) {
    sim::SimSuspendChangeOp::create(builder, function.getLoc(),
                                    sensitivity.front(), ValueRange{},
                                    sim::ContinuationSiteAttr{}, loopHeader);
    return success();
  }
  SmallVector<int32_t> edges(sensitivity.size(),
                             static_cast<int32_t>(sim::EdgeKind::Change));
  sim::SimSuspendAnyOp::create(builder, function.getLoc(),
                               sensitivity.getArrayRef(),
                               builder.getDenseI32ArrayAttr(edges),
                               sim::ContinuationSiteAttr{}, loopHeader);
  return success();
}

class ObeliskSimLowerUnitPass
    : public impl::ObeliskSimLowerUnitPassBase<ObeliskSimLowerUnitPass> {
public:
  void runOnOperation() override {
    sim::SimFuncOp function = getOperation();
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      return;

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

    UnitLowering lowering(function);
    LogicalResult result = lowering.lower(sourceRoots);
    for (Operation *source : sourceRoots)
      source->erase();
    if (failed(result))
      signalPassFailure();
  }
};

} // namespace
} // namespace obelisk
