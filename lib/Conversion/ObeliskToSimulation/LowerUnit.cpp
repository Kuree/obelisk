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
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <cmath>
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

static Operation *getSingleRegionRoot(Region &region) {
  if (region.empty() || region.front().empty())
    return nullptr;
  return &region.front().front();
}

/// True when an expression denotes storage rather than a computed value, so a
/// suspension can watch it directly.
static bool isAddressableExpression(Operation *op) {
  if (isa<semantic::SVNamedValueExpressionOp,
          semantic::SVHierarchicalValueExpressionOp>(op))
    return true;
  if (isa<semantic::SVMemberAccessExpressionOp>(op)) {
    SmallVector<Operation *> children = getChildren(op);
    return !children.empty() && isAddressableExpression(children.front());
  }
  if (!isa<semantic::SVElementSelectExpressionOp,
           semantic::SVRangeSelectExpressionOp>(op))
    return false;
  SmallVector<Operation *> children = getChildren(op);
  size_t expected =
      isa<semantic::SVElementSelectExpressionOp>(op) ? 2u : 3u;
  if (children.size() != expected ||
      !isAddressableExpression(children.front()))
    return false;
  // A direct scheduler subscription captures one stable handle. Dynamic
  // indices require a computed observer so changes to the index can both
  // trigger and retarget the expression.
  return llvm::all_of(ArrayRef<Operation *>(children).drop_front(),
                      [](Operation *index) {
                        return getConstantSpelling(index).has_value();
                      });
}

class UnitLowering {
public:
  explicit UnitLowering(sim::SimFuncOp function);

  LogicalResult lower(ArrayRef<Operation *> roots);

private:
  FailureOr<Value> lowerExpression(Operation *op, bool lvalue = false);
  FailureOr<Value> lowerNamedValue(semantic::SVNamedValueExpressionOp op,
                                   bool lvalue);
  FailureOr<Value> lowerReferencedValue(Operation *op, StringRef path,
                                        bool lvalue);
  FailureOr<Value> lowerLiteral(Operation *op);
  FailureOr<Value> lowerConcatenation(Operation *op);
  FailureOr<Value> lowerReplication(Operation *op);
  FailureOr<Value> lowerMember(semantic::SVMemberAccessExpressionOp op,
                               bool lvalue);
  FailureOr<Value> lowerTaggedUnion(semantic::SVTaggedUnionExpressionOp op);
  FailureOr<Value> lowerAssignmentPattern(Operation *op);
  FailureOr<Value> lowerSelection(Operation *op, bool lvalue);
  FailureOr<Value> lowerAssignment(semantic::SVAssignmentExpressionOp op);
  LogicalResult writeLValue(Operation *destination, Value value,
                            bool sourceSigned, bool nonblocking,
                            Location location, Value delay = {});
  FailureOr<Value> lowerUnary(semantic::SVUnaryExpressionOp op);
  FailureOr<Value> lowerBinary(semantic::SVBinaryExpressionOp op);
  FailureOr<Value> lowerCall(semantic::SVCallExpressionOp op);
  FailureOr<Value> lowerSystemCall(semantic::SVCallExpressionOp op);
  LogicalResult lowerPortConnection(semantic::SVPortConnectionOp op);

  LogicalResult lowerStatement(Operation *op);
  LogicalResult lowerSequence(ArrayRef<Operation *> operations);
  LogicalResult lowerConditional(semantic::SVConditionalStatementOp op);
  LogicalResult lowerCase(semantic::SVCaseStatementOp op);
  LogicalResult lowerWhile(Operation *op);
  LogicalResult lowerFor(Operation *op);
  LogicalResult lowerRepeat(Operation *op);
  LogicalResult lowerFork(semantic::SVBlockStatementOp op);
  LogicalResult lowerBlock(semantic::SVBlockStatementOp op);
  LogicalResult lowerDisable(semantic::SVDisableStatementOp op);
  FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>>
  outlineForkBranch(Operation *branch, uint64_t forkNode, unsigned branchIndex);
  LogicalResult
  lowerVariableDeclaration(semantic::SVVariableDeclStatementOp op);
  LogicalResult lowerTiming(Operation *control, Operation *statement);
  LogicalResult emitEventSuspend(Operation *control, Block *continuation,
                                 ValueRange continuationOperands = {});
  LogicalResult emitRepeatedEventSuspend(Operation *control,
                                         Block *continuation,
                                         ValueRange continuationOperands = {});
  FailureOr<Value> lowerDelayValue(Operation *control);
  LogicalResult lowerWait(semantic::SVWaitStatementOp op);
  LogicalResult lowerEventTrigger(semantic::SVEventTriggerStatementOp op);
  void recordSensitivity(Value value);

  FailureOr<Value> convert(Value value, Type targetType, bool sourceSigned,
                           Location location);
  FailureOr<Value> toPackedScalar(Value value, Location location);
  FailureOr<Value> truthValue(Value value, Location location);
  FailureOr<Value> toLogic(Value value, Location location);
  LogicalResult emitFunctionReturn(Location location,
                                   std::optional<Value> explicitResult,
                                   bool resultSigned = false);
  Block *addBlock();
  void setCurrent(Block *block);
  void emitBranch(Block *destination);
  void emitControlLeaves(size_t first, Location location);
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
  llvm::DenseMap<uint64_t, Value> nodeLvalues;
  llvm::StringMap<Value> localDefaults;
  llvm::StringSet<> automaticLocals;
  llvm::StringMap<Value> copyOutDestinations;
  llvm::SetVector<Value> sensitivity;
  llvm::SetVector<Value> *observedDependencies = nullptr;
  Value expressionPlaceholder;
  std::string returnPath;
  SmallVector<std::string> copyOutPaths;
  struct LoopTargets {
    Block *breakTarget;
    Block *continueTarget;
    SmallVector<Value> continueOperands;
    size_t controlDepth;
  };
  SmallVector<LoopTargets> loopTargets;
  struct ControlScope {
    std::string path;
    uint64_t targetID;
    Value activation;
    Block *exit;
  };
  SmallVector<ControlScope> controlScopes;
  llvm::StringMap<uint64_t> inheritedControlIDs;
  uint64_t nextForkOrdinal = 0;
};

