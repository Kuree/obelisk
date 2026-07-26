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
#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <cmath>
#include <functional>
#include <limits>

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

static bool isWeakReferenceCall(semantic::SVCallExpressionOp op) {
  auto path = op->getAttrOfType<StringAttr>("referenced_path");
  return path && path.getValue().starts_with("std::weak_reference#(");
}

/// Fold one already-lowered SSA value to an attribute without rewriting the
/// surrounding CFG. Unlike m_Constant, this follows pure chains such as
/// logic.is_true(logic.constant) and arithmetic on frozen parameters.
static Attribute foldConstantValue(Value value) {
  llvm::DenseMap<Value, Attribute> constants;
  llvm::DenseSet<Value> active;
  std::function<Attribute(Value)> foldValue = [&](Value current) -> Attribute {
    if (auto found = constants.find(current); found != constants.end())
      return found->second;
    if (!active.insert(current).second)
      return {};

    auto finish = [&](Attribute result) {
      active.erase(current);
      if (result)
        constants.try_emplace(current, result);
      return result;
    };

    Attribute direct;
    if (matchPattern(current, m_Constant(&direct)))
      return finish(direct);

    auto result = dyn_cast<OpResult>(current);
    if (!result)
      return finish({});
    Operation *producer = result.getOwner();
    SmallVector<Attribute> operands;
    operands.reserve(producer->getNumOperands());
    for (Value operand : producer->getOperands()) {
      Attribute constant = foldValue(operand);
      if (!constant)
        return finish({});
      operands.push_back(constant);
    }

    SmallVector<OpFoldResult> folded;
    if (failed(producer->fold(operands, folded)) ||
        folded.size() != producer->getNumResults())
      return finish({});
    OpFoldResult replacement = folded[result.getResultNumber()];
    if (!replacement)
      return finish({});
    if (auto attribute = dyn_cast<Attribute>(replacement))
      return finish(attribute);
    Value replacementValue = cast<Value>(replacement);
    if (replacementValue == current)
      return finish({});
    return finish(foldValue(replacementValue));
  };
  return foldValue(value);
}

static std::optional<bool> foldConstantTruth(Value value) {
  auto integer = dyn_cast_or_null<IntegerAttr>(foldConstantValue(value));
  if (!integer)
    return std::nullopt;
  return !integer.getValue().isZero();
}

static bool isUnboundedEndpoint(Operation *op) {
  while (isa<semantic::SVConversionExpressionOp>(op)) {
    SmallVector<Operation *> children = getChildren(op);
    if (children.size() != 1)
      return false;
    op = children.front();
  }
  return isa<semantic::SVUnboundedLiteralOp>(op);
}

static Operation *getSingleRegionRoot(Region &region) {
  if (region.empty() || region.front().empty())
    return nullptr;
  return &region.front().front();
}

static uint64_t stableCodeUnitID(StringRef key) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (uint8_t byte : key.bytes()) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  hash &= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  return hash == 0 ? 1 : hash;
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
  size_t expected = isa<semantic::SVElementSelectExpressionOp>(op) ? 2u : 3u;
  if (children.size() != expected || !isAddressableExpression(children.front()))
    return false;
  // A direct scheduler subscription captures one stable handle. Dynamic
  // indices require a computed observer so changes to the index can both
  // trigger and retarget the expression.
  return llvm::all_of(
      ArrayRef<Operation *>(children).drop_front(),
      [](Operation *index) { return getConstantSpelling(index).has_value(); });
}

static bool isStaticallyAllocatedOverrideTarget(Value value) {
  while (value) {
    if (auto extract = value.getDefiningOp<sim::SimRefExtractOp>()) {
      value = extract.getInput();
      continue;
    }
    if (auto extract = value.getDefiningOp<sim::SimNetExtractOp>()) {
      value = extract.getInput();
      continue;
    }
    if (value.getDefiningOp<sim::SimRefAllocOp>())
      return false;
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      auto function =
          dyn_cast_or_null<sim::SimFuncOp>(argument.getOwner()->getParentOp());
      return function &&
             !function.getArgAttr(argument.getArgNumber(),
                                  "obelisk_sim.automatic_reference_capture");
    }
    return true;
  }
  return false;
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
  FailureOr<Value> lowerInside(semantic::SVInsideExpressionOp op);
  FailureOr<Value> lowerCall(semantic::SVCallExpressionOp op);
  FailureOr<Value> lowerNewClass(semantic::SVNewClassExpressionOp op);
  FailureOr<Value> lowerSystemCall(semantic::SVCallExpressionOp op);
  LogicalResult lowerPortConnection(semantic::SVPortConnectionOp op);

  LogicalResult lowerStatement(Operation *op);
  LogicalResult lowerSequence(ArrayRef<Operation *> operations);
  LogicalResult lowerConditional(semantic::SVConditionalStatementOp op);
  LogicalResult
  lowerQualifiedConditional(semantic::SVConditionalStatementOp op);
  LogicalResult lowerCase(semantic::SVCaseStatementOp op);
  LogicalResult lowerPatternCase(semantic::SVPatternCaseStatementOp op);
  FailureOr<Value> lowerPattern(Value input, Operation *pattern,
                                semantic::SVCaseCondition condition,
                                llvm::StringMap<Value> *captures = nullptr);
  FailureOr<Value> lowerCaseLabel(Value selector, Type selectorType,
                                  Operation *selectorNode, Operation *label,
                                  semantic::SVCaseCondition condition);
  void emitQualifierWarning(Location location,
                            semantic::SVUniquePriorityCheck qualifier,
                            StringRef statementKind, StringRef reason);
  LogicalResult lowerWhile(Operation *op);
  LogicalResult lowerDoWhile(Operation *op);
  LogicalResult lowerFor(semantic::SVForLoopStatementOp op);
  LogicalResult lowerForever(Operation *op);
  LogicalResult lowerForeach(semantic::SVForeachLoopStatementOp op);
  LogicalResult lowerRepeat(Operation *op);
  LogicalResult lowerFork(semantic::SVBlockStatementOp op);
  LogicalResult lowerBlock(semantic::SVBlockStatementOp op);
  LogicalResult lowerDisable(semantic::SVDisableStatementOp op);
  FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>>
  outlineForkBranch(Operation *branch, uint64_t forkNode, unsigned branchIndex,
                    bool captureReferences = false);
  FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>>
  outlinePostponedDisplay(semantic::SVCallExpressionOp call,
                          StringRef immediateName, bool persistent);
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
  FailureOr<Value> bindObserver(Operation *expression);
  void recordSensitivity(Value value);

  FailureOr<Value> convert(Value value, Type targetType, bool sourceSigned,
                           Location location, bool targetSigned = false);
  FailureOr<Value> toPackedScalar(Value value, Location location);
  FailureOr<Value> truthValue(Value value, Location location);
  FailureOr<Value> toLogic(Value value, Location location);
  Type getReferenceElementType(Value reference) const;
  FailureOr<Value> loadReference(Value reference, Location location);
  LogicalResult storeReference(Value reference, Value value, Location location);
  FailureOr<Value> toArgumentReference(Value reference, Type elementType,
                                       Location location);
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
  Value thisObject;
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
  uint64_t nextPostponedOrdinal = 0;
  bool invalidBindings = false;
};

UnitLowering::UnitLowering(sim::SimFuncOp function)
    : function(function), builder(function.getContext()),
      current(&function.getBody().front()) {
  builder.setInsertionPointToStart(current);
  if (auto argument =
          function->getAttrOfType<IntegerAttr>("obelisk_sim.this_argument")) {
    uint64_t index = argument.getValue().getZExtValue();
    if (index < function.getNumArguments())
      thisObject = function.getBody().front().getArgument(index);
    else
      invalidBindings = true;
  }
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
  return {};
}

FailureOr<Value> UnitLowering::loadReference(Value reference,
                                             Location location) {
  if (auto type = dyn_cast<sim::RefType>(reference.getType()))
    return sim::SimRefLoadOp::create(builder, location, type.getElementType(),
                                     reference)
        .getResult();
  if (auto type = dyn_cast<sim::ManagedRefType>(reference.getType()))
    return sim::SimManagedLoadOp::create(builder, location,
                                         type.getElementType(), reference)
        .getResult();
  if (auto type = dyn_cast<sim::ArgumentRefType>(reference.getType()))
    return sim::SimArgumentRefLoadOp::create(builder, location,
                                             type.getElementType(), reference)
        .getResult();
  return failure();
}

