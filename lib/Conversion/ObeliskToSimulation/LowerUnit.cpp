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
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
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

/// Spelling of an elaborated integer constant.
///
/// References to parameters and enum values acquire the prefixed attribute
/// while their unit is frozen, after the referenced symbol is still available.
static std::optional<StringRef> getConstantSpelling(Operation *op) {
  if (auto literal = dyn_cast<semantic::SVIntegerLiteralOp>(op))
    return literal.getConstantValue();
  if (auto literal = dyn_cast<semantic::SVUnbasedUnsizedIntegerLiteralOp>(op))
    return literal.getConstantValue();
  if (auto constant =
          op->getAttrOfType<StringAttr>("obelisk_sim.constant_value"))
    return constant.getValue();
  return std::nullopt;
}

static bool isIntegerConstant(Operation *op) {
  return getConstantSpelling(op).has_value();
}

static bool isWeakReferenceCall(semantic::SVCallExpressionOp op) {
  auto path = op->getAttrOfType<StringAttr>("referenced_path");
  return path && path.getValue().starts_with("std::weak_reference#(");
}

struct ContainerElementDescriptor {
  uint64_t typeID;
  uint32_t kind;
  uint32_t flags;
  uint64_t valueSize;
  uint64_t alignment;
  uint64_t bitWidth;
  SmallVector<int64_t, 2> traceOffsets;
  SmallVector<int32_t, 2> traceKinds;
};

static uint64_t stableTypeID(Type type) {
  std::string spelling;
  llvm::raw_string_ostream stream(spelling);
  type.print(stream);
  stream.flush();
  uint64_t hash = UINT64_C(14695981039346656037);
  for (unsigned char byte : spelling) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash ? hash : 1;
}

static FailureOr<ContainerElementDescriptor>
describeContainerElement(Type type, Location location) {
  ContainerElementDescriptor result{stableTypeID(type), 0, 0, 0, 1, 0, {}, {}};
  if (auto integer = dyn_cast<IntegerType>(type)) {
    result.kind = 1;
    result.valueSize = (integer.getWidth() + 7) / 8;
    result.bitWidth = integer.getWidth();
    return result;
  }
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    result.kind = 2;
    result.flags = 1;
    result.valueSize = (logic.getWidth() + 7) / 8;
    result.bitWidth = logic.getWidth();
    return result;
  }
  if (auto real = dyn_cast<FloatType>(type)) {
    result.kind = 3;
    result.valueSize = real.getWidth() / 8;
    result.bitWidth = real.getWidth();
    return result;
  }
  if (isa<sim::ClassHandleType>(type)) {
    result.kind = 4;
    result.valueSize = sizeof(void *);
    return result;
  }
  if (isa<sim::StringType>(type)) {
    result.kind = 5;
    result.valueSize = sizeof(void *);
    return result;
  }
  if (isa<sim::EventType>(type)) {
    result.kind = 8;
    result.valueSize = sizeof(uint64_t);
    return result;
  }
  if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(type)) {
    result.kind = 6;
    result.valueSize = sizeof(void *);
    return result;
  }
  if (Type scalar = sim::getPackedScalarType(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || *width == 0)
      return failure();
    bool fourState = isa<sim::LogicType>(scalar);
    result.kind = fourState ? 2 : 1;
    result.flags = fourState ? 1 : 0;
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
    std::function<LogicalResult(Type, uint64_t)> collectTrace =
        [&](Type nested, uint64_t baseBitOffset) -> LogicalResult {
      if (sim::isManagedHandleType(nested)) {
        if ((baseBitOffset & 7) != 0 || baseBitOffset / 8 > uint64_t{INT64_MAX})
          return failure();
        int32_t kind = 1;
        if (isa<sim::StringType>(nested))
          kind = 2;
        else if (isa<sim::DynamicArrayType, sim::QueueType,
                     sim::AssocArrayType>(nested))
          kind = 3;
        result.traceOffsets.push_back(static_cast<int64_t>(baseBitOffset / 8));
        result.traceKinds.push_back(kind);
        return success();
      }
      if (!sim::isAggregateType(nested))
        return success();
      if (isa<sim::PackedUnionType, sim::UnpackedUnionType>(nested)) {
        for (unsigned index = 0; index < sim::getAggregateNumElements(nested);
             ++index) {
          SmallVector<uint64_t, 2> offsets;
          if (!sim::getManagedHandleOffsets(
                  sim::getAggregateElementType(nested, index), offsets))
            return failure();
          if (!offsets.empty())
            return failure();
        }
        return success();
      }
      for (unsigned index = 0; index < sim::getAggregateNumElements(nested);
           ++index) {
        auto child = sim::getAggregateProvenanceSubelement(nested, index);
        if (!child || child->first > UINT64_MAX - baseBitOffset ||
            failed(collectTrace(sim::getAggregateElementType(nested, index),
                                baseBitOffset + child->first)))
          return failure();
      }
      return success();
    };
    if (failed(collectTrace(type, 0))) {
      emitError(location)
          << "dynamic-array aggregate element has no canonical trace layout: "
          << type;
      return failure();
    }
    bool fourState = false;
    type.walk([&](sim::LogicType) { fourState = true; });
    result.kind = 7;
    result.flags = fourState ? 1 : 0;
    result.valueSize = (*width + 7) / 8;
    result.bitWidth = result.valueSize * 8;
    return result;
  }
  emitError(location)
      << "dynamic-array element type has no canonical container ABI: " << type;
  return failure();
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