UnitLowering::UnitLowering(sim::SimFuncOp function)
    : function(function), builder(function.getContext()),
      current(&function.getBody().front()) {
  builder.setInsertionPointToStart(current);
  auto bindings = function->getAttrOfType<ArrayAttr>(bindingsAttrName);
  if (auto inherited =
          function->getAttrOfType<ArrayAttr>("inherited_controls"))
    for (Attribute attribute : inherited) {
      auto entry = dyn_cast<DictionaryAttr>(attribute);
      auto path = entry ? entry.getAs<StringAttr>("path") : StringAttr{};
      auto id = entry ? entry.getAs<IntegerAttr>("id") : IntegerAttr{};
      if (path && id)
        inheritedControlIDs[path.getValue()] =
            id.getValue().getZExtValue();
    }
  if (!bindings)
    return;
  for (Attribute attr : bindings) {
    auto dictionary = cast<DictionaryAttr>(attr);
    StringRef path = dictionary.getAs<StringAttr>("path").getValue();
    if (auto argument = dictionary.getAs<IntegerAttr>("argument")) {
      Value value = function.getBody().front().getArgument(
          argument.getValue().getZExtValue());
      if (dictionary.contains("copy_out_destination")) {
        copyOutDestinations[path] = value;
        continue;
      }
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
        if (auto node = dictionary.getAs<IntegerAttr>("lvalue_node_id"))
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
        }
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
    localDefaults[path] = initial;
    bool isReturn = dictionary.contains("is_return");
    if (isReturn)
      returnPath = path.str();
    if (dictionary.contains("automatic")) {
      automaticLocals.insert(path);
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

void UnitLowering::setCurrent(Block *block) {
  current = block;
  builder.setInsertionPointToEnd(block);
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
  if (!isa<sim::RefType, sim::NetType>(value.getType()))
    return;
  if (observedDependencies)
    observedDependencies->insert(value);
  if (auto argument = dyn_cast<BlockArgument>(value);
      argument && argument.getOwner() == &function.getBody().front())
    sensitivity.insert(value);
}

//===----------------------------------------------------------------------===//
// Normalized value conversions
//===----------------------------------------------------------------------===//

FailureOr<Value> UnitLowering::convert(Value value, Type targetType,
                                       bool sourceSigned, Location location) {
  if (value.getType() == targetType)
    return value;
  if (sim::isAggregateType(value.getType())) {
    Type scalarType = sim::getPackedScalarType(value.getType());
    if (!scalarType) {
      emitError(location) << "cannot convert unpacked aggregate "
                          << value.getType() << " to " << targetType;
      return failure();
    }
    Value flattened =
        sim::SimPackedFlattenOp::create(builder, location, scalarType, value);
    return convert(flattened, targetType, sourceSigned, location);
  }
  if (sim::isAggregateType(targetType)) {
    Type scalarType = sim::getPackedScalarType(targetType);
    if (!scalarType) {
      emitError(location) << "cannot convert " << value.getType()
                          << " to unpacked aggregate " << targetType;
      return failure();
    }
    FailureOr<Value> converted =
        convert(value, scalarType, sourceSigned, location);
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

FailureOr<Value> UnitLowering::truthValue(Value value, Location location) {
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
      if (!storage || !destination ||
          !isa<sim::RefType>(storage.getType()) ||
          storage.getType() != destination.getType()) {
        function.emitError()
            << "task copy-out formal has inconsistent activation storage: "
            << path;
        return failure();
      }
      Value value = sim::SimRefLoadOp::create(
          builder, location,
          cast<sim::RefType>(storage.getType()).getElementType(), storage);
      sim::SimRefStoreOp::create(builder, location, value, destination);
    }
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
  return lowerReferencedValue(op, op.getReferencedPath(), lvalue);
}

FailureOr<Value> UnitLowering::lowerReferencedValue(Operation *op,
                                                    StringRef path,
                                                    bool lvalue) {
  Location location = getSemanticLocation(op);
  Value value;
  if (lvalue)
    if (auto node = op->getAttrOfType<IntegerAttr>("node_id"))
      value = nodeLvalues.lookup(node.getValue().getZExtValue());
  if (!value)
    value = lvalue ? lvalues.lookup(path) : values.lookup(path);
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
  if (auto ref = dyn_cast<sim::RefType>(value.getType())) {
    recordSensitivity(value);
    return sim::SimRefLoadOp::create(builder, location, ref.getElementType(),
                                     value)
        .getResult();
  }
  if (auto net = dyn_cast<sim::NetType>(value.getType())) {
    recordSensitivity(value);
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
  Type scalarType = sim::getPackedScalarType(*type);
  std::optional<unsigned> width = sim::getPackedWidth(scalarType);
  std::optional<StringRef> spelling = getConstantSpelling(op);
  if (!width || !spelling) {
    unsupported(op) << " (integer literal representation)";
    return failure();
  }
  FailureOr<ParsedConstant> parsed =
      parseSVInteger(*spelling, *width, location);
  if (failed(parsed))
    return failure();
  Value value;
  if (auto integer = dyn_cast<IntegerType>(scalarType))
    value = arith::ConstantOp::create(
        builder, location, integer,
        builder.getIntegerAttr(integer, parsed->value));
  else {
    auto planeType = IntegerType::get(op->getContext(), *width);
    value = sim::SimLogicConstantOp::create(
        builder, location, scalarType,
        builder.getIntegerAttr(planeType, parsed->value),
        builder.getIntegerAttr(planeType, parsed->unknown));
  }
  return convert(value, *type, isSignedNode(op), location);
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
  Type scalarResultType = sim::getPackedScalarType(*resultType);
  if (!scalarResultType) {
    unsupported(op) << " (unpacked concatenation result)";
    return failure();
  }
  SmallVector<Value> inputs;
  for (Operation *child : children) {
    FailureOr<Value> input = lowerExpression(child);
    if (failed(input))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*input, location);
    if (failed(scalar))
      return failure();
    inputs.push_back(*scalar);
  }
  if (auto resultLogic = dyn_cast<sim::LogicType>(scalarResultType)) {
    SmallVector<Value> logicInputs;
    for (Value input : inputs) {
      FailureOr<Value> logic = toLogic(input, location);
      if (failed(logic))
        return failure();
      logicInputs.push_back(*logic);
    }
    Value result = sim::SimLogicConcatOp::create(builder, location, resultLogic,
                                                 logicInputs);
    return convert(result, *resultType, false, location);
  }
  auto resultInteger = cast<IntegerType>(scalarResultType);
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
  return convert(combined, *resultType, false, location);
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
  FailureOr<Value> scalarInput = toPackedScalar(*input, location);
  Type scalarResultType = sim::getPackedScalarType(*resultType);
  if (failed(scalarInput) || !scalarResultType) {
    if (succeeded(scalarInput))
      unsupported(op) << " (unpacked replication result)";
    return failure();
  }
  input = *scalarInput;
  if (!count->unknown.isZero() || count->value.isZero()) {
    emitError(location) << "replication count must be a known positive value";
    return failure();
  }
  uint64_t repetitions = count->value.getZExtValue();
  if (auto resultLogic = dyn_cast<sim::LogicType>(scalarResultType)) {
    FailureOr<Value> logicInput = toLogic(*input, location);
    if (failed(logicInput))
      return failure();
    Value result = sim::SimLogicReplicateOp::create(
        builder, location, resultLogic, *logicInput,
        builder.getI64IntegerAttr(repetitions));
    return convert(result, *resultType, false, location);
  }
  auto resultInteger = cast<IntegerType>(scalarResultType);
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
  return convert(combined, *resultType, false, location);
}

FailureOr<Value>
UnitLowering::lowerMember(semantic::SVMemberAccessExpressionOp op,
                          bool lvalue) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 1) {
    unsupported(op) << " (member access arity)";
    return failure();
  }
  auto ordinalAttr = op->getAttrOfType<IntegerAttr>("field_ordinal");
  if (!ordinalAttr || ordinalAttr.getValue().isNegative() ||
      ordinalAttr.getValue().getActiveBits() > 32) {
    emitError(location) << "member access has no valid declaration ordinal";
    return failure();
  }
  unsigned ordinal = ordinalAttr.getValue().getZExtValue();
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  FailureOr<Value> input = lowerExpression(children.front(), lvalue);
  if (failed(resultType) || failed(input))
    return failure();
  Type inputValueType = (*input).getType();
  if (auto reference = dyn_cast<sim::RefType>(inputValueType)) {
    if (sim::getAggregateElementType(reference.getElementType(), ordinal) !=
        *resultType) {
      emitError(location) << "member ordinal does not match the aggregate type";
      return failure();
    }
    Type selected = sim::RefType::get(function.getContext(), *resultType);
    return sim::SimRefSubelementOp::create(
               builder, location, selected, *input,
               builder.getDenseI64ArrayAttr({static_cast<int64_t>(ordinal)}))
        .getResult();
  }
  if (auto driver = dyn_cast<sim::DriverType>(inputValueType)) {
    if (sim::getAggregateElementType(driver.getElementType(), ordinal) !=
        *resultType) {
      emitError(location) << "member ordinal does not match the aggregate type";
      return failure();
    }
    Type selected = sim::DriverType::get(function.getContext(), *resultType);
    return sim::SimDriverSubelementOp::create(
               builder, location, selected, *input,
               builder.getDenseI64ArrayAttr({static_cast<int64_t>(ordinal)}))
        .getResult();
  }
  if (sim::getAggregateElementType(inputValueType, ordinal) != *resultType) {
    emitError(location) << "member access input is not a matching aggregate";
    return failure();
  }
  if (isa<sim::PackedUnionType, sim::UnpackedUnionType>(inputValueType))
    return sim::SimUnionExtractOp::create(builder, location, *resultType,
                                          *input, ordinal)
        .getResult();
  return sim::SimAggregateExtractOp::create(builder, location, *resultType,
                                            *input, ordinal)
      .getResult();
}

FailureOr<Value>
UnitLowering::lowerTaggedUnion(semantic::SVTaggedUnionExpressionOp op) {
  Location location = getSemanticLocation(op);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  bool tagged = false;
  if (auto packed = dyn_cast<sim::PackedUnionType>(*resultType))
    tagged = packed.getIsTagged();
  else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(*resultType))
    tagged = unpacked.getIsTagged();
  else {
    unsupported(op) << " (result is not a union)";
    return failure();
  }
  if (!tagged) {
    emitError(location) << "tagged union expression has an untagged result";
    return failure();
  }

  auto ordinalAttr = op->getAttrOfType<IntegerAttr>("field_ordinal");
  if (!ordinalAttr || ordinalAttr.getValue().isNegative() ||
      ordinalAttr.getValue().getActiveBits() > 32) {
    emitError(location)
        << "tagged union expression has no valid declaration ordinal";
    return failure();
  }
  unsigned ordinal = ordinalAttr.getValue().getZExtValue();
  Type fieldType = sim::getAggregateElementType(*resultType, ordinal);
  if (!fieldType) {
    emitError(location) << "tagged union member ordinal is out of range";
    return failure();
  }

  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 1) {
    unsupported(op) << " (void tagged-union members are not normalized)";
    return failure();
  }
  FailureOr<Value> value = lowerExpression(children.front());
  if (failed(value))
    return failure();
  FailureOr<Value> converted =
      convert(*value, fieldType, isSignedNode(children.front()), location);
  if (failed(converted))
    return failure();
  return sim::SimUnionConstructOp::create(builder, location, *resultType,
                                          *converted, ordinal)
      .getResult();
}

FailureOr<Value> UnitLowering::lowerAssignmentPattern(Operation *op) {
  Location location = getSemanticLocation(op);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType) || !sim::isAggregateType(*resultType)) {
    unsupported(op) << " (non-aggregate assignment pattern)";
    return failure();
  }
  SmallVector<Operation *> children = getChildren(op);
  if (isa<sim::PackedUnionType, sim::UnpackedUnionType>(*resultType)) {
    if (children.size() != 1) {
      unsupported(op) << " (union assignment pattern arity)";
      return failure();
    }
    FailureOr<Value> value = lowerExpression(children.front());
    Type fieldType = sim::getAggregateElementType(*resultType, 0);
    if (failed(value))
      return failure();
    FailureOr<Value> converted =
        convert(*value, fieldType, isSignedNode(children.front()), location);
    if (failed(converted))
      return failure();
    return sim::SimUnionConstructOp::create(builder, location, *resultType,
                                            *converted, 0)
        .getResult();
  }
  if (children.size() != sim::getAggregateNumElements(*resultType)) {
    unsupported(op) << " (assignment pattern element inventory)";
    return failure();
  }
  SmallVector<Value> elements;
  for (auto [index, child] : llvm::enumerate(children)) {
    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    FailureOr<Value> converted =
        convert(*value, sim::getAggregateElementType(*resultType, index),
                isSignedNode(child), location);
    if (failed(converted))
      return failure();
    elements.push_back(*converted);
  }
  return sim::SimAggregateConstructOp::create(builder, location, *resultType,
                                              elements)
      .getResult();
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

  Type sourceValueType = (*input).getType();
  if (auto reference = dyn_cast<sim::RefType>(sourceValueType))
    sourceValueType = reference.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>(sourceValueType))
    sourceValueType = driver.getElementType();

  // Fixed packed and unpacked arrays remain first-class aggregates. Their
  // dynamic operation consumes a source index, while static views use a
  // declaration-order ordinal.
  if (element &&
      isa<sim::PackedArrayType, sim::UnpackedArrayType>(sourceValueType)) {
    std::optional<unsigned> ordinal;
    if (isIntegerLiteral(children[1])) {
      FailureOr<Type> indexType = getNormalizedSemanticType(children[1]);
      std::optional<unsigned> indexWidth =
          succeeded(indexType) ? sim::getPackedWidth(*indexType) : std::nullopt;
      if (failed(indexType) || !indexWidth)
        return failure();
      FailureOr<ParsedConstant> parsed = parseSVInteger(
          *getConstantSpelling(children[1]), *indexWidth, location);
      if (failed(parsed))
        return failure();
      if (parsed->unknown.isZero()) {
        APInt index = isSignedNode(children[1]) ? parsed->value.sextOrTrunc(65)
                                                : parsed->value.zextOrTrunc(65);
        if (index.isSignedIntN(64))
          ordinal = sim::getArrayElementOrdinal(sourceValueType,
                                                index.getSExtValue());
      }
    }
    if (ordinal) {
      if (auto reference = dyn_cast<sim::RefType>((*input).getType()))
        return sim::SimRefSubelementOp::create(
                   builder, location,
                   sim::RefType::get(function.getContext(), *resultType),
                   *input,
                   builder.getDenseI64ArrayAttr(
                       {static_cast<int64_t>(*ordinal)}))
            .getResult();
      if (auto driver = dyn_cast<sim::DriverType>((*input).getType()))
        return sim::SimDriverSubelementOp::create(
                   builder, location,
                   sim::DriverType::get(function.getContext(), *resultType),
                   *input,
                   builder.getDenseI64ArrayAttr(
                       {static_cast<int64_t>(*ordinal)}))
            .getResult();
      return sim::SimAggregateExtractOp::create(builder, location, *resultType,
                                                *input, *ordinal)
          .getResult();
    }

    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> scalarIndex = toPackedScalar(*index, location);
    if (failed(scalarIndex))
      return failure();
    index = *scalarIndex;
    std::optional<unsigned> indexWidth =
        sim::getPackedWidth((*index).getType());
    if (!indexWidth || *indexWidth > std::numeric_limits<unsigned>::max() - 1) {
      emitError(location) << "array index is too wide to normalize";
      return failure();
    }
    unsigned widenedWidth = std::max(*indexWidth, 64u) + 1;
    Type widenedType =
        isa<sim::LogicType>((*index).getType())
            ? Type(sim::LogicType::get(function.getContext(), widenedWidth))
            : Type(IntegerType::get(function.getContext(), widenedWidth));
    FailureOr<Value> widened =
        convert(*index, widenedType, isSignedNode(children[1]), location);
    if (failed(widened))
      return failure();
    if (isa<sim::RefType>((*input).getType()))
      return sim::SimRefArrayElementOp::create(
                 builder, location,
                 sim::RefType::get(function.getContext(), *resultType), *input,
                 *widened)
          .getResult();
    if (isa<sim::DriverType>((*input).getType()))
      return sim::SimDriverArrayElementOp::create(
                 builder, location,
                 sim::DriverType::get(function.getContext(), *resultType),
                 *input, *widened)
          .getResult();
    return sim::SimArrayDynExtractOp::create(builder, location, *resultType,
                                             *input, *widened)
        .getResult();
  }

  Type scalarResultType = sim::getPackedScalarType(*resultType);
  std::optional<unsigned> resultWidth = sim::getPackedWidth(scalarResultType);
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
    Type scalarType = sim::getPackedScalarType(type);
    std::optional<unsigned> width = sim::getPackedWidth(scalarType);
    if (!width || *width > std::numeric_limits<unsigned>::max() - 2) {
      emitError(location) << "selection index is too wide to normalize";
      return failure();
    }
    unsigned arithmeticWidth = std::max(*width, 64u) + 2;
    if (isa<sim::LogicType>(scalarType))
      return sim::LogicType::get(function.getContext(), arithmeticWidth);
    if (isa<IntegerType>(scalarType))
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
    FailureOr<Value> scalar = toPackedScalar(value, location);
    if (failed(scalar))
      return failure();
    FailureOr<Type> arithmeticType =
        getIndexArithmeticType((*scalar).getType());
    if (failed(arithmeticType))
      return failure();
    return convert(*scalar, *arithmeticType, isSignedNode(source), location);
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
  FailureOr<Value> scalarInput = toPackedScalar(*input, location);
  if (failed(scalarInput))
    return failure();
  input = *scalarInput;
  if (isa<sim::LogicType>((*input).getType())) {
    auto selected = cast<sim::LogicType>(scalarResultType);
    Value value;
    if (constant)
      value =
          sim::SimLogicExtractOp::create(builder, location, selected, *input,
                                         builder.getI64IntegerAttr(lowBit));
    else
      value = sim::SimLogicDynExtractOp::create(builder, location, selected,
                                                *input, dynamicLow);
    return convert(value, *resultType, false, location);
  }
  auto selected = cast<IntegerType>(scalarResultType);
  auto inputInteger = cast<IntegerType>((*input).getType());
  if (!constant) {
    Value value = sim::SimBitsDynExtractOp::create(builder, location, selected,
                                                   *input, dynamicLow);
    return convert(value, *resultType, false, location);
  }
  Value amount =
      arith::ConstantOp::create(builder, location, inputInteger,
                                builder.getIntegerAttr(inputInteger, lowBit));
  Value shifted = arith::ShRUIOp::create(builder, location, *input, amount);
  Value value = selected == inputInteger
                    ? shifted
                    : Value(arith::TruncIOp::create(builder, location, selected,
                                                    shifted));
  return convert(value, *resultType, false, location);
}