LogicalResult UnitLowering::storeReference(Value reference, Value value,
                                           Location location) {
  if (isa<sim::RefType>(reference.getType()))
    sim::SimRefStoreOp::create(builder, location, value, reference);
  else if (isa<sim::ManagedRefType>(reference.getType()))
    sim::SimManagedStoreOp::create(builder, location, value, reference);
  else if (isa<sim::ArgumentRefType>(reference.getType()))
    sim::SimArgumentRefStoreOp::create(builder, location, value, reference);
  else
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
  if (isa<sim::EventType>(value.getType())) {
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

FailureOr<Value> UnitLowering::bindObserver(Operation *expression) {
  Location location = getSemanticLocation(expression);
  auto evaluator =
      expression->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.observer");
  auto capturePaths =
      expression->getAttrOfType<ArrayAttr>("obelisk_sim.observer_captures");
  auto dependencyPaths =
      expression->getAttrOfType<ArrayAttr>("obelisk_sim.observer_dependencies");
  auto resultKind =
      expression->getAttrOfType<IntegerAttr>("obelisk_sim.observer_result");
  if (!evaluator || !capturePaths || !dependencyPaths || !resultKind) {
    emitError(location) << "computed timing expression has no observer binding";
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
  Type resultType;
  uint64_t kind = resultKind.getValue().getZExtValue();
  if (kind == 2 || kind == 3) {
    resultType = builder.getI1Type();
  } else {
    FailureOr<Type> normalized = getNormalizedSemanticType(expression);
    if (failed(normalized))
      return failure();
    resultType = sim::getPackedScalarType(*normalized);
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
  if (kind == 3)
    binding->setAttr("obelisk_sim.event_primary", builder.getUnitAttr());
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
  if (isa<sim::ClassHandleType>(value.getType()) &&
      isa<sim::ClassHandleType>(targetType))
    return sim::SimClassCastOp::create(builder, location, targetType, value)
        .getResult();
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

FailureOr<Value> UnitLowering::truthValue(Value value, Location location) {
  if (value.getType().isF64()) {
    Value zero = arith::ConstantOp::create(builder, location, value.getType(),
                                           builder.getF64FloatAttr(0.0));
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
  if (auto field =
          op->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_field")) {
    if (!thisObject) {
      emitError(getSemanticLocation(op))
          << "instance property reference has no this object";
      return failure();
    }
    FailureOr<Type> elementType = getNormalizedSemanticType(op);
    auto objectType = dyn_cast<sim::ClassHandleType>(thisObject.getType());
    if (failed(elementType) || !objectType)
      return failure();
    Type referenceType = sim::ManagedRefType::get(
        function.getContext(), *elementType, objectType.getClassName());
    Value reference = sim::SimClassFieldRefOp::create(
        builder, getSemanticLocation(op), referenceType, thisObject, field);
    if (lvalue)
      return reference;
    return sim::SimManagedLoadOp::create(builder, getSemanticLocation(op),
                                         *elementType, reference)
        .getResult();
  }
  return lowerReferencedValue(op, op.getReferencedPath(), lvalue);
}

FailureOr<Value>
UnitLowering::lowerReferencedValue(Operation *op, StringRef path, bool lvalue) {
  Location location = getSemanticLocation(op);
  Value value;
  if (lvalue)
    if (auto node = op->getAttrOfType<IntegerAttr>("node_id"))
      value = nodeLvalues.lookup(node.getValue().getZExtValue());
  if (!value)
    value = lvalue ? lvalues.lookup(path) : values.lookup(path);
  if (!value && thisObject && path.ends_with(".this"))
    value = thisObject;
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
  if (auto ref = dyn_cast<sim::ArgumentRefType>(value.getType()))
    return sim::SimArgumentRefLoadOp::create(builder, location,
                                             ref.getElementType(), value)
        .getResult();
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
  if (!scalarType) {
    unsupported(op) << " (integer literal has an unpacked result type)";
    return failure();
  }
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
  if (auto field =
          op->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_field")) {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    FailureOr<Value> object = lowerExpression(children.front());
    auto objectType = succeeded(object)
                          ? dyn_cast<sim::ClassHandleType>((*object).getType())
                          : sim::ClassHandleType{};
    if (failed(resultType) || failed(object) || !objectType)
      return failure();
    Type referenceType = sim::ManagedRefType::get(
        function.getContext(), *resultType, objectType.getClassName());
    Value reference = sim::SimClassFieldRefOp::create(
        builder, location, referenceType, *object, field);
    if (lvalue)
      return reference;
    return sim::SimManagedLoadOp::create(builder, location, *resultType,
                                         reference)
        .getResult();
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
  if (children.empty()) {
    Value placeholder = createDefaultValue(builder, location, fieldType);
    if (!placeholder) {
      emitError(location)
          << "void tagged-union member has no physical placeholder";
      return failure();
    }
    return sim::SimUnionConstructOp::create(builder, location, *resultType,
                                            placeholder, ordinal)
        .getResult();
  }
  if (children.size() != 1) {
    emitError(location) << "malformed tagged-union value inventory";
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
  else if (auto net = dyn_cast<sim::NetType>(sourceValueType))
    sourceValueType = net.getElementType();
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
  if (!scalarResultType) {
    unsupported(op) << " (selection result type)";
    return failure();
  }
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
    if (!scalarType) {
      emitError(location) << "selection index is not a packed value: " << type;
      return failure();
    }
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
  if (isa<sim::NetType>((*input).getType())) {
    if (!constant) {
      emitError(location)
          << "force and release require constant built-in net selects";
      return failure();
    }
    Type selected = sim::NetType::get(function.getContext(), *resultType);
    return sim::SimNetExtractOp::create(builder, location, selected, *input,
                                        builder.getI64IntegerAttr(lowBit))
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
    FailureOr<Value> converted = convert(value, *destinationType, sourceSigned,
                                         location, isSignedNode(destination));
    if (failed(converted))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*converted, location);
    if (failed(scalar))
      return failure();
    std::optional<unsigned> totalWidth =
        sim::getPackedWidth((*scalar).getType());
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
        part =
            sim::SimLogicExtractOp::create(builder, location, selected, *scalar,
                                           builder.getI64IntegerAttr(trailing));
      } else {
        auto integer = cast<IntegerType>((*scalar).getType());
        Value amount = arith::ConstantOp::create(
            builder, location, integer,
            builder.getIntegerAttr(integer, trailing));
        Value shifted =
            arith::ShRUIOp::create(builder, location, *scalar, amount);
        auto selected = IntegerType::get(function.getContext(), *childWidth);
        part = selected == integer ? shifted
                                   : Value(arith::TruncIOp::create(
                                         builder, location, selected, shifted));
      }
      FailureOr<Value> childValue = convert(part, *childType, false, location);
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
  else if (auto managed = dyn_cast<sim::ManagedRefType>((*lowered).getType()))
    elementType = managed.getElementType();
  else if (auto argument = dyn_cast<sim::ArgumentRefType>((*lowered).getType()))
    elementType = argument.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>((*lowered).getType()))
    elementType = driver.getElementType();
  else {
    emitError(location)
        << "assignment destination is not a reference or driver";
    return failure();
  }
  FailureOr<Value> converted = convert(value, elementType, sourceSigned,
                                       location, isSignedNode(destination));
  if (failed(converted))
    return failure();
  if (isa<sim::ManagedRefType>((*lowered).getType())) {
    if (nonblocking)
      sim::SimManagedNBAEnqueueOp::create(builder, location, *converted,
                                          *lowered, delay);
    else
      sim::SimManagedStoreOp::create(builder, location, *converted, *lowered);
  } else if (isa<sim::RefType>((*lowered).getType())) {
    if (nonblocking)
      sim::SimNBAEnqueueOp::create(builder, location, *converted, *lowered,
                                   delay, sim::NBASiteAttr{});
    else
      sim::SimRefStoreOp::create(builder, location, *converted, *lowered);
  } else if (isa<sim::ArgumentRefType>((*lowered).getType())) {
    if (nonblocking) {
      emitError(location)
          << "nonblocking assignment cannot target a ref formal";
      return failure();
    }
    sim::SimArgumentRefStoreOp::create(builder, location, *converted, *lowered);
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
  FailureOr<Value> value = convert(*rhs, *destinationType, isSignedNode(source),
                                   location, isSignedNode(destination));
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
    sim::SimSuspendDelayOp::create(
        builder, location, *delay, sim::TimingSiteAttr{}, ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
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
    if (failed(emitEventSuspend(control, continuation, ValueRange{*value})))
      return failure();
    setCurrent(continuation);
  }
  Value capturedValue = continuation->getArgument(0);
  if (failed(writeLValue(destination, capturedValue, false, false, location)))
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
    bool sourceSigned = actual->getAttrOfType<TypeAttr>("semantic_type") &&
                        isSignedNode(actual);
    // A non-ANSI formal can have an aggregate internal expression such as
    // `{high, low}`. Use the same evaluate-once write plan as assignments so
    // every leaf receives the correct slice of the converted actual.
    if (internal)
      return writeLValue(internal, *source, sourceSigned, false, location);
    FailureOr<Value> destination = endpoint(internalPath, nullptr, true);
    if (failed(destination))
      return failure();
    return write(*destination, *source, sourceSigned);
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
    Type referenceType = getReferenceElementType(*destination);
    if (!referenceType) {
      emitError(location) << "increment and decrement require a variable "
                             "reference";
      return failure();
    }
    FailureOr<Value> loaded = loadReference(*destination, location);
    if (failed(loaded))
      return failure();
    Value oldValue = *loaded;
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
    FailureOr<Value> newValue = convert(
        newScalar, referenceType, isSignedNode(children.front()), location);
    if (failed(newValue))
      return failure();
    if (failed(storeReference(*destination, *newValue, location)))
      return failure();
    bool post = kind == semantic::SVUnaryOperator::Postincrement ||
                kind == semantic::SVUnaryOperator::Postdecrement;
    return convert(post ? oldValue : *newValue, *resultType,
                   isSignedNode(children.front()), location);
  }

  FailureOr<Value> input = lowerExpression(children.front());
  if (failed(input))
    return failure();
  if ((*input).getType().isF64()) {
    if (kind != semantic::SVUnaryOperator::LogicalNot) {
      emitError(location)
          << "real arithmetic and unary sign operations are not supported";
      return failure();
    }
    FailureOr<Value> truth = truthValue(*input, location);
    if (failed(truth))
      return failure();
    Value value = arith::XOrIOp::create(
        builder, location, *truth,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    return convert(value, *resultType, false, location);
  }
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
  FailureOr<Value> lhs = failure();
  FailureOr<Value> rhs = failure();
  if (isa<semantic::SVNullLiteralOp>(children[0])) {
    rhs = lowerExpression(children[1]);
    if (succeeded(rhs) && isa<sim::ClassHandleType>((*rhs).getType()))
      lhs = sim::SimClassNullOp::create(builder, location, (*rhs).getType())
                .getResult();
  } else if (isa<semantic::SVNullLiteralOp>(children[1])) {
    lhs = lowerExpression(children[0]);
    if (succeeded(lhs) && isa<sim::ClassHandleType>((*lhs).getType()))
      rhs = sim::SimClassNullOp::create(builder, location, (*lhs).getType())
                .getResult();
  } else {
    lhs = lowerExpression(children[0]);
    rhs = lowerExpression(children[1]);
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(lhs) || failed(rhs) || failed(resultType))
    return failure();
  Binary kind = op.getOperatorKind();
  if (isa<sim::ClassHandleType>((*lhs).getType()) ||
      isa<sim::ClassHandleType>((*rhs).getType())) {
    if (!isa<sim::ClassHandleType>((*lhs).getType()) ||
        !isa<sim::ClassHandleType>((*rhs).getType()) ||
        (kind != Binary::Equality && kind != Binary::Inequality &&
         kind != Binary::CaseEquality && kind != Binary::CaseInequality)) {
      unsupported(op) << " (class-handle operator)";
      return failure();
    }
    Value lhsID =
        sim::SimClassIdOp::create(builder, location, *lhs).getResult();
    Value rhsID =
        sim::SimClassIdOp::create(builder, location, *rhs).getResult();
    arith::CmpIPredicate predicate =
        kind == Binary::Equality || kind == Binary::CaseEquality
            ? arith::CmpIPredicate::eq
            : arith::CmpIPredicate::ne;
    Value compared =
        arith::CmpIOp::create(builder, location, predicate, lhsID, rhsID);
    return convert(compared, *resultType, false, location);
  }
  if (isa<sim::EventType>((*lhs).getType()) ||
      isa<sim::EventType>((*rhs).getType())) {
    if (!isa<sim::EventType>((*lhs).getType()) ||
        !isa<sim::EventType>((*rhs).getType()) ||
        (kind != Binary::Equality && kind != Binary::Inequality &&
         kind != Binary::CaseEquality && kind != Binary::CaseInequality)) {
      unsupported(op) << " (event-handle operator)";
      return failure();
    }
    Value equal = sim::SimEventEqualOp::create(builder, location,
                                               builder.getI1Type(), *lhs, *rhs);
    if (kind == Binary::Inequality || kind == Binary::CaseInequality)
      equal = arith::XOrIOp::create(
          builder, location, equal,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
    return convert(equal, *resultType, false, location);
  }
  if ((*lhs).getType().isF64() || (*rhs).getType().isF64()) {
    if (!(*lhs).getType().isF64()) {
      lhs = convert(*lhs, builder.getF64Type(), isSignedNode(children[0]),
                    location);
      if (failed(lhs))
        return failure();
    }
    if (!(*rhs).getType().isF64()) {
      rhs = convert(*rhs, builder.getF64Type(), isSignedNode(children[1]),
                    location);
      if (failed(rhs))
        return failure();
    }
    if (kind == Binary::LogicalAnd || kind == Binary::LogicalOr) {
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
    std::optional<arith::CmpFPredicate> predicate;
    switch (kind) {
    case Binary::Equality:
    case Binary::CaseEquality:
      predicate = arith::CmpFPredicate::OEQ;
      break;
    case Binary::Inequality:
    case Binary::CaseInequality:
      predicate = arith::CmpFPredicate::UNE;
      break;
    case Binary::GreaterThanEqual:
      predicate = arith::CmpFPredicate::OGE;
      break;
    case Binary::GreaterThan:
      predicate = arith::CmpFPredicate::OGT;
      break;
    case Binary::LessThanEqual:
      predicate = arith::CmpFPredicate::OLE;
      break;
    case Binary::LessThan:
      predicate = arith::CmpFPredicate::OLT;
      break;
    default:
      break;
    }
    if (!predicate) {
      emitError(location)
          << "real arithmetic is not supported; only comparisons and logical "
             "operators are executable";
      return failure();
    }
    Value compared =
        arith::CmpFOp::create(builder, location, *predicate, *lhs, *rhs);
    return convert(compared, *resultType, false, location);
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
    case Binary::WildcardEquality:
      compare = sim::CompareKind::WildEq;
      break;
    case Binary::WildcardInequality:
      compare = sim::CompareKind::WildNe;
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
  case Binary::WildcardInequality:
    predicate = arith::CmpIPredicate::ne;
    break;
  case Binary::WildcardEquality:
    predicate = arith::CmpIPredicate::eq;
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

FailureOr<Value> UnitLowering::lowerInside(semantic::SVInsideExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op.getItemCount() <= 0 ||
      children.size() != static_cast<size_t>(1 + op.getItemCount())) {
    emitError(location) << "malformed inside item inventory";
    return failure();
  }
  FailureOr<Value> loweredSelector = lowerExpression(children.front());
  if (failed(loweredSelector))
    return failure();
  FailureOr<Value> scalarSelector = toPackedScalar(*loweredSelector, location);
  if (failed(scalarSelector))
    return failure();
  Value selector = *scalarSelector;
  bool logic = isa<sim::LogicType>(selector.getType());
  Value matched;
  if (logic) {
    auto i1 = builder.getI1Type();
    matched = sim::SimLogicConstantOp::create(
        builder, location, sim::LogicType::get(function.getContext(), 1),
        builder.getIntegerAttr(i1, 0), builder.getIntegerAttr(i1, 0));
  } else {
    matched = arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false));
  }

  auto combine = [&](Value lhs, Value rhs, bool conjunction,
                     Location itemLocation) -> Value {
    if (logic)
      return sim::SimLogicLogicalOp::create(
          builder, itemLocation, sim::LogicType::get(function.getContext(), 1),
          conjunction ? sim::LogicalKind::And : sim::LogicalKind::Or, lhs, rhs);
    return conjunction
               ? Value(arith::AndIOp::create(builder, itemLocation, lhs, rhs))
               : Value(arith::OrIOp::create(builder, itemLocation, lhs, rhs));
  };
  auto compare = [&](Value candidate, sim::CompareKind logicKind,
                     arith::CmpIPredicate integerKind,
                     Location itemLocation) -> FailureOr<Value> {
    FailureOr<Value> normalized =
        convert(candidate, (*loweredSelector).getType(), false, itemLocation);
    if (failed(normalized))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*normalized, itemLocation);
    if (failed(scalar))
      return failure();
    if ((*scalar).getType() != selector.getType()) {
      emitError(itemLocation)
          << "inside item does not normalize to the selector type";
      return failure();
    }
    if (logic)
      return sim::SimLogicCompareOp::create(
                 builder, itemLocation,
                 sim::LogicType::get(function.getContext(), 1), logicKind,
                 selector, *scalar)
          .getResult();
    return arith::CmpIOp::create(builder, itemLocation, integerKind, selector,
                                 *scalar)
        .getResult();
  };
  std::function<LogicalResult(Value, Location)> addCandidate =
      [&](Value candidate, Location itemLocation) -> LogicalResult {
    if (auto array = dyn_cast<sim::UnpackedArrayType>(candidate.getType())) {
      unsigned count = sim::getAggregateNumElements(array);
      for (unsigned index = 0; index < count; ++index) {
        Value element = sim::SimAggregateExtractOp::create(
            builder, itemLocation, array.getElementType(), candidate, index);
        if (failed(addCandidate(element, itemLocation)))
          return failure();
      }
      return success();
    }
    FailureOr<Value> equal = compare(candidate, sim::CompareKind::WildEq,
                                     arith::CmpIPredicate::eq, itemLocation);
    if (failed(equal))
      return failure();
    matched = combine(matched, *equal, false, itemLocation);
    return success();
  };

  for (Operation *item : ArrayRef<Operation *>(children).drop_front()) {
    Location itemLocation = getSemanticLocation(item);
    if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item)) {
      if (range.getRangeKind() != semantic::SVValueRangeKind::Simple) {
        emitError(itemLocation)
            << "real-valued tolerance ranges are not executable";
        return failure();
      }
      SmallVector<Operation *> endpoints = getChildren(item);
      if (endpoints.size() != 2) {
        emitError(itemLocation) << "malformed inside range inventory";
        return failure();
      }
      Value inRange;
      bool signedSelector = isSignedNode(children.front());
      if (!isUnboundedEndpoint(endpoints[0])) {
        FailureOr<Value> lower = lowerExpression(endpoints[0]);
        if (failed(lower))
          return failure();
        FailureOr<Value> above = compare(
            *lower,
            signedSelector ? sim::CompareKind::SGE : sim::CompareKind::UGE,
            signedSelector ? arith::CmpIPredicate::sge
                           : arith::CmpIPredicate::uge,
            itemLocation);
        if (failed(above))
          return failure();
        inRange = *above;
      }
      if (!isUnboundedEndpoint(endpoints[1])) {
        FailureOr<Value> upper = lowerExpression(endpoints[1]);
        if (failed(upper))
          return failure();
        FailureOr<Value> below = compare(
            *upper,
            signedSelector ? sim::CompareKind::SLE : sim::CompareKind::ULE,
            signedSelector ? arith::CmpIPredicate::sle
                           : arith::CmpIPredicate::ule,
            itemLocation);
        if (failed(below))
          return failure();
        inRange =
            inRange ? combine(inRange, *below, true, itemLocation) : *below;
      }
      if (!inRange) {
        emitError(itemLocation)
            << "inside range cannot have two unbounded endpoints";
        return failure();
      }
      matched = combine(matched, inRange, false, itemLocation);
      continue;
    }
    FailureOr<Value> candidate = lowerExpression(item);
    if (failed(candidate) || failed(addCandidate(*candidate, itemLocation)))
      return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  return convert(matched, *resultType, false, location);
}

FailureOr<Value> UnitLowering::lowerCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op.getIsSystemCall())
    return lowerSystemCall(op);
  if (isWeakReferenceCall(op)) {
    StringRef name = op.getCalleeName();
    if (name == "get") {
      if (children.size() != 1)
        return emitError(location)
                   << "weak_reference::get requires one receiver",
               failure();
      FailureOr<Value> weak = lowerExpression(children.front());
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(weak) || failed(resultType) ||
          !isa<sim::ClassHandleType>((*weak).getType()) ||
          !isa<sim::ClassHandleType>(*resultType))
        return failure();
      return sim::SimWeakGetOp::create(builder, location, *resultType, *weak)
          .getResult();
    }
    if (name == "clear") {
      if (children.size() != 1)
        return emitError(location)
                   << "weak_reference::clear requires one receiver",
               failure();
      FailureOr<Value> weak = lowerExpression(children.front());
      if (failed(weak) || !isa<sim::ClassHandleType>((*weak).getType()))
        return failure();
      sim::SimWeakClearOp::create(builder, location, *weak);
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    if (name == "get_id") {
      if (children.size() != 1)
        return emitError(location)
                   << "weak_reference::get_id requires one object argument",
               failure();
      FailureOr<Value> object = lowerExpression(children.front());
      if (failed(object) || !isa<sim::ClassHandleType>((*object).getType()))
        return failure();
      return sim::SimClassIdOp::create(builder, location, *object).getResult();
    }
    return emitError(location)
               << "unsupported weak_reference built-in method " << name,
           failure();
  }
  auto callee = op->getAttrOfType<FlatSymbolRefAttr>(calleeAttrName);
  if (!callee) {
    unsupported(op) << " (indirect or system call)";
    return failure();
  }
  if (op->hasAttr("obelisk_sim.class_instance")) {
    auto formals = op->getAttrOfType<ArrayAttr>(calleeFormalsAttrName);
    bool superCall = op->hasAttr("obelisk_sim.class_super");
    bool implicitThis = !superCall && formals && thisObject &&
                        children.size() == formals.size();
    if (!formals ||
        formals.size() + (superCall || implicitThis ? 0 : 1) !=
            children.size() ||
        ((superCall || implicitThis) && !thisObject)) {
      emitError(location)
          << "instance call has no receiver or complete formal inventory";
      return failure();
    }
    FailureOr<Value> receiver = superCall || implicitThis
                                    ? FailureOr<Value>(thisObject)
                                    : lowerExpression(children.front());
    if (failed(receiver) || !isa<sim::ClassHandleType>((*receiver).getType()))
      return failure();
    auto method =
        op->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_method");
    auto declaration =
        method
            ? SymbolTable::lookupNearestSymbolFrom<sim::SimClassMethodDeclOp>(
                  op, method)
            : sim::SimClassMethodDeclOp{};
    if (declaration) {
      Type targetType = sim::ClassHandleType::get(function.getContext(),
                                                  declaration.getOwnerAttr());
      if ((*receiver).getType() != targetType)
        receiver = sim::SimClassCastOp::create(builder, location, targetType,
                                               *receiver)
                       .getResult();
    } else if (superCall || implicitThis) {
      emitError(location) << "implicit receiver has no class method descriptor";
      return failure();
    }
    struct ClassCopyOut {
      Value destination;
      Type formalType;
      bool formalSigned;
      bool destinationSigned;
    };
    struct TaskIndirectCopyOut {
      Value temporary;
      Value destination;
      bool formalSigned;
      bool destinationSigned;
    };
    SmallVector<Value> arguments;
    SmallVector<ClassCopyOut> copyOuts;
    SmallVector<TaskIndirectCopyOut> taskIndirectCopyOuts;
    bool classTask = op->hasAttr("obelisk_sim.is_task");
    for (auto [actual, formalAttr] :
         llvm::zip_equal(ArrayRef<Operation *>(children).drop_front(
                             superCall || implicitThis ? 0 : 1),
                         formals)) {
      auto formal = cast<DictionaryAttr>(formalAttr);
      auto direction = static_cast<semantic::SVArgumentDirection>(
          formal.getAs<IntegerAttr>("direction").getInt());
      Type formalType = formal.getAs<TypeAttr>("type").getValue();
      bool formalSigned = formal.getAs<BoolAttr>("is_signed").getValue();
      if (direction == semantic::SVArgumentDirection::In) {
        FailureOr<Value> argument = lowerExpression(actual);
        if (failed(argument))
          return failure();
        FailureOr<Value> converted =
            convert(*argument, formalType, isSignedNode(actual), location,
                    formalSigned);
        if (failed(converted))
          return failure();
        arguments.push_back(*converted);
        continue;
      }

      Operation *destination = actual;
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
        SmallVector<Operation *> outputChildren = getChildren(assignment);
        if (outputChildren.size() == 2) {
          Operation *placeholder = outputChildren[1];
          while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
            SmallVector<Operation *> converted = getChildren(placeholder);
            if (converted.size() != 1)
              break;
            placeholder = converted.front();
          }
          if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder))
            destination = outputChildren.front();
        }
      }
      FailureOr<Value> destinationRef = lowerExpression(destination, true);
      if (failed(destinationRef))
        return failure();
      Type destinationType;
      if (auto ref = dyn_cast<sim::RefType>((*destinationRef).getType()))
        destinationType = ref.getElementType();
      else if (auto ref =
                   dyn_cast<sim::ManagedRefType>((*destinationRef).getType()))
        destinationType = ref.getElementType();
      else if (auto ref =
                   dyn_cast<sim::ArgumentRefType>((*destinationRef).getType()))
        destinationType = ref.getElementType();
      else {
        emitError(location)
            << "class method output, inout, and ref actuals must be variable "
               "references";
        return failure();
      }
      if (direction == semantic::SVArgumentDirection::Ref) {
        FailureOr<Value> argument =
            toArgumentReference(*destinationRef, formalType, location);
        if (failed(argument)) {
          emitError(location)
              << "class method ref actual type must exactly match the formal "
                 "type";
          return failure();
        }
        arguments.push_back(*argument);
        continue;
      }

      Value initial;
      if (direction == semantic::SVArgumentDirection::Out) {
        initial = createDefaultValue(builder, location, formalType);
        if (!initial) {
          emitError(location)
              << "cannot materialize a class output-formal default for type "
              << formalType;
          return failure();
        }
      } else {
        FailureOr<Value> loaded = loadReference(*destinationRef, location);
        if (failed(loaded))
          return failure();
        if (isa<sim::RefType>((*destinationRef).getType()))
          recordSensitivity(*destinationRef);
        FailureOr<Value> converted =
            convert(*loaded, formalType, isSignedNode(destination), location,
                    formalSigned);
        if (failed(converted))
          return failure();
        initial = *converted;
      }
      arguments.push_back(initial);
      if (classTask) {
        if (!isa<sim::RefType>((*destinationRef).getType())) {
          Value temporary = sim::SimRefAllocOp::create(
              builder, location,
              sim::RefType::get(function.getContext(), formalType), initial);
          arguments.push_back(temporary);
          taskIndirectCopyOuts.push_back({temporary, *destinationRef,
                                          formalSigned,
                                          isSignedNode(destination)});
        } else {
          arguments.push_back(*destinationRef);
        }
      } else {
        copyOuts.push_back({*destinationRef, formalType, formalSigned,
                            isSignedNode(destination)});
      }
    }
    if (auto captures = op->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
      for (Attribute captureAttr : captures) {
        StringRef path = cast<StringAttr>(captureAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture) {
          emitError(location)
              << "method capture has no frozen local binding: " << path;
          return failure();
        }
        arguments.push_back(capture);
      }
    SmallVector<Type> resultTypes;
    if (auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
        semanticType && !isa<semantic::VoidType>(semanticType.getValue())) {
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(resultType))
        return failure();
      resultTypes.push_back(*resultType);
    }
    bool hasFunctionResult = !resultTypes.empty();
    if (!classTask)
      for (const ClassCopyOut &copyOut : copyOuts)
        resultTypes.push_back(copyOut.formalType);
    if (classTask) {
      Block *continuation = addBlock();
      auto finishTask = [&]() -> FailureOr<Value> {
        setCurrent(continuation);
        for (const TaskIndirectCopyOut &copyOut : taskIndirectCopyOuts) {
          auto temporaryType =
              cast<sim::RefType>(copyOut.temporary.getType()).getElementType();
          Value copied = sim::SimRefLoadOp::create(
              builder, location, temporaryType, copyOut.temporary);
          Type destinationType = getReferenceElementType(copyOut.destination);
          if (!destinationType)
            return failure();
          FailureOr<Value> converted =
              convert(copied, destinationType, copyOut.formalSigned, location,
                      copyOut.destinationSigned);
          if (failed(converted))
            return failure();
          if (failed(storeReference(copyOut.destination, *converted, location)))
            return failure();
        }
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false))
            .getResult();
      };
      auto emitTaskCall = [&](FlatSymbolRefAttr target, Value taskReceiver) {
        SmallVector<Value> operands{function.getBody().front().getArgument(0),
                                    taskReceiver};
        llvm::append_range(operands, arguments);
        sim::SimTaskCallOp::create(builder, location, target, operands,
                                   builder.getI64IntegerAttr(operands.size()),
                                   sim::ContinuationSiteAttr{}, continuation);
      };
      if (!op->hasAttr("obelisk_sim.class_virtual")) {
        emitTaskCall(callee, *receiver);
        return finishTask();
      }

      auto signature =
          op->getAttrOfType<IntegerAttr>("obelisk_sim.class_signature");
      auto receiverType = dyn_cast<sim::ClassHandleType>((*receiver).getType());
      sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
      sim::SimClassDeclOp staticClass =
          receiverType
              ? SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                    function, receiverType.getClassName())
              : sim::SimClassDeclOp{};
      if (!method || !signature || !design || !staticClass)
        return emitError(location)
                   << "virtual class task has no complete dispatch inventory",
               failure();

      SmallVector<sim::SimClassMethodDeclOp> methods(
          design.getBody().front().getOps<sim::SimClassMethodDeclOp>());
      auto lookupClass = [&](FlatSymbolRefAttr symbol) {
        return SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, symbol);
      };
      auto derivesFrom = [&](sim::SimClassDeclOp candidate,
                             sim::SimClassDeclOp target) {
        for (sim::SimClassDeclOp current = candidate; current;) {
          if (current == target)
            return true;
          if (target.getIsInterface() && current.getInterfacesAttr())
            for (Attribute interface : current.getInterfacesAttr())
              if (cast<FlatSymbolRefAttr>(interface).getValue() ==
                  target.getSymName())
                return true;
          current = current.getBaseAttr() ? lookupClass(current.getBaseAttr())
                                          : sim::SimClassDeclOp{};
        }
        return false;
      };
      auto inheritanceDepth = [&](sim::SimClassDeclOp candidate) {
        uint64_t depth = 0;
        for (sim::SimClassDeclOp current = candidate; current;
             current = current.getBaseAttr()
                           ? lookupClass(current.getBaseAttr())
                           : sim::SimClassDeclOp{})
          ++depth;
        return depth;
      };
      struct TaskTarget {
        FlatSymbolRefAttr dynamicClass;
        FlatSymbolRefAttr implementation;
        uint64_t depth;
        uint64_t classId;
      };
      SmallVector<TaskTarget> targets;
      for (sim::SimClassDeclOp candidate :
           design.getBody().front().getOps<sim::SimClassDeclOp>()) {
        if (candidate.getIsAbstract() || candidate.getIsInterface() ||
            !derivesFrom(candidate, staticClass))
          continue;
        sim::SimClassMethodDeclOp implementation;
        for (sim::SimClassDeclOp current = candidate;
             current && !implementation;
             current = current.getBaseAttr()
                           ? lookupClass(current.getBaseAttr())
                           : sim::SimClassDeclOp{})
          for (sim::SimClassMethodDeclOp candidateMethod : methods)
            if (candidateMethod.getOwner() == current.getSymName() &&
                candidateMethod.getSignatureIdAttr() &&
                candidateMethod.getSignatureId() ==
                    signature.getValue().getZExtValue() &&
                candidateMethod.getImplementationAttr()) {
              implementation = candidateMethod;
              break;
            }
        if (implementation)
          targets.push_back({
              FlatSymbolRefAttr::get(function.getContext(),
                                     candidate.getSymName()),
              implementation.getImplementationAttr(),
              inheritanceDepth(candidate),
              candidate.getId(),
          });
      }
      llvm::sort(targets, [](const TaskTarget &lhs, const TaskTarget &rhs) {
        return std::tuple(lhs.depth, lhs.classId) >
               std::tuple(rhs.depth, rhs.classId);
      });
      if (targets.empty())
        return emitError(location)
                   << "virtual class task has no concrete implementation",
               failure();
      for (const TaskTarget &target : targets) {
        Value matches = sim::SimClassIsInstanceOp::create(
            builder, location, *receiver, target.dynamicClass);
        Block *invoke = addBlock();
        Block *next = addBlock();
        cf::CondBranchOp::create(builder, location, matches, invoke,
                                 ValueRange{}, next, ValueRange{});
        setCurrent(invoke);
        auto implementation =
            SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(
                function, target.implementation);
        if (!implementation ||
            implementation.getFunctionType().getNumInputs() < 2)
          return emitError(location)
                     << "virtual class task implementation is missing",
                 failure();
        Type expectedReceiver = implementation.getFunctionType().getInput(1);
        Value adjusted = *receiver;
        if (adjusted.getType() != expectedReceiver)
          adjusted = sim::SimClassCastOp::create(builder, location,
                                                 expectedReceiver, adjusted);
        emitTaskCall(target.implementation, adjusted);
        setCurrent(next);
      }
      Value verbosity =
          arith::ConstantOp::create(builder, location, builder.getI32Type(),
                                    builder.getI32IntegerAttr(1));
      sim::SimFatalOp::create(builder, location,
                              function.getBody().front().getArgument(0),
                              verbosity);
      emitBranch(continuation);
      return finishTask();
    }
    ValueRange results;
    if (op->hasAttr("obelisk_sim.class_virtual")) {
      auto slot = op->getAttrOfType<IntegerAttr>("obelisk_sim.class_slot");
      auto signature =
          op->getAttrOfType<IntegerAttr>("obelisk_sim.class_signature");
      if (!method || !slot || !signature || signature.getValue().isZero()) {
        emitError(location)
            << "virtual call has no frozen method slot and signature";
        return failure();
      }
      results = sim::SimClassVirtualCallOp::create(
                    builder, location, resultTypes, *receiver, method, slot,
                    signature, arguments)
                    .getResults();
    } else {
      results =
          sim::SimClassDirectCallOp::create(builder, location, resultTypes,
                                            callee, *receiver, arguments)
              .getResults();
    }
    if (!classTask)
      for (auto [index, copyOut] : llvm::enumerate(copyOuts)) {
        Value result = results[index + (hasFunctionResult ? 1 : 0)];
        Type destinationType = getReferenceElementType(copyOut.destination);
        if (!destinationType)
          return failure();
        FailureOr<Value> converted =
            convert(result, destinationType, copyOut.formalSigned, location,
                    copyOut.destinationSigned);
        if (failed(converted))
          return failure();
        if (failed(storeReference(copyOut.destination, *converted, location)))
          return failure();
      }
    if (hasFunctionResult)
      return results.front();
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
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
    Value taskDestination;
    Type formalType;
    bool formalSigned;
    bool destinationSigned;
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
        emitError(location) << "DPI formal has no fixed packed integral width";
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
        if (outputChildren.size() == 2) {
          Operation *placeholder = outputChildren[1];
          while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
            SmallVector<Operation *> converted = getChildren(placeholder);
            if (converted.size() != 1)
              break;
            placeholder = converted.front();
          }
          if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder))
            actual = outputChildren.front();
        }
      }

    if (isInput) {
      FailureOr<Value> argument = lowerExpression(actual);
      if (failed(argument))
        return failure();
      FailureOr<Value> converted = convert(
          *argument, formalType, isSignedNode(actual), location, formalSigned);
      if (failed(converted))
        return failure();
      operands.push_back(*converted);
      continue;
    }

    FailureOr<Value> destination = lowerExpression(actual, true);
    if (failed(destination))
      return failure();
    Type destinationType = getReferenceElementType(*destination);
    if (!destinationType) {
      emitError(location)
          << "output, inout, and ref actuals must be variable references";
      return failure();
    }
    if (direction == semantic::SVArgumentDirection::Ref) {
      auto reference = dyn_cast<sim::RefType>((*destination).getType());
      if (!reference || reference.getElementType() != formalType) {
        emitError(location)
            << "ref actual type must exactly match the formal type and use "
               "ordinary variable storage";
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
      FailureOr<Value> loaded = loadReference(*destination, location);
      if (failed(loaded))
        return failure();
      FailureOr<Value> converted = convert(
          *loaded, formalType, isSignedNode(actual), location, formalSigned);
      if (failed(converted))
        return failure();
      initial = *converted;
      if (isa<sim::RefType>((*destination).getType()))
        recordSensitivity(*destination);
    }
    operands.push_back(initial);
    Value taskDestination;
    if (directTask) {
      taskDestination = *destination;
      if (!isa<sim::RefType>((*destination).getType()))
        taskDestination = sim::SimRefAllocOp::create(
            builder, location,
            sim::RefType::get(function.getContext(), formalType), initial);
      operands.push_back(taskDestination);
    }
    copyOuts.push_back({*destination, taskDestination, formalType, formalSigned,
                        isSignedNode(actual), dpiCategory});
  }

  llvm::StringSet<> readCaptures;
  if (auto reads = op->getAttrOfType<ArrayAttr>(calleeReadCapturesAttrName))
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
  BoolAttr dpiTaskAttr = op->getAttrOfType<BoolAttr>("obelisk.dpi.is_task");
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
  if (auto importID = op->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id")) {
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
      auto semanticResult = op->getAttrOfType<TypeAttr>("semantic_type");
      if (!semanticResult)
        return emitError(location)
                   << "DPI function result has no semantic ABI type",
               failure();
      FailureOr<DPIABIKind> resultCategory =
          getDPIABIKind(semanticResult.getValue(), location);
      if (failed(resultCategory))
        return failure();
      signature.push_back(sim::DPIABIAttr::get(
          builder.getContext(), static_cast<sim::DPIABIKind>(*resultCategory),
          sim::DPIArgumentDirection::Result, *width,
          isa<sim::LogicType>(sim::getPackedScalarType(resultType)),
          isSignedSemanticType(semanticResult.getValue())));
    }
    for (const CopyOut &copyOut : copyOuts) {
      std::optional<unsigned> width = sim::getPackedWidth(copyOut.formalType);
      signature.push_back(sim::DPIABIAttr::get(
          builder.getContext(),
          static_cast<sim::DPIABIKind>(copyOut.dpiCategory),
          sim::DPIArgumentDirection::Output, *width,
          isa<sim::LogicType>(sim::getPackedScalarType(copyOut.formalType)),
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
        builder.getStringAttr(sourceFile),
        builder.getI32IntegerAttr(sourceLine),
        builder.getI32IntegerAttr(sourceColumn), runtimeContext, operands);
    sim::SimStatusCheckOp::create(builder, location, call.getResults().back());
    callResults = call.getResults().drop_back();
  } else if (!directTask) {
    auto call =
        sim::SimCallOp::create(builder, location, callResultTypes, callee,
                               operands, ArrayAttr{}, ArrayAttr{});
    callResults = call.getResults();
  } else {
    Block *continuation = addBlock();
    sim::SimTaskCallOp::create(builder, location, callee, operands,
                               builder.getI64IntegerAttr(operands.size()),
                               sim::ContinuationSiteAttr{}, continuation);
    setCurrent(continuation);
  }
  if (!directTask) {
    Value requested = sim::SimTerminationRequestedOp::create(
        builder, location, builder.getI1Type(),
        function.getBody().front().getArgument(0));
    Block *terminate = addBlock();
    Block *resume = addBlock();
    cf::CondBranchOp::create(builder, location, requested, terminate,
                             ValueRange{}, resume, ValueRange{});
    setCurrent(terminate);
    if (function.getEntryKind() == sim::EntryKind::Observer) {
      SmallVector<Value> results;
      for (Type type : function.getFunctionType().getResults()) {
        Value result = createDefaultValue(builder, location, type);
        if (!result) {
          function.emitError(
              "cannot materialize a termination result for observer");
          return failure();
        }
        results.push_back(result);
      }
      sim::SimReturnOp::create(builder, location, results);
    } else if (failed(emitFunctionReturn(location, std::nullopt, false))) {
      return failure();
    }
    setCurrent(resume);
  }
  if (!directTask) {
    for (auto [index, copyOut] : llvm::enumerate(copyOuts)) {
      Type destinationType = getReferenceElementType(copyOut.destination);
      if (!destinationType)
        return failure();
      FailureOr<Value> converted =
          convert(callResults[index + (dpiTask ? 0 : 1)], destinationType,
                  copyOut.formalSigned, location, copyOut.destinationSigned);
      if (failed(converted))
        return failure();
      if (failed(storeReference(copyOut.destination, *converted, location)))
        return failure();
    }
  } else {
    for (const CopyOut &copyOut : copyOuts) {
      if (copyOut.taskDestination == copyOut.destination)
        continue;
      FailureOr<Value> copied =
          loadReference(copyOut.taskDestination, location);
      Type destinationType = getReferenceElementType(copyOut.destination);
      if (failed(copied) || !destinationType)
        return failure();
      FailureOr<Value> converted =
          convert(*copied, destinationType, copyOut.formalSigned, location,
                  copyOut.destinationSigned);
      if (failed(converted) ||
          failed(storeReference(copyOut.destination, *converted, location)))
        return failure();
    }
  }
  if (!dpiTask && !directTask)
    return callResults.front();
  return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                   builder.getBoolAttr(false))
      .getResult();
}