/// True when an expression selects a subvalue through a dynamic array or
/// queue. Such lvalues must rebuild the captured container instead of
/// materializing an unstable interior reference.
static bool isSequentialContainerSubvalue(Operation *expression) {
  SmallVector<Operation *> children = getChildren(expression);
  if (isa<semantic::SVElementSelectExpressionOp>(expression) &&
      children.size() == 2) {
    FailureOr<Type> baseType = getNormalizedSemanticType(children.front());
    if (succeeded(baseType) &&
        isa<sim::DynamicArrayType, sim::QueueType>(*baseType))
      return true;
    return isSequentialContainerSubvalue(children.front());
  }
  if (isa<semantic::SVMemberAccessExpressionOp>(expression) &&
      children.size() == 1)
    return isSequentialContainerSubvalue(children.front());
  return false;
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
  struct CapturedLValue {
    enum class Kind {
      Reference,
      ContainerElement,
      AssociativeElement,
      AggregateElement,
      StringCharacter,
      Concatenation,
    };

    Kind kind = Kind::Reference;
    Operation *semanticNode = nullptr;
    Type type;
    Value reference;
    Value container;
    Value index;
    unsigned ordinal = 0;
    std::vector<CapturedLValue> children;
  };

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
  FailureOr<Value> lowerNewArray(Operation *op);
  FailureOr<Value> lowerSelection(Operation *op, bool lvalue);
  FailureOr<Value> lowerAssignment(semantic::SVAssignmentExpressionOp op);
  FailureOr<CapturedLValue> captureLValue(Operation *destination,
                                         Location location);
  FailureOr<Value> loadCapturedLValue(const CapturedLValue &destination,
                                      Location location);
  LogicalResult writeCapturedLValue(CapturedLValue &destination,
                                    Value value, bool sourceSigned,
                                    bool nonblocking, Location location,
                                    Value delay = {});
  bool haveSameCapturedStorage(const CapturedLValue &lhs,
                               const CapturedLValue &rhs) const;
  void propagateCapturedContainers(const CapturedLValue &source,
                                   CapturedLValue &destination);
  void appendCapturedValues(const CapturedLValue &destination,
                            SmallVectorImpl<Value> &values);
  LogicalResult replaceCapturedValues(CapturedLValue &destination,
                                      ValueRange values, unsigned &next);
  LogicalResult writeLValue(Operation *destination, Value value,
                            bool sourceSigned, bool nonblocking,
                            Location location, Value delay = {});
  FailureOr<Value> lowerUnary(semantic::SVUnaryExpressionOp op);
  FailureOr<Value> lowerBinary(semantic::SVBinaryExpressionOp op);
  FailureOr<Value>
  lowerConditionalExpression(semantic::SVConditionalExpressionOp op);
  FailureOr<Value> conditionalPredicate(Value value, Location location);
  FailureOr<Value> conditionalEqual(Value lhs, Value rhs, Type type,
                                    Location location,
                                    bool caseEquality = false);
  FailureOr<Value> logicalEqual(Value lhs, Value rhs, Type type,
                                Location location);
  FailureOr<Value> mergeConditionalValues(Value condition, Value trueValue,
                                          Value falseValue, Type type,
                                          Location location);
  FailureOr<Value> lowerInside(semantic::SVInsideExpressionOp op);
  FailureOr<Value> lowerCall(semantic::SVCallExpressionOp op);
  FailureOr<Value> lowerArrayMethod(semantic::SVCallExpressionOp op,
                                    Value receiverOverride = {},
                                    Value iteratorKeys = {});
  FailureOr<Value>
  lowerAssociativeArrayMethod(semantic::SVCallExpressionOp op);
  FailureOr<Value> lowerNewClass(semantic::SVNewClassExpressionOp op);
  FailureOr<Value> lowerSystemCall(semantic::SVCallExpressionOp op);
  LogicalResult initializeObjectRandomStream(Value object, Location location);
  LogicalResult lowerPortConnection(semantic::SVPortConnectionOp op);

  LogicalResult lowerStatement(Operation *op);
  LogicalResult lowerSequence(ArrayRef<Operation *> operations);
  LogicalResult
  lowerImmediateAssertion(semantic::SVImmediateAssertionStatementOp op);
  void emitDefaultAssertionFailure(Location location);
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
  Value cloneSequentialValue(Value value, Location location);
  FailureOr<Value> createAssocArray(sim::AssocArrayType type,
                                    Location location);
  FailureOr<Value> ensureAssocArray(Value value, Location location);
  FailureOr<std::pair<Value, Value>>
  traverseAssoc(Value array, Value key, int32_t direction, bool endpoint,
                Location location);
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
  llvm::StringMap<Value> iteratorIndices;
  Value thisObject;
  llvm::SetVector<Value> sensitivity;
  llvm::SetVector<Value> *observedDependencies = nullptr;
  Value expressionPlaceholder;
  Value lvalueReferencePlaceholder;
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

FailureOr<Value>
UnitLowering::createAssocArray(sim::AssocArrayType type, Location location) {
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(type.getElementType(), location);
  if (failed(descriptor))
    return failure();
  bool stringKey = isa<sim::StringType>(type.getKeyType());
  std::optional<unsigned> width =
      stringKey ? std::optional<unsigned>(0)
                : sim::getPackedWidth(type.getKeyType());
  if (!width || *width > 64 || (!stringKey && *width == 0)) {
    emitError(location)
        << "associative array key must be string or integral up to 64 bits";
    return failure();
  }
  uint32_t keyKind =
      stringKey ? 3 : (type.getSignedKey() ? 2 : 1);
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
  Value isNull = sim::SimManagedIsNullOp::create(
      builder, location, builder.getI1Type(), value);
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
  Value isNull = sim::SimManagedIsNullOp::create(
      builder, location, builder.getI1Type(), array);
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
  cf::BranchOp::create(builder, location, resume,
                       ValueRange{key, falseValue});
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
    Value bits = sim::SimStringToPackedOp::create(builder, location, bitsType,
                                                  value);
    Value scalar = bits;
    if (isa<sim::LogicType>(scalarType))
      scalar = sim::SimLogicFromBitsOp::create(builder, location, scalarType,
                                               bits);
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
    uint32_t containerKind = isa<sim::DynamicArrayType>(targetType) ? 1 : 2;
    uint64_t bound = 0;
    if (auto queue = dyn_cast<sim::QueueType>(targetType))
      bound = queue.getBound() ? queue.getBound() : UINT64_MAX;
    Value allocationSize = containerKind == 1
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
      return sim::SimRealFromIntegerOp::create(
                 builder, location, targetType, value, sourceSigned)
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
    Value wide = arith::ExtFOp::create(builder, location, builder.getF64Type(),
                                      value);
    return convert(wide, targetType, sourceSigned, location, targetSigned);
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
  if (isa<sim::StringType>(value.getType())) {
    Value length = sim::SimStringLengthOp::create(
        builder, location, builder.getI64Type(), value);
    Value zero = arith::ConstantOp::create(builder, location,
                                           builder.getI64Type(),
                                           builder.getI64IntegerAttr(0));
    return arith::CmpIOp::create(builder, location,
                                 arith::CmpIPredicate::ne, length, zero)
        .getResult();
  }
  if (isa<FloatType>(value.getType())) {
    Value zero = arith::ConstantOp::create(builder, location, value.getType(),
                                           builder.getFloatAttr(
                                               value.getType(), 0.0));
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

static FailureOr<Value> lowerStringLiteralValue(OpBuilder &builder,
                                                Operation *op, Type type,
                                                Location location) {
  auto spelling = op->getAttrOfType<StringAttr>("constant_value");
  if (!spelling)
    return emitError(location) << "string literal has no byte payload",
           failure();
  if (isa<sim::StringType>(type))
    return sim::SimStringLiteralOp::create(builder, location, type, spelling)
        .getResult();

  Type scalar = sim::getPackedScalarType(type);
  std::optional<unsigned> width =
      scalar ? sim::getPackedWidth(scalar) : std::nullopt;
  if (!scalar || !width)
    return emitError(location)
               << "string literal has a non-packed, non-string result type",
           failure();
  APInt bits(*width, 0);
  for (uint8_t byte : spelling.getValue().bytes()) {
    bits <<= std::min<unsigned>(8, *width);
    bits |= APInt(*width, byte);
  }
  Value value;
  auto planeType = IntegerType::get(type.getContext(), *width);
  if (isa<IntegerType>(scalar))
    value = arith::ConstantOp::create(builder, location, scalar,
                                      builder.getIntegerAttr(scalar, bits));
  else
    value = sim::SimLogicConstantOp::create(
        builder, location, scalar, builder.getIntegerAttr(planeType, bits),
        builder.getIntegerAttr(planeType, APInt(*width, 0)));
  if (scalar != type)
    value =
        sim::SimPackedUnflattenOp::create(builder, location, type, value);
  return value;
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
  if (isa<sim::StringType>(*resultType)) {
    SmallVector<Value> inputs;
    for (Operation *child : children) {
      FailureOr<Value> input = lowerExpression(child);
      if (failed(input))
        return failure();
      FailureOr<Value> converted =
          convert(*input, *resultType, isSignedNode(child), location);
      if (failed(converted))
        return failure();
      inputs.push_back(*converted);
    }
    return sim::SimStringConcatOp::create(builder, location, *resultType,
                                          inputs)
        .getResult();
  }
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
  if (children.size() != 2) {
    unsupported(op) << " (replication arity)";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  if (isa<sim::StringType>(*resultType)) {
    FailureOr<Value> count = lowerExpression(children.front());
    FailureOr<Value> input = lowerExpression(children[1]);
    if (failed(count) || failed(input))
      return failure();
    FailureOr<Value> count64 =
        convert(*count, builder.getI64Type(), isSignedNode(children.front()),
                location);
    FailureOr<Value> string =
        convert(*input, *resultType, isSignedNode(children[1]), location);
    if (failed(count64) || failed(string))
      return failure();
    return sim::SimStringRepeatOp::create(builder, location, *resultType,
                                          *string, *count64)
        .getResult();
  }
  if (!isIntegerConstant(children.front())) {
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
  if (getConstantSpelling(op)) {
    if (lvalue) {
      emitError(location) << "constant member access is not an lvalue";
      return failure();
    }
    return lowerLiteral(op);
  }
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
  if (failed(resultType))
    return failure();
  if (auto array = dyn_cast<sim::AssocArrayType>(*resultType)) {
    auto structured =
        dyn_cast<semantic::SVStructuredAssignmentPatternExpressionOp>(op);
    if (!structured) {
      emitError(location)
          << "associative assignment patterns require keyed setters";
      return failure();
    }
    if (structured.getMemberSetterCount() != 0 ||
        structured.getTypeSetterCount() != 0) {
      emitError(location)
          << "associative assignment pattern contains a non-index setter";
      return failure();
    }
    SmallVector<Operation *> children = getChildren(op);
    uint64_t indexCount = structured.getIndexSetterCount();
    uint64_t expected =
        indexCount * 2 + (structured.getHasDefaultSetter() ? 1 : 0);
    if (children.size() != expected) {
      emitError(location)
          << "malformed associative assignment-pattern setter inventory";
      return failure();
    }
    FailureOr<Value> created = createAssocArray(array, location);
    if (failed(created))
      return failure();
    Value result = *created;
    for (uint64_t index = 0; index < indexCount; ++index) {
      Operation *keyNode = children[index * 2];
      Operation *valueNode = children[index * 2 + 1];
      FailureOr<Value> key = lowerExpression(keyNode);
      FailureOr<Value> value = lowerExpression(valueNode);
      if (failed(key) || failed(value))
        return failure();
      FailureOr<Value> convertedKey =
          convert(*key, array.getKeyType(), isSignedNode(keyNode),
                  getSemanticLocation(keyNode), array.getSignedKey());
      FailureOr<Value> convertedValue =
          convert(*value, array.getElementType(), isSignedNode(valueNode),
                  getSemanticLocation(valueNode));
      if (failed(convertedKey) || failed(convertedValue))
        return failure();
      sim::SimAssocWriteOp::create(builder, getSemanticLocation(valueNode),
                                   result, *convertedKey, *convertedValue);
    }
    if (structured.getHasDefaultSetter()) {
      Operation *defaultNode = children.back();
      FailureOr<Value> value = lowerExpression(defaultNode);
      if (failed(value))
        return failure();
      FailureOr<Value> converted =
          convert(*value, array.getElementType(), isSignedNode(defaultNode),
                  getSemanticLocation(defaultNode));
      if (failed(converted))
        return failure();
      sim::SimAssocSetDefaultOp::create(
          builder, getSemanticLocation(defaultNode), result, *converted);
    }
    return result;
  }
  if (auto array = dyn_cast<sim::DynamicArrayType>(*resultType)) {
    SmallVector<Operation *> children = getChildren(op);
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(array.getElementType(), location);
    if (failed(descriptor))
      return failure();
    Value size =
        arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                  builder.getI64IntegerAttr(children.size()));
    Value result = sim::SimContainerCreateOp::create(
        builder, location, *resultType, size, descriptor->typeID,
        descriptor->kind, descriptor->flags, descriptor->valueSize,
        descriptor->alignment, descriptor->bitWidth,
        builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
        builder.getDenseI32ArrayAttr(descriptor->traceKinds), 1, 0);
    for (auto [index, child] : llvm::enumerate(children)) {
      FailureOr<Value> value = lowerExpression(child);
      if (failed(value))
        return failure();
      FailureOr<Value> converted =
          convert(*value, array.getElementType(), isSignedNode(child),
                  getSemanticLocation(child));
      if (failed(converted))
        return failure();
      Value ordinal =
          arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                    builder.getI64IntegerAttr(index));
      sim::SimContainerWriteOp::create(builder, location, result, ordinal,
                                       *converted);
    }
    return result;
  }
  if (!sim::isAggregateType(*resultType)) {
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

FailureOr<Value> UnitLowering::lowerNewArray(Operation *op) {
  Location location = getSemanticLocation(op);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  auto array = succeeded(resultType)
                   ? dyn_cast<sim::DynamicArrayType>(*resultType)
                   : sim::DynamicArrayType{};
  SmallVector<Operation *> children = getChildren(op);
  if (!array || children.empty() || children.size() > 2) {
    emitError(location)
        << "new dynamic array requires a size and at most one initializer";
    return failure();
  }
  FailureOr<Value> sizeValue = lowerExpression(children.front());
  if (failed(sizeValue))
    return failure();
  FailureOr<Value> scalarSize = toPackedScalar(*sizeValue, location);
  if (failed(scalarSize))
    return failure();
  FailureOr<Value> size = convert(*scalarSize, builder.getI64Type(),
                                  isSignedNode(children.front()), location);
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(array.getElementType(), location);
  if (failed(size) || failed(descriptor))
    return failure();
  Value result = sim::SimContainerCreateOp::create(
      builder, location, *resultType, *size, descriptor->typeID,
      descriptor->kind, descriptor->flags, descriptor->valueSize,
      descriptor->alignment, descriptor->bitWidth,
      builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
      builder.getDenseI32ArrayAttr(descriptor->traceKinds), 1, 0);
  if (children.size() == 1)
    return result;

  FailureOr<Value> source = lowerExpression(children[1]);
  if (failed(source))
    return failure();
  FailureOr<Value> convertedSource =
      convert(*source, *resultType, isSignedNode(children[1]), location);
  if (failed(convertedSource))
    return failure();
  Value sourceSize = sim::SimContainerSizeOp::create(
      builder, location, builder.getI64Type(), *convertedSource);
  Value sourceShorter = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, sourceSize, *size);
  Value copySize = arith::SelectOp::create(builder, location, sourceShorter,
                                           sourceSize, *size);
  Block *header = addBlock();
  header->addArgument(builder.getI64Type(), location);
  Block *body = addBlock();
  Block *exit = addBlock();
  Value zero = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
  cf::BranchOp::create(builder, location, header, ValueRange{zero});
  setCurrent(header);
  Value index = header->getArgument(0);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, index, copySize);
  cf::CondBranchOp::create(builder, location, more, body, ValueRange{}, exit,
                           ValueRange{});
  setCurrent(body);
  Value element = sim::SimContainerReadOp::create(
      builder, location, array.getElementType(), *convertedSource, index);
  sim::SimContainerWriteOp::create(builder, location, result, index, element);
  Value one = arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                        builder.getI64IntegerAttr(1));
  Value next = arith::AddIOp::create(builder, location, index, one);
  cf::BranchOp::create(builder, location, header, ValueRange{next});
  setCurrent(exit);
  return result;
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

  if (element) {
    if (auto array = dyn_cast<sim::AssocArrayType>(sourceValueType)) {
      Value container = *input;
      bool isReference =
          isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType>(
              container.getType());
      if (isReference) {
        FailureOr<Value> loaded = loadReference(container, location);
        if (failed(loaded))
          return failure();
        container = *loaded;
      }
      FailureOr<Value> key = lowerExpression(children[1]);
      FailureOr<Value> convertedKey =
          succeeded(key)
              ? convert(*key, array.getKeyType(), isSignedNode(children[1]),
                        location, array.getSignedKey())
              : FailureOr<Value>(failure());
      FailureOr<Value> materialized =
          succeeded(convertedKey)
              ? ensureAssocArray(container, location)
              : FailureOr<Value>(failure());
      if (failed(convertedKey) || failed(materialized))
        return failure();
      container = *materialized;
      if (lvalue) {
        if (!isReference)
          return emitError(location)
                     << "associative element lvalue has no owning storage",
                 failure();
        if (failed(storeReference(*input, container, location)))
          return failure();
        FailureOr<Value> published = loadReference(*input, location);
        FailureOr<Value> owner =
            toArgumentReference(*input, sourceValueType, location);
        if (failed(published) || failed(owner))
          return failure();
        Type pathType =
            sim::ReferencePathType::get(function.getContext(), *resultType);
        return sim::SimReferencePathAssocOp::create(
                   builder, location, pathType,
                   function.getBody().front().getArgument(0), *published,
                   *convertedKey, *owner)
            .getResult();
      }
      return sim::SimAssocReadOp::create(builder, location, *resultType,
                                         container, *convertedKey)
          .getResult();
    }
  }

  if (element && isa<sim::DynamicArrayType, sim::QueueType>(sourceValueType)) {
    Value container = *input;
    if (isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType>(
            container.getType())) {
      FailureOr<Value> loaded = loadReference(container, location);
      if (failed(loaded))
        return failure();
      container = *loaded;
    }
    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> scalarIndex = toPackedScalar(*index, location);
    if (failed(scalarIndex))
      return failure();
    FailureOr<Value> index64 = convert(*scalarIndex, builder.getI64Type(),
                                       isSignedNode(children[1]), location);
    if (failed(index64))
      return failure();
    if (lvalue) {
      Type pathType =
          sim::ReferencePathType::get(function.getContext(), *resultType);
      FailureOr<Value> ownerReference =
          toArgumentReference(*input, sourceValueType, location);
      if (failed(ownerReference))
        return failure();
      return sim::SimReferencePathIndexOp::create(
                 builder, location, pathType,
                 function.getBody().front().getArgument(0), container, *index64,
                 *ownerReference)
          .getResult();
    }
    return sim::SimContainerReadOp::create(builder, location, *resultType,
                                           container, *index64)
        .getResult();
  }

  if (isa<sim::StringType>(sourceValueType)) {
    if (lvalue) {
      unsupported(op)
          << " (escaping string-character lvalues are not yet materialized)";
      return failure();
    }
    if (!element) {
      unsupported(op) << " (string range selection)";
      return failure();
    }
    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> index64 =
        convert(*index, builder.getI64Type(), isSignedNode(children[1]),
                location);
    if (failed(index64))
      return failure();
    Value byte = sim::SimStringGetcOp::create(builder, location,
                                              builder.getI8Type(), *input,
                                              *index64);
    return convert(byte, *resultType, false, location);
  }

  // Fixed packed and unpacked arrays remain first-class aggregates. Their
  // dynamic operation consumes a source index, while static views use a
  // declaration-order ordinal.
  if (element &&
      isa<sim::PackedArrayType, sim::UnpackedArrayType>(sourceValueType)) {
    std::optional<unsigned> ordinal;
    if (isIntegerConstant(children[1])) {
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

  bool literalIndex = isIntegerConstant(children[1]);
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
      if (!isIntegerConstant(children[2])) {
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

FailureOr<UnitLowering::CapturedLValue>
UnitLowering::captureLValue(Operation *destination, Location location) {
  CapturedLValue captured;
  captured.semanticNode = destination;
  FailureOr<Type> destinationType = getNormalizedSemanticType(destination);
  if (failed(destinationType))
    return failure();
  captured.type = *destinationType;

  if (isa<semantic::SVConcatenationExpressionOp>(destination)) {
    SmallVector<Operation *> children = getChildren(destination);
    if (children.empty())
      return failure();
    captured.kind = CapturedLValue::Kind::Concatenation;
    for (Operation *child : children) {
      FailureOr<CapturedLValue> element = captureLValue(child, location);
      if (failed(element))
        return failure();
      captured.children.push_back(std::move(*element));
    }
    return captured;
  }

  if (isa<semantic::SVElementSelectExpressionOp>(destination)) {
    SmallVector<Operation *> selection = getChildren(destination);
    if (selection.size() == 2) {
      FailureOr<Type> baseType = getNormalizedSemanticType(selection.front());
      if (failed(baseType))
        return failure();
      if (isa<sim::DynamicArrayType, sim::QueueType>(*baseType)) {
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        FailureOr<Value> container =
            succeeded(base) ? loadCapturedLValue(*base, location)
                            : FailureOr<Value>(failure());
        FailureOr<Value> index = lowerExpression(selection[1]);
        if (failed(base) || failed(container) || failed(index))
          return failure();
        FailureOr<Value> scalarIndex = toPackedScalar(*index, location);
        if (failed(scalarIndex))
          return failure();
        FailureOr<Value> index64 =
            convert(*scalarIndex, builder.getI64Type(),
                    isSignedNode(selection[1]), location);
        if (failed(index64))
          return failure();
        captured.kind = CapturedLValue::Kind::ContainerElement;
        captured.container = *container;
        captured.index = *index64;
        captured.children.push_back(std::move(*base));
        return captured;
      }
      if (auto array = dyn_cast<sim::AssocArrayType>(*baseType)) {
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        FailureOr<Value> container =
            succeeded(base) ? loadCapturedLValue(*base, location)
                            : FailureOr<Value>(failure());
        FailureOr<Value> key = lowerExpression(selection[1]);
        if (failed(base) || failed(container) || failed(key))
          return failure();
        FailureOr<Value> convertedKey =
            convert(*key, array.getKeyType(), isSignedNode(selection[1]),
                    location, array.getSignedKey());
        if (failed(convertedKey))
          return failure();
        captured.kind = CapturedLValue::Kind::AssociativeElement;
        captured.container = *container;
        captured.index = *convertedKey;
        captured.children.push_back(std::move(*base));
        return captured;
      }
      if (isa<sim::StringType>(*baseType)) {
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        FailureOr<Value> index = lowerExpression(selection[1]);
        if (failed(base) || failed(index))
          return failure();
        FailureOr<Value> index64 =
            convert(*index, builder.getI64Type(), isSignedNode(selection[1]),
                    location);
        if (failed(index64))
          return failure();
        captured.kind = CapturedLValue::Kind::StringCharacter;
        captured.index = *index64;
        captured.children.push_back(std::move(*base));
        return captured;
      }

      if (isa<sim::PackedArrayType, sim::UnpackedArrayType>(*baseType) &&
          isIntegerConstant(selection[1]) &&
          isSequentialContainerSubvalue(selection.front())) {
        FailureOr<Type> indexType = getNormalizedSemanticType(selection[1]);
        std::optional<unsigned> indexWidth =
            succeeded(indexType) ? sim::getPackedWidth(*indexType)
                                 : std::nullopt;
        if (failed(indexType) || !indexWidth)
          return failure();
        FailureOr<ParsedConstant> parsed = parseSVInteger(
            *getConstantSpelling(selection[1]), *indexWidth, location);
        if (failed(parsed) || !parsed->unknown.isZero())
          return failure();
        APInt index = isSignedNode(selection[1])
                          ? parsed->value.sextOrTrunc(65)
                          : parsed->value.zextOrTrunc(65);
        if (!index.isSignedIntN(64))
          return failure();
        std::optional<unsigned> ordinal =
            sim::getArrayElementOrdinal(*baseType, index.getSExtValue());
        if (!ordinal)
          return failure();
        FailureOr<CapturedLValue> base =
            captureLValue(selection.front(), location);
        if (failed(base))
          return failure();
        captured.kind = CapturedLValue::Kind::AggregateElement;
        captured.ordinal = *ordinal;
        captured.children.push_back(std::move(*base));
        return captured;
      }
    }
  }

  if (isa<semantic::SVMemberAccessExpressionOp>(destination) &&
      !destination->hasAttr("obelisk_sim.class_field") &&
      (isSequentialContainerSubvalue(destination) ||
       sim::isManagedHandleType(*destinationType))) {
    SmallVector<Operation *> members = getChildren(destination);
    auto ordinalAttr = destination->getAttrOfType<IntegerAttr>("field_ordinal");
    if (members.size() != 1 || !ordinalAttr ||
        ordinalAttr.getValue().isNegative() ||
        ordinalAttr.getValue().getActiveBits() > 32)
      return failure();
    FailureOr<CapturedLValue> base =
        captureLValue(members.front(), location);
    if (failed(base))
      return failure();
    Type baseType = base->type;
    unsigned ordinal = ordinalAttr.getValue().getZExtValue();
    if (isa<sim::PackedUnionType, sim::UnpackedUnionType>(baseType) ||
        sim::getAggregateElementType(baseType, ordinal) != *destinationType)
      return failure();
    captured.kind = CapturedLValue::Kind::AggregateElement;
    captured.ordinal = ordinal;
    captured.children.push_back(std::move(*base));
    return captured;
  }

  FailureOr<Value> reference = lowerExpression(destination, true);
  if (failed(reference))
    return failure();
  Type elementType = getReferenceElementType(*reference);
  if (!elementType) {
    if (auto driver = dyn_cast<sim::DriverType>((*reference).getType()))
      elementType = driver.getElementType();
  }
  if (!elementType) {
    emitError(location)
        << "assignment destination is not a reference or driver";
    return failure();
  }
  captured.reference = *reference;
  captured.type = elementType;
  return captured;
}

FailureOr<Value>
UnitLowering::loadCapturedLValue(const CapturedLValue &destination,
                                 Location location) {
  switch (destination.kind) {
  case CapturedLValue::Kind::Reference:
    return loadReference(destination.reference, location);
  case CapturedLValue::Kind::ContainerElement:
    return sim::SimContainerReadOp::create(
               builder, location, destination.type, destination.container,
               destination.index)
        .getResult();
  case CapturedLValue::Kind::AssociativeElement: {
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), destination.container);
    Block *missing = addBlock();
    Block *present = addBlock();
    Block *resume = addBlock();
    resume->addArgument(destination.type, location);
    cf::CondBranchOp::create(builder, location, isNull, missing, ValueRange{},
                             present, ValueRange{});
    setCurrent(missing);
    Value defaultValue = createDefaultValue(builder, location, destination.type);
    if (!defaultValue)
      return failure();
    cf::BranchOp::create(builder, location, resume,
                         ValueRange{defaultValue});
    setCurrent(present);
    Value value = sim::SimAssocReadOp::create(
        builder, location, destination.type, destination.container,
        destination.index);
    cf::BranchOp::create(builder, location, resume, ValueRange{value});
    setCurrent(resume);
    return resume->getArgument(0);
  }
  case CapturedLValue::Kind::AggregateElement: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> aggregate =
        loadCapturedLValue(destination.children.front(), location);
    if (failed(aggregate) ||
        sim::getAggregateElementType((*aggregate).getType(),
                                     destination.ordinal) != destination.type)
      return failure();
    return sim::SimAggregateExtractOp::create(
               builder, location, destination.type, *aggregate,
               destination.ordinal)
        .getResult();
  }
  case CapturedLValue::Kind::StringCharacter: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> string =
        loadCapturedLValue(destination.children.front(), location);
    if (failed(string))
      return failure();
    Value character = sim::SimStringGetcOp::create(
        builder, location, builder.getI8Type(), *string, destination.index);
    return convert(character, destination.type, false, location,
                   isSignedNode(destination.semanticNode));
  }
  case CapturedLValue::Kind::Concatenation: {
    Type scalarResultType = sim::getPackedScalarType(destination.type);
    if (!scalarResultType || destination.children.empty())
      return failure();
    SmallVector<Value> inputs;
    for (const CapturedLValue &child : destination.children) {
      FailureOr<Value> input = loadCapturedLValue(child, location);
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
      Value result = sim::SimLogicConcatOp::create(
          builder, location, resultLogic, logicInputs);
      return convert(result, destination.type, false, location,
                     isSignedNode(destination.semanticNode));
    }
    auto resultInteger = dyn_cast<IntegerType>(scalarResultType);
    if (!resultInteger)
      return failure();
    Value combined = arith::ConstantOp::create(
        builder, location, resultInteger,
        builder.getIntegerAttr(resultInteger, 0));
    unsigned trailingWidth = resultInteger.getWidth();
    for (Value input : inputs) {
      auto inputInteger = dyn_cast<IntegerType>(input.getType());
      if (!inputInteger || inputInteger.getWidth() > trailingWidth)
        return failure();
      trailingWidth -= inputInteger.getWidth();
      FailureOr<Value> extended =
          convert(input, resultInteger, false, location);
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
    return convert(combined, destination.type, false, location,
                   isSignedNode(destination.semanticNode));
  }
  }
  llvm_unreachable("unknown captured lvalue kind");
}

bool UnitLowering::haveSameCapturedStorage(const CapturedLValue &lhs,
                                           const CapturedLValue &rhs) const {
  if (lhs.kind != rhs.kind)
    return false;
  switch (lhs.kind) {
  case CapturedLValue::Kind::Reference: {
    if (lhs.reference == rhs.reference)
      return true;
    auto lhsField = lhs.reference.getDefiningOp<sim::SimClassFieldRefOp>();
    auto rhsField = rhs.reference.getDefiningOp<sim::SimClassFieldRefOp>();
    return lhsField && rhsField &&
           lhsField.getObject() == rhsField.getObject() &&
           lhsField.getFieldAttr() == rhsField.getFieldAttr();
  }
  case CapturedLValue::Kind::AggregateElement:
    return lhs.ordinal == rhs.ordinal && lhs.children.size() == 1 &&
           rhs.children.size() == 1 &&
           haveSameCapturedStorage(lhs.children.front(),
                                   rhs.children.front());
  case CapturedLValue::Kind::ContainerElement:
  case CapturedLValue::Kind::AssociativeElement: {
    if (lhs.children.size() != 1 || rhs.children.size() != 1 ||
        !haveSameCapturedStorage(lhs.children.front(), rhs.children.front()))
      return false;
    if (lhs.index == rhs.index)
      return true;
    Attribute lhsConstant;
    Attribute rhsConstant;
    return matchPattern(lhs.index, m_Constant(&lhsConstant)) &&
           matchPattern(rhs.index, m_Constant(&rhsConstant)) &&
           lhsConstant == rhsConstant;
  }
  case CapturedLValue::Kind::StringCharacter:
  case CapturedLValue::Kind::Concatenation:
    return false;
  }
  llvm_unreachable("unknown captured lvalue kind");
}

void UnitLowering::propagateCapturedContainers(
    const CapturedLValue &source, CapturedLValue &destination) {
  // Concatenation leaves commit in source order. When two leaves target the
  // same captured container storage, feed the first leaf's rebuilt container
  // into the second so its write cannot restore the encounter-time snapshot
  // and discard the earlier update.
  if ((source.kind == CapturedLValue::Kind::ContainerElement ||
       source.kind == CapturedLValue::Kind::AssociativeElement) &&
      source.kind == destination.kind &&
      source.children.size() == 1 && destination.children.size() == 1 &&
      haveSameCapturedStorage(source.children.front(),
                              destination.children.front()))
    destination.container = source.container;
  for (CapturedLValue &child : destination.children)
    propagateCapturedContainers(source, child);
  for (const CapturedLValue &child : source.children)
    propagateCapturedContainers(child, destination);
}

LogicalResult UnitLowering::writeCapturedLValue(
    CapturedLValue &destination, Value value, bool sourceSigned,
    bool nonblocking, Location location, Value delay) {
  switch (destination.kind) {
  case CapturedLValue::Kind::Reference: {
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    Value published = *converted;
    if (isa<sim::DynamicArrayType, sim::QueueType,
            sim::AssocArrayType>(destination.type))
      published = sim::SimContainerCloneOp::create(
          builder, location, destination.type, published);
    Type referenceType = destination.reference.getType();
    if (isa<sim::ManagedRefType>(referenceType)) {
      if (nonblocking)
        sim::SimManagedNBAEnqueueOp::create(
            builder, location, published, destination.reference, delay);
      else
        sim::SimManagedStoreOp::create(builder, location, published,
                                       destination.reference);
    } else if (isa<sim::RefType>(referenceType)) {
      if (nonblocking)
        sim::SimNBAEnqueueOp::create(builder, location, published,
                                     destination.reference, delay,
                                     sim::NBASiteAttr{});
      else
        sim::SimRefStoreOp::create(builder, location, published,
                                   destination.reference);
    } else if (isa<sim::ArgumentRefType>(referenceType)) {
      if (nonblocking) {
        emitError(location)
            << "nonblocking assignment cannot target a ref formal";
        return failure();
      }
      sim::SimArgumentRefStoreOp::create(builder, location, published,
                                         destination.reference);
    } else if (isa<sim::ReferencePathType>(referenceType)) {
      if (nonblocking)
        sim::SimReferencePathNBAEnqueueOp::create(
            builder, location, published, destination.reference, delay);
      else if (failed(storeReference(destination.reference, published,
                                     location)))
        return failure();
    } else if (isa<sim::DriverType>(referenceType)) {
      if (nonblocking) {
        emitError(location)
            << "nonblocking assignment cannot target a driver";
        return failure();
      }
      sim::SimDriverDriveOp::create(builder, location, destination.reference,
                                    published);
    } else {
      return failure();
    }
    return success();
  }
  case CapturedLValue::Kind::ContainerElement: {
    if (destination.children.size() != 1)
      return failure();
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    CapturedLValue &base = destination.children.front();
    if (nonblocking) {
      if (base.kind != CapturedLValue::Kind::Reference)
        return failure();
      FailureOr<Value> owner = toArgumentReference(
          base.reference, destination.container.getType(), location);
      if (failed(owner))
        return failure();
      Type pathType = sim::ReferencePathType::get(function.getContext(),
                                                  destination.type);
      Value path = sim::SimReferencePathIndexOp::create(
          builder, location, pathType,
          function.getBody().front().getArgument(0), destination.container,
          destination.index, *owner);
      sim::SimReferencePathNBAEnqueueOp::create(builder, location, *converted,
                                                path, delay);
      return success();
    }

    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), destination.container);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    Value nonnegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sge, destination.index, zero);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, destination.index, size);
    Value valid =
        arith::AndIOp::create(builder, location, nonnegative, inRange);
    Block *write = addBlock();
    Block *resume = addBlock();
    resume->addArgument(destination.container.getType(), location);
    cf::CondBranchOp::create(builder, location, valid, write, ValueRange{},
                             resume, ValueRange{destination.container});
    setCurrent(write);
    Value updated = cloneSequentialValue(destination.container, location);
    sim::SimContainerWriteOp::create(builder, location, updated,
                                     destination.index, *converted);
    if (failed(writeCapturedLValue(base, updated, false, false, location)))
      return failure();
    if (current->empty() ||
        !current->back().hasTrait<OpTrait::IsTerminator>())
      cf::BranchOp::create(builder, location, resume, ValueRange{updated});
    setCurrent(resume);
    destination.container = resume->getArgument(0);
    return success();
  }
  case CapturedLValue::Kind::AssociativeElement: {
    if (destination.children.size() != 1)
      return failure();
    if (nonblocking) {
      emitError(location)
          << "nonblocking assignment cannot target an associative element";
      return failure();
    }
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(converted))
      return failure();
    CapturedLValue &base = destination.children.front();
    CapturedLValue createBase = base;
    CapturedLValue existingBase = base;
    Value isNull = sim::SimManagedIsNullOp::create(
        builder, location, builder.getI1Type(), destination.container);
    Block *create = addBlock();
    Block *existing = addBlock();
    Block *resume = addBlock();
    resume->addArgument(destination.container.getType(), location);
    cf::CondBranchOp::create(builder, location, isNull, create, ValueRange{},
                             existing, ValueRange{});
    setCurrent(create);
    auto arrayType =
        cast<sim::AssocArrayType>(destination.container.getType());
    FailureOr<Value> allocated = createAssocArray(arrayType, location);
    if (failed(allocated))
      return failure();
    sim::SimAssocWriteOp::create(builder, location, *allocated,
                                 destination.index, *converted);
    if (failed(writeCapturedLValue(createBase, *allocated, false, false,
                                   location)))
      return failure();
    cf::BranchOp::create(builder, location, resume, ValueRange{*allocated});

    setCurrent(existing);
    Value updated = sim::SimContainerCloneOp::create(
        builder, location, destination.container.getType(),
        destination.container);
    sim::SimAssocWriteOp::create(builder, location, updated, destination.index,
                                 *converted);
    if (failed(writeCapturedLValue(existingBase, updated, false, false,
                                   location)))
      return failure();
    cf::BranchOp::create(builder, location, resume, ValueRange{updated});
    setCurrent(resume);
    destination.container = resume->getArgument(0);
    return success();
  }
  case CapturedLValue::Kind::AggregateElement: {
    if (destination.children.size() != 1)
      return failure();
    CapturedLValue &base = destination.children.front();
    FailureOr<Value> aggregate = loadCapturedLValue(base, location);
    FailureOr<Value> replacement =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
    if (failed(aggregate) || failed(replacement) ||
        sim::getAggregateElementType((*aggregate).getType(),
                                     destination.ordinal) != destination.type)
      return failure();
    Value updated = sim::SimAggregateInsertOp::create(
        builder, location, (*aggregate).getType(), *aggregate, *replacement,
        destination.ordinal);
    return writeCapturedLValue(base, updated, false, nonblocking, location,
                               delay);
  }
  case CapturedLValue::Kind::StringCharacter: {
    if (destination.children.size() != 1) {
      return failure();
    }
    if (nonblocking) {
      emitError(location)
          << "nonblocking string-character assignment requires a captured "
             "element path";
      return failure();
    }
    CapturedLValue &base = destination.children.front();
    FailureOr<Value> string = loadCapturedLValue(base, location);
    FailureOr<Value> character =
        convert(value, builder.getI8Type(), sourceSigned, location);
    if (failed(string) || failed(character))
      return failure();
    Value updated = sim::SimStringPutcOp::create(
        builder, location, sim::StringType::get(function.getContext()),
        *string, destination.index, *character);
    return writeCapturedLValue(base, updated, false, false, location);
  }
  case CapturedLValue::Kind::Concatenation: {
    FailureOr<Value> converted =
        convert(value, destination.type, sourceSigned, location,
                isSignedNode(destination.semanticNode));
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
    for (auto [childIndex, child] : llvm::enumerate(destination.children)) {
      for (CapturedLValue &previous :
           MutableArrayRef(destination.children).take_front(childIndex))
        propagateCapturedContainers(previous, child);
      std::optional<unsigned> childWidth = sim::getPackedWidth(child.type);
      if (!childWidth || *childWidth > trailing) {
        emitError(location) << "concatenation lvalue width is inconsistent";
        return failure();
      }
      trailing -= *childWidth;
      Value part;
      if (isa<sim::LogicType>((*scalar).getType())) {
        auto selected =
            sim::LogicType::get(function.getContext(), *childWidth);
        part = sim::SimLogicExtractOp::create(
            builder, location, selected, *scalar,
            builder.getI64IntegerAttr(trailing));
      } else {
        auto integer = dyn_cast<IntegerType>((*scalar).getType());
        if (!integer)
          return failure();
        Value amount = arith::ConstantOp::create(
            builder, location, integer,
            builder.getIntegerAttr(integer, trailing));
        Value shifted =
            arith::ShRUIOp::create(builder, location, *scalar, amount);
        auto selected =
            IntegerType::get(function.getContext(), *childWidth);
        part = selected == integer
                   ? shifted
                   : Value(arith::TruncIOp::create(builder, location, selected,
                                                  shifted));
      }
      if (failed(writeCapturedLValue(child, part, false, nonblocking, location,
                                     delay)))
        return failure();
    }
    if (trailing != 0) {
      emitError(location) << "concatenation lvalue does not consume its value";
      return failure();
    }
    return success();
  }
  }
  llvm_unreachable("unknown captured lvalue kind");
}