LogicalResult UnitLowering::writeLValue(Operation *destination, Value value,
                                        bool sourceSigned, bool nonblocking,
                                        Location location, Value delay) {
  if (isa<semantic::SVConcatenationExpressionOp>(destination)) {
    SmallVector<Operation *> children = getChildren(destination);
    FailureOr<Type> destinationType = getNormalizedSemanticType(destination);
    if (children.empty() || failed(destinationType))
      return failure();
    FailureOr<Value> converted =
        convert(value, *destinationType, sourceSigned, location);
    if (failed(converted))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*converted, location);
    if (failed(scalar))
      return failure();
    std::optional<unsigned> totalWidth = sim::getPackedWidth((*scalar).getType());
    if (!totalWidth)
      return failure();
    uint64_t trailing = *totalWidth;
    for (Operation *child : children) {
      FailureOr<Type> childType = getNormalizedSemanticType(child);
      std::optional<unsigned> childWidth =
          succeeded(childType) ? sim::getPackedWidth(*childType) : std::nullopt;
      if (!childWidth || *childWidth > trailing) {
        emitError(location) << "concatenation lvalue width is inconsistent";
        return failure();
      }
      trailing -= *childWidth;
      Value part;
      if (auto logic = dyn_cast<sim::LogicType>((*scalar).getType())) {
        auto selected = sim::LogicType::get(function.getContext(), *childWidth);
        part = sim::SimLogicExtractOp::create(
            builder, location, selected, *scalar,
            builder.getI64IntegerAttr(trailing));
      } else {
        auto integer = cast<IntegerType>((*scalar).getType());
        Value amount = arith::ConstantOp::create(
            builder, location, integer,
            builder.getIntegerAttr(integer, trailing));
        Value shifted =
            arith::ShRUIOp::create(builder, location, *scalar, amount);
        auto selected = IntegerType::get(function.getContext(), *childWidth);
        part = selected == integer
                   ? shifted
                   : Value(arith::TruncIOp::create(builder, location, selected,
                                                   shifted));
      }
      FailureOr<Value> childValue =
          convert(part, *childType, false, location);
      if (failed(childValue) ||
          failed(writeLValue(child, *childValue, false, nonblocking, location,
                             delay)))
        return failure();
    }
    if (trailing != 0) {
      emitError(location) << "concatenation lvalue does not consume its value";
      return failure();
    }
    return success();
  }

  FailureOr<Value> lowered = lowerExpression(destination, true);
  if (failed(lowered))
    return failure();
  Type elementType;
  if (auto ref = dyn_cast<sim::RefType>((*lowered).getType()))
    elementType = ref.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>((*lowered).getType()))
    elementType = driver.getElementType();
  else {
    emitError(location) << "assignment destination is not a ref or driver";
    return failure();
  }
  FailureOr<Value> converted =
      convert(value, elementType, sourceSigned, location);
  if (failed(converted))
    return failure();
  if (isa<sim::RefType>((*lowered).getType())) {
    if (nonblocking)
      sim::SimNBAEnqueueOp::create(builder, location, *converted, *lowered,
                                   delay, sim::NBASiteAttr{});
    else
      sim::SimRefStoreOp::create(builder, location, *converted, *lowered);
  } else {
    if (nonblocking) {
      emitError(location) << "nonblocking assignment cannot target a driver";
      return failure();
    }
    sim::SimDriverDriveOp::create(builder, location, *lowered, *converted);
  }
  return success();
}

FailureOr<Value>
UnitLowering::lowerAssignment(semantic::SVAssignmentExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op.getOperatorKind()) {
    unsupported(op) << " (compound assignment)";
    return failure();
  }
  bool timed = op.getHasTimingControl();
  size_t expected = timed ? 3 : 2;
  if (children.size() != expected) {
    unsupported(op) << " (assignment child inventory)";
    return failure();
  }
  Operation *control = timed ? children[0] : nullptr;
  Operation *destination = children[timed ? 1 : 0];
  Operation *source = children[timed ? 2 : 1];
  FailureOr<Value> rhs = lowerExpression(source);
  if (failed(rhs))
    return failure();
  FailureOr<Type> destinationType = getNormalizedSemanticType(destination);
  if (failed(destinationType))
    return failure();
  FailureOr<Value> value =
      convert(*rhs, *destinationType, isSignedNode(source), location);
  if (failed(value))
    return failure();
  bool nonblocking =
      op.getAssignmentKind() == semantic::SVAssignmentKind::Nonblocking;
  if (!timed) {
    if (failed(writeLValue(destination, *value, false, nonblocking, location)))
      return failure();
    return *value;
  }

  if (isa<semantic::SVDelayControlOp>(control)) {
    FailureOr<Value> delay = lowerDelayValue(control);
    if (failed(delay))
      return failure();
    if (nonblocking) {
      // Both the RHS and destination handle are captured at encounter time.
      if (failed(
              writeLValue(destination, *value, false, true, location, *delay)))
        return failure();
      return *value;
    }

    // A blocking intra-assignment delay captures only the RHS. The destination
    // expression is intentionally resolved after resumption at commit time.
    Block *continuation = addBlock();
    sim::SimSuspendDelayOp::create(builder, location, *delay,
                                   sim::TimingSiteAttr{}, ValueRange{},
                                   sim::ContinuationSiteAttr{}, continuation);
    setCurrent(continuation);
    if (failed(writeLValue(destination, *value, false, false, location)))
      return failure();
    return *value;
  }

  if (nonblocking) {
    unsupported(control)
        << " (nonblocking intra-assignment event/repeat control requires a "
           "scheduler-owned deferred action)";
    return failure();
  }

  // Event-controlled blocking assignments capture the RHS at encounter time,
  // suspend the caller, and resolve the destination only on commit.
  Block *continuation = addBlock();
  continuation->addArgument((*value).getType(), location);
  if (isa<semantic::SVRepeatedEventControlOp>(control)) {
    if (failed(emitRepeatedEventSuspend(control, continuation,
                                        ValueRange{*value})))
      return failure();
  } else {
    if (failed(
            emitEventSuspend(control, continuation, ValueRange{*value})))
      return failure();
    setCurrent(continuation);
  }
  Value capturedValue = continuation->getArgument(0);
  if (failed(
          writeLValue(destination, capturedValue, false, false, location)))
    return failure();
  return capturedValue;
}