FailureOr<Value>
UnitLowering::lowerNewClass(semantic::SVNewClassExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.empty()) {
    sim::ClassHandleType receiverType;
    Value receiver;
    sim::SimClassDeclOp declaration;
    if (op.getIsSuperClass()) {
      if (!thisObject || !(receiverType = dyn_cast<sim::ClassHandleType>(
                               thisObject.getType()))) {
        emitError(location) << "implicit super.new has no current this object";
        return failure();
      }
      sim::SimClassDeclOp derived =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, receiverType.getClassName());
      if (!derived || !derived.getBaseAttr()) {
        emitError(location) << "implicit super.new has no resolved base class";
        return failure();
      }
      declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, derived.getBaseAttr());
      receiverType = sim::ClassHandleType::get(function.getContext(),
                                               derived.getBaseAttr());
      receiver = sim::SimClassCastOp::create(builder, location, receiverType,
                                             thisObject);
    } else {
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(resultType) ||
          !(receiverType = dyn_cast<sim::ClassHandleType>(*resultType)))
        return failure();
      declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, receiverType.getClassName());
      receiver = sim::SimClassAllocOp::create(
          builder, location, receiverType,
          function.getBody().front().getArgument(0));
    }
    auto constructorName = declaration ? declaration->getAttrOfType<StringAttr>(
                                             "obelisk_sim.implicit_constructor")
                                       : StringAttr{};
    if (!constructorName) {
      emitError(location)
          << "implicit class constructor has no executable implementation";
      return failure();
    }
    FlatSymbolRefAttr constructor = FlatSymbolRefAttr::get(
        function.getContext(), constructorName.getValue());
    sim::SimClassDirectCallOp::create(builder, location, TypeRange{},
                                      constructor, receiver, ValueRange{});
    if (!op.getIsSuperClass())
      return receiver;
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  if (children.size() != 1) {
    unsupported(op) << " (constructor inventory)";
    return failure();
  }
  auto call = dyn_cast<semantic::SVCallExpressionOp>(children.front());
  if (call && isWeakReferenceCall(call) && call.getCalleeName() == "new") {
    SmallVector<Operation *> actuals = getChildren(call);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (actuals.size() != 1 || failed(resultType) ||
        !isa<sim::ClassHandleType>(*resultType))
      return emitError(location)
                 << "weak_reference constructor requires one class handle",
             failure();
    FailureOr<Value> referent = lowerExpression(actuals.front());
    if (failed(referent) || !isa<sim::ClassHandleType>((*referent).getType()))
      return failure();
    return sim::SimWeakCreateOp::create(
               builder, location, *resultType,
               function.getBody().front().getArgument(0), *referent)
        .getResult();
  }
  auto callee = call ? call->getAttrOfType<FlatSymbolRefAttr>(calleeAttrName)
                     : FlatSymbolRefAttr{};
  auto formals = call ? call->getAttrOfType<ArrayAttr>(calleeFormalsAttrName)
                      : ArrayAttr{};
  if (!call || !callee || !formals) {
    unsupported(op) << " (unresolved constructor"
                    << (!call ? ": missing call" : "")
                    << (call && !callee ? ": missing callee" : "")
                    << (call && !formals ? ": missing formals" : "") << ")";
    return failure();
  }

  Value receiver;
  if (op.getIsSuperClass()) {
    if (!thisObject) {
      emitError(location) << "super.new has no current this object";
      return failure();
    }
    receiver = thisObject;
  } else {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType) || !isa<sim::ClassHandleType>(*resultType))
      return failure();
    receiver =
        sim::SimClassAllocOp::create(builder, location, *resultType,
                                     function.getBody().front().getArgument(0));
  }

  SmallVector<Operation *> actuals = getChildren(call);
  if (actuals.size() != formals.size()) {
    emitError(location)
        << "constructor has no complete frozen formal inventory";
    return failure();
  }
  struct ConstructorCopyOut {
    Value destination;
    Type formalType;
    bool formalSigned;
    bool destinationSigned;
  };
  SmallVector<Value> arguments;
  SmallVector<ConstructorCopyOut> copyOuts;
  for (auto [actual, formalAttr] : llvm::zip_equal(actuals, formals)) {
    auto formal = cast<DictionaryAttr>(formalAttr);
    auto direction = static_cast<semantic::SVArgumentDirection>(
        formal.getAs<IntegerAttr>("direction").getInt());
    Type formalType = formal.getAs<TypeAttr>("type").getValue();
    bool formalSigned = formal.getAs<BoolAttr>("is_signed").getValue();
    if (direction == semantic::SVArgumentDirection::In) {
      FailureOr<Value> argument = lowerExpression(actual);
      if (failed(argument))
        return failure();
      FailureOr<Value> converted = convert(
          *argument, formalType, isSignedNode(actual), location, formalSigned);
      if (failed(converted))
        return failure();
      arguments.push_back(*converted);
      continue;
    }

    Operation *destination = actual;
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2) {
        Operation *placeholder = outputChildren[1];
        while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
          SmallVector<Operation *> converted = getChildren(placeholder);
          if (converted.size() != 1)
            break;
          placeholder = converted.front();
        }
        if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder))
          destination = outputChildren.front();
      }
    }
    FailureOr<Value> destinationRef = lowerExpression(destination, true);
    if (failed(destinationRef))
      return failure();
    Type destinationType;
    if (auto ref = dyn_cast<sim::RefType>((*destinationRef).getType()))
      destinationType = ref.getElementType();
    else if (auto ref =
                 dyn_cast<sim::ManagedRefType>((*destinationRef).getType()))
      destinationType = ref.getElementType();
    else if (auto ref =
                 dyn_cast<sim::ArgumentRefType>((*destinationRef).getType()))
      destinationType = ref.getElementType();
    else
      return emitError(location)
                 << "constructor output, inout, and ref actuals must be "
                    "variable references",
             failure();

    if (direction == semantic::SVArgumentDirection::Ref) {
      FailureOr<Value> argument =
          toArgumentReference(*destinationRef, formalType, location);
      if (failed(argument))
        return emitError(location)
                   << "constructor ref actual type must exactly match the "
                      "formal type",
               failure();
      arguments.push_back(*argument);
      continue;
    }

    Value initial;
    if (direction == semantic::SVArgumentDirection::Out) {
      initial = createDefaultValue(builder, location, formalType);
      if (!initial)
        return emitError(location)
                   << "cannot materialize a constructor output-formal "
                      "default for type "
                   << formalType,
               failure();
    } else {
      FailureOr<Value> loaded = loadReference(*destinationRef, location);
      if (failed(loaded))
        return failure();
      if (isa<sim::RefType>((*destinationRef).getType()))
        recordSensitivity(*destinationRef);
      FailureOr<Value> converted =
          convert(*loaded, formalType, isSignedNode(destination), location,
                  formalSigned);
      if (failed(converted))
        return failure();
      initial = *converted;
    }
    arguments.push_back(initial);
    copyOuts.push_back(
        {*destinationRef, formalType, formalSigned, isSignedNode(destination)});
  }
  if (auto captures = call->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
    for (Attribute captureAttr : captures) {
      StringRef path = cast<StringAttr>(captureAttr).getValue();
      Value capture = values.lookup(path);
      if (!capture) {
        emitError(location)
            << "constructor capture has no frozen local binding: " << path;
        return failure();
      }
      arguments.push_back(capture);
    }
  auto constructor =
      SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(function, callee);
  if (!constructor || constructor.getFunctionType().getNumInputs() < 2 ||
      !isa<sim::ClassHandleType>(constructor.getFunctionType().getInput(1))) {
    emitError(location) << "constructor implementation has no this parameter";
    return failure();
  }
  FailureOr<Value> adjustedReceiver = convert(
      receiver, constructor.getFunctionType().getInput(1), false, location);
  if (failed(adjustedReceiver))
    return failure();
  SmallVector<Type> resultTypes;
  for (const ConstructorCopyOut &copyOut : copyOuts)
    resultTypes.push_back(copyOut.formalType);
  if (constructor.getFunctionType().getNumResults() != resultTypes.size()) {
    emitError(location)
        << "constructor implementation has inconsistent copy-out results";
    return failure();
  }
  auto invocation = sim::SimClassDirectCallOp::create(
      builder, location, resultTypes, callee, *adjustedReceiver, arguments);
  for (auto [result, copyOut] :
       llvm::zip_equal(invocation.getResults(), copyOuts)) {
    Type destinationType = getReferenceElementType(copyOut.destination);
    if (!destinationType)
      return failure();
    FailureOr<Value> converted =
        convert(result, destinationType, copyOut.formalSigned, location,
                copyOut.destinationSigned);
    if (failed(converted))
      return failure();
    if (failed(storeReference(copyOut.destination, *converted, location)))
      return failure();
  }
  if (!op.getIsSuperClass())
    return receiver;
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

  if (name == "$cast") {
    if (children.size() != 2) {
      emitError(location) << "$cast requires exactly two arguments";
      return failure();
    }
    Operation *destination = children.front();
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(destination)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2) {
        Operation *placeholder = outputChildren[1];
        while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
          SmallVector<Operation *> converted = getChildren(placeholder);
          if (converted.size() != 1)
            break;
          placeholder = converted.front();
        }
        if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder))
          destination = outputChildren.front();
      }
    }

    FailureOr<Value> destinationRef = lowerExpression(destination, true);
    if (failed(destinationRef))
      return failure();
    Type destinationType;
    if (auto ref = dyn_cast<sim::RefType>((*destinationRef).getType())) {
      destinationType = ref.getElementType();
    } else if (auto ref =
                   dyn_cast<sim::ManagedRefType>((*destinationRef).getType())) {
      destinationType = ref.getElementType();
    } else {
      emitError(location)
          << "$cast destination must be a variable or class property";
      return failure();
    }
    auto targetClass = dyn_cast<sim::ClassHandleType>(destinationType);
    if (!targetClass) {
      emitError(location) << "$cast currently requires class-handle operands";
      return failure();
    }
    FailureOr<Value> source =
        isa<semantic::SVNullLiteralOp>(children[1])
            ? FailureOr<Value>(sim::SimClassNullOp::create(
                                   builder, getSemanticLocation(children[1]),
                                   destinationType)
                                   .getResult())
            : lowerExpression(children[1]);
    if (failed(source) || !isa<sim::ClassHandleType>((*source).getType())) {
      emitError(location) << "$cast currently requires class-handle operands";
      return failure();
    }

    Value casted = sim::SimClassCastOp::create(builder, location,
                                               destinationType, *source);
    Value instance = sim::SimClassIsInstanceOp::create(
        builder, location, builder.getI1Type(), *source,
        FlatSymbolRefAttr::get(function.getContext(),
                               targetClass.getClassName().getRootReference()));
    Value sourceID = sim::SimClassIdOp::create(builder, location,
                                               builder.getI64Type(), *source);
    Value nullID = constant(builder.getI64Type(), 0);
    Value isNull = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, sourceID, nullID);
    Value succeeded = arith::OrIOp::create(builder, location, instance, isNull);
    Block *store = addBlock();
    Block *resume = addBlock();
    cf::CondBranchOp::create(builder, location, succeeded, store, resume);
    setCurrent(store);
    if (isa<sim::RefType>((*destinationRef).getType()))
      sim::SimRefStoreOp::create(builder, location, casted, *destinationRef);
    else
      sim::SimManagedStoreOp::create(builder, location, casted,
                                     *destinationRef);
    emitBranch(resume);
    setCurrent(resume);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType))
      return failure();
    return convert(succeeded, *resultType, false, location);
  }

  if (name == "$bits") {
    if (children.size() != 1) {
      emitError(location) << "$bits requires exactly one argument";
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType) {
      emitError(getSemanticLocation(children.front()))
          << "$bits argument has no elaborated semantic type";
      return failure();
    }
    std::optional<uint64_t> width =
        getSemanticBitstreamWidth(semanticType.getValue());
    if (!width) {
      emitError(getSemanticLocation(children.front()))
          << "$bits of a dynamically sized bitstream is not yet executable";
      return failure();
    }
    // `$bits` is an inquiry function: its operand is unevaluated. Preserve
    // Slang/SystemVerilog's signed 32-bit result by retaining the low 32 bits
    // even for an exceptionally large elaborated type.
    Value result = arith::ConstantOp::create(
        builder, location, i32, builder.getIntegerAttr(i32, APInt(32, *width)));
    return convertResult(result);
  }

  bool isDimensionCount =
      name == "$dimensions" || name == "$unpacked_dimensions";
  bool isRangeQuery = name == "$left" || name == "$right" || name == "$low" ||
                      name == "$high" || name == "$increment" ||
                      name == "$size";
  if (isDimensionCount || isRangeQuery) {
    size_t maximumArguments = isDimensionCount ? 1 : 2;
    if (children.empty() || children.size() > maximumArguments) {
      emitError(location) << name << " requires "
                          << (isDimensionCount ? "exactly one argument"
                                               : "one or two arguments");
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType) {
      emitError(getSemanticLocation(children.front()))
          << name << " argument has no elaborated semantic type";
      return failure();
    }

    SmallVector<SemanticDimension> dimensions =
        getSemanticDimensions(semanticType.getValue());
    if (isDimensionCount) {
      uint64_t count = dimensions.size();
      if (name == "$unpacked_dimensions") {
        count = 0;
        for (const SemanticDimension &dimension : dimensions) {
          if (!dimension.unpacked)
            break;
          ++count;
        }
      }
      // Dimension inquiry functions are unevaluated and always return a
      // signed 32-bit integer. Match the specified modulo-2^32 conversion.
      Value result = arith::ConstantOp::create(
          builder, location, i32,
          builder.getIntegerAttr(i32, APInt(32, count)));
      return convertResult(result);
    }

    if (dimensions.empty()) {
      emitError(getSemanticLocation(children.front()))
          << name << " argument has no queryable dimension";
      return failure();
    }

    auto rangeExtent = [](int64_t left,
                          int64_t right) -> std::optional<uint64_t> {
      uint64_t lhs = static_cast<uint64_t>(left);
      uint64_t rhs = static_cast<uint64_t>(right);
      uint64_t distance = left >= right ? lhs - rhs : rhs - lhs;
      if (distance == std::numeric_limits<uint64_t>::max())
        return std::nullopt;
      return distance + 1;
    };
    auto fixedQueryValue =
        [&](const SemanticDimension &dimension) -> std::optional<APInt> {
      if (!dimension.isFixed())
        return std::nullopt;
      int64_t value;
      if (name == "$left")
        value = dimension.left;
      else if (name == "$right")
        value = dimension.right;
      else if (name == "$low")
        value = std::min(dimension.left, dimension.right);
      else if (name == "$high")
        value = std::max(dimension.left, dimension.right);
      else if (name == "$increment")
        value = dimension.left >= dimension.right ? 1 : -1;
      else {
        std::optional<uint64_t> extent =
            rangeExtent(dimension.left, dimension.right);
        if (!extent)
          return std::nullopt;
        return APInt(32, *extent);
      }
      return APInt(32, static_cast<uint64_t>(value), true);
    };

    // A string has one packed, runtime-sized dimension. A literal string is
    // nevertheless a known object value and can be answered here without
    // introducing a runtime string representation.
    auto stringLiteral = getStringLiteral(children.front());
    auto queryValue =
        [&](const SemanticDimension &dimension) -> std::optional<APInt> {
      if (std::optional<APInt> value = fixedQueryValue(dimension))
        return value;
      if (dimension.kind != SemanticDimensionKind::String || !stringLiteral)
        return std::nullopt;
      uint64_t size = stringLiteral.getConstantValue().size();
      if (name == "$left" || name == "$low")
        return APInt(32, 0);
      if (name == "$right" || name == "$high")
        return APInt(32, size - 1);
      if (name == "$increment")
        return APInt(32, static_cast<uint64_t>(-1), true);
      return APInt(32, size);
    };

    SmallVector<APInt> values;
    values.reserve(dimensions.size());
    for (const SemanticDimension &dimension : dimensions) {
      std::optional<APInt> value = queryValue(dimension);
      if (!value) {
        emitError(getSemanticLocation(children.front()))
            << name
            << " requires the runtime value of a dynamically sized object";
        return failure();
      }
      values.push_back(*value);
    }

    if (children.size() == 1) {
      Value result = arith::ConstantOp::create(
          builder, location, i32, builder.getIntegerAttr(i32, values.front()));
      return convertResult(result);
    }

    // The dimension selector is evaluated exactly once. Case equality against
    // each valid one-based index preserves X/Z: an unknown, non-positive, or
    // out-of-range selector falls through to the all-X result.
    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> logicIndex =
        toLogic(*index, getSemanticLocation(children[1]));
    if (failed(logicIndex))
      return failure();
    auto indexType = cast<sim::LogicType>((*logicIndex).getType());
    unsigned indexWidth = indexType.getWidth();
    bool indexSigned = isSignedNode(children[1]);
    auto canRepresentPositive = [&](uint64_t value) {
      if (!indexWidth)
        return false;
      if (!indexSigned)
        return indexWidth >= 64 || value < (uint64_t(1) << indexWidth);
      if (indexWidth > 64)
        return true;
      if (indexWidth == 64)
        return value <=
               static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
      if (indexWidth == 1)
        return false;
      return value <= (uint64_t(1) << (indexWidth - 1)) - 1;
    };

    auto resultType = sim::LogicType::get(function.getContext(), 32);
    auto createLogicConstant = [&](sim::LogicType type, const APInt &value,
                                   const APInt &unknown) -> Value {
      auto planeType = builder.getIntegerType(type.getWidth());
      return sim::SimLogicConstantOp::create(
                 builder, location, type,
                 builder.getIntegerAttr(planeType, value),
                 builder.getIntegerAttr(planeType, unknown))
          .getResult();
    };
    Value result =
        createLogicConstant(resultType, APInt(32, 0), APInt::getAllOnes(32));
    for (auto [zeroBased, value] : llvm::enumerate(values)) {
      uint64_t oneBased = zeroBased + 1;
      if (!canRepresentPositive(oneBased))
        continue;
      Value expected = createLogicConstant(
          indexType, APInt(indexWidth, oneBased), APInt(indexWidth, 0));
      Value matches = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
          *logicIndex, expected);
      Value queryResult = createLogicConstant(resultType, value, APInt(32, 0));
      result = arith::SelectOp::create(builder, location, matches, queryResult,
                                       result);
    }
    return convertResult(result);
  }

  if (name == "$signed" || name == "$unsigned") {
    if (children.size() != 1) {
      emitError(location) << name << " requires exactly one argument";
      return failure();
    }
    FailureOr<Value> value = lowerExpression(children.front());
    if (failed(value))
      return failure();
    // Signedness is source-semantic metadata on the call expression. The
    // physical width and four-state domain are deliberately unchanged.
    return convertResult(*value);
  }

  auto lowerBitstream = [&](Operation *child) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    if (sim::getPackedScalarType((*value).getType()))
      return toLogic(*value, getSemanticLocation(child));
    if (sim::getProvenanceSpan((*value).getType()))
      return *value;
    emitError(getSemanticLocation(child))
        << "operand is not a fixed bitstream value: " << (*value).getType();
    return failure();
  };
  auto lowerStateControl = [&](Operation *child) -> FailureOr<Value> {
    FailureOr<Value> control = lowerExpression(child);
    if (failed(control))
      return failure();
    FailureOr<Value> logic = toLogic(*control, getSemanticLocation(child));
    if (failed(logic))
      return failure();
    if (cast<sim::LogicType>((*logic).getType()).getWidth() == 1)
      return *logic;
    return sim::SimLogicExtractOp::create(
               builder, getSemanticLocation(child),
               sim::LogicType::get(function.getContext(), 1), *logic,
               builder.getI64IntegerAttr(0))
        .getResult();
  };
  auto stateConstant = [&](bool value, bool unknown) -> Value {
    auto logic = sim::LogicType::get(function.getContext(), 1);
    auto plane = builder.getI1Type();
    return sim::SimLogicConstantOp::create(
               builder, location, logic,
               builder.getIntegerAttr(plane, value ? 1 : 0),
               builder.getIntegerAttr(plane, unknown ? 1 : 0))
        .getResult();
  };

  if (name == "$clog2") {
    if (children.size() != 1) {
      emitError(location) << "$clog2 requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerBitstream(children.front());
    if (failed(input))
      return failure();
    if (!isa<sim::LogicType>((*input).getType())) {
      emitError(getSemanticLocation(children.front()))
          << "$clog2 requires an integral operand";
      return failure();
    }
    Value result = sim::SimLogicClog2Op::create(builder, location, i32, *input);
    return convertResult(result);
  }

  if (name == "$countbits" || name == "$countones" || name == "$onehot" ||
      name == "$onehot0" || name == "$isunknown") {
    if ((name == "$countbits" && children.size() < 2) ||
        (name != "$countbits" && children.size() != 1)) {
      emitError(location)
          << name
          << (name == "$countbits"
                  ? " requires a bitstream and at least one control argument"
                  : " requires exactly one argument");
      return failure();
    }
    FailureOr<Value> input = lowerBitstream(children.front());
    if (failed(input))
      return failure();
    SmallVector<Value> controls;
    if (name == "$countbits") {
      for (Operation *child : ArrayRef(children).drop_front()) {
        FailureOr<Value> control = lowerStateControl(child);
        if (failed(control))
          return failure();
        controls.push_back(*control);
      }
    } else if (name == "$isunknown") {
      controls.push_back(stateConstant(false, true)); // X
      controls.push_back(stateConstant(true, true));  // Z
    } else {
      controls.push_back(stateConstant(true, false));
    }
    Value count = sim::SimLogicCountBitsOp::create(builder, location, i32,
                                                   *input, controls);
    if (name == "$countbits" || name == "$countones")
      return convertResult(count);

    arith::CmpIPredicate predicate = name == "$onehot0"
                                         ? arith::CmpIPredicate::ule
                                         : arith::CmpIPredicate::eq;
    int64_t limit = name == "$isunknown" ? 0 : 1;
    if (name == "$isunknown")
      predicate = arith::CmpIPredicate::ne;
    Value result = arith::CmpIOp::create(builder, location, predicate, count,
                                         constant(i32, limit));
    return convertResult(result);
  }

  if (name == "$time" || name == "$stime" || name == "$realtime") {
    if (!children.empty()) {
      emitError(location) << name << " accepts no arguments";
      return failure();
    }
    auto scaleAttr = function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
    if (!scaleAttr || !scaleAttr.getValue().isStrictlyPositive()) {
      function.emitError("code unit has no valid frozen time scale");
      return failure();
    }
    Value now = sim::SimTimeNowOp::create(builder, location, i64, context);
    if (name == "$realtime") {
      Value real = sim::SimTimeToRealOp::create(
          builder, location, builder.getF64Type(), now, scaleAttr);
      return convertResult(real);
    }
    Value scale = arith::ConstantOp::create(builder, location, i64, scaleAttr);
    Value quotient = arith::DivUIOp::create(builder, location, now, scale);
    Value remainder = arith::RemUIOp::create(builder, location, now, scale);
    uint64_t threshold = scaleAttr.getValue().getZExtValue() / 2 +
                         scaleAttr.getValue().getZExtValue() % 2;
    Value halfway = constant(i64, threshold);
    Value increment = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::uge, remainder, halfway);
    Value extended = arith::ExtUIOp::create(builder, location, i64, increment);
    Value rounded =
        arith::AddIOp::create(builder, location, quotient, extended);
    if (name == "$stime")
      rounded = arith::TruncIOp::create(builder, location, i32, rounded);
    return convertResult(rounded);
  }

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
    recordSensitivity(*event);
    Value triggered = sim::SimEventTriggeredOp::create(
        builder, location, builder.getI1Type(), *event);
    return convertResult(triggered);
  }

  if (name == "$finish" || name == "$stop") {
    if (children.size() > 1) {
      emitError(location) << name << " accepts at most one verbosity argument";
      return failure();
    }
    Value verbosity = constant(i32, 1);
    if (!children.empty()) {
      FailureOr<Value> lowered = lowerInteger(children.front(), i32);
      if (failed(lowered))
        return failure();
      verbosity = *lowered;
    }
    if (name == "$finish")
      sim::SimFinishOp::create(builder, location, context, verbosity);
    else
      sim::SimStopOp::create(builder, location, context, verbosity);
    if (failed(emitFunctionReturn(location, std::nullopt, false)))
      return failure();
    setCurrent(addBlock());
    return dummyTaskResult();
  }

  if (name == "$monitoron" || name == "$monitoroff") {
    if (!children.empty()) {
      emitError(location) << name << " accepts no arguments";
      return failure();
    }
    sim::SimMonitorControlOp::create(builder, location, name == "$monitoron");
    return dummyTaskResult();
  }

  StringRef postponedDisplay;
  bool persistentMonitor = false;
  if (name == "$strobe")
    postponedDisplay = "$display";
  else if (name == "$strobeb")
    postponedDisplay = "$displayb";
  else if (name == "$strobeo")
    postponedDisplay = "$displayo";
  else if (name == "$strobeh")
    postponedDisplay = "$displayh";
  else if (name == "$monitor") {
    postponedDisplay = "$display";
    persistentMonitor = true;
  } else if (name == "$monitorb") {
    postponedDisplay = "$displayb";
    persistentMonitor = true;
  } else if (name == "$monitoro") {
    postponedDisplay = "$displayo";
    persistentMonitor = true;
  } else if (name == "$monitorh") {
    postponedDisplay = "$displayh";
    persistentMonitor = true;
  }
  if (!postponedDisplay.empty()) {
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlinePostponedDisplay(op, postponedDisplay, persistentMonitor);
    if (failed(callback))
      return failure();
    sim::SimSpawnOp spawned = sim::SimSpawnOp::create(
        builder, location, callback->first.getSymNameAttr(), callback->second,
        ArrayAttr{}, ArrayAttr{});
    if (persistentMonitor)
      sim::SimMonitorRegisterOp::create(builder, location,
                                        spawned.getProcess());
    return dummyTaskResult();
  }

  struct DisplayKind {
    bool file = false;
    bool newline = false;
    int32_t radix = 10;
    uint32_t descriptor = 1;
    size_t skippedArguments = 0;
    StringRef severity;
    bool fatal = false;
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
  else if (name == "$info")
    display = DisplayKind{false, true, 10, 0x80000002u, 0, "INFO", false};
  else if (name == "$warning")
    display = DisplayKind{false, true, 10, 0x80000002u, 0, "WARNING", false};
  else if (name == "$error")
    display = DisplayKind{false, true, 10, 0x80000002u, 0, "ERROR", false};
  else if (name == "$fatal")
    display =
        DisplayKind{false,   true, 10, 0x80000002u, children.empty() ? 0u : 1u,
                    "FATAL", true};
  if (display) {
    size_t firstItem = display->file ? 1 : display->skippedArguments;
    if (children.size() < firstItem) {
      emitError(location) << name << " has too few arguments";
      return failure();
    }
    Value verbosity;
    if (display->fatal) {
      verbosity = constant(i32, 1);
      if (!children.empty()) {
        FailureOr<Value> lowered = lowerInteger(children.front(), i32);
        if (failed(lowered))
          return failure();
        verbosity = *lowered;
      }
    }
    Value descriptor = constant(i32, static_cast<int32_t>(display->descriptor));
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
    if (!display->severity.empty()) {
      std::string file = "<unknown>";
      unsigned line = 0;
      if (auto source = location->findInstanceOf<FileLineColLoc>()) {
        file = source.getFilename().str();
        line = source.getLine();
      }
      std::string prefix =
          (Twine(display->severity) + ": " + file + ":" + Twine(line) + ": ")
              .str();
      if (children.size() == firstItem)
        prefix += name.str() + " called.";
      std::string escaped;
      escaped.reserve(prefix.size());
      for (char character : prefix) {
        escaped.push_back(character);
        if (character == '%')
          escaped.push_back('%');
      }
      items.push_back(
          sim::SimBytesConstantOp::create(builder, location, escaped)
              .getResult());
      flags.push_back(0);
    }
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
        if ((*value).getType().isF64()) {
          items.push_back(*value);
          flags.push_back(4);
        } else {
          FailureOr<Value> scalar =
              toPackedScalar(*value, getSemanticLocation(child));
          if (failed(scalar))
            return failure();
          items.push_back(*scalar);
          flags.push_back(isSignedNode(child) ? 1 : 0);
        }
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
      lexicalScope =
          function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
    if (!lexicalScope) {
      op.emitError("display call has no elaborated lexical scope");
      return failure();
    }
    if (display->fatal)
      sim::SimFatalOp::create(builder, location, context, verbosity);
    sim::SimDisplayOp::create(builder, location, context, descriptor, items,
                              display->newline, display->radix, flags,
                              lexicalScope, op.getSystemLibraryCellAttr(),
                              timeMultiplier);
    if (display->fatal) {
      if (failed(emitFunctionReturn(location, std::nullopt, false)))
        return failure();
      setCurrent(addBlock());
    }
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
  if (isa<semantic::SVRealLiteralOp, semantic::SVTimeLiteralOp>(op)) {
    emitError(getSemanticLocation(op))
        << "real literals are supported only as procedural delay literals";
    return failure();
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
    if (isa<semantic::SVNullLiteralOp>(children.front()) &&
        isa<sim::ClassHandleType>(*target))
      return sim::SimClassNullOp::create(builder, getSemanticLocation(op),
                                         *target)
          .getResult();
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    return convert(*input, *target, isSignedNode(children.front()),
                   getSemanticLocation(op), isSignedNode(op));
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
    return sim::SimClassCopyOp::create(
               builder, getSemanticLocation(op), *resultType,
               function.getBody().front().getArgument(0), *converted)
        .getResult();
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
  if (auto inside = dyn_cast<semantic::SVInsideExpressionOp>(op))
    return lowerInside(inside);
  if (auto call = dyn_cast<semantic::SVCallExpressionOp>(op))
    return lowerCall(call);
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
    auto spelling = realLiteral->getAttrOfType<StringAttr>("constant_value");
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
    double precisionSteps = amount * static_cast<double>(scale / quantum);
    double roundedSteps = std::round(precisionSteps);
    long double ticks = static_cast<long double>(roundedSteps) * quantum;
    if (!std::isfinite(roundedSteps) || ticks < 0 ||
        ticks > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
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
    APInt scaled = amount * APInt(128, scaleAttr.getValue().getZExtValue());
    if (scaled.ugt(APInt(
            128, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())))) {
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
  if ((*amount).getType().isF64()) {
    auto quantumAttr =
        function->getAttrOfType<IntegerAttr>(delayQuantumAttrName);
    if (!quantumAttr) {
      function.emitError("code unit has no frozen delay quantum");
      return failure();
    }
    return sim::SimTimeFromRealOp::create(
               builder, location, sim::TimeType::get(function.getContext()),
               *amount, scaleAttr, quantumAttr)
        .getResult();
  }
  FailureOr<Value> scalar = toPackedScalar(*amount, location);
  if (failed(scalar))
    return failure();
  Value normalized = *scalar;
  if (auto logic = dyn_cast<sim::LogicType>(normalized.getType())) {
    Type bitsType = IntegerType::get(function.getContext(), logic.getWidth());
    Value bits =
        sim::SimLogicToBitsOp::create(builder, location, bitsType, normalized);
    Value roundTrip =
        sim::SimLogicFromBitsOp::create(builder, location, logic, bits);
    Value known = sim::SimLogicCompareOp::create(
        builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
        normalized, roundTrip);
    Value zero = arith::ConstantOp::create(builder, location, bitsType,
                                           builder.getIntegerAttr(bitsType, 0));
    normalized = arith::SelectOp::create(builder, location, known, bits, zero);
  }
  auto integer = dyn_cast<IntegerType>(normalized.getType());
  if (!integer || !integer.isSignless()) {
    emitError(location) << "dynamic delay is not an integral packed value";
    return failure();
  }
  if (isSignedNode(children.front())) {
    Value zero = arith::ConstantOp::create(builder, location, integer,
                                           builder.getIntegerAttr(integer, 0));
    Value nonnegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sge, normalized, zero);
    normalized = arith::SelectOp::create(builder, location, nonnegative,
                                         normalized, zero);
  }
  if (integer.getWidth() > 64) {
    emitError(location) << "dynamic delay wider than 64 bits is not executable";
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
  Value maximum =
      arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                builder.getI64IntegerAttr(maximumInput));
  Value inRange = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ule, *normalized64, maximum);
  Value checked = arith::SelectOp::create(builder, location, inRange,
                                          *normalized64, maximum);
  return sim::SimTimeScaleOp::create(builder, location,
                                     sim::TimeType::get(function.getContext()),
                                     checked, scaleAttr,
                                     /*is_signed=*/builder.getBoolAttr(false))
      .getResult();
}

LogicalResult UnitLowering::emitEventSuspend(Operation *control,
                                             Block *continuation,
                                             ValueRange continuationOperands) {
  Location location = getSemanticLocation(control);
  auto emitDirect = [&](Value watched, sim::EdgeKind edge, Block *successor,
                        ValueRange operands) {
    if (isa<sim::EventType>(watched.getType()))
      sim::SimSuspendEventOp::create(builder, location, watched, operands,
                                     sim::ContinuationSiteAttr{},
                                     sim::EventRegionAttr{}, successor);
    else if (edge == sim::EdgeKind::Change)
      sim::SimSuspendChangeOp::create(builder, location, watched, operands,
                                      sim::ContinuationSiteAttr{},
                                      sim::EventRegionAttr{}, successor);
    else
      sim::SimSuspendEdgeOp::create(builder, location, edge, watched, operands,
                                    sim::ContinuationSiteAttr{},
                                    sim::EventRegionAttr{}, successor);
  };
  auto evaluateInitial = [&](Operation *expression) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(expression);
    if (failed(value))
      return failure();
    if (isa<sim::EventType>((*value).getType()))
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    return toPackedScalar(*value, getSemanticLocation(expression));
  };
  auto emitObserved =
      [&](ArrayRef<semantic::SVSignalEventControlOp> events) -> LogicalResult {
    SmallVector<Value> primaries;
    SmallVector<Value> initials;
    SmallVector<Value> conditions;
    SmallVector<int32_t> edges;
    SmallVector<int32_t> conditionIndices;
    for (semantic::SVSignalEventControlOp event : events) {
      SmallVector<Operation *> children = getChildren(event);
      size_t expected = event.getHasIff() ? 2 : 1;
      if (children.size() != expected) {
        unsupported(event) << " (event expression inventory)";
        return failure();
      }
      FailureOr<Value> initial = evaluateInitial(children.front());
      FailureOr<Value> primary = bindObserver(children.front());
      if (failed(initial) || failed(primary))
        return failure();
      primaries.push_back(*primary);
      initials.push_back(*initial);
      auto edge = static_cast<int32_t>(event.getEdgeKind());
      FailureOr<Type> primaryType = getNormalizedSemanticType(children.front());
      if (succeeded(primaryType) && isa<sim::EventType>(*primaryType))
        edge = static_cast<int32_t>(sim::EdgeKind::Change);
      edges.push_back(edge);
      if (!event.getHasIff()) {
        conditionIndices.push_back(-1);
        continue;
      }
      FailureOr<Value> condition = bindObserver(children[1]);
      if (failed(condition))
        return failure();
      conditionIndices.push_back(static_cast<int32_t>(conditions.size()));
      conditions.push_back(*condition);
    }
    SmallVector<Value> values(primaries);
    llvm::append_range(values, initials);
    llvm::append_range(values, conditions);
    llvm::append_range(values, continuationOperands);
    sim::SimSuspendObserveOp::create(
        builder, location, values, static_cast<uint32_t>(conditions.size()),
        edges, conditionIndices, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr{}, continuation);
    return success();
  };

  if (auto event = dyn_cast<semantic::SVSignalEventControlOp>(control)) {
    SmallVector<Operation *> children = getChildren(event);
    size_t expected = event.getHasIff() ? 2 : 1;
    if (children.size() != expected) {
      unsupported(event) << " (event expression inventory)";
      return failure();
    }
    FailureOr<Type> watchedType = getNormalizedSemanticType(children.front());
    if (failed(watchedType))
      return failure();
    bool computed =
        !isAddressableExpression(children.front()) ||
        (event.getHasIff() && (!isAddressableExpression(children[1]) ||
                               isa<sim::EventType>(*watchedType)));
    if (computed)
      return emitObserved(ArrayRef<semantic::SVSignalEventControlOp>(event));
    FailureOr<Value> handle =
        lowerExpression(children.front(), !isa<sim::EventType>(*watchedType));
    if (failed(handle))
      return failure();
    auto edge = static_cast<sim::EdgeKind>(event.getEdgeKind());
    if (!event.getHasIff()) {
      emitDirect(*handle, edge, continuation, continuationOperands);
      return success();
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
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
    return success();
  }

  auto list = dyn_cast<semantic::SVEventListControlOp>(control);
  if (!list) {
    unsupported(control) << " (event timing control)";
    return failure();
  }
  SmallVector<semantic::SVSignalEventControlOp> events;
  bool computed = false;
  for (Operation *eventOp : getChildren(list)) {
    auto event = dyn_cast<semantic::SVSignalEventControlOp>(eventOp);
    if (!event) {
      unsupported(eventOp) << " (event-list member)";
      return failure();
    }
    SmallVector<Operation *> eventChildren = getChildren(event);
    size_t expected = event.getHasIff() ? 2 : 1;
    if (eventChildren.size() != expected) {
      unsupported(event) << " (event expression inventory)";
      return failure();
    }
    computed |=
        event.getHasIff() || !isAddressableExpression(eventChildren.front());
    FailureOr<Type> watchedType =
        getNormalizedSemanticType(eventChildren.front());
    if (failed(watchedType))
      return failure();
    computed |= isa<sim::EventType>(*watchedType);
    events.push_back(event);
  }
  if (events.empty()) {
    unsupported(control) << " (empty event list)";
    return failure();
  }
  if (computed)
    return emitObserved(events);

  SmallVector<Value> watched;
  SmallVector<int32_t> edges;
  for (semantic::SVSignalEventControlOp event : events) {
    Operation *expression = getChildren(event).front();
    FailureOr<Value> handle = lowerExpression(expression, true);
    if (failed(handle))
      return failure();
    watched.push_back(*handle);
    edges.push_back(static_cast<int32_t>(event.getEdgeKind()));
  }
  if (watched.size() == 1) {
    emitDirect(watched.front(), static_cast<sim::EdgeKind>(edges.front()),
               continuation, continuationOperands);
    return success();
  }
  SmallVector<Value> values(watched);
  llvm::append_range(values, continuationOperands);
  sim::SimSuspendAnyOp::create(
      builder, location, values, builder.getDenseI32ArrayAttr(edges),
      sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
  return success();
}

LogicalResult
UnitLowering::emitRepeatedEventSuspend(Operation *control, Block *continuation,
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
  Value zero = arith::ConstantOp::create(builder, location, countType,
                                         builder.getI64IntegerAttr(0));
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
  Value one = arith::ConstantOp::create(builder, location, countType,
                                        builder.getI64IntegerAttr(1));
  Value resumeZero = arith::ConstantOp::create(builder, location, countType,
                                               builder.getI64IntegerAttr(0));
  Value remaining =
      arith::SubIOp::create(builder, location, resume->getArgument(0), one);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, remaining, resumeZero);
  SmallVector<Value> nextWaitOperands{remaining};
  llvm::append_range(nextWaitOperands, resume->getArguments().drop_front());
  cf::CondBranchOp::create(builder, location, more, wait, nextWaitOperands,
                           continuation, resume->getArguments().drop_front());
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
      unsupported(control)
          << " (@* controlled statement has no readable dependency)";
      return failure();
    }
    setCurrent(waitBlock);
    SmallVector<int32_t> edges(dependencies.size(),
                               static_cast<int32_t>(sim::EdgeKind::Change));
    if (dependencies.size() == 1)
      sim::SimSuspendChangeOp::create(builder, location, dependencies.front(),
                                      ValueRange{}, sim::ContinuationSiteAttr{},
                                      sim::EventRegionAttr{}, continuation);
    else
      sim::SimSuspendAnyOp::create(
          builder, location, dependencies.getArrayRef(),
          builder.getDenseI32ArrayAttr(edges), sim::ContinuationSiteAttr{},
                                   sim::EventRegionAttr{}, continuation);
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
    sim::SimSuspendDelayOp::create(
        builder, location, *delay, sim::TimingSiteAttr{}, ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
  } else if (isa<semantic::SVOneStepDelayControlOp>(control)) {
    if (!children.empty()) {
      unsupported(control) << " (#1step inventory)";
      return failure();
    }
    Value delay = sim::SimTimeConstantOp::create(
        builder, location, sim::TimeType::get(function.getContext()),
        builder.getI64IntegerAttr(1));
    sim::SimSuspendDelayOp::create(
        builder, location, delay, sim::TimingSiteAttr{}, ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
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
    std::optional<bool> truth = foldConstantTruth(*condition);
    if (!truth) {
      unsupported(op)
          << " (computed wait condition has no readable dependency)";
      return failure();
    }
    emitBranch(*truth ? bodyBlock : suspendBlock);
    if (!*truth) {
      setCurrent(suspendBlock);
      sim::SimSuspendForeverOp::create(builder, location, ValueRange{},
                                       sim::ContinuationSiteAttr{},
                                       sim::EventRegionAttr{}, bodyBlock);
    } else
      suspendBlock->erase();
    setCurrent(bodyBlock);
    return lowerStatement(children[1]);
  }
  if (dependencies.size() != 1 || !isAddressableExpression(children[0])) {
    if (!children[0]->hasAttr("obelisk_sim.observer")) {
      unsupported(op) << " (computed wait condition requires an observer)";
      return failure();
    }
    cf::CondBranchOp::create(builder, location, *condition, bodyBlock,
                             ValueRange{}, suspendBlock, ValueRange{});
    setCurrent(suspendBlock);
    FailureOr<Value> observer = bindObserver(children[0]);
    if (failed(observer))
      return failure();
    SmallVector<Value> values{*observer, *condition};
    sim::SimSuspendObserveOp::create(
        builder, location, values, 0, ArrayRef<int32_t>{0},
        ArrayRef<int32_t>{-1}, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr{}, bodyBlock);
    setCurrent(bodyBlock);
    return lowerStatement(children[1]);
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
                                 sim::ContinuationSiteAttr{},
                                 sim::EventRegionAttr{}, bodyBlock);

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
    emitError(location) << "a timed named-event trigger must be nonblocking";
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
  sim::SimEventTriggerOp::create(builder, location, *event, delay,
                                 builder.getBoolAttr(op.getIsNonblocking()),
                                 sim::EventSiteAttr{});
  return success();
}

LogicalResult
UnitLowering::lowerQualifiedConditional(semantic::SVConditionalStatementOp op) {
  struct Branch {
    SmallVector<Operation *> expressions;
    SmallVector<Operation *> patterns;
    SmallVector<bool> hasPattern;
    Operation *body = nullptr;
    llvm::StringMap<Value> captures;
  };
  Location location = getSemanticLocation(op);
  SmallVector<Branch> branches;
  Operation *finalElse = nullptr;
  semantic::SVConditionalStatementOp cursor = op;
  while (cursor) {
    SmallVector<Operation *> children = getChildren(cursor);
    ArrayRef<int64_t> flags = cursor.getConditionPatternFlags();
    if (flags.size() != cursor.getConditionCount() ||
        cursor.getConditionCount() == 0) {
      emitError(getSemanticLocation(cursor))
          << "malformed qualified conditional inventory";
      return failure();
    }
    size_t conditionChildren = cursor.getConditionCount();
    for (int64_t flag : flags)
      conditionChildren += static_cast<size_t>(flag);
    size_t statementCount = 1 + cursor.getHasElse();
    if (children.size() != conditionChildren + statementCount) {
      emitError(getSemanticLocation(cursor))
          << "malformed qualified conditional statements";
      return failure();
    }
    Branch branch;
    size_t next = 0;
    for (int64_t flag : flags) {
      branch.expressions.push_back(children[next++]);
      branch.hasPattern.push_back(flag != 0);
      branch.patterns.push_back(flag ? children[next++] : nullptr);
    }
    branch.body = children[conditionChildren];
    branches.push_back(std::move(branch));
    if (!cursor.getHasElse())
      break;
    Operation *otherwise = children[conditionChildren + 1];
    if (auto nested = dyn_cast<semantic::SVConditionalStatementOp>(otherwise);
        nested &&
        nested.getCheckKind() == semantic::SVUniquePriorityCheck::None) {
      cursor = nested;
      continue;
    }
    finalElse = otherwise;
    break;
  }

  auto evaluateBranch = [&](Branch &branch, Block *matchedDestination,
                            Block *unmatchedDestination) -> LogicalResult {
    // Hoist this branch's capture storage ahead of its short-circuit CFG.
    // The allocation still executes once per dynamic statement execution, but
    // now dominates both later conditions and the deferred unique/unique0
    // dispatch body even when an earlier condition fails.
    SmallVector<semantic::SVVariablePatternOp> capturePatterns;
    for (Operation *pattern : branch.patterns)
      if (pattern)
        pattern->walk([&](semantic::SVVariablePatternOp variable) {
          capturePatterns.push_back(variable);
        });
    for (semantic::SVVariablePatternOp variable : capturePatterns) {
      StringRef path = variable.getReferencedPath();
      Value initial = localDefaults.lookup(path);
      if (path.empty() || !initial) {
        emitError(getSemanticLocation(variable))
            << "pattern variable has no activation-local binding";
        return failure();
      }
      if (branch.captures.count(path))
        continue;
      Value destination = sim::SimRefAllocOp::create(
          builder, getSemanticLocation(variable),
          sim::RefType::get(function.getContext(), initial.getType()), initial);
      branch.captures[path] = destination;
      values[path] = destination;
      lvalues[path] = destination;
    }

    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    for (size_t index = 0; index < branch.expressions.size(); ++index) {
      Operation *expression = branch.expressions[index];
      FailureOr<Value> value = lowerExpression(expression);
      if (failed(value))
        return failure();
      FailureOr<Value> matched;
      if (branch.hasPattern[index])
        matched =
            lowerPattern(*value, branch.patterns[index],
                         semantic::SVCaseCondition::Normal, &branch.captures);
      else
        matched = truthValue(*value, getSemanticLocation(expression));
      if (failed(matched))
        return failure();
      if (index + 1 == branch.expressions.size()) {
        cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                                 *matched, matchedDestination,
                                 ValueRange{trueValue}, unmatchedDestination,
                                 ValueRange{falseValue});
      } else {
        Block *nextCondition = addBlock();
        cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                                 *matched, nextCondition, ValueRange{},
                                 unmatchedDestination, ValueRange{falseValue});
        setCurrent(nextCondition);
      }
    }
    return success();
  };

  Block *mergeBlock = addBlock();
  bool inspectAll =
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique ||
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique0;
  if (!inspectAll) {
    for (Branch &branch : branches) {
      Block *bodyBlock = addBlock();
      Block *nextBranch = addBlock();
      bodyBlock->addArgument(builder.getI1Type(), location);
      nextBranch->addArgument(builder.getI1Type(), location);
      if (failed(evaluateBranch(branch, bodyBlock, nextBranch)))
        return failure();
      setCurrent(bodyBlock);
      for (auto &capture : branch.captures)
        values[capture.getKey()] = capture.getValue();
      if (failed(lowerStatement(branch.body)))
        return failure();
      emitBranch(mergeBlock);
      setCurrent(nextBranch);
    }
    if (!finalElse)
      emitQualifierWarning(location, op.getCheckKind(), "if", "no match");
    else if (failed(lowerStatement(finalElse)))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(mergeBlock);
    return success();
  }

  auto i32 = builder.getI32Type();
  Value matchCount = arith::ConstantOp::create(builder, location, i32,
                                               builder.getI32IntegerAttr(0));
  Value selected = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(-1));
  for (auto [branchIndex, branch] : llvm::enumerate(branches)) {
    Block *groupDone = addBlock();
    groupDone->addArgument(builder.getI1Type(), location);
    if (failed(evaluateBranch(branch, groupDone, groupDone)))
      return failure();
    setCurrent(groupDone);
    Value groupMatched = groupDone->getArgument(0);
    Value one = arith::ConstantOp::create(builder, location, i32,
                                          builder.getI32IntegerAttr(1));
    Value incremented =
        arith::AddIOp::create(builder, location, matchCount, one);
    matchCount = arith::SelectOp::create(builder, location, groupMatched,
                                         incremented, matchCount);
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noSelection = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, selected, zero);
    Value choose =
        arith::AndIOp::create(builder, location, groupMatched, noSelection);
    Value indexValue = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(branchIndex));
    selected = arith::SelectOp::create(builder, location, choose, indexValue,
                                       selected);
    if (branchIndex + 1 != branches.size()) {
      Block *nextBranch = addBlock();
      nextBranch->addArgument(i32, location);
      nextBranch->addArgument(i32, location);
      cf::BranchOp::create(builder, location, nextBranch,
                           ValueRange{matchCount, selected});
      setCurrent(nextBranch);
      matchCount = nextBranch->getArgument(0);
      selected = nextBranch->getArgument(1);
    }
  }
  Value one = arith::ConstantOp::create(builder, location, i32,
                                        builder.getI32IntegerAttr(1));
  Value overlap = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, matchCount, one);
  Block *overlapWarning = addBlock();
  Block *checkNoMatch = addBlock();
  Block *dispatch = addBlock();
  cf::CondBranchOp::create(builder, location, overlap, overlapWarning,
                           ValueRange{}, checkNoMatch, ValueRange{});
  setCurrent(overlapWarning);
  emitQualifierWarning(location, op.getCheckKind(), "if", "multiple matches");
  emitBranch(dispatch);
  setCurrent(checkNoMatch);
  if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique &&
      !finalElse) {
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noMatch = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, matchCount, zero);
    Block *noMatchWarning = addBlock();
    cf::CondBranchOp::create(builder, location, noMatch, noMatchWarning,
                             ValueRange{}, dispatch, ValueRange{});
    setCurrent(noMatchWarning);
    emitQualifierWarning(location, op.getCheckKind(), "if", "no match");
    emitBranch(dispatch);
  } else {
    emitBranch(dispatch);
  }
  setCurrent(dispatch);
  for (auto [branchIndex, branch] : llvm::enumerate(branches)) {
    Value indexValue = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(branchIndex));
    Value isSelected = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, selected, indexValue);
    Block *bodyBlock = addBlock();
    Block *nextDispatch = addBlock();
    cf::CondBranchOp::create(builder, location, isSelected, bodyBlock,
                             ValueRange{}, nextDispatch, ValueRange{});
    setCurrent(bodyBlock);
    for (auto &capture : branch.captures)
      values[capture.getKey()] = capture.getValue();
    if (failed(lowerStatement(branch.body)))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(nextDispatch);
  }
  if (finalElse && failed(lowerStatement(finalElse)))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