void UnitLowering::appendCapturedValues(
    const CapturedLValue &destination, SmallVectorImpl<Value> &values) {
  if (destination.kind == CapturedLValue::Kind::Reference)
    values.push_back(destination.reference);
  for (const CapturedLValue &child : destination.children)
    appendCapturedValues(child, values);
  if (destination.kind == CapturedLValue::Kind::ContainerElement ||
      destination.kind == CapturedLValue::Kind::AssociativeElement) {
    values.push_back(destination.container);
    values.push_back(destination.index);
  } else if (destination.kind == CapturedLValue::Kind::StringCharacter) {
    values.push_back(destination.index);
  }
}

LogicalResult UnitLowering::replaceCapturedValues(CapturedLValue &destination,
                                                  ValueRange values,
                                                  unsigned &next) {
  if (destination.kind == CapturedLValue::Kind::Reference) {
    if (next >= values.size())
      return failure();
    destination.reference = values[next++];
  }
  for (CapturedLValue &child : destination.children) {
    if (failed(replaceCapturedValues(child, values, next)))
      return failure();
  }
  if (destination.kind == CapturedLValue::Kind::ContainerElement ||
      destination.kind == CapturedLValue::Kind::AssociativeElement) {
    if (next > values.size() || values.size() - next < 2)
      return failure();
    destination.container = values[next++];
    destination.index = values[next++];
  } else if (destination.kind == CapturedLValue::Kind::StringCharacter) {
    if (next >= values.size())
      return failure();
    destination.index = values[next++];
  }
  return success();
}