LogicalResult
UnitLowering::lowerPortConnection(semantic::SVPortConnectionOp op) {
  Location location = getSemanticLocation(op);
  Operation *internal = getSingleRegionRoot(op.getInternal());
  Operation *actual = getSingleRegionRoot(op.getActual());
  if (!actual)
    return success();

  auto loadPath = [&](StringRef path) -> FailureOr<Value> {
    Value handle = values.lookup(path);
    if (!handle) {
      emitError(location) << "port endpoint has no frozen binding: " << path;
      return failure();
    }
    recordSensitivity(handle);
    if (auto reference = dyn_cast<sim::RefType>(handle.getType()))
      return sim::SimRefLoadOp::create(builder, location,
                                       reference.getElementType(), handle)
          .getResult();
    if (auto net = dyn_cast<sim::NetType>(handle.getType()))
      return sim::SimNetReadOp::create(builder, location, net.getElementType(),
                                       handle)
          .getResult();
    return handle;
  };
  auto endpoint = [&](StringRef path, Operation *expression,
                      bool lvalue) -> FailureOr<Value> {
    if (expression)
      return lowerExpression(expression, lvalue);
    if (lvalue) {
      Value value = lvalues.lookup(path);
      if (!value) {
        emitError(location) << "port endpoint has no lvalue binding: " << path;
        return failure();
      }
      return value;
    }
    return loadPath(path);
  };
  auto write = [&](Value destination, Value source,
                   bool sourceSigned) -> LogicalResult {
    Type elementType;
    if (auto reference = dyn_cast<sim::RefType>(destination.getType()))
      elementType = reference.getElementType();
    else if (auto driver = dyn_cast<sim::DriverType>(destination.getType()))
      elementType = driver.getElementType();
    else {
      emitError(location)
          << "port connection sink is not variable storage or a net driver";
      return failure();
    }
    FailureOr<Value> converted =
        convert(source, elementType, sourceSigned, location);
    if (failed(converted))
      return failure();
    if (isa<sim::RefType>(destination.getType()))
      sim::SimRefStoreOp::create(builder, location, *converted, destination);
    else
      sim::SimDriverDriveOp::create(builder, location, destination, *converted);
    return success();
  };

  StringRef internalPath = op.getInternalPath().value_or(StringRef{});
  if (op.getDirection() == semantic::SVArgumentDirection::In) {
    FailureOr<Value> source = lowerExpression(actual);
    if (failed(source))
      return failure();
    bool sourceSigned =
        actual->getAttrOfType<TypeAttr>("semantic_type") &&
        isSignedNode(actual);
    // A non-ANSI formal can have an aggregate internal expression such as
    // `{high, low}`. Use the same evaluate-once write plan as assignments so
    // every leaf receives the correct slice of the converted actual.
    if (internal)
      return writeLValue(internal, *source, sourceSigned, false, location);
    FailureOr<Value> destination = endpoint(internalPath, nullptr, true);
    if (failed(destination))
      return failure();
    return write(*destination, *source,
                 sourceSigned);
  }
  if (op.getDirection() != semantic::SVArgumentDirection::Out) {
    emitError(location) << "non-static ref or inout port reached unit lowering";
    return failure();
  }

  auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(actual);
  SmallVector<Operation *> children =
      assignment ? getChildren(assignment) : SmallVector<Operation *>{};
  if (!assignment || children.size() != 2) {
    emitError(location) << "malformed resolved output port expression";
    return failure();
  }
  FailureOr<Value> source = endpoint(internalPath, internal, false);
  if (failed(source))
    return failure();
  Value previousPlaceholder = expressionPlaceholder;
  expressionPlaceholder = *source;
  FailureOr<Value> converted = lowerExpression(children[1]);
  expressionPlaceholder = previousPlaceholder;
  if (failed(converted))
    return failure();
  return writeLValue(children[0], *converted, isSignedNode(children[1]), false,
                     location);
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
    FailureOr<Value> oldScalar = toPackedScalar(oldValue, location);
    if (failed(oldScalar))
      return failure();
    Value one;
    Value newScalar;
    if (auto logic = dyn_cast<sim::LogicType>((*oldScalar).getType())) {
      auto planeType =
          IntegerType::get(function.getContext(), logic.getWidth());
      one = sim::SimLogicConstantOp::create(
          builder, location, logic, builder.getIntegerAttr(planeType, 1),
          builder.getIntegerAttr(planeType, 0));
      newScalar = sim::SimLogicBinaryOp::create(
          builder, location, logic,
          increment ? sim::BinaryKind::Add : sim::BinaryKind::Sub, *oldScalar,
          one);
    } else {
      auto integer = cast<IntegerType>((*oldScalar).getType());
      one = arith::ConstantOp::create(builder, location, integer,
                                      builder.getIntegerAttr(integer, 1));
      newScalar =
          increment
              ? Value(arith::AddIOp::create(builder, location, *oldScalar, one))
              : Value(
                    arith::SubIOp::create(builder, location, *oldScalar, one));
    }
    FailureOr<Value> newValue =
        convert(newScalar, reference.getElementType(),
                isSignedNode(children.front()), location);
    if (failed(newValue))
      return failure();
    sim::SimRefStoreOp::create(builder, location, *newValue, *destination);
    bool post = kind == semantic::SVUnaryOperator::Postincrement ||
                kind == semantic::SVUnaryOperator::Postdecrement;
    return convert(post ? oldValue : *newValue, *resultType,
                   isSignedNode(children.front()), location);
  }

  FailureOr<Value> input = lowerExpression(children.front());
  if (failed(input))
    return failure();
  FailureOr<Value> scalarInput = toPackedScalar(*input, location);
  if (failed(scalarInput))
    return failure();
  input = *scalarInput;
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
                           ? sim::getPackedScalarType(*resultType)
                           : (*input).getType();
    if (!unaryResult)
      return failure();
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
  if (isa<sim::EventType>((*lhs).getType()) ||
      isa<sim::EventType>((*rhs).getType())) {
    if (!isa<sim::EventType>((*lhs).getType()) ||
        !isa<sim::EventType>((*rhs).getType()) ||
        (kind != Binary::Equality && kind != Binary::Inequality &&
         kind != Binary::CaseEquality && kind != Binary::CaseInequality)) {
      unsupported(op) << " (event-handle operator)";
      return failure();
    }
    Value equal = sim::SimEventEqualOp::create(
        builder, location, builder.getI1Type(), *lhs, *rhs);
    if (kind == Binary::Inequality || kind == Binary::CaseInequality)
      equal = arith::XOrIOp::create(
          builder, location, equal,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
    return convert(equal, *resultType, false, location);
  }
  FailureOr<Value> scalarLhs = toPackedScalar(*lhs, location);
  FailureOr<Value> scalarRhs = toPackedScalar(*rhs, location);
  Type scalarResultType = sim::getPackedScalarType(*resultType);
  if (failed(scalarLhs) || failed(scalarRhs) || !scalarResultType)
    return failure();
  lhs = *scalarLhs;
  rhs = *scalarRhs;
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
    if (compare) {
      Value compared = sim::SimLogicCompareOp::create(
          builder, location, scalarResultType, *compare, *lhs, *rhs);
      return convert(compared, *resultType, false, location);
    }

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
    Value value = sim::SimLogicBinaryOp::create(
        builder, location, (*lhs).getType(), binary, *lhs, *rhs);
    return convert(value, *resultType, signedOp, location);
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
  if (op.getIsSystemCall())
    return lowerSystemCall(op);
  auto callee = op->getAttrOfType<FlatSymbolRefAttr>(calleeAttrName);
  if (!callee) {
    unsupported(op) << " (indirect or system call)";
    return failure();
  }
  bool directTask = op->hasAttr("obelisk_sim.is_task");
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
    uint32_t dpiCategory;
  };
  SmallVector<CopyOut> copyOuts;
  SmallVector<Attribute> dpiOperandABI;
  for (auto [child, formalAttr] : llvm::zip_equal(children, formals)) {
    auto formal = cast<DictionaryAttr>(formalAttr);
    auto direction = static_cast<semantic::SVArgumentDirection>(
        formal.getAs<IntegerAttr>("direction").getInt());
    Type formalType = formal.getAs<TypeAttr>("type").getValue();
    bool formalSigned = formal.getAs<BoolAttr>("is_signed").getValue();
    auto dpiCategoryAttr = formal.getAs<IntegerAttr>("dpi_category");
    uint32_t dpiCategory =
        dpiCategoryAttr ? static_cast<uint32_t>(dpiCategoryAttr.getInt()) : 0;
    bool isInput = direction == semantic::SVArgumentDirection::In;
    if (op->hasAttr("obelisk.dpi.import_id")) {
      std::optional<unsigned> width = sim::getPackedWidth(formalType);
      if (!width) {
        emitError(location)
            << "DPI formal has no fixed packed integral width";
        return failure();
      }
      dpiOperandABI.push_back(sim::DPIABIAttr::get(
          builder.getContext(), static_cast<sim::DPIABIKind>(dpiCategory),
          static_cast<sim::DPIArgumentDirection>(direction), *width,
          isa<sim::LogicType>(sim::getPackedScalarType(formalType)),
          formalSigned));
    }

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
      recordSensitivity(*destination);
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
      recordSensitivity(*destination);
    }
    operands.push_back(initial);
    if (directTask)
      operands.push_back(*destination);
    copyOuts.push_back(
        {*destination, formalType, formalSigned, dpiCategory});
  }

  llvm::StringSet<> readCaptures;
  if (auto reads =
          op->getAttrOfType<ArrayAttr>(calleeReadCapturesAttrName))
    for (Attribute read : reads)
      readCaptures.insert(cast<StringAttr>(read).getValue());
  if (auto captures = op->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
    for (Attribute captureAttr : captures) {
      StringRef path = cast<StringAttr>(captureAttr).getValue();
      Value capture = values.lookup(path);
      if (!capture) {
        emitError(location)
            << "direct callee capture has no frozen local binding: " << path;
        return failure();
      }
      if (readCaptures.contains(path))
        recordSensitivity(capture);
      operands.push_back(capture);
    }
  BoolAttr dpiTaskAttr =
      op->getAttrOfType<BoolAttr>("obelisk.dpi.is_task");
  bool dpiTask = dpiTaskAttr && dpiTaskAttr.getValue();
  SmallVector<Type> callResultTypes;
  if (!dpiTask && !directTask) {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType))
      return failure();
    callResultTypes.push_back(*resultType);
  }
  if (!directTask)
    for (const CopyOut &copyOut : copyOuts)
      callResultTypes.push_back(copyOut.formalType);
  ValueRange callResults;
  if (auto importID =
          op->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id")) {
    if (operands.empty())
      return emitError(location) << "DPI call is missing its runtime context",
             failure();
    Value runtimeContext = operands.front();
    operands.erase(operands.begin());
    SmallVector<Attribute> signature(dpiOperandABI);
    if (!dpiTask) {
      Type resultType = callResultTypes.front();
      std::optional<unsigned> width = sim::getPackedWidth(resultType);
      if (!width)
        return emitError(location)
                   << "DPI function result has no fixed packed width",
               failure();
      auto semanticResult =
          op->getAttrOfType<TypeAttr>("semantic_type");
      if (!semanticResult)
        return emitError(location)
                   << "DPI function result has no semantic ABI type",
               failure();
      FailureOr<DPIABIKind> resultCategory =
          getDPIABIKind(semanticResult.getValue(), location);
      if (failed(resultCategory))
        return failure();
      signature.push_back(sim::DPIABIAttr::get(
          builder.getContext(),
          static_cast<sim::DPIABIKind>(*resultCategory),
          sim::DPIArgumentDirection::Result, *width,
          isa<sim::LogicType>(sim::getPackedScalarType(resultType)),
          isSignedSemanticType(semanticResult.getValue())));
    }
    for (const CopyOut &copyOut : copyOuts) {
      std::optional<unsigned> width =
          sim::getPackedWidth(copyOut.formalType);
      signature.push_back(sim::DPIABIAttr::get(
          builder.getContext(),
          static_cast<sim::DPIABIKind>(copyOut.dpiCategory),
          sim::DPIArgumentDirection::Output, *width,
          isa<sim::LogicType>(
              sim::getPackedScalarType(copyOut.formalType)),
          copyOut.formalSigned));
    }
    FileLineColLoc fileLocation = dyn_cast<FileLineColLoc>(location);
    StringRef sourceFile =
        fileLocation ? fileLocation.getFilename() : StringRef{};
    uint32_t sourceLine = fileLocation ? fileLocation.getLine() : 0;
    uint32_t sourceColumn = fileLocation ? fileLocation.getColumn() : 0;
    SmallVector<Type> dpiResultTypes(callResultTypes);
    dpiResultTypes.push_back(runtime::StatusType::get(builder.getContext()));
    auto call = sim::SimDPICallOp::create(
        builder, location, dpiResultTypes,
        builder.getI32IntegerAttr(
            static_cast<uint32_t>(importID.getValue().getZExtValue())),
        op->getAttrOfType<StringAttr>("obelisk.dpi.c_identifier"),
        op->getAttrOfType<IntegerAttr>("obelisk.dpi.scope_id"),
        builder.getArrayAttr(signature),
        op->getAttrOfType<BoolAttr>("obelisk.dpi.is_pure"),
        op->getAttrOfType<BoolAttr>("obelisk.dpi.is_context"),
        op->getAttrOfType<BoolAttr>("obelisk.dpi.is_task"),
        builder.getStringAttr(sourceFile), builder.getI32IntegerAttr(sourceLine),
        builder.getI32IntegerAttr(sourceColumn), runtimeContext, operands);
    sim::SimStatusCheckOp::create(builder, location,
                                  call.getResults().back());
    callResults = call.getResults().drop_back();
  } else if (!directTask) {
    auto call = sim::SimCallOp::create(builder, location, callResultTypes,
                                       callee, operands, ArrayAttr{},
                                       ArrayAttr{});
    callResults = call.getResults();
  } else {
    Block *continuation = addBlock();
    sim::SimTaskCallOp::create(
        builder, location, callee, operands,
        builder.getI64IntegerAttr(operands.size()),
        sim::ContinuationSiteAttr{}, continuation);
    setCurrent(continuation);
  }
  if (!directTask)
    for (auto [index, copyOut] : llvm::enumerate(copyOuts)) {
    auto destinationType =
        cast<sim::RefType>(copyOut.destination.getType()).getElementType();
    FailureOr<Value> converted =
        convert(callResults[index + (dpiTask ? 0 : 1)], destinationType,
                copyOut.formalSigned, location);
    if (failed(converted))
      return failure();
    sim::SimRefStoreOp::create(builder, location, *converted,
                               copyOut.destination);
    }
  if (!dpiTask && !directTask)
    return callResults.front();
  return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                   builder.getBoolAttr(false))
      .getResult();
}