LogicalResult
UnitLowering::lowerConditional(semantic::SVConditionalStatementOp op) {
  if (op.getCheckKind() != semantic::SVUniquePriorityCheck::None)
    return lowerQualifiedConditional(op);
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  ArrayRef<int64_t> patternFlags = op.getConditionPatternFlags();
  if (op.getConditionCount() == 0 ||
      patternFlags.size() != op.getConditionCount()) {
    emitError(location) << "malformed conditional inventory";
    return failure();
  }
  size_t conditionChildren = op.getConditionCount();
  for (int64_t flag : patternFlags)
    conditionChildren += static_cast<size_t>(flag);
  size_t statementCount = 1 + op.getHasElse();
  if (children.size() != conditionChildren + statementCount) {
    emitError(location) << "malformed conditional expression inventory";
    return failure();
  }
  ArrayRef<Operation *> conditions =
      ArrayRef<Operation *>(children).take_front(conditionChildren);
  ArrayRef<Operation *> statements =
      ArrayRef<Operation *>(children).take_back(statementCount);
  Block *thenBlock = addBlock();
  Block *elseBlock = addBlock();
  Block *mergeBlock = addBlock();
  size_t childIndex = 0;
  for (size_t conditionIndex = 0; conditionIndex < op.getConditionCount();
       ++conditionIndex) {
    Operation *expression = conditions[childIndex++];
    FailureOr<Value> conditionValue = lowerExpression(expression);
    if (failed(conditionValue))
      return failure();
    FailureOr<Value> condition;
    if (patternFlags[conditionIndex]) {
      Operation *pattern = conditions[childIndex++];
      condition = lowerPattern(*conditionValue, pattern,
                               semantic::SVCaseCondition::Normal);
    } else {
      condition = truthValue(*conditionValue, getSemanticLocation(expression));
    }
    if (failed(condition))
      return failure();
    if (conditionIndex + 1 == op.getConditionCount()) {
      cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                               *condition, thenBlock, ValueRange{}, elseBlock,
                               ValueRange{});
    } else {
      Block *nextCondition = addBlock();
      cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                               *condition, nextCondition, ValueRange{},
                               elseBlock, ValueRange{});
      setCurrent(nextCondition);
    }
  }
  setCurrent(thenBlock);
  if (failed(lowerStatement(statements[0])))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(elseBlock);
  if (op.getHasElse() && failed(lowerStatement(statements[1])))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