LogicalResult UnitLowering::writeLValue(Operation *destination, Value value,
                                        bool sourceSigned, bool nonblocking,
                                        Location location, Value delay) {
  FailureOr<CapturedLValue> captured = captureLValue(destination, location);
  if (failed(captured))
    return failure();
  return writeCapturedLValue(*captured, value, sourceSigned, nonblocking,
                             location, delay);
}

FailureOr<Value>
UnitLowering::lowerAssignment(semantic::SVAssignmentExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  bool timed = op.getHasTimingControl();
  size_t expected = timed ? 3 : 2;
  if (children.size() != expected) {
    unsupported(op) << " (assignment child inventory)";
    return failure();
  }
  Operation *control = timed ? children[0] : nullptr;
  Operation *destination = children[timed ? 1 : 0];
  Operation *source = children[timed ? 2 : 1];
  bool compound = op.getOperatorKind().has_value();
  bool nonblocking =
      op.getAssignmentKind() == semantic::SVAssignmentKind::Nonblocking;
  if (compound && nonblocking) {
    emitError(location)
        << "nonblocking compound assignment is not valid SystemVerilog";
    return failure();
  }

  std::optional<CapturedLValue> captured;
  FailureOr<Value> rhs = failure();
  if (compound) {
    FailureOr<CapturedLValue> destinationCapture =
        captureLValue(destination, location);
    if (failed(destinationCapture))
      return failure();
    captured = std::move(*destinationCapture);
    FailureOr<Value> oldValue = loadCapturedLValue(*captured, location);
    if (failed(oldValue))
      return failure();

    // Slang represents the left operand of a compound assignment's explicit
    // binary subtree with an lvalue-reference placeholder. Resolve it to the
    // value loaded from the already captured destination, so every other
    // binary conversion rule remains shared with ordinary expressions.
    Value previousPlaceholder = lvalueReferencePlaceholder;
    lvalueReferencePlaceholder = *oldValue;
    rhs = lowerExpression(source);
    lvalueReferencePlaceholder = previousPlaceholder;
  } else {
    rhs = lowerExpression(source);
  }
  if (failed(rhs))
    return failure();
  FailureOr<Type> destinationType = getNormalizedSemanticType(destination);
  if (failed(destinationType))
    return failure();
  FailureOr<Value> value = convert(*rhs, *destinationType, isSignedNode(source),
                                   location, isSignedNode(destination));
  if (failed(value))
    return failure();
  if (!timed) {
    LogicalResult written =
        compound
            ? writeCapturedLValue(*captured, *value, false, false, location)
            : writeLValue(destination, *value, false, nonblocking, location);
    if (failed(written))
      return failure();
    return *value;
  }

  if (compound) {
    SmallVector<Value> continuationOperands;
    appendCapturedValues(*captured, continuationOperands);
    continuationOperands.push_back(*value);
    Block *continuation = addBlock();
    for (Value operand : continuationOperands)
      continuation->addArgument(operand.getType(), location);

    if (isa<semantic::SVDelayControlOp>(control)) {
      FailureOr<Value> delay = lowerDelayValue(control);
      if (failed(delay))
        return failure();
      sim::SimSuspendDelayOp::create(
          builder, location, *delay, sim::TimingSiteAttr{},
          continuationOperands, sim::ContinuationSiteAttr{},
          sim::EventRegionAttr{}, continuation);
      setCurrent(continuation);
    } else if (isa<semantic::SVRepeatedEventControlOp>(control)) {
      if (failed(emitRepeatedEventSuspend(control, continuation,
                                          continuationOperands)))
        return failure();
    } else {
      if (failed(
              emitEventSuspend(control, continuation, continuationOperands)))
        return failure();
      setCurrent(continuation);
    }

    unsigned next = 0;
    if (failed(replaceCapturedValues(*captured,
                                     continuation->getArguments(), next)))
      return failure();
    if (next >= continuation->getNumArguments())
      return failure();
    Value storedValue = continuation->getArgument(next);
    if (next + 1 != continuation->getNumArguments() ||
        failed(writeCapturedLValue(*captured, storedValue, false, false,
                                   location)))
      return failure();
    return storedValue;
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
    continuation->addArgument((*value).getType(), location);
    sim::SimSuspendDelayOp::create(
        builder, location, *delay, sim::TimingSiteAttr{}, ValueRange{*value},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
    setCurrent(continuation);
    Value capturedValue = continuation->getArgument(0);
    if (failed(
            writeLValue(destination, capturedValue, false, false, location)))
      return failure();
    return capturedValue;
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
    if (isa<FloatType>(oldValue.getType())) {
      Value one = arith::ConstantOp::create(
          builder, location, oldValue.getType(),
          builder.getFloatAttr(oldValue.getType(), 1.0));
      Value newValue =
          increment
              ? Value(arith::AddFOp::create(builder, location, oldValue, one))
              : Value(arith::SubFOp::create(builder, location, oldValue, one));
      if (failed(storeReference(*destination, newValue, location)))
        return failure();
      bool post = kind == semantic::SVUnaryOperator::Postincrement ||
                  kind == semantic::SVUnaryOperator::Postdecrement;
      return convert(post ? oldValue : newValue, *resultType, false, location);
    }
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
  if (isa<FloatType>((*input).getType())) {
    Value value;
    if (kind == semantic::SVUnaryOperator::Plus) {
      value = *input;
    } else if (kind == semantic::SVUnaryOperator::Minus) {
      value = arith::NegFOp::create(builder, location, *input);
    } else if (kind == semantic::SVUnaryOperator::LogicalNot) {
      FailureOr<Value> truth = truthValue(*input, location);
      if (failed(truth))
        return failure();
      value = arith::XOrIOp::create(
          builder, location, *truth,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
    } else {
      emitError(location)
          << "floating-point operand does not support this unary operator";
      return failure();
    }
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
    if (succeeded(rhs)) {
      if (isa<sim::ClassHandleType>((*rhs).getType()))
        lhs = sim::SimClassNullOp::create(builder, location, (*rhs).getType())
                  .getResult();
      else if (isa<sim::EventType>((*rhs).getType()))
        lhs = sim::SimEventNullOp::create(builder, location, (*rhs).getType())
                  .getResult();
    }
  } else if (isa<semantic::SVNullLiteralOp>(children[1])) {
    lhs = lowerExpression(children[0]);
    if (succeeded(lhs)) {
      if (isa<sim::ClassHandleType>((*lhs).getType()))
        rhs = sim::SimClassNullOp::create(builder, location, (*lhs).getType())
                  .getResult();
      else if (isa<sim::EventType>((*lhs).getType()))
        rhs = sim::SimEventNullOp::create(builder, location, (*lhs).getType())
                  .getResult();
    }
  } else {
    lhs = lowerExpression(children[0]);
    rhs = lowerExpression(children[1]);
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(lhs) || failed(rhs) || failed(resultType))
    return failure();
  Binary kind = op.getOperatorKind();
  if (isa<sim::StringType>((*lhs).getType()) ||
      isa<sim::StringType>((*rhs).getType())) {
    Type stringType = sim::StringType::get(function.getContext());
    FailureOr<Value> left =
        convert(*lhs, stringType, isSignedNode(children[0]), location);
    FailureOr<Value> right =
        convert(*rhs, stringType, isSignedNode(children[1]), location);
    if (failed(left) || failed(right))
      return failure();
    if (kind == Binary::Add) {
      Value joined = sim::SimStringConcatOp::create(
          builder, location, stringType, ValueRange{*left, *right});
      return convert(joined, *resultType, false, location);
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
      predicate = arith::CmpIPredicate::sge;
      break;
    case Binary::GreaterThan:
      predicate = arith::CmpIPredicate::sgt;
      break;
    case Binary::LessThanEqual:
      predicate = arith::CmpIPredicate::sle;
      break;
    case Binary::LessThan:
      predicate = arith::CmpIPredicate::slt;
      break;
    default:
      unsupported(op) << " (string operator)";
      return failure();
    }
    Value compared = sim::SimStringCompareOp::create(
        builder, location, builder.getI32Type(), *left, *right,
        builder.getBoolAttr(false));
    Value zero = arith::ConstantOp::create(builder, location,
                                           builder.getI32Type(),
                                           builder.getI32IntegerAttr(0));
    Value result =
        arith::CmpIOp::create(builder, location, *predicate, compared, zero);
    return convert(result, *resultType, false, location);
  }
  if (isa<sim::DynamicArrayType, sim::QueueType,
          sim::AssocArrayType>((*lhs).getType()) ||
      isa<sim::DynamicArrayType, sim::QueueType,
          sim::AssocArrayType>((*rhs).getType())) {
    if ((*lhs).getType() != (*rhs).getType() ||
        (kind != Binary::Equality && kind != Binary::Inequality &&
         kind != Binary::CaseEquality && kind != Binary::CaseInequality)) {
      unsupported(op) << " (sequential-container operator)";
      return failure();
    }
    bool caseEquality =
        kind == Binary::CaseEquality || kind == Binary::CaseInequality;
    FailureOr<Value> equal =
        caseEquality
            ? conditionalEqual(*lhs, *rhs, (*lhs).getType(), location, true)
            : logicalEqual(*lhs, *rhs, (*lhs).getType(), location);
    if (failed(equal))
      return failure();
    Value result = *equal;
    if (kind == Binary::Inequality || kind == Binary::CaseInequality) {
      if (caseEquality)
        result = arith::XOrIOp::create(
            builder, location, result,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true)));
      else
        result =
            sim::SimLogicUnaryOp::create(builder, location, result.getType(),
                                         sim::UnaryKind::LogicalNot, result);
    }
    return convert(result, *resultType, false, location);
  }
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
  if (isa<FloatType>((*lhs).getType()) ||
      isa<FloatType>((*rhs).getType())) {
    Type arithmeticType =
        (*lhs).getType().isF64() || (*rhs).getType().isF64()
            ? Type(builder.getF64Type())
            : Type(builder.getF32Type());
    if ((*lhs).getType() != arithmeticType) {
      lhs = convert(*lhs, arithmeticType, isSignedNode(children[0]),
                    location);
      if (failed(lhs))
        return failure();
    }
    if ((*rhs).getType() != arithmeticType) {
      rhs = convert(*rhs, arithmeticType, isSignedNode(children[1]),
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
    if (predicate) {
      Value compared =
          arith::CmpFOp::create(builder, location, *predicate, *lhs, *rhs);
      return convert(compared, *resultType, false, location);
    }
    Value value;
    switch (kind) {
    case Binary::Add:
      value = arith::AddFOp::create(builder, location, *lhs, *rhs);
      break;
    case Binary::Subtract:
      value = arith::SubFOp::create(builder, location, *lhs, *rhs);
      break;
    case Binary::Multiply:
      value = arith::MulFOp::create(builder, location, *lhs, *rhs);
      break;
    case Binary::Divide:
      value = arith::DivFOp::create(builder, location, *lhs, *rhs);
      break;
    case Binary::Power:
      value = math::PowFOp::create(builder, location, arithmeticType, *lhs,
                                   *rhs)
                  .getResult();
      break;
    default:
      emitError(location)
          << "floating-point operand does not support this binary operator";
      return failure();
    }
    return convert(value, *resultType, false, location);
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

FailureOr<Value> UnitLowering::conditionalPredicate(Value value,
                                                    Location location) {
  if (isa<sim::StringType, FloatType>(value.getType())) {
    FailureOr<Value> truth = truthValue(value, location);
    if (failed(truth))
      return failure();
    return sim::SimLogicFromBitsOp::create(
               builder, location,
               sim::LogicType::get(function.getContext(), 1), *truth)
        .getResult();
  }
  FailureOr<Value> scalar = toPackedScalar(value, location);
  if (failed(scalar))
    return failure();
  if (isa<sim::LogicType>((*scalar).getType()))
    return sim::SimLogicReductionOp::create(
               builder, location,
               sim::LogicType::get(function.getContext(), 1),
               sim::ReductionKind::Or, *scalar)
        .getResult();
  FailureOr<Value> truth = truthValue(*scalar, location);
  if (failed(truth))
    return failure();
  return sim::SimLogicFromBitsOp::create(
             builder, location, sim::LogicType::get(function.getContext(), 1),
             *truth)
      .getResult();
}

FailureOr<Value> UnitLowering::conditionalEqual(Value lhs, Value rhs, Type type,
                                                Location location,
                                                bool caseEquality) {
  auto falseValue = [&]() -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false));
  };
  if (Type scalarType = sim::getPackedScalarType(type)) {
    FailureOr<Value> left = toPackedScalar(lhs, location);
    FailureOr<Value> right = toPackedScalar(rhs, location);
    if (failed(left) || failed(right))
      return failure();
    if (isa<sim::LogicType>(scalarType)) {
      if (caseEquality)
        return sim::SimLogicCompareOp::create(
                   builder, location, builder.getI1Type(),
                   sim::CompareKind::CaseEq, *left, *right)
            .getResult();
      Value compared = sim::SimLogicCompareOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          sim::CompareKind::Eq, *left, *right);
      return sim::SimLogicIsTrueOp::create(builder, location,
                                           builder.getI1Type(), compared)
          .getResult();
    }
    return arith::CmpIOp::create(builder, location,
                                 arith::CmpIPredicate::eq, *left, *right)
        .getResult();
  }
  if (isa<FloatType>(type))
    return arith::CmpFOp::create(builder, location,
                                 arith::CmpFPredicate::OEQ, lhs, rhs)
        .getResult();
  if (isa<sim::StringType>(type)) {
    Value compared = sim::SimStringCompareOp::create(
        builder, location, builder.getI32Type(), lhs, rhs,
        builder.getBoolAttr(false));
    Value zero = arith::ConstantOp::create(builder, location,
                                           builder.getI32Type(),
                                           builder.getI32IntegerAttr(0));
    return arith::CmpIOp::create(builder, location,
                                 arith::CmpIPredicate::eq, compared, zero)
        .getResult();
  }
  if (isa<sim::ClassHandleType>(type)) {
    Value left = sim::SimClassIdOp::create(builder, location, lhs);
    Value right = sim::SimClassIdOp::create(builder, location, rhs);
    return arith::CmpIOp::create(builder, location,
                                 arith::CmpIPredicate::eq, left, right)
        .getResult();
  }
  if (isa<sim::EventType>(type))
    return sim::SimEventEqualOp::create(builder, location,
                                        builder.getI1Type(), lhs, rhs)
        .getResult();
  if (auto associative = dyn_cast<sim::AssocArrayType>(type)) {
    Value leftSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), lhs);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), rhs);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *start = addBlock();
    Block *falseBlock = addBlock();
    Block *loop = addBlock();
    Block *body = addBlock();
    Block *next = addBlock();
    Block *result = addBlock();
    loop->addArgument(associative.getKeyType(), location);
    loop->addArgument(builder.getI1Type(), location);
    next->addArgument(associative.getKeyType(), location);
    result->addArgument(builder.getI1Type(), location);
    cf::CondBranchOp::create(builder, location, sameSize, start, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(start);
    Value defaultKey =
        createDefaultValue(builder, location, associative.getKeyType());
    FailureOr<std::pair<Value, Value>> first =
        traverseAssoc(lhs, defaultKey, 1, true, location);
    if (failed(first))
      return failure();
    cf::BranchOp::create(
        builder, location, loop,
        ValueRange{first->first, first->second});
    setCurrent(loop);
    Value key = loop->getArgument(0);
    Value valid = loop->getArgument(1);
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    cf::CondBranchOp::create(builder, location, valid, body, ValueRange{},
                             result, ValueRange{trueValue});
    setCurrent(body);
    Value exists = sim::SimAssocExistsOp::create(
        builder, location, builder.getI1Type(), rhs, key);
    Block *compare = addBlock();
    cf::CondBranchOp::create(builder, location, exists, compare, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(compare);
    Value left = sim::SimAssocReadOp::create(
        builder, location, associative.getElementType(), lhs, key);
    Value right = sim::SimAssocReadOp::create(
        builder, location, associative.getElementType(), rhs, key);
    FailureOr<Value> equal =
        conditionalEqual(left, right, associative.getElementType(), location,
                         caseEquality);
    if (failed(equal))
      return failure();
    cf::CondBranchOp::create(builder, location, *equal, next, ValueRange{key},
                             falseBlock, ValueRange{});
    setCurrent(next);
    FailureOr<std::pair<Value, Value>> following =
        traverseAssoc(lhs, next->getArgument(0), 1, false, location);
    if (failed(following))
      return failure();
    cf::BranchOp::create(
        builder, location, loop,
        ValueRange{following->first, following->second});
    setCurrent(falseBlock);
    cf::BranchOp::create(builder, location, result,
                         ValueRange{falseValue()});
    setCurrent(result);
    return result->getArgument(0);
  }
  if (isa<sim::DynamicArrayType, sim::QueueType>(type)) {
    Value leftSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), lhs);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), rhs);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *compareBlock = addBlock();
    Block *falseBlock = addBlock();
    Block *loopBlock = addBlock();
    Block *bodyBlock = addBlock();
    Block *resultBlock = addBlock();
    loopBlock->addArgument(builder.getI64Type(), location);
    resultBlock->addArgument(builder.getI1Type(), location);
    cf::CondBranchOp::create(builder, location, sameSize, compareBlock,
                             ValueRange{}, falseBlock, ValueRange{});

    setCurrent(compareBlock);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(),
        builder.getI64IntegerAttr(0));
    cf::BranchOp::create(builder, location, loopBlock, ValueRange{zero});

    setCurrent(loopBlock);
    Value index = loopBlock->getArgument(0);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, leftSize);
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    cf::CondBranchOp::create(builder, location, inRange, bodyBlock,
                             ValueRange{}, resultBlock,
                             ValueRange{trueValue});

    setCurrent(bodyBlock);
    Type elementType =
        isa<sim::DynamicArrayType>(type)
            ? cast<sim::DynamicArrayType>(type).getElementType()
            : cast<sim::QueueType>(type).getElementType();
    Value left = sim::SimContainerReadOp::create(
        builder, location, elementType, lhs, index);
    Value right = sim::SimContainerReadOp::create(
        builder, location, elementType, rhs, index);
    FailureOr<Value> equal =
        conditionalEqual(left, right, elementType, location, caseEquality);
    if (failed(equal))
      return failure();
    Block *nextBlock = addBlock();
    cf::CondBranchOp::create(builder, location, *equal, nextBlock,
                             ValueRange{}, falseBlock, ValueRange{});
    setCurrent(nextBlock);
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(),
        builder.getI64IntegerAttr(1));
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, loopBlock, ValueRange{next});

    setCurrent(falseBlock);
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{falseValue});
    setCurrent(resultBlock);
    return resultBlock->getArgument(0);
  }
  if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type)) {
    unsigned count = sim::getAggregateNumElements(unionType);
    if (!unionType.getIsTagged()) {
      Value equal = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      for (unsigned index = 0; index < count; ++index) {
        Type elementType = sim::getAggregateElementType(unionType, index);
        if (!elementType)
          return failure();
        Value left = sim::SimUnionExtractOp::create(
            builder, location, elementType, lhs, index);
        Value right = sim::SimUnionExtractOp::create(
            builder, location, elementType, rhs, index);
        FailureOr<Value> elementEqual =
            conditionalEqual(left, right, elementType, location, caseEquality);
        if (failed(elementEqual))
          return failure();
        equal =
            arith::AndIOp::create(builder, location, equal, *elementEqual);
      }
      return equal;
    }

    Block *falseBlock = addBlock();
    Block *resultBlock = addBlock();
    resultBlock->addArgument(builder.getI1Type(), location);
    for (unsigned index = 0; index < count; ++index) {
      Value leftActive = sim::SimUnionIsActiveOp::create(
          builder, location, builder.getI1Type(), lhs, index);
      Value rightActive = sim::SimUnionIsActiveOp::create(
          builder, location, builder.getI1Type(), rhs, index);
      Value sameActive = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, leftActive,
          rightActive);
      Block *sameBlock = addBlock();
      Block *compareBlock = addBlock();
      Block *nextBlock = addBlock();
      cf::CondBranchOp::create(builder, location, sameActive, sameBlock,
                               ValueRange{}, falseBlock, ValueRange{});

      setCurrent(sameBlock);
      cf::CondBranchOp::create(builder, location, leftActive, compareBlock,
                               ValueRange{}, nextBlock, ValueRange{});

      setCurrent(compareBlock);
      Type elementType = sim::getAggregateElementType(unionType, index);
      if (!elementType)
        return failure();
      Value left = sim::SimUnionExtractOp::create(
          builder, location, elementType, lhs, index);
      Value right = sim::SimUnionExtractOp::create(
          builder, location, elementType, rhs, index);
      FailureOr<Value> elementEqual =
          conditionalEqual(left, right, elementType, location, caseEquality);
      if (failed(elementEqual))
        return failure();
      cf::CondBranchOp::create(builder, location, *elementEqual, nextBlock,
                               ValueRange{}, falseBlock, ValueRange{});
      setCurrent(nextBlock);
    }
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{trueValue});

    setCurrent(falseBlock);
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{falseValue()});
    setCurrent(resultBlock);
    return resultBlock->getArgument(0);
  }
  if (!isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type))
    return falseValue();

  Value equal = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(true));
  unsigned count = sim::getAggregateNumElements(type);
  for (unsigned index = 0; index < count; ++index) {
    Type elementType = sim::getAggregateElementType(type, index);
    if (!elementType)
      return failure();
    Value left = sim::SimAggregateExtractOp::create(
        builder, location, elementType, lhs, index);
    Value right = sim::SimAggregateExtractOp::create(
        builder, location, elementType, rhs, index);
    FailureOr<Value> elementEqual =
        conditionalEqual(left, right, elementType, location, caseEquality);
    if (failed(elementEqual))
      return failure();
    equal =
        arith::AndIOp::create(builder, location, equal, *elementEqual);
  }
  return equal;
}