FailureOr<Value>
UnitLowering::lowerSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  Value context = function.getBody().front().getArgument(0);
  auto i32 = builder.getI32Type();
  auto i64 = builder.getI64Type();

  auto constant = [&](IntegerType type, int64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(type, value));
  };
  auto lowerInteger = [&](Operation *child,
                          IntegerType type) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    return convert(*value, type, isSignedNode(child), location);
  };
  auto getStringLiteral = [&](Operation *child) {
    Operation *spelling = child;
    while (isa<semantic::SVConversionExpressionOp>(spelling)) {
      SmallVector<Operation *> convertedChildren = getChildren(spelling);
      if (convertedChildren.size() != 1)
        break;
      spelling = convertedChildren.front();
    }
    return dyn_cast<semantic::SVStringLiteralOp>(spelling);
  };
  auto lowerBytes = [&](Operation *child) -> FailureOr<Value> {
    auto literal = getStringLiteral(child);
    if (!literal) {
      emitError(getSemanticLocation(child))
          << "only literal strings are supported by this system call";
      return failure();
    }
    return sim::SimBytesConstantOp::create(builder,
                                           getSemanticLocation(literal),
                                           literal.getConstantValue())
        .getResult();
  };
  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };
  auto dummyTaskResult = [&]() -> Value {
    return constant(builder.getI1Type(), 0);
  };

  if (name == "triggered") {
    if (children.size() != 1) {
      emitError(location) << "event .triggered requires one event operand";
      return failure();
    }
    FailureOr<Value> event = lowerExpression(children.front());
    if (failed(event))
      return failure();
    if (!isa<sim::EventType>((*event).getType())) {
      emitError(location) << ".triggered operand is not an event handle";
      return failure();
    }
    Value triggered = sim::SimEventTriggeredOp::create(
        builder, location, builder.getI1Type(), *event);
    return convertResult(triggered);
  }

  struct DisplayKind {
    bool file = false;
    bool newline = false;
    int32_t radix = 10;
  };
  std::optional<DisplayKind> display;
  if (name == "$display")
    display = DisplayKind{false, true, 10};
  else if (name == "$displayb")
    display = DisplayKind{false, true, 2};
  else if (name == "$displayo")
    display = DisplayKind{false, true, 8};
  else if (name == "$displayh")
    display = DisplayKind{false, true, 16};
  else if (name == "$write")
    display = DisplayKind{false, false, 10};
  else if (name == "$writeb")
    display = DisplayKind{false, false, 2};
  else if (name == "$writeo")
    display = DisplayKind{false, false, 8};
  else if (name == "$writeh")
    display = DisplayKind{false, false, 16};
  else if (name == "$fdisplay")
    display = DisplayKind{true, true, 10};
  else if (name == "$fdisplayb")
    display = DisplayKind{true, true, 2};
  else if (name == "$fdisplayo")
    display = DisplayKind{true, true, 8};
  else if (name == "$fdisplayh")
    display = DisplayKind{true, true, 16};
  else if (name == "$fwrite")
    display = DisplayKind{true, false, 10};
  else if (name == "$fwriteb")
    display = DisplayKind{true, false, 2};
  else if (name == "$fwriteo")
    display = DisplayKind{true, false, 8};
  else if (name == "$fwriteh")
    display = DisplayKind{true, false, 16};
  if (display) {
    size_t firstItem = display->file ? 1 : 0;
    if (children.size() < firstItem) {
      emitError(location) << name << " has too few arguments";
      return failure();
    }
    Value descriptor = constant(i32, 1);
    if (display->file) {
      if (children.empty()) {
        emitError(location) << name << " requires a descriptor";
        return failure();
      }
      FailureOr<Value> lowered = lowerInteger(children.front(), i32);
      if (failed(lowered))
        return failure();
      descriptor = *lowered;
    }
    SmallVector<Value> items;
    SmallVector<int32_t> flags;
    for (Operation *child : ArrayRef(children).drop_front(firstItem)) {
      if (isa<semantic::SVEmptyArgumentExpressionOp>(child)) {
        flags.push_back(2);
      } else if (getStringLiteral(child)) {
        FailureOr<Value> value = lowerBytes(child);
        if (failed(value))
          return failure();
        items.push_back(*value);
        flags.push_back(0);
      } else {
        FailureOr<Value> value = lowerExpression(child);
        if (failed(value))
          return failure();
        FailureOr<Value> scalar =
            toPackedScalar(*value, getSemanticLocation(child));
        if (failed(scalar))
          return failure();
        items.push_back(*scalar);
        flags.push_back(isSignedNode(child) ? 1 : 0);
      }
    }
    auto timeMultiplier =
        function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
    if (!timeMultiplier) {
      function.emitError("code unit has no frozen time scale");
      return failure();
    }
    StringAttr lexicalScope = op.getSystemScopePathAttr();
    if (!lexicalScope)
      lexicalScope = function->getAttrOfType<StringAttr>(
          sim::metadata::hierarchicalName);
    if (!lexicalScope) {
      op.emitError("display call has no elaborated lexical scope");
      return failure();
    }
    sim::SimDisplayOp::create(
        builder, location, context, descriptor, items, display->newline,
        display->radix, flags, lexicalScope,
        op.getSystemLibraryCellAttr(), timeMultiplier);
    return dummyTaskResult();
  }

  if (name == "$fopen") {
    if (children.size() != 1 && children.size() != 2) {
      emitError(location) << "$fopen requires one or two arguments";
      return failure();
    }
    FailureOr<Value> path = lowerBytes(children[0]);
    if (failed(path))
      return failure();
    Value descriptor;
    if (children.size() == 1)
      descriptor =
          sim::SimFileOpenMCDOp::create(builder, location, i32, context, *path)
              .getDescriptor();
    else {
      FailureOr<Value> mode = lowerBytes(children[1]);
      if (failed(mode))
        return failure();
      descriptor = sim::SimFileOpenOp::create(builder, location, i32, context,
                                              *path, *mode)
                       .getDescriptor();
    }
    return convertResult(descriptor);
  }

  auto oneDescriptor = [&]() -> FailureOr<Value> {
    if (children.size() != 1) {
      emitError(location) << name << " requires one descriptor argument";
      return failure();
    }
    return lowerInteger(children.front(), i32);
  };
  if (name == "$fclose" || name == "$feof" || name == "$ftell" ||
      name == "$rewind" || name == "$fgetc") {
    FailureOr<Value> descriptor = oneDescriptor();
    if (failed(descriptor))
      return failure();
    Value result;
    if (name == "$fclose") {
      sim::SimFileCloseOp::create(builder, location, context, *descriptor);
      return dummyTaskResult();
    } else if (name == "$feof")
      result = sim::SimFileEofOp::create(builder, location, i32, context,
                                         *descriptor);
    else if (name == "$ftell")
      result = sim::SimFileTellOp::create(builder, location, i64, context,
                                          *descriptor);
    else if (name == "$rewind")
      result = sim::SimFileRewindOp::create(builder, location, i32, context,
                                            *descriptor);
    else
      result = sim::SimFileGetcOp::create(builder, location, i32, context,
                                          *descriptor);
    return convertResult(result);
  }

  if (name == "$fflush") {
    if (children.size() > 1) {
      emitError(location) << "$fflush accepts zero or one argument";
      return failure();
    }
    Value descriptor = constant(i32, 0);
    if (!children.empty()) {
      FailureOr<Value> lowered = lowerInteger(children.front(), i32);
      if (failed(lowered))
        return failure();
      descriptor = *lowered;
    }
    sim::SimFileFlushOp::create(builder, location, context, descriptor);
    return dummyTaskResult();
  }

  if (name == "$ungetc") {
    if (children.size() != 2) {
      emitError(location) << "$ungetc requires a byte and descriptor";
      return failure();
    }
    FailureOr<Value> byte = lowerInteger(children[0], i32);
    FailureOr<Value> descriptor = lowerInteger(children[1], i32);
    if (failed(byte) || failed(descriptor))
      return failure();
    Value result = sim::SimFileUngetcOp::create(builder, location, i32, context,
                                                *byte, *descriptor);
    return convertResult(result);
  }

  if (name == "$fseek") {
    if (children.size() != 3) {
      emitError(location) << "$fseek requires descriptor, offset, and origin";
      return failure();
    }
    FailureOr<Value> descriptor = lowerInteger(children[0], i32);
    FailureOr<Value> offset = lowerInteger(children[1], i64);
    FailureOr<Value> origin = lowerInteger(children[2], i32);
    if (failed(descriptor) || failed(offset) || failed(origin))
      return failure();
    Value result = sim::SimFileSeekOp::create(builder, location, i32, context,
                                              *descriptor, *offset, *origin);
    return convertResult(result);
  }

  if (name == "$fgets" || name == "$fread") {
    if (children.size() != 2) {
      emitError(location)
          << name << " currently requires a packed destination and descriptor";
      return failure();
    }
    Operation *actual = children[0];
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2 &&
          isa<semantic::SVEmptyArgumentExpressionOp>(outputChildren[1]))
        actual = outputChildren.front();
    }
    FailureOr<Value> destination = lowerExpression(actual, true);
    FailureOr<Value> descriptor = lowerInteger(children[1], i32);
    if (failed(destination) || failed(descriptor))
      return failure();
    auto reference = dyn_cast<sim::RefType>((*destination).getType());
    if (!reference) {
      emitError(getSemanticLocation(actual))
          << name << " destination must be a packed variable";
      return failure();
    }
    std::optional<unsigned> width =
        sim::getPackedWidth(reference.getElementType());
    if (!width) {
      emitError(getSemanticLocation(actual))
          << name << " destination must be a packed integral variable";
      return failure();
    }
    IntegerType packedType = builder.getIntegerType(*width);
    Value data;
    Value count;
    if (name == "$fgets") {
      auto read = sim::SimFileGetlineOp::create(
          builder, location, TypeRange{packedType, i32}, context, *descriptor);
      data = read.getData();
      count = read.getCount();
    } else {
      auto read = sim::SimFileReadPackedOp::create(
          builder, location, TypeRange{packedType, i32}, context, *descriptor);
      data = read.getData();
      count = read.getCount();
    }
    FailureOr<Value> converted =
        convert(data, reference.getElementType(), false, location);
    if (failed(converted))
      return failure();
    sim::SimRefStoreOp::create(builder, location, *converted, *destination);
    return convertResult(count);
  }

  unsupported(op) << " (unsupported system call " << name << ")";
  return failure();
}