FailureOr<Value> UnitLowering::lowerPattern(Value input, Operation *pattern,
                                            semantic::SVCaseCondition condition,
                                            llvm::StringMap<Value> *captures) {
  Location location = getSemanticLocation(pattern);
  auto trueValue = [&]() -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(true));
  };
  if (isa<semantic::SVWildcardPatternOp>(pattern))
    return trueValue();
  if (auto constant = dyn_cast<semantic::SVConstantPatternOp>(pattern)) {
    SmallVector<Operation *> children = getChildren(pattern);
    if (children.size() != 1) {
      emitError(location) << "constant pattern must contain one expression";
      return failure();
    }
    return lowerCaseLabel(input, input.getType(), children.front(),
                          children.front(), condition);
  }
  if (auto variable = dyn_cast<semantic::SVVariablePatternOp>(pattern)) {
    StringRef path = variable.getReferencedPath();
    if (path.empty()) {
      emitError(location) << "pattern variable has no resolved binding";
      return failure();
    }
    Value initial = localDefaults.lookup(path);
    if (!initial || initial.getType() != input.getType()) {
      emitError(location)
          << "pattern variable has no compatible activation-local binding";
      return failure();
    }
    Value destination = captures ? captures->lookup(path) : Value{};
    if (destination) {
      auto reference = dyn_cast<sim::RefType>(destination.getType());
      if (!reference || reference.getElementType() != input.getType()) {
        emitError(location)
            << "pattern capture storage has an incompatible type";
        return failure();
      }
      sim::SimRefStoreOp::create(builder, location, input, destination);
    } else {
      destination = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), input.getType()), input);
    }
    values[path] = destination;
    lvalues[path] = destination;
    if (captures)
      (*captures)[path] = destination;
    return trueValue();
  }
  if (auto tagged = dyn_cast<semantic::SVTaggedPatternOp>(pattern)) {
    auto ordinalAttr = tagged.getFieldOrdinalAttr();
    if (!ordinalAttr || ordinalAttr.getValue().isNegative()) {
      emitError(location) << "tagged pattern has no valid field ordinal";
      return failure();
    }
    uint64_t ordinal = ordinalAttr.getValue().getZExtValue();
    Type fieldType =
        ordinal <= std::numeric_limits<unsigned>::max()
            ? sim::getAggregateElementType(input.getType(), ordinal)
            : Type{};
    if (!fieldType) {
      emitError(location) << "tagged pattern field ordinal is out of range";
      return failure();
    }
    Value active = sim::SimUnionIsActiveOp::create(
        builder, location, builder.getI1Type(), input, ordinal);
    SmallVector<Operation *> children = getChildren(pattern);
    if (children.empty())
      return active;
    if (children.size() != 1) {
      emitError(location) << "tagged pattern has malformed value inventory";
      return failure();
    }
    Value field = sim::SimUnionExtractOp::create(builder, location, fieldType,
                                                 input, ordinal);
    FailureOr<Value> nested =
        lowerPattern(field, children.front(), condition, captures);
    if (failed(nested))
      return failure();
    return arith::AndIOp::create(builder, location, active, *nested)
        .getResult();
  }
  if (auto structure = dyn_cast<semantic::SVStructurePatternOp>(pattern)) {
    ArrayRef<int64_t> ordinals = structure.getFieldOrdinals();
    SmallVector<Operation *> children = getChildren(pattern);
    if (ordinals.size() != children.size()) {
      emitError(location) << "malformed structure pattern inventory";
      return failure();
    }
    if (!isa<sim::PackedStructType, sim::UnpackedStructType>(input.getType())) {
      emitError(location) << "structure pattern input is not a fixed struct";
      return failure();
    }
    Value matched = trueValue();
    for (auto [ordinal, child] : llvm::zip_equal(ordinals, children)) {
      if (ordinal < 0) {
        emitError(location) << "structure pattern field ordinal is negative";
        return failure();
      }
      Type fieldType = sim::getAggregateElementType(
          input.getType(), static_cast<unsigned>(ordinal));
      if (!fieldType) {
        emitError(location)
            << "structure pattern field ordinal is out of range";
        return failure();
      }
      Value field = sim::SimAggregateExtractOp::create(
          builder, getSemanticLocation(child), fieldType, input, ordinal);
      FailureOr<Value> nested = lowerPattern(field, child, condition, captures);
      if (failed(nested))
        return failure();
      matched = arith::AndIOp::create(builder, getSemanticLocation(child),
                                      matched, *nested);
    }
    return matched;
  }
  emitError(location) << "unsupported executable pattern kind";
  return failure();
}