FailureOr<Value> UnitLowering::logicalEqual(Value lhs, Value rhs, Type type,
                                            Location location) {
  Type logicType = sim::LogicType::get(function.getContext(), 1);
  auto known = [&](bool value) -> Value {
    Type i1 = builder.getI1Type();
    return sim::SimLogicConstantOp::create(
        builder, location, logicType, builder.getIntegerAttr(i1, value ? 1 : 0),
        builder.getIntegerAttr(i1, 0));
  };
  auto fromBits = [&](Value value) -> Value {
    return sim::SimLogicFromBitsOp::create(builder, location, logicType, value);
  };
  auto conjunction = [&](Value left, Value right) -> Value {
    return sim::SimLogicBinaryOp::create(builder, location, logicType,
                                         sim::BinaryKind::And, left, right);
  };

  if (Type scalarType = sim::getPackedScalarType(type)) {
    FailureOr<Value> left = toPackedScalar(lhs, location);
    FailureOr<Value> right = toPackedScalar(rhs, location);
    if (failed(left) || failed(right))
      return failure();
    if (isa<sim::LogicType>(scalarType))
      return sim::SimLogicCompareOp::create(builder, location, logicType,
                                            sim::CompareKind::Eq, *left, *right)
          .getResult();
    Value equal = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, *left, *right);
    return fromBits(equal);
  }
  if (isa<FloatType, sim::StringType, sim::ClassHandleType, sim::EventType>(
          type)) {
    FailureOr<Value> equal = conditionalEqual(lhs, rhs, type, location);
    if (failed(equal))
      return failure();
    return fromBits(*equal);
  }
  if (auto associative = dyn_cast<sim::AssocArrayType>(type)) {
    Value leftSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), lhs);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), rhs);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *start = addBlock();
    Block *falseBlock = addBlock();
    Block *loop = addBlock();
    Block *body = addBlock();
    Block *next = addBlock();
    Block *result = addBlock();
    loop->addArgument(associative.getKeyType(), location);
    loop->addArgument(builder.getI1Type(), location);
    loop->addArgument(logicType, location);
    next->addArgument(associative.getKeyType(), location);
    next->addArgument(logicType, location);
    result->addArgument(logicType, location);
    cf::CondBranchOp::create(builder, location, sameSize, start, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(start);
    Value defaultKey =
        createDefaultValue(builder, location, associative.getKeyType());
    FailureOr<std::pair<Value, Value>> first =
        traverseAssoc(lhs, defaultKey, 1, true, location);
    if (failed(first))
      return failure();
    cf::BranchOp::create(
        builder, location, loop,
        ValueRange{first->first, first->second, known(true)});
    setCurrent(loop);
    Value key = loop->getArgument(0);
    Value valid = loop->getArgument(1);
    Value accumulated = loop->getArgument(2);
    cf::CondBranchOp::create(builder, location, valid, body, ValueRange{},
                             result, ValueRange{accumulated});
    setCurrent(body);
    Value exists = sim::SimAssocExistsOp::create(
        builder, location, builder.getI1Type(), rhs, key);
    Block *compare = addBlock();
    cf::CondBranchOp::create(builder, location, exists, compare, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(compare);
    Value left = sim::SimAssocReadOp::create(
        builder, location, associative.getElementType(), lhs, key);
    Value right = sim::SimAssocReadOp::create(
        builder, location, associative.getElementType(), rhs, key);
    FailureOr<Value> elementEqual =
        logicalEqual(left, right, associative.getElementType(), location);
    if (failed(elementEqual))
      return failure();
    Value combined = conjunction(accumulated, *elementEqual);
    cf::BranchOp::create(builder, location, next,
                         ValueRange{key, combined});
    setCurrent(next);
    FailureOr<std::pair<Value, Value>> following =
        traverseAssoc(lhs, next->getArgument(0), 1, false, location);
    if (failed(following))
      return failure();
    cf::BranchOp::create(
        builder, location, loop,
        ValueRange{following->first, following->second, next->getArgument(1)});
    setCurrent(falseBlock);
    cf::BranchOp::create(builder, location, result,
                         ValueRange{known(false)});
    setCurrent(result);
    return result->getArgument(0);
  }
  if (isa<sim::DynamicArrayType, sim::QueueType>(type)) {
    Value leftSize = sim::SimContainerSizeOp::create(builder, location,
                                                     builder.getI64Type(), lhs);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), rhs);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *compareBlock = addBlock();
    Block *falseBlock = addBlock();
    Block *loopBlock = addBlock();
    Block *bodyBlock = addBlock();
    Block *resultBlock = addBlock();
    loopBlock->addArgument(builder.getI64Type(), location);
    loopBlock->addArgument(logicType, location);
    resultBlock->addArgument(logicType, location);
    cf::CondBranchOp::create(builder, location, sameSize, compareBlock,
                             ValueRange{}, falseBlock, ValueRange{});

    setCurrent(compareBlock);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    cf::BranchOp::create(builder, location, loopBlock,
                         ValueRange{zero, known(true)});

    setCurrent(loopBlock);
    Value index = loopBlock->getArgument(0);
    Value accumulated = loopBlock->getArgument(1);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, leftSize);
    cf::CondBranchOp::create(builder, location, inRange, bodyBlock,
                             ValueRange{}, resultBlock,
                             ValueRange{accumulated});

    setCurrent(bodyBlock);
    Type elementType = isa<sim::DynamicArrayType>(type)
                           ? cast<sim::DynamicArrayType>(type).getElementType()
                           : cast<sim::QueueType>(type).getElementType();
    Value left = sim::SimContainerReadOp::create(builder, location, elementType,
                                                 lhs, index);
    Value right = sim::SimContainerReadOp::create(builder, location,
                                                  elementType, rhs, index);
    FailureOr<Value> elementEqual =
        logicalEqual(left, right, elementType, location);
    if (failed(elementEqual))
      return failure();
    Value nextAccumulated = conjunction(accumulated, *elementEqual);
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, loopBlock,
                         ValueRange{next, nextAccumulated});

    setCurrent(falseBlock);
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{known(false)});
    setCurrent(resultBlock);
    return resultBlock->getArgument(0);
  }
  if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
    Value equal = known(true);
    unsigned count = sim::getAggregateNumElements(type);
    for (unsigned index = 0; index < count; ++index) {
      Type elementType = sim::getAggregateElementType(type, index);
      if (!elementType)
        return failure();
      Value left = sim::SimAggregateExtractOp::create(builder, location,
                                                      elementType, lhs, index);
      Value right = sim::SimAggregateExtractOp::create(builder, location,
                                                       elementType, rhs, index);
      FailureOr<Value> elementEqual =
          logicalEqual(left, right, elementType, location);
      if (failed(elementEqual))
        return failure();
      equal = conjunction(equal, *elementEqual);
    }
    return equal;
  }

  // Tagged and untagged union comparison first requires matching active
  // alternatives. The existing exact comparator supplies a known result for
  // that structural check.
  FailureOr<Value> equal = conditionalEqual(lhs, rhs, type, location);
  if (failed(equal))
    return failure();
  return fromBits(*equal);
}