FailureOr<Value> UnitLowering::lowerExpression(Operation *op, bool lvalue) {
  if (isa<semantic::SVEmptyArgumentExpressionOp>(op)) {
    if (expressionPlaceholder)
      return expressionPlaceholder;
    emitError(getSemanticLocation(op))
        << "empty expression placeholder has no resolved value";
    return failure();
  }
  if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(op))
    return lowerNamedValue(named, lvalue);
  if (auto hierarchical =
          dyn_cast<semantic::SVHierarchicalValueExpressionOp>(op))
    return lowerReferencedValue(op, hierarchical.getReferencedPath(), lvalue);
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
  if (auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(op))
    return lowerMember(member, lvalue);
  if (auto tagged = dyn_cast<semantic::SVTaggedUnionExpressionOp>(op))
    return lowerTaggedUnion(tagged);
  if (isa<semantic::SVSimpleAssignmentPatternExpressionOp>(op))
    return lowerAssignmentPattern(op);
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

FailureOr<Value> UnitLowering::lowerDelayValue(Operation *control) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  if (!isa<semantic::SVDelayControlOp>(control) || children.size() != 1) {
    unsupported(control) << " (delay inventory)";
    return failure();
  }
  auto scaleAttr = function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
  if (!scaleAttr) {
    function.emitError("code unit has no frozen delay scale");
    return failure();
  }

  Operation *realLiteral = children.front();
  bool negateRealLiteral = false;
  if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(realLiteral)) {
    SmallVector<Operation *> unaryChildren = getChildren(unary);
    if (unaryChildren.size() == 1 &&
        (unary.getOperatorKind() == semantic::SVUnaryOperator::Plus ||
         unary.getOperatorKind() == semantic::SVUnaryOperator::Minus) &&
        isa<semantic::SVRealLiteralOp, semantic::SVTimeLiteralOp>(
            unaryChildren.front())) {
      negateRealLiteral =
          unary.getOperatorKind() == semantic::SVUnaryOperator::Minus;
      realLiteral = unaryChildren.front();
    }
  }
  if (isa<semantic::SVRealLiteralOp, semantic::SVTimeLiteralOp>(realLiteral)) {
    auto spelling =
        realLiteral->getAttrOfType<StringAttr>("constant_value");
    auto quantumAttr =
        function->getAttrOfType<IntegerAttr>(delayQuantumAttrName);
    if (!spelling || !quantumAttr) {
      function.emitError("code unit has incomplete real-delay metadata");
      return failure();
    }
    double amount = 0;
    if (spelling.getValue().getAsDouble(amount) || !std::isfinite(amount)) {
      emitError(location) << "real delay literal is not finite";
      return failure();
    }
    if (negateRealLiteral)
      amount = -amount;
    if (amount < 0)
      amount = 0;
    uint64_t scale = scaleAttr.getValue().getZExtValue();
    uint64_t quantum = quantumAttr.getValue().getZExtValue();
    if (scale == 0 || quantum == 0 || scale % quantum != 0) {
      function.emitError("code unit has invalid real-delay scaling metadata");
      return failure();
    }
    // Slang has already expressed a time literal in the lexical timeunit.
    // Round the real value to the lexical timeprecision before converting to
    // the design-wide precision, matching TimeScale::apply's std::round rule.
    double precisionSteps =
        amount * static_cast<double>(scale / quantum);
    double roundedSteps = std::round(precisionSteps);
    long double ticks =
        static_cast<long double>(roundedSteps) * quantum;
    if (!std::isfinite(roundedSteps) || ticks < 0 ||
        ticks > static_cast<long double>(
                    std::numeric_limits<int64_t>::max())) {
      emitError(location)
          << "scaled real delay exceeds the simulation time range";
      return failure();
    }
    return sim::SimTimeConstantOp::create(
               builder, location, sim::TimeType::get(function.getContext()),
               builder.getI64IntegerAttr(static_cast<uint64_t>(ticks)))
        .getResult();
  }

  if (isIntegerLiteral(children.front())) {
    FailureOr<ParsedConstant> parsed =
        parseSVInteger(*getConstantSpelling(children.front()), 64, location);
    if (failed(parsed))
      return failure();
    // An X/Z or negative delay is treated as zero. This normalization happens
    // before scaling so native and bytecode tiers see the same time value.
    bool zero = !parsed->unknown.isZero() ||
                (isSignedNode(children.front()) && parsed->value.isNegative());
    APInt amount(128, zero ? 0 : parsed->value.getZExtValue());
    APInt scaled =
        amount * APInt(128, scaleAttr.getValue().getZExtValue());
    if (scaled.ugt(APInt(128, static_cast<uint64_t>(
                                  std::numeric_limits<int64_t>::max())))) {
      emitError(location) << "scaled delay exceeds the simulation time range";
      return failure();
    }
    return sim::SimTimeConstantOp::create(
               builder, location, sim::TimeType::get(function.getContext()),
               builder.getI64IntegerAttr(scaled.getZExtValue()))
        .getResult();
  }

  FailureOr<Value> amount = lowerExpression(children.front());
  if (failed(amount))
    return failure();
  FailureOr<Value> scalar = toPackedScalar(*amount, location);
  if (failed(scalar))
    return failure();
  Value normalized = *scalar;
  if (auto logic = dyn_cast<sim::LogicType>(normalized.getType())) {
    Type bitsType =
        IntegerType::get(function.getContext(), logic.getWidth());
    Value bits = sim::SimLogicToBitsOp::create(builder, location, bitsType,
                                               normalized);
    Value roundTrip = sim::SimLogicFromBitsOp::create(
        builder, location, logic, bits);
    Value known = sim::SimLogicCompareOp::create(
        builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
        normalized, roundTrip);
    Value zero = arith::ConstantOp::create(
        builder, location, bitsType, builder.getIntegerAttr(bitsType, 0));
    normalized =
        arith::SelectOp::create(builder, location, known, bits, zero);
  }
  auto integer = dyn_cast<IntegerType>(normalized.getType());
  if (!integer || !integer.isSignless()) {
    emitError(location) << "dynamic delay is not an integral packed value";
    return failure();
  }
  if (isSignedNode(children.front())) {
    Value zero = arith::ConstantOp::create(
        builder, location, integer, builder.getIntegerAttr(integer, 0));
    Value nonnegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sge, normalized, zero);
    normalized =
        arith::SelectOp::create(builder, location, nonnegative, normalized,
                                zero);
  }
  if (integer.getWidth() > 64) {
    emitError(location)
        << "dynamic delay wider than 64 bits is not executable";
    return failure();
  }
  FailureOr<Value> normalized64 =
      convert(normalized, builder.getI64Type(), false, location);
  if (failed(normalized64))
    return failure();

  // Keep the multiplication in the supported nonnegative signed-time range
  // on every backend. The source language's X/Z and negative rules have
  // already mapped those values to zero above.
  uint64_t scale = scaleAttr.getValue().getZExtValue();
  uint64_t maximumInput =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / scale;
  Value maximum = arith::ConstantOp::create(
      builder, location, builder.getI64Type(),
      builder.getI64IntegerAttr(maximumInput));
  Value inRange = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ule, *normalized64, maximum);
  Value checked = arith::SelectOp::create(builder, location, inRange,
                                          *normalized64, maximum);
  return sim::SimTimeScaleOp::create(
             builder, location, sim::TimeType::get(function.getContext()),
             checked, scaleAttr,
             /*is_signed=*/builder.getBoolAttr(false))
      .getResult();
}