void UnitLowering::emitQualifierWarning(
    Location location, semantic::SVUniquePriorityCheck qualifier,
    StringRef statementKind, StringRef reason) {
  StringRef qualifierName;
  switch (qualifier) {
  case semantic::SVUniquePriorityCheck::Unique:
    qualifierName = "unique";
    break;
  case semantic::SVUniquePriorityCheck::Unique0:
    qualifierName = "unique0";
    break;
  case semantic::SVUniquePriorityCheck::Priority:
    qualifierName = "priority";
    break;
  case semantic::SVUniquePriorityCheck::None:
    return;
  }
  std::string file = "<unknown>";
  unsigned line = 0;
  unsigned column = 0;
  if (auto fileLocation = location->findInstanceOf<FileLineColLoc>()) {
    file = fileLocation.getFilename().str();
    line = fileLocation.getLine();
    column = fileLocation.getColumn();
  }
  std::string message = (Twine(file) + ":" + Twine(line) + ":" + Twine(column) +
                         ": warning: " + qualifierName + " " + statementKind +
                         " violation: " + reason)
                            .str();
  Value text =
      sim::SimBytesConstantOp::create(builder, location, message).getResult();
  Value descriptor = arith::ConstantOp::create(
      builder, location, builder.getI32Type(),
      builder.getI32IntegerAttr(static_cast<int32_t>(0x80000002u)));
  StringAttr scope =
      function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
  IntegerAttr multiplier =
      function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
  sim::SimDisplayOp::create(
      builder, location, function.getBody().front().getArgument(0), descriptor,
      ValueRange{text}, true, 10, ArrayRef<int32_t>{0}, scope, StringAttr{},
      multiplier);
}