FailureOr<Value>
UnitLowering::mergeConditionalValues(Value condition, Value trueValue,
                                     Value falseValue, Type type,
                                     Location location) {
  if (sim::getPackedScalarType(type)) {
    FailureOr<Value> leftScalar = toPackedScalar(trueValue, location);
    FailureOr<Value> rightScalar = toPackedScalar(falseValue, location);
    if (failed(leftScalar) || failed(rightScalar))
      return failure();
    FailureOr<Value> left = toLogic(*leftScalar, location);
    FailureOr<Value> right = toLogic(*rightScalar, location);
    if (failed(left) || failed(right))
      return failure();
    Value merged = sim::SimLogicMuxOp::create(
        builder, location, (*left).getType(), condition, *left, *right);
    return convert(merged, type, false, location);
  }
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
    SmallVector<Value> elements;
    unsigned count = sim::getAggregateNumElements(array);
    elements.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
      Type elementType = array.getElementType();
      Value left = sim::SimAggregateExtractOp::create(
          builder, location, elementType, trueValue, index);
      Value right = sim::SimAggregateExtractOp::create(
          builder, location, elementType, falseValue, index);
      FailureOr<Value> equal =
          conditionalEqual(left, right, elementType, location);
      if (failed(equal))
        return failure();
      Value defaultValue = createDefaultValue(builder, location, elementType);
      if (!defaultValue) {
        emitError(location) << "cannot materialize conditional default for "
                            << elementType;
        return failure();
      }
      elements.push_back(arith::SelectOp::create(
          builder, location, *equal, left, defaultValue));
    }
    return sim::SimAggregateConstructOp::create(builder, location, type,
                                                 elements)
        .getResult();
  }
  if (isa<sim::DynamicArrayType, sim::QueueType>(type)) {
    Type elementType =
        isa<sim::DynamicArrayType>(type)
            ? cast<sim::DynamicArrayType>(type).getElementType()
            : cast<sim::QueueType>(type).getElementType();
    Value leftSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), trueValue);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), falseValue);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *createBlock = addBlock();
    Block *defaultBlock = addBlock();
    Block *loopBlock = addBlock();
    Block *bodyBlock = addBlock();
    Block *resultBlock = addBlock();
    loopBlock->addArgument(builder.getI64Type(), location);
    resultBlock->addArgument(type, location);
    cf::CondBranchOp::create(builder, location, sameSize, createBlock,
                             ValueRange{}, defaultBlock, ValueRange{});

    setCurrent(defaultBlock);
    Value defaultContainer = createDefaultValue(builder, location, type);
    if (!defaultContainer)
      return failure();
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{defaultContainer});

    setCurrent(createBlock);
    Value result = sim::SimContainerCreateLikeOp::create(
        builder, location, type, trueValue, falseValue, leftSize);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(),
        builder.getI64IntegerAttr(0));
    cf::BranchOp::create(builder, location, loopBlock, ValueRange{zero});

    setCurrent(loopBlock);
    Value index = loopBlock->getArgument(0);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, leftSize);
    cf::CondBranchOp::create(builder, location, inRange, bodyBlock,
                             ValueRange{}, resultBlock, ValueRange{result});

    setCurrent(bodyBlock);
    Value left = sim::SimContainerReadOp::create(
        builder, location, elementType, trueValue, index);
    Value right = sim::SimContainerReadOp::create(
        builder, location, elementType, falseValue, index);
    FailureOr<Value> equal =
        conditionalEqual(left, right, elementType, location);
    if (failed(equal))
      return failure();
    Value defaultElement = createDefaultValue(builder, location, elementType);
    if (!defaultElement)
      return failure();
    Value selected = arith::SelectOp::create(
        builder, location, *equal, left, defaultElement);
    sim::SimContainerWriteOp::create(builder, location, result, index,
                                     selected);
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(),
        builder.getI64IntegerAttr(1));
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, loopBlock, ValueRange{next});

    setCurrent(resultBlock);
    return resultBlock->getArgument(0);
  }
  Value defaultValue = createDefaultValue(builder, location, type);
  if (!defaultValue) {
    emitError(location) << "cannot materialize ambiguous conditional default "
                        << type;
    return failure();
  }
  return defaultValue;
}

FailureOr<Value> UnitLowering::lowerConditionalExpression(
    semantic::SVConditionalExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  ArrayRef<int64_t> patternFlags = op.getConditionPatternFlags();
  if (op.getConditionCount() == 0 ||
      patternFlags.size() != op.getConditionCount()) {
    emitError(location) << "malformed conditional-expression metadata";
    return failure();
  }
  size_t conditionChildren = op.getConditionCount();
  for (int64_t flag : patternFlags) {
    if (flag != 0 && flag != 1) {
      emitError(location)
          << "conditional-expression pattern flags must be zero or one";
      return failure();
    }
    conditionChildren += static_cast<size_t>(flag);
  }
  if (children.size() != conditionChildren + 2) {
    emitError(location) << "malformed conditional-expression inventory";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();

  ArrayRef<Operation *> conditions =
      ArrayRef<Operation *>(children).take_front(conditionChildren);
  Operation *trueExpression = children[conditionChildren];
  Operation *falseExpression = children[conditionChildren + 1];

  Block *trueBlock = addBlock();
  Block *falseBlock = addBlock();
  Block *ambiguousBlock = addBlock();
  Block *mergeBlock = addBlock();
  Type predicateType = sim::LogicType::get(function.getContext(), 1);
  mergeBlock->addArgument(*resultType, location);

  Value sawUnknown = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(false));
  size_t childIndex = 0;
  for (size_t conditionIndex = 0; conditionIndex < op.getConditionCount();
       ++conditionIndex) {
    Operation *expression = conditions[childIndex++];
    FailureOr<Value> input = lowerExpression(expression);
    if (failed(input))
      return failure();
    FailureOr<Value> predicate;
    if (patternFlags[conditionIndex]) {
      Operation *pattern = conditions[childIndex++];
      FailureOr<Value> matched = lowerPattern(
          *input, pattern, semantic::SVCaseCondition::Normal);
      if (failed(matched))
        return failure();
      predicate =
          conditionalPredicate(*matched, getSemanticLocation(expression));
    } else {
      predicate =
          conditionalPredicate(*input, getSemanticLocation(expression));
    }
    if (failed(predicate))
      return failure();

    Type bitsType = builder.getI1Type();
    Value bits = sim::SimLogicToBitsOp::create(builder, location, bitsType,
                                               *predicate);
    Value roundTrip = sim::SimLogicFromBitsOp::create(
        builder, location, predicateType, bits);
    Value known = sim::SimLogicCompareOp::create(
        builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
        *predicate, roundTrip);
    Value isTrue = sim::SimLogicIsTrueOp::create(
        builder, getSemanticLocation(expression), builder.getI1Type(),
        *predicate);
    Value isFalse = arith::AndIOp::create(
        builder, getSemanticLocation(expression), known,
        arith::XOrIOp::create(
            builder, getSemanticLocation(expression), isTrue,
            arith::ConstantOp::create(builder, getSemanticLocation(expression),
                                      builder.getI1Type(),
                                      builder.getBoolAttr(true))));
    sawUnknown = arith::OrIOp::create(
        builder, getSemanticLocation(expression), sawUnknown,
        arith::XOrIOp::create(
            builder, getSemanticLocation(expression), known,
            arith::ConstantOp::create(builder, getSemanticLocation(expression),
                                      builder.getI1Type(),
                                      builder.getBoolAttr(true))));

    Block *nonFalse = addBlock();
    cf::CondBranchOp::create(builder, getSemanticLocation(expression), isFalse,
                             falseBlock, ValueRange{}, nonFalse, ValueRange{});
    setCurrent(nonFalse);
    if (conditionIndex + 1 != op.getConditionCount())
      continue;
    cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                             sawUnknown, ambiguousBlock, ValueRange{},
                             trueBlock, ValueRange{});
  }

  auto lowerArm = [&](Operation *expression) -> FailureOr<Value> {
    if (isa<semantic::SVNullLiteralOp>(expression)) {
      Value value = createDefaultValue(builder, getSemanticLocation(expression),
                                       *resultType);
      return value ? FailureOr<Value>(value) : FailureOr<Value>(failure());
    }
    FailureOr<Value> value = lowerExpression(expression);
    if (failed(value))
      return failure();
    return convert(*value, *resultType, isSignedNode(expression),
                   getSemanticLocation(expression), isSignedNode(op));
  };

  setCurrent(trueBlock);
  FailureOr<Value> trueResult = lowerArm(trueExpression);
  if (failed(trueResult))
    return failure();
  cf::BranchOp::create(builder, getSemanticLocation(trueExpression), mergeBlock,
                       ValueRange{*trueResult});

  setCurrent(falseBlock);
  FailureOr<Value> falseResult = lowerArm(falseExpression);
  if (failed(falseResult))
    return failure();
  cf::BranchOp::create(builder, getSemanticLocation(falseExpression),
                       mergeBlock, ValueRange{*falseResult});

  setCurrent(ambiguousBlock);
  FailureOr<Value> ambiguousTrue = lowerArm(trueExpression);
  FailureOr<Value> ambiguousFalse = lowerArm(falseExpression);
  if (failed(ambiguousTrue) || failed(ambiguousFalse))
    return failure();
  Type i1 = builder.getI1Type();
  Value ambiguousCondition = sim::SimLogicConstantOp::create(
      builder, location, predicateType, builder.getIntegerAttr(i1, 0),
      builder.getIntegerAttr(i1, 1));
  FailureOr<Value> merged = mergeConditionalValues(
      ambiguousCondition, *ambiguousTrue, *ambiguousFalse, *resultType,
      location);
  if (failed(merged))
    return failure();
  cf::BranchOp::create(builder, location, mergeBlock, ValueRange{*merged});

  setCurrent(mergeBlock);
  return mergeBlock->getArgument(0);
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

FailureOr<Value>
UnitLowering::lowerArrayMethod(semantic::SVCallExpressionOp op,
                               Value receiverOverride, Value iteratorKeys) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  bool withClause = op.getHasIteratorExpression();
  if (children.size() != (withClause ? 2u : 1u)) {
    emitError(location) << "malformed array-method expression inventory";
    return failure();
  }
  Operation *receiverNode = children.back();
  Operation *clause = withClause ? children.front() : nullptr;
  StringRef name = op.getCalleeName();
  bool mutatesReceiver = name == "delete" || name == "reverse" ||
                         name == "shuffle" || name == "sort" || name == "rsort";
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
    if (withClause)
      return emitError(location) << "size does not accept a with clause",
             failure();
    Value size = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    return failed(resultType) ? FailureOr<Value>(failure())
                              : convert(size, *resultType, false, location);
  }
  if (name == "delete") {
    if (withClause)
      return emitError(location) << "delete does not accept a with clause",
             failure();
    sim::SimContainerDeleteOp::create(builder, location, *receiver);
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
      FailureOr<Value> converted =
          convert(sourceIndex(index), queue.getElementType(), true, location,
                  true);
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
  if (name == "first" || name == "last" || name == "next" ||
      name == "prev") {
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
      inputKey =
          createDefaultValue(builder, location, arrayType.getKeyType());
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
        convert(traversed->first, *destinationType,
                arrayType.getSignedKey(), location, isSignedNode(children[1]));
    if (failed(converted) ||
        failed(storeReference(*destination, *converted, location)))
      return failure();
    cf::BranchOp::create(builder, location, done);
    setCurrent(done);
    return result(traversed->second);
  }

  bool expressionMethod =
      name == "sum" || name == "product" || name == "and" || name == "or" ||
      name == "xor" || name == "find" || name == "find_index" ||
      name == "find_first" || name == "find_first_index" ||
      name == "find_last" || name == "find_last_index" || name == "min" ||
      name == "max" || name == "unique" || name == "unique_index" ||
      name == "map";
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
    Type valueQueueType =
        sim::QueueType::get(function.getContext(), arrayType.getElementType(),
                            0);
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
    cf::BranchOp::create(
        builder, location, header,
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
    cf::BranchOp::create(
        builder, location, header,
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
      Value one = arith::ConstantOp::create(
          builder, location, builder.getI64Type(),
          builder.getI64IntegerAttr(1));
      Value following =
          arith::AddIOp::create(builder, location, index, one);
      cf::BranchOp::create(builder, location, mapHeader,
                           ValueRange{following});
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

FailureOr<Value> UnitLowering::lowerCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op.getIsSystemCall() && op.getCalleeName() == "index" &&
      children.size() == 1) {
    auto path = children.front()->getAttrOfType<StringAttr>("referenced_path");
    if (path) {
      auto found = iteratorIndices.find(path.getValue());
      if (found != iteratorIndices.end()) {
        FailureOr<Type> resultType = getNormalizedSemanticType(op);
        if (failed(resultType))
          return failure();
        return convert(found->second, *resultType, true, location, true);
      }
    }
  }
  bool stringBuiltin = false;
  bool containerBuiltin = false;
  bool associativeBuiltin = false;
  if (op.getIsSystemCall() && !op.getCalleeName().starts_with("$") &&
      !children.empty()) {
    Operation *receiverNode =
        op.getHasIteratorExpression() ? children.back() : children.front();
    FailureOr<Type> receiverType = getNormalizedSemanticType(receiverNode);
    stringBuiltin =
        succeeded(receiverType) && isa<sim::StringType>(*receiverType);
    containerBuiltin =
        succeeded(receiverType) &&
        isa<sim::DynamicArrayType, sim::QueueType>(*receiverType);
    associativeBuiltin =
        succeeded(receiverType) && isa<sim::AssocArrayType>(*receiverType);
  }
  if (op.getIsSystemCall() && !stringBuiltin && !containerBuiltin &&
      !associativeBuiltin)
    return lowerSystemCall(op);
  if (associativeBuiltin)
    return lowerAssociativeArrayMethod(op);
  if (containerBuiltin)
    return lowerArrayMethod(op);
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
  if (!callee && !children.empty()) {
    FailureOr<Type> receiverType = getNormalizedSemanticType(children.front());
    if (succeeded(receiverType) && isa<sim::StringType>(*receiverType)) {
      StringRef name = op.getCalleeName();
      auto result = [&](Value value) -> FailureOr<Value> {
        FailureOr<Type> resultType = getNormalizedSemanticType(op);
        if (failed(resultType))
          return failure();
        return convert(value, *resultType, false, location);
      };
      auto receiver = [&]() -> FailureOr<Value> {
        return lowerExpression(children.front());
      };
      auto integerArgument = [&](unsigned index,
                                 Type type) -> FailureOr<Value> {
        if (index >= children.size())
          return failure();
        FailureOr<Value> value = lowerExpression(children[index]);
        if (failed(value))
          return failure();
        return convert(*value, type, isSignedNode(children[index]), location);
      };

      if (name == "len" && children.size() == 1) {
        FailureOr<Value> input = receiver();
        if (failed(input))
          return failure();
        return result(sim::SimStringLengthOp::create(
            builder, location, builder.getI64Type(), *input));
      }
      if (name == "getc" && children.size() == 2) {
        FailureOr<Value> input = receiver();
        FailureOr<Value> index = integerArgument(1, builder.getI64Type());
        if (failed(input) || failed(index))
          return failure();
        return result(sim::SimStringGetcOp::create(
            builder, location, builder.getI8Type(), *input, *index));
      }
      if ((name == "toupper" || name == "tolower") &&
          children.size() == 1) {
        FailureOr<Value> input = receiver();
        if (failed(input))
          return failure();
        return result(sim::SimStringCaseConvertOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input, builder.getBoolAttr(name == "toupper")));
      }
      if ((name == "compare" || name == "icompare") &&
          children.size() == 2) {
        FailureOr<Value> left = receiver();
        FailureOr<Value> right = lowerExpression(children[1]);
        if (failed(left) || failed(right))
          return failure();
        FailureOr<Value> converted =
            convert(*right, sim::StringType::get(function.getContext()),
                    isSignedNode(children[1]), location);
        if (failed(converted))
          return failure();
        return result(sim::SimStringCompareOp::create(
            builder, location, builder.getI32Type(), *left, *converted,
            builder.getBoolAttr(name == "icompare")));
      }
      if (name == "substr" && children.size() == 3) {
        FailureOr<Value> input = receiver();
        FailureOr<Value> left = integerArgument(1, builder.getI64Type());
        FailureOr<Value> right = integerArgument(2, builder.getI64Type());
        if (failed(input) || failed(left) || failed(right))
          return failure();
        return result(sim::SimStringSubstrOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input, *left, *right));
      }
      if ((name == "atoi" || name == "atohex" || name == "atooct" ||
           name == "atobin") &&
          children.size() == 1) {
        FailureOr<Value> input = receiver();
        if (failed(input))
          return failure();
        unsigned radix = name == "atobin"   ? 2
                         : name == "atooct" ? 8
                         : name == "atohex" ? 16
                                            : 10;
        return result(sim::SimStringParseIntegerOp::create(
            builder, location, builder.getI64Type(), *input,
            builder.getI32IntegerAttr(radix)));
      }
      if (name == "atoreal" && children.size() == 1) {
        FailureOr<Value> input = receiver();
        if (failed(input))
          return failure();
        return result(sim::SimStringParseRealOp::create(
            builder, location, builder.getF64Type(), *input));
      }
      if ((name == "itoa" || name == "hextoa" || name == "octtoa" ||
           name == "bintoa") &&
          children.size() == 2) {
        FailureOr<Value> destination =
            lowerExpression(children.front(), true);
        FailureOr<Value> input = integerArgument(1, builder.getI64Type());
        if (failed(destination) || failed(input))
          return failure();
        unsigned radix = name == "bintoa"   ? 2
                         : name == "octtoa" ? 8
                         : name == "hextoa" ? 16
                                            : 10;
        Value updated = sim::SimStringFormatIntegerOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input, builder.getI32IntegerAttr(radix),
            builder.getBoolAttr(name == "itoa" &&
                                isSignedNode(children[1])));
        if (failed(storeReference(*destination, updated, location)))
          return failure();
        return arith::ConstantOp::create(
                   builder, location, builder.getI1Type(),
                   builder.getBoolAttr(false))
            .getResult();
      }
      if (name == "realtoa" && children.size() == 2) {
        FailureOr<Value> destination =
            lowerExpression(children.front(), true);
        FailureOr<Value> input = lowerExpression(children[1]);
        if (failed(destination) || failed(input))
          return failure();
        FailureOr<Value> real =
            convert(*input, builder.getF64Type(), false, location);
        if (failed(real))
          return failure();
        Value updated = sim::SimStringFormatRealOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *real);
        if (failed(storeReference(*destination, updated, location)))
          return failure();
        return arith::ConstantOp::create(
                   builder, location, builder.getI1Type(),
                   builder.getBoolAttr(false))
            .getResult();
      }
      if (name == "putc" && children.size() == 3) {
        FailureOr<Value> destination =
            lowerExpression(children.front(), true);
        FailureOr<Value> index = integerArgument(1, builder.getI64Type());
        FailureOr<Value> character = integerArgument(2, builder.getI8Type());
        if (failed(destination) || failed(index) || failed(character))
          return failure();
        FailureOr<Value> input = loadReference(*destination, location);
        if (failed(input))
          return failure();
        Value updated = sim::SimStringPutcOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input, *index, *character);
        if (failed(storeReference(*destination, updated, location)))
          return failure();
        return arith::ConstantOp::create(
                   builder, location, builder.getI1Type(),
                   builder.getBoolAttr(false))
            .getResult();
      }
      emitError(location) << "unsupported string built-in method " << name;
      return failure();
    }
  }
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
        arguments.push_back(cloneSequentialValue(*converted, location));
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
      else if (auto ref = dyn_cast<sim::ReferencePathType>(
                   (*destinationRef).getType()))
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
      arguments.push_back(cloneSequentialValue(initial, location));
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
      if (isa<semantic::DynArrayType, semantic::QueueType,
              semantic::AssocArrayType, sim::DynamicArrayType, sim::QueueType,
              sim::AssocArrayType>(formalType)) {
        emitError(location)
            << "DPI-C dynamic-array, queue, and associative-array "
               "marshalling is unsupported";
        return failure();
      }
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
      operands.push_back(cloneSequentialValue(*converted, location));
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
      if (directTask) {
        if (!isa<sim::RefType>((*destination).getType())) {
          emitError(location) << "task ref actual must be directly addressable";
          return failure();
        }
        operands.push_back(*destination);
        continue;
      }
      FailureOr<Value> argument =
          toArgumentReference(*destination, formalType, location);
      if (failed(argument)) {
        emitError(location)
            << "ref actual type must exactly match the formal type";
        return failure();
      }
      operands.push_back(*argument);
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
    operands.push_back(cloneSequentialValue(initial, location));
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
      if (failed(initializeObjectRandomStream(receiver, location)))
        return failure();
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
    receiver = sim::SimClassAllocOp::create(
        builder, location, *resultType,
        function.getBody().front().getArgument(0));
    if (failed(initializeObjectRandomStream(receiver, location)))
      return failure();
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
      arguments.push_back(cloneSequentialValue(*converted, location));
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
    else if (auto ref =
                 dyn_cast<sim::ReferencePathType>((*destinationRef).getType()))
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
    arguments.push_back(cloneSequentialValue(initial, location));
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