LogicalResult
UnitLowering::emitEventSuspend(Operation *control, Block *continuation,
                               ValueRange continuationOperands) {
  Location location = getSemanticLocation(control);
  auto emitDirect = [&](Value watched, sim::EdgeKind edge,
                        Block *successor, ValueRange operands) {
    if (isa<sim::EventType>(watched.getType()))
      sim::SimSuspendEventOp::create(
          builder, location, watched, operands, sim::ContinuationSiteAttr{},
          successor);
    else if (edge == sim::EdgeKind::Change)
      sim::SimSuspendChangeOp::create(
          builder, location, watched, operands, sim::ContinuationSiteAttr{},
          successor);
    else
      sim::SimSuspendEdgeOp::create(builder, location, edge, watched, operands,
                                    sim::ContinuationSiteAttr{}, successor);
  };

  if (auto event = dyn_cast<semantic::SVSignalEventControlOp>(control)) {
    SmallVector<Operation *> children = getChildren(event);
    size_t expected = event.getHasIff() ? 2 : 1;
    if (children.size() != expected) {
      unsupported(event) << " (event expression inventory)";
      return failure();
    }
    if (!isAddressableExpression(children.front())) {
      unsupported(event) << " (computed edge expression)";
      return failure();
    }
    FailureOr<Type> watchedType =
        getNormalizedSemanticType(children.front());
    if (failed(watchedType))
      return failure();
    FailureOr<Value> handle =
        lowerExpression(children.front(), !isa<sim::EventType>(*watchedType));
    if (failed(handle))
      return failure();
    auto edge = static_cast<sim::EdgeKind>(event.getEdgeKind());
    if (!event.getHasIff()) {
      emitDirect(*handle, edge, continuation, continuationOperands);
      return success();
    }

    if (!isAddressableExpression(children[1])) {
      unsupported(event) << " (computed iff condition)";
      return failure();
    }
    FailureOr<Value> condition = lowerExpression(children[1], true);
    if (failed(condition))
      return failure();
    if (!isa<sim::RefType, sim::NetType>((*handle).getType()) ||
        !isa<sim::RefType, sim::NetType>((*condition).getType())) {
      unsupported(event) << " (iff requires signal handles)";
      return failure();
    }
    sim::SimSuspendEdgeIffOp::create(
        builder, location, edge, *handle, *condition, continuationOperands,
        sim::ContinuationSiteAttr{}, continuation);
    return success();
  }

  auto list = dyn_cast<semantic::SVEventListControlOp>(control);
  if (!list) {
    unsupported(control) << " (event timing control)";
    return failure();
  }
  SmallVector<Value> watched;
  SmallVector<int32_t> edges;
  for (Operation *eventOp : getChildren(list)) {
    auto event = dyn_cast<semantic::SVSignalEventControlOp>(eventOp);
    if (!event) {
      unsupported(eventOp) << " (event-list member)";
      return failure();
    }
    SmallVector<Operation *> eventChildren = getChildren(event);
    if (eventChildren.size() != 1) {
      unsupported(event) << (event.getHasIff() ? " (event-list iff condition)"
                                               : " (event expression inventory)");
      return failure();
    }
    if (!isAddressableExpression(eventChildren.front())) {
      unsupported(event) << " (computed edge expression)";
      return failure();
    }
    FailureOr<Type> watchedType =
        getNormalizedSemanticType(eventChildren.front());
    if (failed(watchedType))
      return failure();
    FailureOr<Value> handle = lowerExpression(
        eventChildren.front(), !isa<sim::EventType>(*watchedType));
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
    emitDirect(watched.front(), static_cast<sim::EdgeKind>(edges.front()),
               continuation, continuationOperands);
    return success();
  }
  SmallVector<Value> values(watched);
  llvm::append_range(values, continuationOperands);
  sim::SimSuspendAnyOp::create(builder, location, values,
                               builder.getDenseI32ArrayAttr(edges),
                               sim::ContinuationSiteAttr{}, continuation);
  return success();
}

LogicalResult
UnitLowering::emitRepeatedEventSuspend(Operation *control,
                                       Block *continuation,
                                       ValueRange continuationOperands) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  if (!isa<semantic::SVRepeatedEventControlOp>(control) ||
      children.size() != 2) {
    unsupported(control) << " (repeated-event inventory)";
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
  Value zero = arith::ConstantOp::create(
      builder, location, countType, builder.getI64IntegerAttr(0));
  Value positive = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, *normalized, zero);
  Block *wait = addBlock();
  wait->addArgument(countType, location);
  for (Value operand : continuationOperands)
    wait->addArgument(operand.getType(), location);
  Block *resume = addBlock();
  resume->addArgument(countType, location);
  for (Value operand : continuationOperands)
    resume->addArgument(operand.getType(), location);
  SmallVector<Value> initialWaitOperands{*normalized};
  llvm::append_range(initialWaitOperands, continuationOperands);
  cf::CondBranchOp::create(builder, location, positive, wait,
                           initialWaitOperands, continuation,
                           continuationOperands);
  setCurrent(wait);
  if (failed(emitEventSuspend(children[1], resume, wait->getArguments())))
    return failure();
  setCurrent(resume);
  Value one = arith::ConstantOp::create(
      builder, location, countType, builder.getI64IntegerAttr(1));
  Value resumeZero = arith::ConstantOp::create(
      builder, location, countType, builder.getI64IntegerAttr(0));
  Value remaining = arith::SubIOp::create(
      builder, location, resume->getArgument(0), one);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, remaining, resumeZero);
  SmallVector<Value> nextWaitOperands{remaining};
  llvm::append_range(nextWaitOperands, resume->getArguments().drop_front());
  cf::CondBranchOp::create(builder, location, more, wait,
                           nextWaitOperands, continuation,
                           resume->getArguments().drop_front());
  setCurrent(continuation);
  return success();
}

LogicalResult UnitLowering::lowerTiming(Operation *control,
                                        Operation *statement) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);

  if (isa<semantic::SVImplicitEventControlOp>(control)) {
    // The dependency set belongs to the controlled statement, including
    // reads reached through direct zero-time calls. Build that continuation
    // first, then terminate the pre-control block with the derived wait.
    Block *waitBlock = current;
    Block *continuation = addBlock();
    setCurrent(continuation);
    llvm::SetVector<Value> dependencies;
    llvm::SetVector<Value> *saved = observedDependencies;
    observedDependencies = &dependencies;
    LogicalResult result = lowerStatement(statement);
    observedDependencies = saved;
    if (failed(result))
      return failure();
    Block *statementEnd = current;
    if (dependencies.empty()) {
      unsupported(control) << " (@* controlled statement has no readable dependency)";
      return failure();
    }
    setCurrent(waitBlock);
    SmallVector<int32_t> edges(
        dependencies.size(), static_cast<int32_t>(sim::EdgeKind::Change));
    if (dependencies.size() == 1)
      sim::SimSuspendChangeOp::create(
          builder, location, dependencies.front(), ValueRange{},
          sim::ContinuationSiteAttr{}, continuation);
    else
      sim::SimSuspendAnyOp::create(builder, location,
                                   dependencies.getArrayRef(),
                                   builder.getDenseI32ArrayAttr(edges),
                                   sim::ContinuationSiteAttr{}, continuation);
    setCurrent(statementEnd);
    return success();
  }

  if (isa<semantic::SVRepeatedEventControlOp>(control)) {
    Block *continuation = addBlock();
    if (failed(emitRepeatedEventSuspend(control, continuation)))
      return failure();
    return lowerStatement(statement);
  }

  Block *continuation = addBlock();
  if (isa<semantic::SVDelayControlOp>(control)) {
    FailureOr<Value> delay = lowerDelayValue(control);
    if (failed(delay))
      return failure();
    sim::SimSuspendDelayOp::create(builder, location, *delay,
                                   sim::TimingSiteAttr{}, ValueRange{},
                                   sim::ContinuationSiteAttr{}, continuation);
  } else if (isa<semantic::SVOneStepDelayControlOp>(control)) {
    if (!children.empty()) {
      unsupported(control) << " (#1step inventory)";
      return failure();
    }
    Value delay = sim::SimTimeConstantOp::create(
        builder, location, sim::TimeType::get(function.getContext()),
        builder.getI64IntegerAttr(1));
    sim::SimSuspendDelayOp::create(builder, location, delay,
                                   sim::TimingSiteAttr{}, ValueRange{},
                                   sim::ContinuationSiteAttr{}, continuation);
  } else if (isa<semantic::SVSignalEventControlOp,
                 semantic::SVEventListControlOp>(control)) {
    if (failed(emitEventSuspend(control, continuation)))
      return failure();
  } else {
    unsupported(control) << " (timing control)";
    return failure();
  }
  setCurrent(continuation);
  return lowerStatement(statement);
}

LogicalResult UnitLowering::lowerWait(semantic::SVWaitStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2) {
    unsupported(op) << " (wait inventory)";
    return failure();
  }

  Block *conditionBlock = addBlock();
  Block *suspendBlock = addBlock();
  Block *bodyBlock = addBlock();
  emitBranch(conditionBlock);
  setCurrent(conditionBlock);
  llvm::SetVector<Value> dependencies;
  llvm::SetVector<Value> *saved = observedDependencies;
  observedDependencies = &dependencies;
  FailureOr<Value> conditionValue = lowerExpression(children[0]);
  observedDependencies = saved;
  if (failed(conditionValue))
    return failure();
  FailureOr<Value> condition = truthValue(*conditionValue, location);
  if (failed(condition))
    return failure();
  if (dependencies.empty()) {
    Attribute constant;
    std::optional<bool> truth;
    if (matchPattern(*condition, m_Constant(&constant)))
      if (auto integer = dyn_cast<IntegerAttr>(constant))
        truth = !integer.getValue().isZero();
    if (!truth) {
      if (auto spelling = getConstantSpelling(children[0])) {
        FailureOr<Type> type = getNormalizedSemanticType(children[0]);
        std::optional<unsigned> width =
            succeeded(type) ? sim::getPackedWidth(*type) : std::nullopt;
        if (failed(type) || !width)
          return failure();
        FailureOr<ParsedConstant> parsed =
            parseSVInteger(*spelling, *width, location);
        if (failed(parsed))
          return failure();
        truth = parsed->unknown.isZero() && !parsed->value.isZero();
      }
    }
    if (!truth) {
      unsupported(op) << " (computed wait condition has no readable dependency)";
      return failure();
    }
    emitBranch(*truth ? bodyBlock : suspendBlock);
    if (!*truth) {
      setCurrent(suspendBlock);
      sim::SimSuspendForeverOp::create(
          builder, location, ValueRange{}, sim::ContinuationSiteAttr{},
          bodyBlock);
    } else
      suspendBlock->erase();
    setCurrent(bodyBlock);
    return lowerStatement(children[1]);
  }
  if (dependencies.size() != 1 ||
      !isAddressableExpression(children[0])) {
    unsupported(op) << " (computed wait condition requires an observer)";
    return failure();
  }
  FailureOr<Value> watched = lowerExpression(children[0], true);
  if (failed(watched))
    return failure();
  if (!isa<sim::RefType, sim::NetType>((*watched).getType())) {
    unsupported(op) << " (wait condition is not directly watchable)";
    return failure();
  }
  cf::CondBranchOp::create(builder, location, *condition, bodyBlock,
                           ValueRange{}, suspendBlock, ValueRange{});
  setCurrent(suspendBlock);
  sim::SimSuspendLevelOp::create(builder, location, *watched, ValueRange{},
                                 sim::ContinuationSiteAttr{}, bodyBlock);

  setCurrent(bodyBlock);
  return lowerStatement(children[1]);
}