FailureOr<Value>
UnitLowering::lowerCaseLabel(Value selector, Type selectorType,
                             Operation *selectorNode, Operation *label,
                             semantic::SVCaseCondition condition) {
  Location location = getSemanticLocation(label);
  bool selectorLogic =
      isa<sim::LogicType>(sim::getPackedScalarType(selectorType));
  auto compareValue =
      [&](Value candidate, sim::CompareKind logicKind,
          arith::CmpIPredicate integerKind) -> FailureOr<Value> {
    FailureOr<Value> normalized =
        convert(candidate, selectorType, false, location);
    if (failed(normalized))
      return failure();
    FailureOr<Value> scalarCandidate = toPackedScalar(*normalized, location);
    FailureOr<Value> scalarSelector = toPackedScalar(selector, location);
    if (failed(scalarCandidate) || failed(scalarSelector) ||
        (*scalarCandidate).getType() != (*scalarSelector).getType())
      return failure();
    if (selectorLogic) {
      bool integerResult = logicKind == sim::CompareKind::CaseEq ||
                           logicKind == sim::CompareKind::CaseZEq ||
                           logicKind == sim::CompareKind::CaseXZEq;
      return sim::SimLogicCompareOp::create(
                 builder, location,
                 integerResult
                     ? Type(builder.getI1Type())
                     : Type(sim::LogicType::get(function.getContext(), 1)),
                 logicKind, *scalarSelector, *scalarCandidate)
          .getResult();
    }
    return arith::CmpIOp::create(builder, location, integerKind,
                                 *scalarSelector, *scalarCandidate)
        .getResult();
  };

  if (condition != semantic::SVCaseCondition::Inside) {
    FailureOr<Value> candidate = lowerExpression(label);
    if (failed(candidate))
      return failure();
    sim::CompareKind logicKind = sim::CompareKind::CaseEq;
    if (condition == semantic::SVCaseCondition::WildcardJustZ)
      logicKind = sim::CompareKind::CaseZEq;
    else if (condition == semantic::SVCaseCondition::WildcardXOrZ)
      logicKind = sim::CompareKind::CaseXZEq;
    return compareValue(*candidate, logicKind, arith::CmpIPredicate::eq);
  }

  auto combineLogic = [&](Value lhs, Value rhs) -> Value {
    if (selectorLogic)
      return sim::SimLogicLogicalOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          sim::LogicalKind::And, lhs, rhs);
    return arith::AndIOp::create(builder, location, lhs, rhs);
  };
  Value result;
  if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(label)) {
    if (range.getRangeKind() != semantic::SVValueRangeKind::Simple) {
      emitError(location) << "case inside tolerance ranges are not executable";
      return failure();
    }
    SmallVector<Operation *> endpoints = getChildren(label);
    if (endpoints.size() != 2) {
      emitError(location) << "malformed case inside range inventory";
      return failure();
    }
    bool signedSelector = isSignedNode(selectorNode);
    if (!isUnboundedEndpoint(endpoints[0])) {
      FailureOr<Value> lower = lowerExpression(endpoints[0]);
      if (failed(lower))
        return failure();
      FailureOr<Value> above = compareValue(
          *lower,
          signedSelector ? sim::CompareKind::SGE : sim::CompareKind::UGE,
          signedSelector ? arith::CmpIPredicate::sge
                         : arith::CmpIPredicate::uge);
      if (failed(above))
        return failure();
      result = *above;
    }
    if (!isUnboundedEndpoint(endpoints[1])) {
      FailureOr<Value> upper = lowerExpression(endpoints[1]);
      if (failed(upper))
        return failure();
      FailureOr<Value> below = compareValue(
          *upper,
          signedSelector ? sim::CompareKind::SLE : sim::CompareKind::ULE,
          signedSelector ? arith::CmpIPredicate::sle
                         : arith::CmpIPredicate::ule);
      if (failed(below))
        return failure();
      result = result ? combineLogic(result, *below) : *below;
    }
  } else {
    FailureOr<Value> candidate = lowerExpression(label);
    if (failed(candidate))
      return failure();
    if (auto array = dyn_cast<sim::UnpackedArrayType>((*candidate).getType())) {
      unsigned count = sim::getAggregateNumElements(array);
      for (unsigned index = 0; index < count; ++index) {
        Value element = sim::SimAggregateExtractOp::create(
            builder, location, array.getElementType(), *candidate, index);
        FailureOr<Value> equal = compareValue(element, sim::CompareKind::WildEq,
                                              arith::CmpIPredicate::eq);
        if (failed(equal))
          return failure();
        if (!result)
          result = *equal;
        else if (selectorLogic)
          result = sim::SimLogicLogicalOp::create(
              builder, location, sim::LogicType::get(function.getContext(), 1),
              sim::LogicalKind::Or, result, *equal);
        else
          result = arith::OrIOp::create(builder, location, result, *equal);
      }
    } else {
      FailureOr<Value> equal = compareValue(
          *candidate, sim::CompareKind::WildEq, arith::CmpIPredicate::eq);
      if (failed(equal))
        return failure();
      result = *equal;
    }
  }
  if (!result) {
    emitError(location) << "case inside item has no comparable value";
    return failure();
  }
  return truthValue(result, location);
}