LogicalResult
UnitLowering::initializeObjectRandomStream(Value object, Location location) {
  auto objectType = dyn_cast<sim::ClassHandleType>(object.getType());
  if (!objectType)
    return failure();
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
  auto incrementField =
      declaration ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                        "obelisk_sim.random_increment_field")
                  : FlatSymbolRefAttr{};
  if (!declaration || !stateField || !incrementField) {
    emitError(location) << "class hierarchy has no inline random stream";
    return failure();
  }

  Value context = function.getBody().front().getArgument(0);
  Value state =
      sim::SimRandomNextOp::create(builder, location, builder.getI64Type(),
                                   context);
  Value increment =
      sim::SimRandomNextOp::create(builder, location, builder.getI64Type(),
                                   context);
  Value one = arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                        builder.getI64IntegerAttr(1));
  increment = arith::OrIOp::create(builder, location, increment, one);
  Type referenceType = sim::ManagedRefType::get(
      function.getContext(), builder.getI64Type(),
      objectType.getClassName());
  Value stateReference = sim::SimClassFieldRefOp::create(
      builder, location, referenceType, object, stateField);
  Value incrementReference = sim::SimClassFieldRefOp::create(
      builder, location, referenceType, object, incrementField);
  sim::SimManagedStoreOp::create(builder, location, state, stateReference);
  sim::SimManagedStoreOp::create(builder, location, increment,
                                 incrementReference);
  return success();
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

  if (name == "$urandom" || name == "$srandom") {
    constexpr size_t maximum = 1;
    size_t minimum = name == "$urandom" ? 0 : 1;
    if (children.size() < minimum || children.size() > maximum) {
      emitError(location) << name
                          << (name == "$urandom"
                                  ? " accepts zero or one seed argument"
                                  : " requires exactly one seed argument");
      return failure();
    }
    if (!children.empty()) {
      FailureOr<Value> seed32 = lowerInteger(children.front(), i32);
      if (failed(seed32))
        return failure();
      Value seed = arith::ExtUIOp::create(builder, location, i64, *seed32);
      sim::SimRandomSeedOp::create(builder, location, context, seed);
    }
    if (name == "$srandom")
      return dummyTaskResult();
    Value value =
        sim::SimRandomNextOp::create(builder, location, i64, context);
    value = arith::TruncIOp::create(builder, location, i32, value);
    return convertResult(value);
  }

  if (name == "$urandom_range") {
    if (children.empty() || children.size() > 2) {
      emitError(location)
          << "$urandom_range requires one or two arguments";
      return failure();
    }
    FailureOr<Value> first32 = lowerInteger(children[0], i32);
    if (failed(first32))
      return failure();
    Value first = arith::ExtUIOp::create(builder, location, i64, *first32);
    Value second = constant(i64, 0);
    if (children.size() == 2) {
      FailureOr<Value> second32 = lowerInteger(children[1], i32);
      if (failed(second32))
        return failure();
      second = arith::ExtUIOp::create(builder, location, i64, *second32);
    }
    Value firstBelow = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, first, second);
    Value low =
        arith::SelectOp::create(builder, location, firstBelow, first, second);
    Value high =
        arith::SelectOp::create(builder, location, firstBelow, second, first);
    Value extent = arith::SubIOp::create(builder, location, high, low);
    extent =
        arith::AddIOp::create(builder, location, extent, constant(i64, 1));
    Value draw = sim::SimRandomBoundedOp::create(
        builder, location, i64, context, extent);
    Value value = arith::AddIOp::create(builder, location, low, draw);
    return convertResult(value);
  }

  if (name == "$random") {
    if (children.size() > 1) {
      emitError(location) << "$random accepts zero or one seed argument";
      return failure();
    }
    FailureOr<Value> seedDestination = failure();
    if (!children.empty()) {
      seedDestination = lowerExpression(children.front(), true);
      if (failed(seedDestination)) {
        emitError(getSemanticLocation(children.front()))
            << "$random seed must be a writable integral variable";
        return failure();
      }
      FailureOr<Value> seedValue =
          loadReference(*seedDestination, getSemanticLocation(children.front()));
      if (failed(seedValue))
        return failure();
      FailureOr<Value> seed32 =
          convert(*seedValue, i32, isSignedNode(children.front()), location);
      if (failed(seed32))
        return failure();
      Value seed = arith::ExtUIOp::create(builder, location, i64, *seed32);
      sim::SimRandomSeedOp::create(builder, location, context, seed);
    }
    Value value =
        sim::SimRandomNextOp::create(builder, location, i64, context);
    value = arith::TruncIOp::create(builder, location, i32, value);
    if (succeeded(seedDestination)) {
      Type destinationType = getReferenceElementType(*seedDestination);
      FailureOr<Value> updated =
          convert(value, destinationType, true, location);
      if (failed(updated) ||
          failed(storeReference(*seedDestination, *updated, location)))
        return failure();
    }
    return convertResult(value);
  }

  if (name == "$sampled") {
    if (children.size() != 1) {
      emitError(location) << "$sampled requires exactly one argument";
      return failure();
    }
    emitError(location)
        << "$sampled requires concurrent assertion Preponed sampling, which "
           "is not executable yet";
    return failure();
  }

  if (name == "$past") {
    if (children.empty() || children.size() > 4) {
      emitError(location) << "$past requires one to four arguments";
      return failure();
    }
    if (children.size() >= 2 && !getConstantSpelling(children[1])) {
      emitError(getSemanticLocation(children[1]))
          << "$past history depth must be a constant integer";
      return failure();
    }
    if (children.size() >= 3) {
      emitError(getSemanticLocation(children[2]))
          << "$past gating expressions are not supported";
      return failure();
    }
    if (children.size() >= 4) {
      emitError(getSemanticLocation(children[3]))
          << "$past alternate clock arguments are not supported";
      return failure();
    }
    emitError(location)
        << "$past requires assertion-clock history, which is unavailable for "
           "this operand";
    return failure();
  }

  if (name == "$rose" || name == "$fell" || name == "$stable" ||
      name == "$changed") {
    if (children.size() != 1) {
      emitError(location) << name << " requires exactly one argument";
      return failure();
    }
    emitError(location)
        << name
        << " requires assertion-clock history, which is unavailable for this "
           "operand";
    return failure();
  }

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

    SmallVector<Value> values;
    values.reserve(dimensions.size());
    for (auto [dimensionIndex, dimension] : llvm::enumerate(dimensions)) {
      if (dimension.kind == SemanticDimensionKind::AssociativeArray &&
          dimensionIndex == 0) {
        FailureOr<Type> normalizedIndex =
            normalizeSemanticType(dimension.indexType, location);
        if (failed(normalizedIndex))
          return failure();
        if (isa<sim::StringType>(*normalizedIndex)) {
          emitError(getSemanticLocation(children.front()))
              << name
              << " is not defined for string-key associative arrays";
          return failure();
        }
        FailureOr<Value> container = lowerExpression(children.front());
        if (failed(container) ||
            !isa<sim::AssocArrayType>((*container).getType())) {
          emitError(getSemanticLocation(children.front()))
              << name << " requires an associative-array value";
          return failure();
        }
        auto associative =
            cast<sim::AssocArrayType>((*container).getType());
        if (children.size() != 1) {
          emitError(location)
              << "a dimension selector is not supported for associative "
                 "array queries";
          return failure();
        }
        if (name == "$size") {
          Value size = sim::SimContainerSizeOp::create(
              builder, location, builder.getI64Type(), *container);
          return convertResult(size);
        }
        if (name == "$left")
          return convertResult(arith::ConstantOp::create(
              builder, location, i32, builder.getI32IntegerAttr(0)));
        if (name == "$right")
          return convertResult(arith::ConstantOp::create(
              builder, location, i32, builder.getI32IntegerAttr(-1)));
        if (name == "$increment")
          return convertResult(arith::ConstantOp::create(
              builder, location, i32, builder.getI32IntegerAttr(-1)));

        Value defaultKey = createDefaultValue(
            builder, location, associative.getKeyType());
        bool first = name == "$low";
        FailureOr<std::pair<Value, Value>> traversed = traverseAssoc(
            *container, defaultKey, first ? 1 : -1, true, location);
        if (failed(traversed))
          return failure();
        FailureOr<Type> queryType = getNormalizedSemanticType(op);
        if (failed(queryType))
          return failure();
        FailureOr<Value> key = convert(
            traversed->first, *queryType, associative.getSignedKey(),
            location, true);
        if (failed(key))
          return failure();
        Value empty;
        if (auto logic = dyn_cast<sim::LogicType>(*queryType)) {
          Type plane = builder.getIntegerType(logic.getWidth());
          empty = sim::SimLogicConstantOp::create(
              builder, location, logic,
              builder.getIntegerAttr(plane, APInt(logic.getWidth(), 0)),
              builder.getIntegerAttr(
                  plane, APInt::getAllOnes(logic.getWidth())));
        } else {
          empty = createDefaultValue(builder, location, *queryType);
        }
        return arith::SelectOp::create(
                   builder, location, traversed->second, *key, empty)
            .getResult();
      }
      if ((dimension.kind == SemanticDimensionKind::DynamicArray ||
           dimension.kind == SemanticDimensionKind::Queue) &&
          dimensionIndex == 0) {
        FailureOr<Value> container = lowerExpression(children.front());
        if (failed(container) || !isa<sim::DynamicArrayType, sim::QueueType>(
                                     (*container).getType())) {
          emitError(getSemanticLocation(children.front()))
              << name << " requires a sequential container value";
          return failure();
        }
        Value runtimeSize = sim::SimContainerSizeOp::create(
            builder, location, builder.getI64Type(), *container);
        Value queried;
        if (name == "$left" || name == "$low")
          queried = arith::ConstantOp::create(builder, location, i32,
                                              builder.getI32IntegerAttr(0));
        else if (name == "$increment")
          queried = arith::ConstantOp::create(builder, location, i32,
                                              builder.getI32IntegerAttr(-1));
        else {
          if (name != "$size") {
            Value one = arith::ConstantOp::create(builder, location,
                                                  builder.getI64Type(),
                                                  builder.getI64IntegerAttr(1));
            runtimeSize =
                arith::SubIOp::create(builder, location, runtimeSize, one);
          }
          queried =
              arith::TruncIOp::create(builder, location, i32, runtimeSize);
        }
        values.push_back(queried);
        continue;
      }
      std::optional<APInt> value = queryValue(dimension);
      if (!value) {
        emitError(getSemanticLocation(children.front()))
            << name
            << " requires the runtime value of a dynamically sized object";
        return failure();
      }
      values.push_back(arith::ConstantOp::create(
          builder, location, i32, builder.getIntegerAttr(i32, *value)));
    }

    if (children.size() == 1) {
      return convertResult(values.front());
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
      Value queryResult =
          sim::SimLogicFromBitsOp::create(builder, location, resultType, value);
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
        if (isa<FloatType>((*value).getType())) {
          FailureOr<Value> real =
              convert(*value, builder.getF64Type(), false,
                      getSemanticLocation(child));
          if (failed(real))
            return failure();
          items.push_back(*real);
          flags.push_back(4);
        } else if (isa<sim::StringType>((*value).getType())) {
          items.push_back(*value);
          flags.push_back(8);
        } else if (isa<sim::DynamicArrayType, sim::QueueType,
                       sim::AssocArrayType>(
                       (*value).getType())) {
          items.push_back(*value);
          flags.push_back(16);
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
    Value descriptor;
    bool literalPath = static_cast<bool>(getStringLiteral(children[0]));
    bool literalMode =
        children.size() == 2 &&
        static_cast<bool>(getStringLiteral(children[1]));
    if (literalPath && (children.size() == 1 || literalMode)) {
      FailureOr<Value> path = lowerBytes(children[0]);
      if (failed(path))
        return failure();
      if (children.size() == 1)
        descriptor = sim::SimFileOpenMCDOp::create(
                         builder, location, i32, context, *path)
                         .getDescriptor();
      else {
        FailureOr<Value> mode = lowerBytes(children[1]);
        if (failed(mode))
          return failure();
        descriptor = sim::SimFileOpenOp::create(
                         builder, location, i32, context, *path, *mode)
                         .getDescriptor();
      }
    } else {
      FailureOr<Value> pathValue = lowerExpression(children[0]);
      if (failed(pathValue))
        return failure();
      Type stringType = sim::StringType::get(function.getContext());
      FailureOr<Value> path =
          convert(*pathValue, stringType, isSignedNode(children[0]), location);
      if (failed(path))
        return failure();
      if (children.size() == 1)
        descriptor = sim::SimFileOpenStringMCDOp::create(
                         builder, location, i32, context, *path)
                         .getDescriptor();
      else {
        FailureOr<Value> modeValue = lowerExpression(children[1]);
        if (failed(modeValue))
          return failure();
        FailureOr<Value> mode =
            convert(*modeValue, stringType, isSignedNode(children[1]), location);
        if (failed(mode))
          return failure();
        descriptor = sim::SimFileOpenStringOp::create(
                         builder, location, i32, context, *path, *mode)
                         .getDescriptor();
      }
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
    if (name == "$fgets" &&
        isa<sim::StringType>(reference.getElementType())) {
      auto read = sim::SimFileGetlineStringOp::create(
          builder, location,
          TypeRange{sim::StringType::get(function.getContext()), i32}, context,
          *descriptor);
      sim::SimRefStoreOp::create(builder, location, read.getData(),
                                 *destination);
      return convertResult(read.getCount());
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
  if (isa<semantic::SVLValueReferenceExpressionOp>(op)) {
    if (lvalueReferencePlaceholder)
      return lvalueReferencePlaceholder;
    emitError(getSemanticLocation(op))
        << "lvalue-reference placeholder has no resolved value";
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
  if (isa<semantic::SVStringLiteralOp>(op)) {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return lowerStringLiteralValue(builder, op, *type,
                                   getSemanticLocation(op));
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
    return arith::ConstantOp::create(
               builder, location, *type, builder.getFloatAttr(*type, value))
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
    Value copy =
        sim::SimClassCopyOp::create(
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
  if (auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(op))
    return lowerMember(member, lvalue);
  if (auto tagged = dyn_cast<semantic::SVTaggedUnionExpressionOp>(op))
    return lowerTaggedUnion(tagged);
  if (isa<semantic::SVSimpleAssignmentPatternExpressionOp,
          semantic::SVStructuredAssignmentPatternExpressionOp>(op))
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
  if (auto conditional =
          dyn_cast<semantic::SVConditionalExpressionOp>(op))
    return lowerConditionalExpression(conditional);
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

  if (isIntegerConstant(children.front())) {
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
  if (isa<FloatType>((*amount).getType())) {
    auto quantumAttr =
        function->getAttrOfType<IntegerAttr>(delayQuantumAttrName);
    if (!quantumAttr) {
      function.emitError("code unit has no frozen delay quantum");
      return failure();
    }
    FailureOr<Value> real =
        convert(*amount, builder.getF64Type(), false, location);
    if (failed(real))
      return failure();
    return sim::SimTimeFromRealOp::create(
               builder, location, sim::TimeType::get(function.getContext()),
               *real, scaleAttr, quantumAttr)
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

void UnitLowering::emitDefaultAssertionFailure(Location location) {
  std::string file = "<unknown>";
  unsigned line = 0;
  if (auto source = location->findInstanceOf<FileLineColLoc>()) {
    file = source.getFilename().str();
    line = source.getLine();
  }
  std::string message =
      (Twine("ERROR: ") + file + ":" + Twine(line) +
       ": immediate assertion failed.")
          .str();
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
  sim::SimDisplayOp::create(
      builder, location, context, descriptor, ValueRange{item}, true, 10,
      builder.getDenseI32ArrayAttr({0}), scope, StringAttr{}, timeMultiplier);
}

LogicalResult UnitLowering::lowerImmediateAssertion(
    semantic::SVImmediateAssertionStatementOp op) {
  Location location = getSemanticLocation(op);
  if (op->hasAttr("obelisk_sim.default_assertion_failure")) {
    emitDefaultAssertionFailure(location);
    return success();
  }
  if (op.getIsDeferred()) {
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string identity =
        (function.getSymName() + ".$deferred_assert." + Twine(node) + "." +
         Twine(node ? 0 : nextForkOrdinal))
            .str();
    uint64_t siteID = stableCodeUnitID(identity);
    Value first = sim::SimDeferredOnceOp::create(
        builder, location, builder.getI64IntegerAttr(siteID));
    Block *schedule = addBlock();
    Block *merge = addBlock();
    cf::CondBranchOp::create(builder, location, first, schedule, ValueRange{},
                             merge, ValueRange{});
    setCurrent(schedule);
    Attribute previousCodeUnit = op->getAttr("obelisk_sim.fork_code_unit_id");
    BoolAttr previousDeferred = op.getIsDeferredAttr();
    op->setAttr("obelisk_sim.deferred_evaluator", builder.getUnitAttr());
    op->setAttr("obelisk_sim.fork_code_unit_id",
                builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    op->setAttr("is_deferred", builder.getBoolAttr(false));
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(op, node, 0, /*captureReferences=*/true);
    op->setAttr("is_deferred", previousDeferred);
    op->removeAttr("obelisk_sim.deferred_evaluator");
    if (previousCodeUnit)
      op->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
    else
      op->removeAttr("obelisk_sim.fork_code_unit_id");
    if (failed(callback))
      return failure();

    sim::EventRegion region = op.getIsFinal() ? sim::EventRegion::Postponed
                                              : sim::EventRegion::Observed;
    callback->first->setAttr("home_region", sim::EventRegionAttr::get(
                                                function.getContext(), region));
    callback->first->setAttr(
        "domain", sim::ExecutionDomainAttr::get(function.getContext(),
                                                 sim::ExecutionDomain::Design));
    sim::SimSpawnOp::create(builder, location, callback->first.getSymNameAttr(),
                            callback->second, ArrayAttr{}, ArrayAttr{});
    emitBranch(merge);
    setCurrent(merge);
    return success();
  }

  SmallVector<Operation *> children = getChildren(op);
  size_t expected = 1 + static_cast<size_t>(op.getHasPassAction()) +
                    static_cast<size_t>(op.getHasFailAction());
  if (children.size() != expected) {
    emitError(location) << "malformed immediate assertion inventory";
    return failure();
  }

  FailureOr<Value> value = lowerExpression(children.front());
  if (failed(value))
    return failure();
  FailureOr<Value> condition = truthValue(*value, location);
  if (failed(condition))
    return failure();

  bool reactiveActions =
      op->hasAttr("obelisk_sim.deferred_evaluator") && !op.getIsFinal();
  auto lowerAction = [&](Operation *action, unsigned branch) -> LogicalResult {
    if (!reactiveActions)
      return lowerStatement(action);
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string identity =
        (function.getSymName() + ".$reactive_assert_action." + Twine(node) +
         "." + Twine(branch))
            .str();
    Attribute previousCodeUnit =
        action->getAttr("obelisk_sim.fork_code_unit_id");
    action->setAttr("obelisk_sim.fork_code_unit_id",
                    builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(action, node, branch, /*captureReferences=*/true);
    if (previousCodeUnit)
      action->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
    else
      action->removeAttr("obelisk_sim.fork_code_unit_id");
    if (failed(callback))
      return failure();
    callback->first->setAttr(
        "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Reactive));
    sim::SimSpawnOp::create(builder, getSemanticLocation(action),
                            callback->first.getSymNameAttr(), callback->second,
                            ArrayAttr{}, ArrayAttr{});
    return success();
  };
  auto lowerDefaultFailure = [&]() -> LogicalResult {
    if (!reactiveActions) {
      emitDefaultAssertionFailure(location);
      return success();
    }
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string identity =
        (function.getSymName() + ".$reactive_assert_default." + Twine(node))
            .str();
    Attribute previousCodeUnit =
        op->getAttr("obelisk_sim.fork_code_unit_id");
    op->setAttr("obelisk_sim.default_assertion_failure",
                builder.getUnitAttr());
    op->setAttr("obelisk_sim.fork_code_unit_id",
                builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(op, node, 3, /*captureReferences=*/true);
    op->removeAttr("obelisk_sim.default_assertion_failure");
    if (previousCodeUnit)
      op->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
    else
      op->removeAttr("obelisk_sim.fork_code_unit_id");
    if (failed(callback))
      return failure();
    callback->first->setAttr(
        "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Reactive));
    callback->first->setAttr(
        "domain", sim::ExecutionDomainAttr::get(function.getContext(),
                                                 sim::ExecutionDomain::Design));
    sim::SimSpawnOp::create(builder, location,
                            callback->first.getSymNameAttr(), callback->second,
                            ArrayAttr{}, ArrayAttr{});
    return success();
  };

  Block *passBlock = addBlock();
  Block *failBlock = addBlock();
  Block *mergeBlock = addBlock();
  cf::CondBranchOp::create(builder, location, *condition, passBlock,
                           ValueRange{}, failBlock, ValueRange{});

  size_t nextChild = 1;
  setCurrent(passBlock);
  bool cover =
      op.getAssertionKind() == semantic::SVAssertionKind::CoverProperty ||
      op.getAssertionKind() == semantic::SVAssertionKind::CoverSequence;
  if (op.getHasPassAction() && failed(lowerAction(children[nextChild++], 0)))
    return failure();
  emitBranch(mergeBlock);

  setCurrent(failBlock);
  if (!cover) {
    if (op.getHasFailAction()) {
      if (failed(lowerAction(children[nextChild++], 1)))
        return failure();
    } else if (op.getAssertionKind() == semantic::SVAssertionKind::Assert &&
               failed(lowerDefaultFailure()))
      return failure();
  }
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
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
  bool selectorString = isa<sim::StringType>(selectorType);
  bool selectorLogic =
      !selectorString &&
      isa<sim::LogicType>(sim::getPackedScalarType(selectorType));
  auto compareValue =
      [&](Value candidate, sim::CompareKind logicKind,
          arith::CmpIPredicate integerKind) -> FailureOr<Value> {
    FailureOr<Value> normalized =
        convert(candidate, selectorType, false, location);
    if (failed(normalized))
      return failure();
    if (selectorString) {
      Value comparison = sim::SimStringCompareOp::create(
          builder, location, builder.getI32Type(), selector, *normalized,
          builder.getBoolAttr(false));
      arith::CmpIPredicate predicate = arith::CmpIPredicate::eq;
      switch (integerKind) {
      case arith::CmpIPredicate::sge:
      case arith::CmpIPredicate::uge:
        predicate = arith::CmpIPredicate::sge;
        break;
      case arith::CmpIPredicate::sle:
      case arith::CmpIPredicate::ule:
        predicate = arith::CmpIPredicate::sle;
        break;
      default:
        break;
      }
      return arith::CmpIOp::create(
                 builder, location, predicate, comparison,
                 arith::ConstantOp::create(builder, location,
                                           builder.getI32Type(),
                                           builder.getI32IntegerAttr(0)))
          .getResult();
    }
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
  if (!sim::getPackedScalarType((*selector).getType()) &&
      !isa<sim::StringType>((*selector).getType())) {
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

    std::function<LogicalResult(unsigned, Value, Block *)> emitDimension =
        [&](unsigned dimensionIndex, Value currentCollection,
            Block *parentStep) -> LogicalResult {
      if (dimensionIndex == dimensions.size()) {
        if (!parentStep) {
          emitError(location) << "runtime foreach has no iterated dimension";
          return failure();
        }
        loopTargets.push_back({exit, parentStep, {}, controlScopes.size()});
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
                               parentStep);
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
        cf::BranchOp::create(
            builder, location, header,
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
              convert(key, dimension.iteratorType,
                      associative.getSignedKey(), location, true);
          if (failed(iterator))
            return failure();
          values[dimension.iteratorPath] = *iterator;
        }

        Value nestedCollection = currentCollection;
        if (dimensionIndex + 1 != dimensions.size())
          nestedCollection = sim::SimAssocReadOp::create(
              builder, location, associative.getElementType(),
              currentCollection, key);
        if (failed(emitDimension(dimensionIndex + 1, nestedCollection, step)))
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
        cf::BranchOp::create(
            builder, location, header,
            ValueRange{next->first, next->second});
        setCurrent(localExit);
        if (parentStep)
          emitBranch(parentStep);
        return success();
      }

      Value count;
      if (dimension.runtime) {
        if (!isa<sim::DynamicArrayType, sim::QueueType>(
                currentCollection.getType())) {
          emitError(location)
              << "runtime foreach dimension is not a sequential container";
          return failure();
        }
        count = sim::SimContainerSizeOp::create(builder, location, indexType,
                                                currentCollection);
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
      if (failed(emitDimension(dimensionIndex + 1, nestedCollection, step)))
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
        emitBranch(parentStep);
      return success();
    };

    if (failed(emitDimension(0, *collection, nullptr)))
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
  if (auto path =
          op->getAttrOfType<StringAttr>("obelisk_sim.initialize_net")) {
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
    FailureOr<Value> converted =
        convert(*value, driverType.getElementType(), isSignedNode(op), location);
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
  if (isa<semantic::SVConcurrentAssertionStatementOp>(op)) {
    emitError(location)
        << "concurrent assertions require typed Preponed sampling and a "
           "verified temporal monitor, which are not executable yet";
    return failure();
  }
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