LogicalResult
UnitLowering::lowerEventTrigger(semantic::SVEventTriggerStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  size_t expected = op.getHasTimingControl() ? 2 : 1;
  if (children.size() != expected) {
    unsupported(op) << " (event trigger inventory)";
    return failure();
  }
  if (op.getHasTimingControl() && !op.getIsNonblocking()) {
    emitError(location)
        << "a timed named-event trigger must be nonblocking";
    return failure();
  }
  FailureOr<Value> event = lowerExpression(children.front());
  if (failed(event))
    return failure();
  if (!isa<sim::EventType>((*event).getType())) {
    emitError(location) << "event trigger operand is not an event handle";
    return failure();
  }
  Value delay;
  if (op.getHasTimingControl()) {
    FailureOr<Value> loweredDelay = lowerDelayValue(children[1]);
    if (failed(loweredDelay))
      return failure();
    delay = *loweredDelay;
  }
  sim::SimEventTriggerOp::create(
      builder, location, *event, delay,
      builder.getBoolAttr(op.getIsNonblocking()), sim::EventSiteAttr{});
  return success();
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
  FailureOr<Value> scalarSelector = toPackedScalar(*selector, location);
  if (failed(scalarSelector))
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
      FailureOr<Value> scalarCandidate =
          toPackedScalar(*normalized, labelLocation);
      if (failed(scalarCandidate))
        return failure();
      // A `case` label matches X and Z bits exactly, which is the four-state
      // case-equality predicate rather than ordinary equality.
      Value equal;
      if (isa<sim::LogicType>((*scalarSelector).getType()))
        equal = sim::SimLogicCompareOp::create(
            builder, labelLocation, builder.getI1Type(),
            sim::CompareKind::CaseEq, *scalarSelector, *scalarCandidate);
      else
        equal = arith::CmpIOp::create(builder, labelLocation,
                                      arith::CmpIPredicate::eq, *scalarSelector,
                                      *scalarCandidate);
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
  loopTargets.push_back(
      {exitBlock, conditionBlock, {}, controlScopes.size()});
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

  loopTargets.push_back({exitBlock, stepBlock, {}, controlScopes.size()});
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
  Value zero = arith::ConstantOp::create(
      builder, location, countType, builder.getI64IntegerAttr(0));
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, header->getArgument(0),
      zero);
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
  Value one = arith::ConstantOp::create(
      builder, location, countType, builder.getI64IntegerAttr(1));
  Value remaining = arith::SubIOp::create(
      builder, location, step->getArgument(0), one);
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
  if (automaticLocals.contains(path)) {
    if (!initial) {
      emitError(location)
          << "automatic variable declaration has no frozen binding type";
      return failure();
    }
    if (!children.empty()) {
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
    values[path] = destination;
    lvalues[path] = destination;
    return success();
  }
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
    cf::CondBranchOp::create(builder, location, first, initialize,
                             ValueRange{}, continuation, ValueRange{});
    setCurrent(initialize);
    FailureOr<Value> lowered = lowerExpression(children.front());
    if (failed(lowered))
      return failure();
    FailureOr<Value> converted = convert(
        *lowered, cast<sim::RefType>(destination.getType()).getElementType(),
        isSignedNode(children.front()), location);
    if (failed(converted))
      return failure();
    sim::SimRefStoreOp::create(builder, location, *converted, destination);
    cf::BranchOp::create(builder, location, continuation);
    setCurrent(continuation);
    return success();
  }
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
                                unsigned branchIndex) {
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
  argumentAttrs.push_back(
      captureMetadata(builder, sim::CaptureKind::Context));

  llvm::StringSet<> capturedPaths;
  ArrayAttr parentBindings =
      function->getAttrOfType<ArrayAttr>(bindingsAttrName);
  if (parentBindings)
    for (Attribute attribute : parentBindings) {
      auto binding = dyn_cast<DictionaryAttr>(attribute);
      auto pathAttr = binding ? binding.getAs<StringAttr>("path")
                              : StringAttr{};
      if (!pathAttr || !capturedPaths.insert(pathAttr.getValue()).second)
        continue;
      Value capture = values.lookup(pathAttr.getValue());
      if (!capture)
        capture = lvalues.lookup(pathAttr.getValue());
      if (!capture)
        continue;
      unsigned argument = inputs.size();
      inputs.push_back(capture.getType());
      captures.push_back(capture);
      argumentAttrs.push_back(
          captureMetadata(builder, sim::CaptureKind::Formal));
      bindings.push_back(builder.getDictionaryAttr({
          builder.getNamedAttr("path", pathAttr),
          builder.getNamedAttr("argument",
                               builder.getI64IntegerAttr(argument)),
      }));
    }

  uint64_t ordinal = nextForkOrdinal++;
  std::string symbol =
      (function.getSymName() + ".fork." + Twine(forkNode) + "." +
       Twine(ordinal) + "." + Twine(branchIndex))
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
  std::string hierarchy =
      (Twine(parentHierarchy) + ".$fork." + Twine(forkNode) + "." +
       Twine(branchIndex))
          .str();
  auto codeUnitIDAttr =
      branch->getAttrOfType<IntegerAttr>("obelisk_sim.fork_code_unit_id");
  if (!codeUnitIDAttr || !codeUnitIDAttr.getValue().isStrictlyPositive())
    return emitError(location) << "fork branch has no prepared code-unit ID",
           failure();
  uint64_t codeUnitID = codeUnitIDAttr.getValue().getZExtValue();

  OpBuilder outlineBuilder(function);
  outlineBuilder.setInsertionPoint(function);
  sim::SimCodeUnitDeclOp::create(
      outlineBuilder, location, codeUnitID, scopeID, sim::EntryKind::Fork,
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
        outlineBuilder.getNamedAttr("path",
                                    outlineBuilder.getStringAttr(path)),
        outlineBuilder.getNamedAttr(
            "id", outlineBuilder.getI64IntegerAttr(inherited.lookup(path))),
    }));
  if (!inheritedControls.empty())
    attributes.push_back(outlineBuilder.getNamedAttr(
        "inherited_controls",
        outlineBuilder.getArrayAttr(inheritedControls)));
  const StringRef inheritedAttributes[] = {
      delayScaleAttrName, delayQuantumAttrName, "home_region", "domain"};
  for (StringRef name : inheritedAttributes)
    if (Attribute attribute = function->getAttr(name))
      attributes.push_back(outlineBuilder.getNamedAttr(name, attribute));
  attributes.push_back(outlineBuilder.getNamedAttr(
      sim::metadata::hierarchicalName,
      outlineBuilder.getStringAttr(hierarchy)));

  auto outlined = sim::SimFuncOp::create(
      outlineBuilder, location, symbol,
      FunctionType::get(context, inputs, TypeRange{}), sim::EntryKind::Fork,
      attributes, argumentAttrs);
  SymbolTable::setSymbolVisibility(outlined,
                                   SymbolTable::Visibility::Private);

  OpBuilder bodyBuilder =
      OpBuilder::atBlockEnd(&outlined.getBody().front());
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

LogicalResult UnitLowering::lowerFork(semantic::SVBlockStatementOp op) {
  Location location = getSemanticLocation(op);
  if (function.getEntryKind() == sim::EntryKind::Function &&
      op.getBlockKind() != semantic::SVStatementBlockKind::JoinNone) {
    emitError(location)
        << "a fork in a zero-time function must use join_none";
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
    processes.push_back(
        sim::SimSpawnOp::create(builder, location,
                                outlined->first.getSymNameAttr(),
                                outlined->second, ArrayAttr{}, ArrayAttr{})
            .getProcess());
  }

  semantic::SVStatementBlockKind kind = op.getBlockKind();
  if (kind == semantic::SVStatementBlockKind::JoinNone || processes.empty())
    return success();
  Block *continuation = addBlock();
  sim::JoinKind joinKind =
      kind == semantic::SVStatementBlockKind::JoinAny ? sim::JoinKind::Any
                                                       : sim::JoinKind::All;
  sim::SimSuspendJoinOp::create(builder, location, joinKind, processes,
                                processes.size(),
                                sim::ContinuationSiteAttr{}, continuation);
  setCurrent(continuation);
  return success();
}

LogicalResult UnitLowering::lowerBlock(semantic::SVBlockStatementOp op) {
  auto path = op.getBlockPathAttr();
  auto lowerContents = [&]() {
    if (op.getBlockKind() == semantic::SVStatementBlockKind::Sequential)
      return lowerSequence(getChildren(op));
    return lowerFork(op);
  };
  if (!path)
    return lowerContents();

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
  controlScopes.push_back(
      {path.getValue().str(), targetID, activation, exit});
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
  sim::SimControlDisableOp::create(
      builder, location, builder.getI64IntegerAttr(targetID), Value{},
      builder.getBoolAttr(true));
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
    sim::SimSuspendChildrenOp::create(
        builder, location, ValueRange{}, sim::ContinuationSiteAttr{},
        continuation);
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
  if (isa<semantic::SVWhileLoopStatementOp>(op))
    return lowerWhile(op);
  if (isa<semantic::SVForLoopStatementOp>(op))
    return lowerFor(op);
  if (isa<semantic::SVRepeatLoopStatementOp>(op))
    return lowerRepeat(op);
  if (isa<semantic::SVBreakStatementOp>(op)) {
    if (loopTargets.empty()) {
      emitError(location) << "break is not nested in a loop";
      return failure();
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
  setCurrent(&function.getBody().front());
  sim::EntryKind entryKind = function.getEntryKind();
  bool loopsForever = entryKind == sim::EntryKind::Always ||
                      entryKind == sim::EntryKind::AlwaysComb ||
                      entryKind == sim::EntryKind::AlwaysFF ||
                      entryKind == sim::EntryKind::AlwaysLatch ||
                      entryKind == sim::EntryKind::Continuous ||
                      entryKind == sim::EntryKind::PortInput ||
                      entryKind == sim::EntryKind::PortOutput;
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
  if (sensitivity.empty()) {
    if (entryKind == sim::EntryKind::Continuous) {
      // A constant continuous assignment is a design-lifetime driver with a
      // one-shot initialization unit; it has no source transition to await.
      sim::SimReturnOp::create(builder, function.getLoc(), ValueRange{});
      return success();
    }
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
    if (function->hasAttr(sim::metadata::lowered))
      return;
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      return;
    // Imported DPI declarations deliberately have no executable body. Their
    // call sites lower to obelisk_sim.dpi.call in the caller instead.
    if (function.getBody().empty())
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
    else
      function->setAttr(sim::metadata::lowered,
                        UnitAttr::get(function.getContext()));
  }
};

} // namespace
} // namespace obelisk