LogicalResult UnitLowering::lowerCase(semantic::SVCaseStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.empty()) {
    emitError(location) << "case statement has no selector";
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
  if (!sim::getPackedScalarType((*selector).getType())) {
    emitError(location) << "case selector is not an executable packed value";
    return failure();
  }

  Block *mergeBlock = addBlock();

  bool inspectAll =
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique ||
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique0;
  if (!inspectAll) {
    size_t nextLabel = 0;
    for (size_t item = 0; item < itemCount; ++item) {
      size_t labelCount = static_cast<size_t>(labelCounts[item]);
      ArrayRef<Operation *> itemLabels = labels.slice(nextLabel, labelCount);
      nextLabel += labelCount;
      Block *itemBlock = addBlock();
      Block *nextItemBlock = addBlock();
      for (auto [labelIndex, label] : llvm::enumerate(itemLabels)) {
        FailureOr<Value> matched =
            lowerCaseLabel(*selector, (*selector).getType(), children.front(),
                           label, op.getConditionKind());
        if (failed(matched))
          return failure();
        if (labelIndex + 1 == itemLabels.size()) {
          cf::CondBranchOp::create(builder, getSemanticLocation(label),
                                   *matched, itemBlock, ValueRange{},
                                   nextItemBlock, ValueRange{});
        } else {
          Block *nextLabelBlock = addBlock();
          cf::CondBranchOp::create(builder, getSemanticLocation(label),
                                   *matched, itemBlock, ValueRange{},
                                   nextLabelBlock, ValueRange{});
          setCurrent(nextLabelBlock);
        }
      }
      setCurrent(itemBlock);
      if (failed(lowerStatement(statements[item])))
        return failure();
      emitBranch(mergeBlock);
      setCurrent(nextItemBlock);
    }
    if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Priority &&
        !hasDefault)
      emitQualifierWarning(location, op.getCheckKind(), "case", "no match");
    if (hasDefault && failed(lowerStatement(statements.back())))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(mergeBlock);
    return success();
  }

  auto i32 = builder.getI32Type();
  Value matchCount = arith::ConstantOp::create(builder, location, i32,
                                               builder.getI32IntegerAttr(0));
  Value selected = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(-1));
  size_t nextLabel = 0;
  for (size_t item = 0; item < itemCount; ++item) {
    size_t labelCount = static_cast<size_t>(labelCounts[item]);
    ArrayRef<Operation *> itemLabels = labels.slice(nextLabel, labelCount);
    nextLabel += labelCount;
    Block *groupDone = addBlock();
    groupDone->addArgument(builder.getI1Type(), location);
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    for (auto [labelIndex, label] : llvm::enumerate(itemLabels)) {
      FailureOr<Value> matched =
          lowerCaseLabel(*selector, (*selector).getType(), children.front(),
                         label, op.getConditionKind());
      if (failed(matched))
        return failure();
      if (labelIndex + 1 == itemLabels.size()) {
        cf::CondBranchOp::create(builder, getSemanticLocation(label), *matched,
                                 groupDone, ValueRange{trueValue}, groupDone,
                                 ValueRange{falseValue});
      } else {
        Block *nextLabelBlock = addBlock();
        cf::CondBranchOp::create(builder, getSemanticLocation(label), *matched,
                                 groupDone, ValueRange{trueValue},
                                 nextLabelBlock, ValueRange{});
        setCurrent(nextLabelBlock);
      }
    }
    setCurrent(groupDone);
    Value groupMatched = groupDone->getArgument(0);
    Value one = arith::ConstantOp::create(builder, location, i32,
                                          builder.getI32IntegerAttr(1));
    Value incremented =
        arith::AddIOp::create(builder, location, matchCount, one);
    matchCount = arith::SelectOp::create(builder, location, groupMatched,
                                         incremented, matchCount);
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noSelection = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, selected, zero);
    Value selectThis =
        arith::AndIOp::create(builder, location, groupMatched, noSelection);
    Value itemIndex = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(item));
    selected = arith::SelectOp::create(builder, location, selectThis, itemIndex,
                                       selected);
    if (item + 1 != itemCount) {
      Block *nextGroup = addBlock();
      nextGroup->addArgument(i32, location);
      nextGroup->addArgument(i32, location);
      cf::BranchOp::create(builder, location, nextGroup,
                           ValueRange{matchCount, selected});
      setCurrent(nextGroup);
      matchCount = nextGroup->getArgument(0);
      selected = nextGroup->getArgument(1);
    }
  }

  Value one = arith::ConstantOp::create(builder, location, i32,
                                        builder.getI32IntegerAttr(1));
  Value overlap = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, matchCount, one);
  Block *overlapWarning = addBlock();
  Block *checkNoMatch = addBlock();
  Block *dispatch = addBlock();
  cf::CondBranchOp::create(builder, location, overlap, overlapWarning,
                           ValueRange{}, checkNoMatch, ValueRange{});
  setCurrent(overlapWarning);
  emitQualifierWarning(location, op.getCheckKind(), "case", "multiple matches");
  emitBranch(dispatch);
  setCurrent(checkNoMatch);
  if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique &&
      !hasDefault) {
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noMatch = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, matchCount, zero);
    Block *noMatchWarning = addBlock();
    cf::CondBranchOp::create(builder, location, noMatch, noMatchWarning,
                             ValueRange{}, dispatch, ValueRange{});
    setCurrent(noMatchWarning);
    emitQualifierWarning(location, op.getCheckKind(), "case", "no match");
    emitBranch(dispatch);
  } else {
    emitBranch(dispatch);
  }

  setCurrent(dispatch);
  for (size_t item = 0; item < itemCount; ++item) {
    Value itemIndex = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(item));
    Value isSelected = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, selected, itemIndex);
    Block *itemBlock = addBlock();
    Block *nextDispatch = addBlock();
    cf::CondBranchOp::create(builder, location, isSelected, itemBlock,
                             ValueRange{}, nextDispatch, ValueRange{});
    setCurrent(itemBlock);
    if (failed(lowerStatement(statements[item])))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(nextDispatch);
  }
  if (hasDefault && failed(lowerStatement(statements.back())))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

LogicalResult
UnitLowering::lowerPatternCase(semantic::SVPatternCaseStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  size_t itemCount = op.getItemCount();
  ArrayRef<int64_t> filterFlags = op.getItemFilterFlags();
  if (filterFlags.size() != itemCount || children.empty()) {
    emitError(location) << "malformed pattern case inventory";
    return failure();
  }
  size_t filterCount = 0;
  for (int64_t flag : filterFlags) {
    if (flag != 0 && flag != 1) {
      emitError(location) << "invalid pattern case filter inventory";
      return failure();
    }
    filterCount += static_cast<size_t>(flag);
  }
  size_t expressionCount = 1 + itemCount + filterCount;
  size_t statementCount = itemCount + op.getHasDefault();
  if (children.size() != expressionCount + statementCount) {
    emitError(location) << "malformed pattern case item inventory";
    return failure();
  }
  FailureOr<Value> selector = lowerExpression(children.front());
  if (failed(selector))
    return failure();
  SmallVector<Operation *> patterns;
  SmallVector<Operation *> filters(itemCount, nullptr);
  size_t next = 1;
  for (size_t item = 0; item < itemCount; ++item) {
    patterns.push_back(children[next++]);
    if (filterFlags[item])
      filters[item] = children[next++];
  }
  ArrayRef<Operation *> statements =
      ArrayRef<Operation *>(children).take_back(statementCount);
  Block *mergeBlock = addBlock();
  bool inspectAll =
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique ||
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique0;

  if (!inspectAll) {
    for (size_t item = 0; item < itemCount; ++item) {
      llvm::StringMap<Value> captures;
      FailureOr<Value> patternMatched = lowerPattern(
          *selector, patterns[item], op.getConditionKind(), &captures);
      if (failed(patternMatched))
        return failure();
      Block *bodyBlock = addBlock();
      Block *nextItem = addBlock();
      if (Operation *filter = filters[item]) {
        Block *filterBlock = addBlock();
        cf::CondBranchOp::create(builder, getSemanticLocation(patterns[item]),
                                 *patternMatched, filterBlock, ValueRange{},
                                 nextItem, ValueRange{});
        setCurrent(filterBlock);
        FailureOr<Value> filtered = lowerExpression(filter);
        if (failed(filtered))
          return failure();
        FailureOr<Value> filterTruth =
            truthValue(*filtered, getSemanticLocation(filter));
        if (failed(filterTruth))
          return failure();
        cf::CondBranchOp::create(builder, getSemanticLocation(filter),
                                 *filterTruth, bodyBlock, ValueRange{},
                                 nextItem, ValueRange{});
      } else {
        cf::CondBranchOp::create(builder, getSemanticLocation(patterns[item]),
                                 *patternMatched, bodyBlock, ValueRange{},
                                 nextItem, ValueRange{});
      }
      setCurrent(bodyBlock);
      for (auto &capture : captures)
        values[capture.getKey()] = capture.getValue();
      if (failed(lowerStatement(statements[item])))
        return failure();
      emitBranch(mergeBlock);
      setCurrent(nextItem);
    }
    if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Priority &&
        !op.getHasDefault())
      emitQualifierWarning(location, op.getCheckKind(), "case", "no match");
    if (op.getHasDefault() && failed(lowerStatement(statements.back())))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(mergeBlock);
    return success();
  }

  auto i32 = builder.getI32Type();
  Value matchCount = arith::ConstantOp::create(builder, location, i32,
                                               builder.getI32IntegerAttr(0));
  Value selected = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(-1));
  SmallVector<llvm::StringMap<Value>> itemCaptures(itemCount);
  for (size_t item = 0; item < itemCount; ++item) {
    FailureOr<Value> patternMatched = lowerPattern(
        *selector, patterns[item], op.getConditionKind(), &itemCaptures[item]);
    if (failed(patternMatched))
      return failure();
    Block *groupDone = addBlock();
    groupDone->addArgument(builder.getI1Type(), location);
    if (Operation *filter = filters[item]) {
      Block *filterBlock = addBlock();
      Value falseValue = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(false));
      cf::CondBranchOp::create(builder, getSemanticLocation(patterns[item]),
                               *patternMatched, filterBlock, ValueRange{},
                               groupDone, ValueRange{falseValue});
      setCurrent(filterBlock);
      FailureOr<Value> filtered = lowerExpression(filter);
      if (failed(filtered))
        return failure();
      FailureOr<Value> filterTruth =
          truthValue(*filtered, getSemanticLocation(filter));
      if (failed(filterTruth))
        return failure();
      cf::BranchOp::create(builder, getSemanticLocation(filter), groupDone,
                           ValueRange{*filterTruth});
    } else {
      cf::BranchOp::create(builder, getSemanticLocation(patterns[item]),
                           groupDone, ValueRange{*patternMatched});
    }
    setCurrent(groupDone);
    Value groupMatched = groupDone->getArgument(0);
    Value one = arith::ConstantOp::create(builder, location, i32,
                                          builder.getI32IntegerAttr(1));
    Value incremented =
        arith::AddIOp::create(builder, location, matchCount, one);
    matchCount = arith::SelectOp::create(builder, location, groupMatched,
                                         incremented, matchCount);
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noSelection = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, selected, zero);
    Value selectThis =
        arith::AndIOp::create(builder, location, groupMatched, noSelection);
    Value itemIndex = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(item));
    selected = arith::SelectOp::create(builder, location, selectThis, itemIndex,
                                       selected);
    if (item + 1 != itemCount) {
      Block *nextGroup = addBlock();
      nextGroup->addArgument(i32, location);
      nextGroup->addArgument(i32, location);
      cf::BranchOp::create(builder, location, nextGroup,
                           ValueRange{matchCount, selected});
      setCurrent(nextGroup);
      matchCount = nextGroup->getArgument(0);
      selected = nextGroup->getArgument(1);
    }
  }
  Value one = arith::ConstantOp::create(builder, location, i32,
                                        builder.getI32IntegerAttr(1));
  Value overlap = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, matchCount, one);
  Block *overlapWarning = addBlock();
  Block *checkNoMatch = addBlock();
  Block *dispatch = addBlock();
  cf::CondBranchOp::create(builder, location, overlap, overlapWarning,
                           ValueRange{}, checkNoMatch, ValueRange{});
  setCurrent(overlapWarning);
  emitQualifierWarning(location, op.getCheckKind(), "case", "multiple matches");
  emitBranch(dispatch);
  setCurrent(checkNoMatch);
  if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique &&
      !op.getHasDefault()) {
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noMatch = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, matchCount, zero);
    Block *noMatchWarning = addBlock();
    cf::CondBranchOp::create(builder, location, noMatch, noMatchWarning,
                             ValueRange{}, dispatch, ValueRange{});
    setCurrent(noMatchWarning);
    emitQualifierWarning(location, op.getCheckKind(), "case", "no match");
    emitBranch(dispatch);
  } else {
    emitBranch(dispatch);
  }
  setCurrent(dispatch);
  for (size_t item = 0; item < itemCount; ++item) {
    Value itemIndex = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(item));
    Value isSelected = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, selected, itemIndex);
    Block *itemBlock = addBlock();
    Block *nextDispatch = addBlock();
    cf::CondBranchOp::create(builder, location, isSelected, itemBlock,
                             ValueRange{}, nextDispatch, ValueRange{});
    setCurrent(itemBlock);
    for (auto &capture : itemCaptures[item])
      values[capture.getKey()] = capture.getValue();
    if (failed(lowerStatement(statements[item])))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(nextDispatch);
  }
  if (op.getHasDefault() && failed(lowerStatement(statements.back())))
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
    cf::CondBranchOp::create(builder, location, first, initialize, ValueRange{},
                             continuation, ValueRange{});
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
    DictionaryAttr metadata =
        captureMetadata(builder, sim::CaptureKind::Formal);
    if (!isStaticallyAllocatedOverrideTarget(capture)) {
      SmallVector<NamedAttribute> entries(metadata.begin(), metadata.end());
      entries.push_back(builder.getNamedAttr(
          "obelisk_sim.automatic_reference_capture", builder.getUnitAttr()));
      metadata = builder.getDictionaryAttr(entries);
    }
    argumentAttrs.push_back(metadata);
    bindings.push_back(sim::ArgumentBindingAttr::get(
        context, builder.getStringAttr(path), argument,
        sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{}));
  };
  ArrayAttr parentBindings =
      function->getAttrOfType<ArrayAttr>(bindingsAttrName);
  llvm::StringSet<> referencedPaths;
  branch->walk([&](Operation *nested) {
    StringRef path;
    if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested))
      path = named.getReferencedPath();
    else if (auto hierarchical =
                 dyn_cast<semantic::SVHierarchicalValueExpressionOp>(nested))
      path = hierarchical.getReferencedPath();
    if (!path.empty())
      referencedPaths.insert(path);
    if (auto callCaptures =
            nested->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
      for (Attribute capture : callCaptures)
        referencedPaths.insert(cast<StringAttr>(capture).getValue());
  });
  if (parentBindings)
    for (Attribute attribute : parentBindings) {
      StringRef path = sim::getUnitBindingPath(attribute);
      if (path.empty())
        continue;
      // Only pass bindings actually referenced by the branch or a direct
      // callee. This also excludes pattern variables from unrelated matches.
      if (!referencedPaths.contains(path))
        continue;
      // Elaborated constants are immutable compile-time facts, not runtime
      // ABI captures. Preserve their typed binding on the outlined child.
      if (isa<sim::ConstantBindingAttr>(attribute)) {
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
  sim::SimControlDisableOp::create(builder, location,
                                   builder.getI64IntegerAttr(targetID), Value{},
                                   builder.getBoolAttr(true));
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
  if (invalidBindings)
    return failure();
  setCurrent(&function.getBody().front());
  sim::EntryKind entryKind = function.getEntryKind();
  if (entryKind == sim::EntryKind::Observer) {
    if (roots.size() != 1) {
      function.emitError("observer entry requires one expression root");
      return failure();
    }
    FailureOr<Value> result = lowerExpression(roots.front());
    if (failed(result))
      return failure();
    auto resultKind =
        function->getAttrOfType<IntegerAttr>("obelisk_sim.observer_result");
    if (!resultKind) {
      function.emitError("observer entry has no result-kind metadata");
      return failure();
    }
    uint64_t kind = resultKind.getValue().getZExtValue();
    if (kind == 3) {
      if (!isa<sim::EventType>((*result).getType())) {
        function.emitError("named-event observer did not produce an event");
        return failure();
      }
      result = sim::SimEventTriggeredOp::create(builder, function.getLoc(),
                                                builder.getI1Type(), *result)
                   .getResult();
    } else if (kind == 2) {
      result = truthValue(*result, function.getLoc());
      if (failed(result))
        return failure();
    } else {
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
