//===- PrepareCoverage.cpp - Build the native coverage schema ------------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Coverage/CoverageDatabase.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "CoverageOrdering.h"
#include "Detail.h"
#include "LowerUnit.h"
#include "obelisk/Analysis/NativeStorageLayout.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPREPARECOVERAGEPASS
#include "obelisk/Conversion/Passes.h.inc"

// Functional container schemas record the runtime associative key kind.
static_assert(
    static_cast<uint32_t>(coverage::FunctionalContainerKeyKind::Unsigned) ==
    static_cast<uint32_t>(sim::AssocKeyKind::Unsigned));
static_assert(
    static_cast<uint32_t>(coverage::FunctionalContainerKeyKind::Signed) ==
    static_cast<uint32_t>(sim::AssocKeyKind::Signed));
static_assert(
    static_cast<uint32_t>(coverage::FunctionalContainerKeyKind::String) ==
    static_cast<uint32_t>(sim::AssocKeyKind::String));
static_assert(
    static_cast<uint32_t>(coverage::FunctionalContainerKeyKind::Class) ==
    static_cast<uint32_t>(sim::AssocKeyKind::Class));
static_assert(
    static_cast<uint32_t>(coverage::FunctionalContainerKeyKind::Process) ==
    static_cast<uint32_t>(sim::AssocKeyKind::Process));
static_assert(
    static_cast<uint32_t>(coverage::FunctionalContainerKeyKind::Wildcard) ==
    static_cast<uint32_t>(sim::AssocKeyKind::Wildcard));
static_assert(
    static_cast<uint32_t>(coverage::FunctionalContainerKeyKind::Element) ==
    static_cast<uint32_t>(sim::AssocKeyKind::Element));

namespace {

using CrossPredicateKey = std::pair<Operation *, Operation *>;

struct PlannedCrossPredicate {
  CrossPredicateKey key;
  Operation *expression = nullptr;
  coverage::CrossPredicateKind kind;
  SmallVector<Operation *> operands;
  SmallVector<CrossPredicateKey> operandKeys;
  uint32_t bitWidth = 0;
  coverage::CoverageSignedness signedness;
  uint32_t flags = 0, targetOrdinal = 0;
  uint32_t elementWidth = 0;
  int64_t indexBase = 0;
};

// Iterator-free subtrees are ordinary typed constructor expressions. The
// dependent part is a topological DAG; no recursive expansion or value-domain
// enumeration is needed to inventory it.
std::optional<SmallVector<PlannedCrossPredicate>>
planCrossPredicate(Operation *predicate, ArrayAttr targetPaths) {
  using Kind = coverage::CrossPredicateKind;
  llvm::StringMap<uint32_t> targets;
  for (auto [ordinal, path] : llvm::enumerate(targetPaths))
    targets.try_emplace(cast<StringAttr>(path).getValue(), ordinal);
  llvm::DenseMap<Operation *, bool> dependent;
  bool hasReplication = false, hasMembership = false;
  predicate->walk<WalkOrder::PostOrder>([&](Operation *op) {
    hasReplication |= isa<ir::SVReplicationExpressionOp>(op);
    hasMembership |= isa<ir::SVInsideExpressionOp>(op);
    if (auto binary = dyn_cast<ir::SVBinaryExpressionOp>(op))
      hasMembership |=
          binary.getOperatorKind() == ir::SVBinaryOperator::WildcardEquality ||
          binary.getOperatorKind() == ir::SVBinaryOperator::WildcardInequality;
    bool iterator = false;
    if (auto reference = dyn_cast<ir::SVNamedValueExpressionOp>(op))
      iterator = targets.contains(reference.getReferencedPath());
    for (Operation *child : simlowering::getChildren(op))
      iterator |= dependent.lookup(child);
    dependent[op] = iterator;
  });
  SmallVector<std::pair<CrossPredicateKey, bool>> pending{
      {{predicate, nullptr}, false}};
  SmallVector<PlannedCrossPredicate> result;
  llvm::DenseMap<CrossPredicateKey, PlannedCrossPredicate> plans;
  auto addPlan = [&](PlannedCrossPredicate plan) {
    for (Operation *operand : plan.operands)
      plan.operandKeys.emplace_back(operand, nullptr);
    plan.operands.clear();
    plans.try_emplace(plan.key, plan);
    pending.emplace_back(plan.key, true);
    for (CrossPredicateKey child : llvm::reverse(plan.operandKeys))
      pending.emplace_back(child, false);
  };
  while (!pending.empty()) {
    auto [key, ready] = pending.pop_back_val();
    Operation *op = key.first;
    if (ready) {
      result.push_back(plans.lookup(key));
      continue;
    }
    if (plans.contains(key))
      continue;
    // Membership clauses share the original selector expression. A separate
    // key identifies its Boolean comparison without cloning the source tree
    // or changing the type of the member expression used by the comparison.
    if (key.second) {
      PlannedCrossPredicate clause;
      clause.key = key;
      clause.expression = op;
      clause.bitWidth = 1;
      clause.signedness = coverage::CoverageSignedness::Unsigned;
      clause.operands.push_back(key.second);
      if (auto range = dyn_cast<ir::SVValueRangeExpressionOp>(op)) {
        auto bounds = simlowering::getChildren(op);
        if (bounds.size() != 2 ||
            range.getRangeKind() == ir::SVValueRangeKind::RelativeTolerance)
          return std::nullopt;
        clause.kind = Kind::InsideRange;
        if (range.getRangeKind() == ir::SVValueRangeKind::AbsoluteTolerance)
          clause.flags |= coverage::CrossPredicateAbsoluteTolerance;
        for (auto [index, bound] : llvm::enumerate(bounds)) {
          if (simlowering::isUnboundedEndpoint(bound)) {
            if (clause.flags & coverage::CrossPredicateAbsoluteTolerance)
              return std::nullopt;
            clause.flags |= index ? coverage::CrossPredicateUpperUnbounded
                                  : coverage::CrossPredicateLowerUnbounded;
          } else
            clause.operands.push_back(bound);
        }
      } else {
        clause.kind = Kind::WildcardEqual;
        clause.operands.push_back(op);
      }
      addPlan(std::move(clause));
      continue;
    }
    auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
    const bool zeroReplication = isa<ir::SVReplicationExpressionOp>(op) &&
                                 semanticType &&
                                 isa<ir::VoidType>(semanticType.getValue());
    FailureOr<Type> normalized =
        zeroReplication ? FailureOr<Type>(failure())
                        : simlowering::getNormalizedSemanticType(op);
    Type scalar =
        succeeded(normalized) ? sim::getPackedScalarType(*normalized) : Type{};
    std::optional<unsigned> width = zeroReplication ? std::optional<unsigned>(0)
                                    : scalar ? sim::getPackedWidth(scalar)
                                             : std::nullopt;
    if (!width || (!zeroReplication &&
                   (!*width || !isa<IntegerType, sim::LogicType>(scalar))))
      return std::nullopt;
    PlannedCrossPredicate plan;
    plan.key = key;
    plan.expression = op;
    plan.bitWidth = *width;
    plan.signedness = semanticType && simlowering::isSignedSemanticType(
                                          semanticType.getValue())
                          ? coverage::CoverageSignedness::Signed
                          : coverage::CoverageSignedness::Unsigned;
    plan.flags =
        !zeroReplication && semanticType &&
                !simlowering::isFourStateSemanticType(semanticType.getValue())
            ? coverage::CrossPredicateTwoState
            : 0;
    if (!dependent.lookup(op) && !zeroReplication) {
      plan.kind = Kind::Value;
    } else if (auto reference = dyn_cast<ir::SVNamedValueExpressionOp>(op)) {
      auto target = targets.find(reference.getReferencedPath());
      if (target == targets.end())
        return std::nullopt;
      plan.kind = Kind::Target;
      plan.targetOrdinal = target->second;
    } else {
      plan.operands = simlowering::getChildren(op);
      if (isa<ir::SVElementSelectExpressionOp, ir::SVRangeSelectExpressionOp>(
              op)) {
        const bool element = isa<ir::SVElementSelectExpressionOp>(op);
        if (plan.operands.size() != (element ? 2u : 3u))
          return std::nullopt;
        auto base = simlowering::getNormalizedSemanticType(plan.operands[0]);
        if (failed(base) || !sim::getPackedWidth(*base))
          return std::nullopt;
        plan.elementWidth = 1;
        if (auto array = dyn_cast<sim::PackedArrayType>(*base)) {
          auto width = sim::getPackedWidth(array.getElementType());
          if (!width || !*width)
            return std::nullopt;
          plan.elementWidth = *width;
        }
        auto type = plan.operands[0]->getAttrOfType<TypeAttr>("semantic_type");
        bool ascending = false;
        if (type) {
          if (auto range =
                  dyn_cast<ir::RangedPackedArrayType>(type.getValue())) {
            plan.indexBase = range.getRight();
            ascending = range.getLeft() < range.getRight();
          } else if (auto integer =
                         dyn_cast<ir::IntegralType>(type.getValue())) {
            plan.indexBase = integer.getRight();
            ascending = integer.getLeft() < integer.getRight();
          }
        }
        if (ascending)
          plan.flags |= coverage::CrossPredicateAscending;
        if (element) {
          plan.kind = Kind::ElementSelect;
        } else {
          auto selection =
              cast<ir::SVRangeSelectExpressionOp>(op).getSelectionKind();
          if (selection == ir::SVRangeSelectionKind::Simple)
            plan.operands[1] = plan.operands[2];
          else if ((ascending &&
                    selection == ir::SVRangeSelectionKind::IndexedUp) ||
                   (!ascending &&
                    selection == ir::SVRangeSelectionKind::IndexedDown))
            plan.flags |= coverage::CrossPredicateBaseNamesHigh;
          plan.operands.resize(2);
          plan.kind = Kind::RangeSelect;
        }
      } else if (auto inside = dyn_cast<ir::SVInsideExpressionOp>(op)) {
        if (inside.getItemCount() < 1 ||
            plan.operands.size() != uint64_t{1} + inside.getItemCount())
          return std::nullopt;
        plan.kind = Kind::Inside;
        Operation *subject = plan.operands.front();
        for (Operation *member : llvm::drop_begin(plan.operands))
          plan.operandKeys.emplace_back(member, subject);
        plan.operands.clear();
      } else if (auto conditional =
                     dyn_cast<ir::SVConditionalExpressionOp>(op)) {
        if (!conditional.getConditionCount() ||
            conditional.getConditionPatternFlags().size() !=
                conditional.getConditionCount() ||
            llvm::any_of(conditional.getConditionPatternFlags(),
                         [](int64_t flag) { return flag != 0; }) ||
            plan.operands.size() != conditional.getConditionCount() + 2)
          return std::nullopt;
        plan.kind = Kind::Conditional;
      } else if (isa<ir::SVReplicationExpressionOp>(op)) {
        if (plan.operands.size() != 2)
          return std::nullopt;
        auto spelling =
            simlowering::getElaboratedConstantSpelling(plan.operands[0]);
        auto countType =
            simlowering::getNormalizedSemanticType(plan.operands[0]);
        auto inputType =
            simlowering::getNormalizedSemanticType(plan.operands[1]);
        auto countWidth = succeeded(countType) ? sim::getPackedWidth(*countType)
                                               : std::nullopt;
        auto inputWidth = succeeded(inputType) ? sim::getPackedWidth(*inputType)
                                               : std::nullopt;
        if (!spelling || !countWidth || !*countWidth || !inputWidth ||
            !*inputWidth)
          return std::nullopt;
        auto count = simlowering::parseSVInteger(
            *spelling, *countWidth,
            simlowering::getSemanticLocation(plan.operands[0]));
        auto countSemanticType =
            plan.operands[0]->getAttrOfType<TypeAttr>("semantic_type");
        if (failed(count) || !count->unknown.isZero() ||
            count->value.getActiveBits() > 32 ||
            (countSemanticType &&
             simlowering::isSignedSemanticType(countSemanticType.getValue()) &&
             count->value.isNegative()) ||
            uint64_t{*inputWidth} * count->value.getZExtValue() !=
                plan.bitWidth)
          return std::nullopt;
        // The constant multiplier is encoded by the result/input widths.
        // Inventory the payload once, including when its result has size zero.
        plan.operands.erase(plan.operands.begin());
        plan.kind = Kind::Replicate;
      } else if (isa<ir::SVConcatenationExpressionOp>(op)) {
        if (plan.operands.empty())
          return std::nullopt;
        plan.kind = Kind::Concatenate;
      } else if (isa<ir::SVConversionExpressionOp>(op)) {
        if (plan.operands.size() != 1)
          return std::nullopt;
        plan.kind = Kind::Cast;
      } else if (auto unary = dyn_cast<ir::SVUnaryExpressionOp>(op)) {
        if (plan.operands.size() != 1)
          return std::nullopt;
        switch (unary.getOperatorKind()) {
        case ir::SVUnaryOperator::Plus:
          plan.kind = Kind::Cast;
          break;
        case ir::SVUnaryOperator::Minus:
          plan.kind = Kind::Negate;
          break;
        case ir::SVUnaryOperator::LogicalNot:
          plan.kind = Kind::LogicalNot;
          break;
        case ir::SVUnaryOperator::BitwiseNot:
          plan.kind = Kind::BitNot;
          break;
        case ir::SVUnaryOperator::BitwiseAnd:
          plan.kind = Kind::ReductionAnd;
          break;
        case ir::SVUnaryOperator::BitwiseOr:
          plan.kind = Kind::ReductionOr;
          break;
        case ir::SVUnaryOperator::BitwiseXor:
          plan.kind = Kind::ReductionXor;
          break;
        case ir::SVUnaryOperator::BitwiseNand:
          plan.kind = Kind::ReductionNand;
          break;
        case ir::SVUnaryOperator::BitwiseNor:
          plan.kind = Kind::ReductionNor;
          break;
        case ir::SVUnaryOperator::BitwiseXnor:
          plan.kind = Kind::ReductionXnor;
          break;
        default:
          return std::nullopt;
        }
      } else if (auto binary = dyn_cast<ir::SVBinaryExpressionOp>(op)) {
        if (plan.operands.size() != 2)
          return std::nullopt;
        switch (binary.getOperatorKind()) {
        case ir::SVBinaryOperator::Equality:
          plan.kind = Kind::Equal;
          break;
        case ir::SVBinaryOperator::Inequality:
          plan.kind = Kind::NotEqual;
          break;
        case ir::SVBinaryOperator::CaseEquality:
          plan.kind = Kind::CaseEqual;
          break;
        case ir::SVBinaryOperator::CaseInequality:
          plan.kind = Kind::CaseNotEqual;
          break;
        case ir::SVBinaryOperator::WildcardEquality:
          plan.kind = Kind::WildcardEqual;
          break;
        case ir::SVBinaryOperator::WildcardInequality:
          plan.kind = Kind::WildcardNotEqual;
          break;
        case ir::SVBinaryOperator::LessThan:
          plan.kind = Kind::Less;
          break;
        case ir::SVBinaryOperator::LessThanEqual:
          plan.kind = Kind::LessEqual;
          break;
        case ir::SVBinaryOperator::GreaterThan:
          plan.kind = Kind::Greater;
          break;
        case ir::SVBinaryOperator::GreaterThanEqual:
          plan.kind = Kind::GreaterEqual;
          break;
        case ir::SVBinaryOperator::LogicalAnd:
          plan.kind = Kind::LogicalAnd;
          break;
        case ir::SVBinaryOperator::LogicalOr:
          plan.kind = Kind::LogicalOr;
          break;
        case ir::SVBinaryOperator::BinaryAnd:
          plan.kind = Kind::BitAnd;
          break;
        case ir::SVBinaryOperator::BinaryOr:
          plan.kind = Kind::BitOr;
          break;
        case ir::SVBinaryOperator::BinaryXor:
          plan.kind = Kind::BitXor;
          break;
        case ir::SVBinaryOperator::BinaryXnor:
          plan.kind = Kind::BitXnor;
          break;
        case ir::SVBinaryOperator::Add:
          plan.kind = Kind::Add;
          break;
        case ir::SVBinaryOperator::Subtract:
          plan.kind = Kind::Subtract;
          break;
        case ir::SVBinaryOperator::LogicalShiftLeft:
          plan.kind = Kind::ShiftLeft;
          break;
        case ir::SVBinaryOperator::LogicalShiftRight:
          plan.kind = Kind::ShiftRight;
          break;
        case ir::SVBinaryOperator::ArithmeticShiftLeft:
          plan.kind = Kind::ArithmeticShiftLeft;
          break;
        case ir::SVBinaryOperator::ArithmeticShiftRight:
          plan.kind = Kind::ArithmeticShiftRight;
          break;
        default:
          return std::nullopt;
        }
      } else
        return std::nullopt;
    }
    addPlan(std::move(plan));
  }
  if (hasReplication || hasMembership ||
      llvm::any_of(result, [](const auto &entry) {
        return entry.kind == Kind::Conditional;
      })) {
    // Capturing operands eagerly must preserve conditional execution and
    // per-tuple effects in membership and replication, including a zero
    // multiplier.
    for (const auto &entry : result) {
      if (entry.kind != Kind::Value)
        continue;
      bool effects = false;
      entry.expression->walk([&](Operation *op) -> WalkResult {
        if (isa<ir::SVCallExpressionOp, ir::SVAssignmentExpressionOp>(op))
          effects = true;
        if (auto unary = dyn_cast<ir::SVUnaryExpressionOp>(op))
          effects |=
              unary.getOperatorKind() == ir::SVUnaryOperator::Preincrement ||
              unary.getOperatorKind() == ir::SVUnaryOperator::Postincrement ||
              unary.getOperatorKind() == ir::SVUnaryOperator::Predecrement ||
              unary.getOperatorKind() == ir::SVUnaryOperator::Postdecrement;
        return effects ? WalkResult::interrupt() : WalkResult::advance();
      });
      if (effects)
        return std::nullopt;
    }
  }
  return result;
}

Operation *peelFunctionalValueCast(Operation *expression,
                                   uint64_t effectiveWidth,
                                   Type effectiveType) {
  const bool pointIsReal =
      effectiveType && isa<Float32Type, Float64Type>(effectiveType);

  auto conversion = dyn_cast<ir::SVConversionExpressionOp>(expression);
  BoolAttr implicit = expression->getAttrOfType<BoolAttr>("is_implicit");
  if (!conversion || !implicit || !implicit.getValue())
    return expression;
  FailureOr<Type> normalized =
      simlowering::getNormalizedSemanticType(expression);
  std::optional<unsigned> width =
      succeeded(normalized) ? sim::getPackedWidth(*normalized) : std::nullopt;
  SmallVector<Operation *> children = simlowering::getChildren(conversion);
  if (children.size() != 1)
    return expression;
  // Slang materializes the 19.5.7 cast to the effective
  // coverpoint type as the outer child of the bins/range
  // node. Inventory its input so runtime resolution can
  // diagnose lossy values and clip partially representable
  // ranges. Any inner expression-context conversion stays
  // intact and remains part of b's original value.
  if (width && *width == effectiveWidth)
    return children.front();
  if (pointIsReal && succeeded(normalized) && *normalized == effectiveType) {
    auto source = simlowering::getNormalizedSemanticType(children.front());
    if (succeeded(source) && (isa<Float32Type, Float64Type>(*source) ||
                              sim::getPackedWidth(*source)))
      return children.front();
  }
  return expression;
}

using simlowering::coverageOrderKey;
using simlowering::sortCoverageRecords;

class ObeliskSimPrepareCoveragePass
    : public impl::ObeliskSimPrepareCoveragePassBase<
          ObeliskSimPrepareCoveragePass> {
public:
  void runOnOperation() override;
};

struct PrefixMap {
  std::string from;
  std::string to;
};

/// Metric families selectable by --coverage, rules, and source directives,
/// indexed by coverageMetricIndex.
static constexpr unsigned kCoverageMetricCount = 5;

struct CoverageRule {
  llvm::SmallBitVector metrics =
      llvm::SmallBitVector(kCoverageMetricCount, true);
  std::optional<std::string> file;
  std::optional<std::string> hierarchy;
  std::string reason;
};

struct CoverageConfiguration {
  SmallVector<CoverageRule> includes;
  SmallVector<CoverageRule> excludes;
};

struct DirectiveInterval {
  uint32_t beginLine = 0;
  uint32_t endLine = 0;
  std::string reason;
};

struct CoverageDirectiveFile {
  std::array<SmallVector<DirectiveInterval>, kCoverageMetricCount> intervals;
};

struct ActiveDirective {
  uint32_t beginLine = 0;
  std::string reason;
};

struct SourceRange {
  std::string startFile;
  std::string endFile;
  std::string macroName;
  uint32_t startLine = 0;
  uint32_t startColumn = 0;
  uint32_t endLine = 0;
  uint32_t endColumn = 0;
};

struct MacroFrame {
  uint64_t node = 0;
  std::string name;
  std::optional<SourceRange> definition;
  std::optional<SourceRange> invocation;
};

struct SourceInfo {
  SourceRange expanded;
  std::optional<SourceRange> original;
  SmallVector<MacroFrame, 2> macroFrames;
};

struct PointCandidate {
  Operation *operation = nullptr;
  sim::SimFuncOp function;
  SourceInfo source;
  uint64_t scope = 0;
  uint64_t codeUnit = 0;
  uint64_t node = 0;
  uint32_t phase = 0;
  bool functionEntry = false;
  bool excluded = false;
  std::string hierarchy;
  coverage::LinePoint point;
};

struct ScopeInventory {
  llvm::DenseMap<uint64_t, uint64_t> ids;
  llvm::DenseMap<uint64_t, std::string> hierarchies;
  llvm::DenseMap<uint64_t, uint32_t> kinds;
};

struct ToggleBindingSegment {
  uint64_t objectLow = 0;
  uint64_t stateLow = 0;
  uint64_t width = 0;
};

struct ToggleTypePlan {
  SmallVector<ToggleBindingSegment> bindings;
  SmallVector<uint8_t> fourState;
  SmallVector<coverage::ToggleDimension> dimensions;
};

struct ToggleCandidate {
  Operation *operation = nullptr;
  Operation *canonical = nullptr;
  SourceInfo source;
  uint64_t scope = 0;
  uint64_t sourceLow = 0;
  uint64_t width = 0;
  std::string hierarchy;
  std::string name;
  Type type;
  ToggleTypePlan plan;
  bool fourState = false;
  bool excluded = false;
  coverage::ToggleObject object;
};

static void setPackedBit(SmallVectorImpl<uint8_t> &plane, uint64_t bit,
                         bool value) {
  if (!value)
    return;
  plane[bit / 8] |= uint8_t{1} << (bit % 8);
}

static bool appendBinding(SmallVectorImpl<ToggleBindingSegment> &bindings,
                          uint64_t objectLow, uint64_t stateLow,
                          uint64_t width) {
  if (!width || objectLow > UINT64_MAX - width || stateLow > UINT64_MAX - width)
    return false;
  if (!bindings.empty()) {
    ToggleBindingSegment &last = bindings.back();
    if (last.objectLow + last.width == objectLow &&
        last.stateLow + last.width == stateLow) {
      last.width += width;
      return true;
    }
  }
  bindings.push_back({objectLow, stateLow, width});
  return true;
}

static uint32_t getUnionTagBits(Type type) {
  if (auto packed = dyn_cast<sim::PackedUnionType>(type))
    return packed.getIsTagged() ? packed.getTagBits() : 0;
  if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(type)) {
    if (unpacked.getIsTagged())
      return llvm::Log2_64_Ceil(
          static_cast<uint64_t>(sim::getAggregateNumElements(type)) + 1);
  }
  return 0;
}

/// Build the dense obligation layout independently of ABI padding. Physical
/// offsets come exclusively from the shared provenance-layout API.
static FailureOr<ToggleTypePlan> buildToggleLayout(Type type,
                                                   unsigned depth = 0) {
  if (depth > 256)
    return failure();
  ToggleTypePlan result;
  if (std::optional<unsigned> width = sim::getPackedWidth(type)) {
    if (!*width || !appendBinding(result.bindings, 0, 0, *width))
      return failure();
    // IEEE 1800-2017 7.2.1 and 7.3.1 define a packed struct or union
    // containing any four-state member as one four-state vector. This also
    // makes the tag and any bits viewed through two-state members four-state.
    result.fourState.resize(*width,
                            analysis::containsFourStateLogic(type) ? 1 : 0);
    return result;
  }
  if (!sim::isAggregateType(type))
    return result;

  auto appendChild = [&](Type childType, uint64_t stateLow) -> LogicalResult {
    FailureOr<ToggleTypePlan> child = buildToggleLayout(childType, depth + 1);
    if (failed(child))
      return failure();
    uint64_t objectLow = result.fourState.size();
    if (child->fourState.size() > UINT64_MAX - objectLow)
      return failure();
    llvm::append_range(result.fourState, child->fourState);
    for (const ToggleBindingSegment &binding : child->bindings) {
      if (binding.objectLow > UINT64_MAX - objectLow ||
          binding.stateLow > UINT64_MAX - stateLow ||
          !appendBinding(result.bindings, objectLow + binding.objectLow,
                         stateLow + binding.stateLow, binding.width))
        return failure();
    }
    return success();
  };

  if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
    for (unsigned index = 0, end = sim::getAggregateNumElements(type);
         index != end; ++index) {
      std::optional<std::pair<uint64_t, uint64_t>> physical =
          sim::getAggregateProvenanceSubelement(type, index);
      if (!physical ||
          failed(appendChild(sim::getAggregateElementType(type, index),
                             physical->first)))
        return failure();
    }
    return result;
  }

  if (isa<sim::UnpackedUnionType>(type)) {
    SmallVector<std::pair<uint64_t, uint8_t>> physicalBits;
    for (unsigned index = 0, end = sim::getAggregateNumElements(type);
         index != end; ++index) {
      std::optional<std::pair<uint64_t, uint64_t>> physical =
          sim::getAggregateProvenanceSubelement(type, index);
      FailureOr<ToggleTypePlan> child = buildToggleLayout(
          sim::getAggregateElementType(type, index), depth + 1);
      if (!physical || failed(child))
        return failure();
      for (const ToggleBindingSegment &binding : child->bindings) {
        if (binding.stateLow > UINT64_MAX - physical->first ||
            binding.width > UINT64_MAX - physical->first - binding.stateLow)
          return failure();
        for (uint64_t bit = 0; bit != binding.width; ++bit)
          physicalBits.push_back({physical->first + binding.stateLow + bit,
                                  child->fourState[binding.objectLow + bit]});
      }
    }
    sortCoverageRecords(physicalBits, [](const auto &entry) {
      return coverageOrderKey(entry.first);
    });
    uint64_t previous = UINT64_MAX;
    for (const auto &[stateBit, fourState] : physicalBits) {
      if (stateBit == previous) {
        result.fourState.back() |= fourState;
        continue;
      }
      uint64_t objectBit = result.fourState.size();
      if (!appendBinding(result.bindings, objectBit, stateBit, 1))
        return failure();
      result.fourState.push_back(fourState);
      previous = stateBit;
    }
    uint32_t tagBits = getUnionTagBits(type);
    if (tagBits) {
      std::optional<uint64_t> payloadSpan = sim::getProvenanceSpan(type);
      uint64_t objectLow = result.fourState.size();
      if (!payloadSpan ||
          !appendBinding(result.bindings, objectLow, *payloadSpan, tagBits))
        return failure();
      // An unpacked tagged union uses an unknown plane exactly when one of
      // its payload types does. The default value then leaves the tag
      // undefined too (11.9), matching the aggregate lowering.
      result.fourState.resize(
          objectLow + tagBits,
          analysis::containsFourStateLogic(type) ? uint8_t{1} : uint8_t{0});
    }
    return result;
  }
  return result;
}

static uint64_t toggleWidth(Type type) {
  FailureOr<ToggleTypePlan> plan = buildToggleLayout(type);
  return succeeded(plan) ? plan->fourState.size() : 0;
}

/// Project a type-local physical layout into the root object's dense toggle
/// index space. A source member can have ABI padding or overlap another union
/// member, so declaration order and the sum of leaf widths are not sufficient
/// to determine its report span.
static FailureOr<std::pair<uint64_t, uint64_t>>
getDenseSpan(const ToggleTypePlan &rootPlan, const ToggleTypePlan &localPlan,
             uint64_t stateBase) {
  uint64_t first = UINT64_MAX;
  uint64_t last = 0;
  uint64_t mapped = 0;
  for (const ToggleBindingSegment &local : localPlan.bindings) {
    if (local.stateLow > UINT64_MAX - stateBase ||
        local.width > UINT64_MAX - stateBase - local.stateLow)
      return failure();
    const uint64_t localLow = stateBase + local.stateLow;
    const uint64_t localEnd = localLow + local.width;
    for (const ToggleBindingSegment &root : rootPlan.bindings) {
      const uint64_t rootEnd = root.stateLow + root.width;
      const uint64_t overlapLow = std::max(localLow, root.stateLow);
      const uint64_t overlapEnd = std::min(localEnd, rootEnd);
      if (overlapLow >= overlapEnd)
        continue;
      const uint64_t denseLow = root.objectLow + overlapLow - root.stateLow;
      const uint64_t denseEnd = denseLow + overlapEnd - overlapLow;
      first = std::min(first, denseLow);
      last = std::max(last, denseEnd);
      if (overlapEnd - overlapLow > UINT64_MAX - mapped)
        return failure();
      mapped += overlapEnd - overlapLow;
    }
  }
  if (!mapped || mapped != localPlan.fourState.size() || first >= last)
    return failure();
  return std::pair<uint64_t, uint64_t>{first, last - first};
}

static FailureOr<uint32_t>
appendToggleTypeTree(Type type, Type sourceType, uint64_t stateBase,
                     uint32_t parent, const ToggleTypePlan &rootPlan,
                     SmallVectorImpl<coverage::ToggleDimension> &dimensions,
                     unsigned depth = 0);

static Type getSourceElementType(Type type) {
  if (!type)
    return {};
  if (auto array = dyn_cast<ir::RangedPackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<ir::RangedUnpackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<ir::PackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<ir::UnpackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<sim::PackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type))
    return array.getElementType();
  return {};
}

static Type getCoverageSetElementType(Type type) {
  if (!type)
    return {};
  if (auto array = dyn_cast<ir::RangedPackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<ir::RangedUnpackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<ir::PackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<ir::UnpackedArrayType>(type))
    return array.getElementType();
  if (auto array = dyn_cast<ir::DynArrayType>(type))
    return array.getElementType();
  if (auto queue = dyn_cast<ir::QueueType>(type))
    return queue.getElementType();
  return {};
}

static Type getSourceFieldType(Type type, StringRef name, uint32_t ordinal) {
  if (!type)
    return {};
  if (auto aggregate = dyn_cast<ir::SourceAggregateType>(type)) {
    for (Attribute attribute : aggregate.getFields()) {
      auto field = dyn_cast<DictionaryAttr>(attribute);
      auto fieldName = field ? field.getAs<StringAttr>("name") : StringAttr{};
      auto fieldOrdinal =
          field ? field.getAs<IntegerAttr>("ordinal") : IntegerAttr{};
      auto fieldType = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
      if (fieldName && fieldOrdinal && fieldType &&
          fieldName.getValue() == name &&
          fieldOrdinal.getValue().getZExtValue() == ordinal)
        return fieldType.getValue();
    }
    return {};
  }
  ArrayAttr simulationFields;
  if (auto aggregate = dyn_cast<sim::PackedStructType>(type))
    simulationFields = aggregate.getFields();
  else if (auto aggregate = dyn_cast<sim::UnpackedStructType>(type))
    simulationFields = aggregate.getFields();
  else if (auto aggregate = dyn_cast<sim::PackedUnionType>(type))
    simulationFields = aggregate.getFields();
  else if (auto aggregate = dyn_cast<sim::UnpackedUnionType>(type))
    simulationFields = aggregate.getFields();
  if (simulationFields)
    for (Attribute attribute : simulationFields) {
      auto field = cast<sim::FieldAttr>(attribute);
      if (field.getName().getValue() == name && field.getOrdinal() == ordinal)
        return field.getType();
    }
  DictionaryAttr fields;
  if (auto aggregate = dyn_cast<ir::PackedStructType>(type))
    fields = aggregate.getFields();
  else if (auto aggregate = dyn_cast<ir::UnpackedStructType>(type))
    fields = aggregate.getFields();
  else if (auto aggregate = dyn_cast<ir::PackedUnionType>(type))
    fields = aggregate.getFields();
  else if (auto aggregate = dyn_cast<ir::UnpackedUnionType>(type))
    fields = aggregate.getFields();
  if (fields)
    if (auto field = dyn_cast_or_null<TypeAttr>(fields.get(name)))
      return field.getValue();
  return {};
}

static FailureOr<uint32_t> appendToggleField(
    StringRef name, Type type, Type sourceType, uint64_t stateBase,
    uint32_t parent, const ToggleTypePlan &rootPlan,
    SmallVectorImpl<coverage::ToggleDimension> &dimensions, unsigned depth) {
  FailureOr<ToggleTypePlan> localPlan = buildToggleLayout(type, depth + 1);
  if (failed(localPlan))
    return failure();
  FailureOr<std::pair<uint64_t, uint64_t>> span =
      getDenseSpan(rootPlan, *localPlan, stateBase);
  if (failed(span) || dimensions.size() >= UINT32_MAX)
    return failure();
  uint32_t index = dimensions.size();
  dimensions.push_back({0, parent, coverage::ToggleDimensionKind::Field,
                        UINT32_MAX, 0, 0, 0, span->first, span->second,
                        name.str(), 0});
  FailureOr<uint32_t> child = appendToggleTypeTree(
      type, sourceType, stateBase, index, rootPlan, dimensions, depth + 1);
  if (failed(child))
    return failure();
  coverage::ToggleDimension &field = dimensions[index];
  field.firstChild = *child;
  field.childCount = dimensions.size() - index - 1;
  return index;
}

static FailureOr<uint32_t>
appendToggleTypeTree(Type type, Type sourceType, uint64_t stateBase,
                     uint32_t parent, const ToggleTypePlan &rootPlan,
                     SmallVectorImpl<coverage::ToggleDimension> &dimensions,
                     unsigned depth) {
  if (depth > 256 || dimensions.size() >= UINT32_MAX)
    return failure();
  FailureOr<ToggleTypePlan> localPlan = buildToggleLayout(type, depth + 1);
  if (failed(localPlan))
    return failure();
  FailureOr<std::pair<uint64_t, uint64_t>> span =
      getDenseSpan(rootPlan, *localPlan, stateBase);
  if (failed(span))
    return failure();
  const uint64_t bitOffset = span->first;
  const uint64_t width = span->second;

  // Keep every declared enum boundary in the report tree, including enums
  // nested below arrays and aggregate fields. The normalized simulation type
  // intentionally erases that typedef identity, so recurse through the same
  // physical type after inserting a source-semantic enum node.
  if (auto enumeration = dyn_cast_or_null<ir::EnumType>(sourceType)) {
    uint32_t index = dimensions.size();
    dimensions.push_back({0, parent, coverage::ToggleDimensionKind::Enum,
                          UINT32_MAX, 0, static_cast<int64_t>(width - 1), 0,
                          bitOffset, width,
                          enumeration.getName().getValue().str(), 0});
    FailureOr<uint32_t> child =
        appendToggleTypeTree(type, enumeration.getBaseType(), stateBase, index,
                             rootPlan, dimensions, depth + 1);
    if (failed(child))
      return failure();
    coverage::ToggleDimension &node = dimensions[index];
    node.firstChild = *child;
    node.childCount = dimensions.size() - index - 1;
    return index;
  }

  uint32_t index = dimensions.size();
  coverage::ToggleDimensionKind kind = coverage::ToggleDimensionKind::Scalar;
  int64_t left = width - 1;
  int64_t right = 0;
  if (auto array = dyn_cast<sim::PackedArrayType>(type)) {
    kind = coverage::ToggleDimensionKind::PackedArray;
    left = array.getLeft();
    right = array.getRight();
  } else if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
    kind = coverage::ToggleDimensionKind::UnpackedArray;
    left = array.getLeft();
    right = array.getRight();
  } else if (isa<sim::PackedStructType>(type)) {
    kind = coverage::ToggleDimensionKind::PackedStruct;
  } else if (isa<sim::UnpackedStructType>(type)) {
    kind = coverage::ToggleDimensionKind::UnpackedStruct;
  } else if (isa<sim::PackedUnionType>(type)) {
    kind = coverage::ToggleDimensionKind::PackedUnion;
  } else if (isa<sim::UnpackedUnionType>(type)) {
    kind = coverage::ToggleDimensionKind::UnpackedUnion;
  }
  dimensions.push_back(
      {0, parent, kind, UINT32_MAX, 0, left, right, bitOffset, width, {}, 0});

  if (auto array = dyn_cast<sim::PackedArrayType>(type)) {
    unsigned element = sim::getAggregateNumElements(type) - 1;
    std::optional<std::pair<uint64_t, uint64_t>> physical =
        sim::getAggregateProvenanceSubelement(type, element);
    if (!physical || physical->first > UINT64_MAX - stateBase ||
        failed(appendToggleTypeTree(array.getElementType(),
                                    getSourceElementType(sourceType),
                                    stateBase + physical->first, index,
                                    rootPlan, dimensions, depth + 1)))
      return failure();
  } else if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
    std::optional<std::pair<uint64_t, uint64_t>> physical =
        sim::getAggregateProvenanceSubelement(type, 0);
    if (!physical || physical->first > UINT64_MAX - stateBase ||
        failed(appendToggleTypeTree(array.getElementType(),
                                    getSourceElementType(sourceType),
                                    stateBase + physical->first, index,
                                    rootPlan, dimensions, depth + 1)))
      return failure();
  } else {
    ArrayAttr fields;
    if (auto aggregate = dyn_cast<sim::PackedStructType>(type)) {
      fields = aggregate.getFields();
    } else if (auto aggregate = dyn_cast<sim::UnpackedStructType>(type)) {
      fields = aggregate.getFields();
    } else if (auto aggregate = dyn_cast<sim::PackedUnionType>(type)) {
      fields = aggregate.getFields();
    } else if (auto aggregate = dyn_cast<sim::UnpackedUnionType>(type)) {
      fields = aggregate.getFields();
    }
    if (fields) {
      for (auto [fieldIndex, attribute] : llvm::enumerate(fields)) {
        auto field = cast<sim::FieldAttr>(attribute);
        if (!toggleWidth(field.getType()))
          continue;
        std::optional<std::pair<uint64_t, uint64_t>> physical =
            sim::getAggregateProvenanceSubelement(type, fieldIndex);
        if (!physical || physical->first > UINT64_MAX - stateBase ||
            failed(appendToggleField(
                field.getName().getValue(), field.getType(),
                getSourceFieldType(sourceType, field.getName().getValue(),
                                   field.getOrdinal()),
                stateBase + physical->first, index, rootPlan, dimensions,
                depth + 1)))
          return failure();
      }
      const uint32_t tagBits = getUnionTagBits(type);
      if (tagBits) {
        std::optional<uint64_t> payloadSpan;
        if (auto packed = dyn_cast<sim::PackedUnionType>(type)) {
          std::optional<unsigned> packedWidth = sim::getPackedWidth(type);
          if (packedWidth && tagBits <= *packedWidth)
            payloadSpan = *packedWidth - tagBits;
        } else {
          payloadSpan = sim::getProvenanceSpan(type);
        }
        ToggleTypePlan tagPlan;
        if (!payloadSpan || *payloadSpan > UINT64_MAX - stateBase ||
            !appendBinding(tagPlan.bindings, 0, 0, tagBits))
          return failure();
        tagPlan.fourState.resize(tagBits);
        FailureOr<std::pair<uint64_t, uint64_t>> tagSpan =
            getDenseSpan(rootPlan, tagPlan, stateBase + *payloadSpan);
        if (failed(tagSpan) || dimensions.size() >= UINT32_MAX)
          return failure();
        dimensions.push_back({0, index, coverage::ToggleDimensionKind::Tag,
                              UINT32_MAX, 0, static_cast<int64_t>(tagBits - 1),
                              0, tagSpan->first, tagSpan->second, "$tag", 0});
      }
    }
  }
  coverage::ToggleDimension &node = dimensions[index];
  if (dimensions.size() != index + 1) {
    node.firstChild = index + 1;
    node.childCount = dimensions.size() - index - 1;
  }
  return index;
}

static FailureOr<ToggleTypePlan> buildToggleTypePlan(Type type,
                                                     Type sourceType = {}) {
  FailureOr<ToggleTypePlan> plan = buildToggleLayout(type);
  if (failed(plan) || plan->fourState.empty())
    return failure();
  plan->dimensions.push_back({0,
                              UINT32_MAX,
                              coverage::ToggleDimensionKind::Root,
                              1,
                              0,
                              0,
                              0,
                              0,
                              plan->fourState.size(),
                              {},
                              0});
  if (failed(appendToggleTypeTree(type, sourceType, 0, 0, *plan,
                                  plan->dimensions, 0)))
    return failure();
  plan->dimensions[0].childCount = plan->dimensions.size() - 1;
  return plan;
}

static uint64_t appendText(uint64_t hash, StringRef text) {
  hash = obelisk_stable_hash_append_uint_le(hash, text.size(), 8);
  return obelisk_stable_hash_append(hash, text.data(), text.size());
}

static uint64_t stableID(StringRef kind, StringRef identity) {
  uint64_t hash = appendText(OBELISK_STABLE_HASH_OFFSET_BASIS, kind);
  hash = appendText(hash, identity);
  return hash ? hash : 1;
}

static std::string normalizeSeparators(StringRef path) {
  std::string result = path.str();
  std::replace(result.begin(), result.end(), '\\', '/');
  while (StringRef(result).starts_with("./"))
    result.erase(0, 2);
  return result;
}

static std::string applyPrefixMaps(StringRef path,
                                   ArrayRef<PrefixMap> mappings) {
  std::string normalized = normalizeSeparators(path);
  for (const PrefixMap &mapping : mappings) {
    StringRef value(normalized);
    if (!value.starts_with(mapping.from))
      continue;
    std::string result = mapping.to;
    result.append(value.drop_front(mapping.from.size()));
    return normalizeSeparators(result);
  }
  return normalized;
}

static std::optional<unsigned> coverageMetricIndex(StringRef name) {
  if (name == "line")
    return 0;
  if (name == "toggle")
    return 1;
  if (name == "functional")
    return 2;
  if (name == "assertion")
    return 3;
  if (name == "fsm")
    return 4;
  return std::nullopt;
}

/// Match a coverage glob. A single star and question mark stay within one
/// file or hierarchy component; a double star may cross component separators.
static bool matchCoverageGlob(StringRef pattern, StringRef value,
                              char separator) {
  SmallVector<uint8_t> previous(value.size() + 1, 0);
  SmallVector<uint8_t> current(value.size() + 1, 0);
  previous[0] = 1;
  for (size_t patternIndex = 0; patternIndex < pattern.size();) {
    std::fill(current.begin(), current.end(), 0);
    const bool globStar = pattern[patternIndex] == '*' &&
                          patternIndex + 1 < pattern.size() &&
                          pattern[patternIndex + 1] == '*';
    const char token = pattern[patternIndex];
    patternIndex += globStar ? 2 : 1;
    if (token == '*') {
      current[0] = previous[0];
      for (size_t valueIndex = 1; valueIndex <= value.size(); ++valueIndex) {
        const bool mayConsume = globStar || value[valueIndex - 1] != separator;
        current[valueIndex] =
            previous[valueIndex] || (mayConsume && current[valueIndex - 1]);
      }
    } else {
      for (size_t valueIndex = 1; valueIndex <= value.size(); ++valueIndex)
        current[valueIndex] =
            previous[valueIndex - 1] &&
            ((token == '?' && value[valueIndex - 1] != separator) ||
             token == value[valueIndex - 1]);
    }
    previous.swap(current);
  }
  return previous[value.size()];
}

static FailureOr<CoverageConfiguration>
parseCoverageConfiguration(ModuleOp module) {
  CoverageConfiguration configuration;
  auto text = cov::get<cov::Config>(module);
  if (!text)
    return configuration;

  llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(text.getValue());
  if (!parsed) {
    module.emitError("invalid coverage configuration: ")
        << llvm::toString(parsed.takeError());
    return failure();
  }
  llvm::json::Object *root = parsed->getAsObject();
  if (!root) {
    module.emitError("coverage configuration root must be an object");
    return failure();
  }
  for (const auto &field : *root) {
    if (field.first != "include" && field.first != "exclude") {
      module.emitError("unknown coverage configuration field '")
          << field.first.str() << "'";
      return failure();
    }
  }

  auto parseRules = [&](StringRef group,
                        SmallVectorImpl<CoverageRule> &rules) -> LogicalResult {
    const llvm::json::Value *records = root->get(group);
    if (!records)
      return success();
    const llvm::json::Array *array = records->getAsArray();
    if (!array) {
      module.emitError("coverage '") << group << "' must be an array";
      return failure();
    }
    for (const llvm::json::Value &entry : *array) {
      const llvm::json::Object *record = entry.getAsObject();
      if (!record) {
        module.emitError("coverage '") << group << "' entries must be objects";
        return failure();
      }
      for (const auto &field : *record) {
        if (field.first != "metrics" && field.first != "file" &&
            field.first != "hierarchy" && field.first != "reason") {
          module.emitError("unknown coverage rule field '")
              << field.first.str() << "'";
          return failure();
        }
      }

      CoverageRule rule;
      if (const llvm::json::Value *metrics = record->get("metrics")) {
        const llvm::json::Array *list = metrics->getAsArray();
        if (!list || list->empty()) {
          module.emitError("coverage rule metrics must be a nonempty array");
          return failure();
        }
        rule.metrics.reset();
        llvm::StringSet<> seen;
        for (const llvm::json::Value &metricValue : *list) {
          std::optional<StringRef> name = metricValue.getAsString();
          std::optional<unsigned> index =
              name ? coverageMetricIndex(*name) : std::nullopt;
          if (!name || !index || !seen.insert(*name).second) {
            module.emitError(
                "coverage rule metrics must contain unique line, toggle, "
                "functional, assertion, or fsm names");
            return failure();
          }
          rule.metrics.set(*index);
        }
      }

      auto parseString = [&](StringRef name,
                             std::optional<std::string> &result) {
        const llvm::json::Value *value = record->get(name);
        if (!value)
          return success();
        std::optional<StringRef> stringValue = value->getAsString();
        if (!stringValue) {
          module.emitError("coverage rule '") << name << "' must be a string";
          return failure();
        }
        result = stringValue->str();
        return success();
      };
      if (failed(parseString("file", rule.file)) ||
          failed(parseString("hierarchy", rule.hierarchy)))
        return failure();
      if (rule.file && StringRef(*rule.file).contains('\\')) {
        module.emitError("coverage file globs must use '/' separators");
        return failure();
      }
      if (rule.hierarchy && StringRef(*rule.hierarchy).contains('/')) {
        module.emitError("coverage hierarchy globs must use '.' separators");
        return failure();
      }
      if (const llvm::json::Value *reason = record->get("reason")) {
        std::optional<StringRef> value = reason->getAsString();
        if (!value) {
          module.emitError("coverage rule 'reason' must be a string");
          return failure();
        }
        if (group == "include") {
          module.emitError("coverage include rules cannot carry a reason");
          return failure();
        }
        rule.reason = value->str();
      }
      rules.push_back(std::move(rule));
    }
    return success();
  };

  if (failed(parseRules("include", configuration.includes)) ||
      failed(parseRules("exclude", configuration.excludes)))
    return failure();
  return configuration;
}

static bool matchesRule(const CoverageRule &rule, unsigned metric,
                        StringRef file, StringRef hierarchy) {
  if (!rule.metrics.test(metric))
    return false;
  if (rule.file && !matchCoverageGlob(*rule.file, file, '/'))
    return false;
  if (rule.hierarchy && !matchCoverageGlob(*rule.hierarchy, hierarchy, '.'))
    return false;
  return true;
}

static std::optional<StringRef> findLineComment(StringRef line,
                                                bool &inBlockComment) {
  bool inString = false;
  bool escaped = false;
  for (size_t index = 0; index < line.size(); ++index) {
    const char current = line[index];
    const char next = index + 1 < line.size() ? line[index + 1] : '\0';
    if (inBlockComment) {
      if (current == '*' && next == '/') {
        inBlockComment = false;
        ++index;
      }
      continue;
    }
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == '"') {
        inString = false;
      }
      continue;
    }
    if (current == '"') {
      inString = true;
      continue;
    }
    if (current == '/' && next == '*') {
      inBlockComment = true;
      ++index;
      continue;
    }
    if (current == '/' && next == '/')
      return line.drop_front(index + 2);
  }
  return std::nullopt;
}

static LogicalResult parseDirectiveMetrics(ModuleOp module, StringRef path,
                                           uint32_t line, StringRef text,
                                           llvm::SmallBitVector &metrics) {
  metrics = llvm::SmallBitVector(kCoverageMetricCount, true);
  if (text.empty())
    return success();
  metrics.reset();
  SmallVector<StringRef> names;
  text.split(names, ',', -1, false);
  llvm::SmallBitVector seen(kCoverageMetricCount);
  for (StringRef name : names) {
    name = name.trim();
    std::optional<unsigned> index = coverageMetricIndex(name);
    if (name.empty() || !index || seen.test(*index)) {
      module.emitError("malformed coverage directive at ")
          << path << ':' << line
          << ": metrics must be unique line, toggle, functional, assertion, "
             "or fsm names";
      return failure();
    }
    seen.set(*index);
    metrics.set(*index);
  }
  return success();
}

static LogicalResult parseCoverageDirectives(ModuleOp module, StringRef path,
                                             CoverageDirectiveFile &result) {
  auto buffer = llvm::MemoryBuffer::getFile(path, false, false);
  // Synthetic and protected source identities deliberately have no readable
  // backing file. Their obligations remain usable without source directives.
  if (!buffer)
    return success();

  std::array<SmallVector<ActiveDirective>, kCoverageMetricCount> active;
  bool inBlockComment = false;
  SmallVector<StringRef> lines;
  (*buffer)->getBuffer().split(lines, '\n');
  for (auto [lineIndex, sourceLine] : llvm::enumerate(lines)) {
    const uint32_t line = static_cast<uint32_t>(lineIndex + 1);
    std::optional<StringRef> comment =
        findLineComment(sourceLine, inBlockComment);
    if (!comment)
      continue;
    StringRef directive = comment->trim();
    constexpr StringLiteral marker = "obelisk coverage";
    if (!directive.starts_with(marker))
      continue;
    directive = directive.drop_front(marker.size());
    if (!directive.empty() && !llvm::isSpace(directive.front())) {
      module.emitError("malformed coverage directive at ")
          << path << ':' << line;
      return failure();
    }
    directive = directive.ltrim();
    size_t actionEnd = directive.find_first_of(" \t\r");
    StringRef action = actionEnd == StringRef::npos
                           ? directive
                           : directive.take_front(actionEnd);
    StringRef remainder = actionEnd == StringRef::npos
                              ? StringRef{}
                              : directive.drop_front(actionEnd).trim();
    const bool isOff = action == "off";
    const bool isOn = action == "on";
    if (!isOff && !isOn) {
      module.emitError("unknown coverage directive at ")
          << path << ':' << line << ": expected 'off' or 'on'";
      return failure();
    }

    std::string reason = "source directive";
    StringRef metricText = remainder;
    size_t whitespace = remainder.find_first_of(" \t\r");
    StringRef trailing;
    if (whitespace != StringRef::npos) {
      metricText = remainder.take_front(whitespace);
      trailing = remainder.drop_front(whitespace).trim();
    } else if (remainder.starts_with("reason=")) {
      metricText = {};
      trailing = remainder;
    }
    if (!trailing.empty()) {
      if (!isOff || !trailing.starts_with("reason=")) {
        module.emitError("malformed coverage directive at ")
            << path << ':' << line;
        return failure();
      }
      StringRef encodedReason = trailing.drop_front(sizeof("reason=") - 1);
      llvm::Expected<llvm::json::Value> parsedReason =
          llvm::json::parse(encodedReason);
      if (!parsedReason) {
        llvm::consumeError(parsedReason.takeError());
        module.emitError("malformed coverage directive reason at ")
            << path << ':' << line;
        return failure();
      }
      std::optional<StringRef> text = parsedReason->getAsString();
      if (!text) {
        module.emitError("coverage directive reason must be a string at ")
            << path << ':' << line;
        return failure();
      }
      reason = text->str();
    }

    llvm::SmallBitVector metrics;
    if (failed(parseDirectiveMetrics(module, path, line, metricText, metrics)))
      return failure();
    if (isOn) {
      for (int metric = metrics.find_first(); metric >= 0;
           metric = metrics.find_next(metric)) {
        if (active[metric].empty()) {
          module.emitError("unmatched coverage 'on' directive at ")
              << path << ':' << line;
          return failure();
        }
      }
      for (int metric = metrics.find_first(); metric >= 0;
           metric = metrics.find_next(metric)) {
        ActiveDirective opened = std::move(active[metric].back());
        active[metric].pop_back();
        if (opened.beginLine < line)
          result.intervals[metric].push_back(
              {opened.beginLine, line - 1, std::move(opened.reason)});
      }
      continue;
    }
    for (int metric = metrics.find_first(); metric >= 0;
         metric = metrics.find_next(metric))
      active[metric].push_back({line + 1, reason});
  }

  for (unsigned metric = 0; metric != active.size(); ++metric) {
    if (active[metric].empty())
      continue;
    const ActiveDirective &opened = active[metric].back();
    module.emitError("unbalanced coverage 'off' directive at ")
        << path << ':' << opened.beginLine - 1;
    return failure();
  }
  return success();
}

static std::optional<StringRef>
directiveReason(const CoverageDirectiveFile &file, unsigned metric,
                uint32_t beginLine, uint32_t endLine) {
  const DirectiveInterval *selected = nullptr;
  for (const DirectiveInterval &interval : file.intervals[metric]) {
    if (interval.endLine < beginLine || interval.beginLine > endLine)
      continue;
    if (!selected || interval.beginLine >= selected->beginLine)
      selected = &interval;
  }
  return selected ? std::optional<StringRef>(selected->reason) : std::nullopt;
}

static std::optional<StringRef>
directiveReason(const llvm::StringMap<CoverageDirectiveFile> &files,
                unsigned metric, const SourceRange &range) {
  auto lookup = [&](StringRef path, uint32_t line) -> std::optional<StringRef> {
    auto found = files.find(normalizeSeparators(path));
    if (found == files.end())
      return std::nullopt;
    return directiveReason(found->second, metric, line, line);
  };
  return lookup(range.startFile, range.startLine);
}

static std::optional<SourceRange> decodeSourceRange(Attribute attribute) {
  auto typeAttr = dyn_cast_or_null<TypeAttr>(attribute);
  auto range = typeAttr ? dyn_cast<ir::SourceRangeType>(typeAttr.getValue())
                        : ir::SourceRangeType{};
  if (range) {
    if (!range.getStartLine() || !range.getEndLine())
      return std::nullopt;
    return SourceRange{range.getStartFile().str(), range.getEndFile().str(),
                       range.getMacroName().str(), range.getStartLine(),
                       range.getStartColumn(),     range.getEndLine(),
                       range.getEndColumn()};
  }
  auto encoded = dyn_cast_or_null<DictionaryAttr>(attribute);
  if (!encoded)
    return std::nullopt;
  auto startFile = encoded.getAs<StringAttr>("start_file");
  auto endFile = encoded.getAs<StringAttr>("end_file");
  auto macroName = encoded.getAs<StringAttr>("macro_name");
  auto startLine = encoded.getAs<IntegerAttr>("start_line");
  auto startColumn = encoded.getAs<IntegerAttr>("start_column");
  auto endLine = encoded.getAs<IntegerAttr>("end_line");
  auto endColumn = encoded.getAs<IntegerAttr>("end_column");
  if (!startFile || !endFile || !macroName || !startLine || !startColumn ||
      !endLine || !endColumn || !startLine.getType().isSignlessInteger(32) ||
      !startColumn.getType().isSignlessInteger(32) ||
      !endLine.getType().isSignlessInteger(32) ||
      !endColumn.getType().isSignlessInteger(32) ||
      startLine.getValue().isNegative() ||
      startColumn.getValue().isNegative() || endLine.getValue().isNegative() ||
      endColumn.getValue().isNegative() || startLine.getValue().isZero() ||
      endLine.getValue().isZero())
    return std::nullopt;
  return SourceRange{
      startFile.getValue().str(),
      endFile.getValue().str(),
      macroName.getValue().str(),
      static_cast<uint32_t>(startLine.getValue().getZExtValue()),
      static_cast<uint32_t>(startColumn.getValue().getZExtValue()),
      static_cast<uint32_t>(endLine.getValue().getZExtValue()),
      static_cast<uint32_t>(endColumn.getValue().getZExtValue())};
}

static bool isExecutableStatement(Operation *operation);

static std::optional<SourceInfo> getSourceInfo(Operation *operation) {
  std::optional<SourceRange> expanded =
      decodeSourceRange(operation->getAttr("source_range"));
  if (!expanded)
    return std::nullopt;
  SourceInfo result{std::move(*expanded), std::nullopt, {}};
  result.original =
      decodeSourceRange(operation->getAttr("original_source_range"));
  operation->walk<WalkOrder::PreOrder>([&](Operation *nested) -> WalkResult {
    // A nested executable statement owns a separate coverage point. Do not
    // make the parent's identity depend on the child's macro inventory.
    if (nested != operation && isExecutableStatement(nested))
      return WalkResult::skip();
    auto stack = nested->getAttrOfType<ArrayAttr>("macro_expansion_stack");
    if (!stack)
      return WalkResult::advance();
    uint64_t node = 0;
    if (auto attr = nested->getAttrOfType<IntegerAttr>("node_id"))
      node = attr.getValue().getZExtValue();
    result.macroFrames.reserve(result.macroFrames.size() + stack.size());
    for (Attribute attribute : stack) {
      auto frame = dyn_cast<DictionaryAttr>(attribute);
      if (!frame)
        continue;
      MacroFrame decoded;
      decoded.node = node;
      if (auto name = frame.getAs<StringAttr>("name"))
        decoded.name = name.getValue().str();
      decoded.definition = decodeSourceRange(frame.get("definition"));
      decoded.invocation = decodeSourceRange(frame.get("invocation"));
      result.macroFrames.push_back(std::move(decoded));
    }
    return WalkResult::advance();
  });
  return result;
}

static uint64_t appendSourceRange(uint64_t hash, const SourceRange &range,
                                  ArrayRef<PrefixMap> prefixMaps) {
  hash = appendText(hash, applyPrefixMaps(range.startFile, prefixMaps));
  hash = appendText(hash, applyPrefixMaps(range.endFile, prefixMaps));
  hash = obelisk_stable_hash_append_uint_le(hash, range.startLine, 4);
  hash = obelisk_stable_hash_append_uint_le(hash, range.startColumn, 4);
  hash = obelisk_stable_hash_append_uint_le(hash, range.endLine, 4);
  hash = obelisk_stable_hash_append_uint_le(hash, range.endColumn, 4);
  return appendText(hash, range.macroName);
}

static uint64_t
appendOptionalSourceRange(uint64_t hash,
                          const std::optional<SourceRange> &range,
                          ArrayRef<PrefixMap> prefixMaps) {
  hash = obelisk_stable_hash_append_uint_le(hash, range.has_value(), 1);
  return range ? appendSourceRange(hash, *range, prefixMaps) : hash;
}

static void appendFunctionalSemantics(llvm::raw_ostream &stream,
                                      Operation *operation) {
  stream << operation->getName() << '{';
  for (NamedAttribute named : operation->getAttrs()) {
    StringRef name = named.getName().strref();
    // Coverage metadata is derived by preparation; an identity must not
    // change when that metadata or its attribute names do.
    if (name == "node_id" || name == "sym_name" ||
        name == "referenced_symbol" || name == "source_range" ||
        name == "original_source_range" || name == "macro_expansion_stack" ||
        name.starts_with("coverage."))
      continue;
    stream << name << '=';
    named.getValue().print(stream);
    stream << ';';
  }
  stream << '}';
  for (Operation *child : simlowering::getChildren(operation))
    appendFunctionalSemantics(stream, child);
}

static coverage::Digest functionalSemanticDigest(Operation *operation) {
  std::string semantics;
  llvm::raw_string_ostream stream(semantics);
  appendFunctionalSemantics(stream, operation);
  return coverage::sha256(reinterpret_cast<const uint8_t *>(semantics.data()),
                          semantics.size());
}

static uint64_t stableFunctionalEntityID(StringRef kind, uint64_t owner,
                                         Operation *operation) {
  std::string identity = simlowering::functionalSemanticIdentity(operation);
  uint64_t hash = appendText(OBELISK_STABLE_HASH_OFFSET_BASIS, kind);
  hash = obelisk_stable_hash_append_uint_le(hash, owner, 8);
  hash = appendText(hash, identity);
  hash &= static_cast<uint64_t>(INT64_MAX);
  return hash ? hash : uint64_t{1};
}

static uint64_t stableFunctionalExpressionID(
    uint64_t owner, coverage::FunctionalExpressionOwnerKind ownerKind,
    coverage::FunctionalExpressionRole role, uint32_t ownerOrdinal,
    uint32_t ownerSubordinal, Operation *operation,
    const coverage::Digest &semanticDigest, ArrayRef<PrefixMap> prefixMaps) {
  uint64_t hash =
      appendText(OBELISK_STABLE_HASH_OFFSET_BASIS, "functional.expression");
  hash = obelisk_stable_hash_append_uint_le(hash, owner, 8);
  hash = obelisk_stable_hash_append_uint_le(
      hash, static_cast<uint32_t>(ownerKind), 4);
  hash =
      obelisk_stable_hash_append_uint_le(hash, static_cast<uint32_t>(role), 4);
  hash = obelisk_stable_hash_append_uint_le(hash, ownerOrdinal, 4);
  hash = obelisk_stable_hash_append_uint_le(hash, ownerSubordinal, 4);
  if (std::optional<SourceInfo> source = getSourceInfo(operation)) {
    hash = appendSourceRange(hash, source->expanded, prefixMaps);
    hash = appendOptionalSourceRange(hash, source->original, prefixMaps);
    for (const MacroFrame &frame : source->macroFrames) {
      hash = appendText(hash, frame.name);
      hash = appendOptionalSourceRange(hash, frame.definition, prefixMaps);
      hash = appendOptionalSourceRange(hash, frame.invocation, prefixMaps);
    }
  }
  for (uint8_t byte : semanticDigest)
    hash = obelisk_stable_hash_append_uint_le(hash, byte, 1);
  hash &= static_cast<uint64_t>(INT64_MAX);
  return hash ? hash : uint64_t{1};
}

static bool isMetricEnabled(ModuleOp module, StringRef expected) {
  auto metrics = cov::get<cov::Metrics>(module);
  if (!metrics)
    return false;
  return llvm::any_of(metrics, [&](Attribute metric) {
    auto text = dyn_cast<StringAttr>(metric);
    return text && text.getValue() == expected;
  });
}

static bool isLineMetricEnabled(ModuleOp module) {
  return isMetricEnabled(module, "line");
}

static std::optional<SourceRange> getSiteRange(Operation *operation) {
  if (std::optional<SourceRange> range =
          decodeSourceRange(operation->getAttr("source_range")))
    return range;
  auto location = operation->getLoc()->findInstanceOf<FileLineColLoc>();
  if (!location)
    return std::nullopt;
  return SourceRange{location.getFilename().str(),
                     location.getFilename().str(),
                     {},
                     location.getLine(),
                     location.getColumn(),
                     location.getLine(),
                     location.getColumn()};
}

static bool isExecutableStatement(Operation *operation) {
  StringRef name = operation->getName().getStringRef();
  if (!name.starts_with("obelisk.sv.statement."))
    return false;
  // IEEE 1800-2017 16.14.4 defines restrict as a formal constraint that is
  // not verified in simulation, so it has no simulator execution obligation.
  if (auto assertion =
          dyn_cast<ir::SVConcurrentAssertionStatementOp>(operation);
      assertion &&
      assertion.getAssertionKind() == ir::SVAssertionKind::Restrict)
    return false;
  if (name == "obelisk.sv.statement.empty" ||
      name == "obelisk.sv.statement.block" ||
      name == "obelisk.sv.statement.list")
    return false;
  // A declaration without an initializer has no procedural execution effect.
  if (name == "obelisk.sv.statement.variable_declaration") {
    Region &region = operation->getRegion(0);
    return !region.empty() && !region.front().empty();
  }
  return true;
}

static coverage::Digest digestFile(StringRef path) {
  auto buffer = llvm::MemoryBuffer::getFile(path, false, false);
  if (!buffer)
    return {};
  StringRef contents = (*buffer)->getBuffer();
  return coverage::sha256(reinterpret_cast<const uint8_t *>(contents.data()),
                          contents.size());
}

} // namespace

namespace {

/// IEEE 1800-2023 40.4.1-40.4.3: a pragma naming the vector that holds an
/// FSM's current state.
struct FSMVectorPragma {
  // Signals whose concatenation, most significant first, holds the state.
  SmallVector<std::string, 2> signals;
  // A part-select [left:right] of the single signal.
  std::optional<std::pair<int64_t, int64_t>> range;
  std::string name;
  std::string enumeration;
  size_t offset = 0;
};

/// IEEE 1800-2023 40.4.1, 40.4.5, 40.4.6: an enumeration pragma inside a
/// parameter declaration (the FSM's states) or a signal declaration (its
/// current and next state).
struct FSMEnumPragma {
  std::string enumeration;
  bool parameters = false;
  SmallVector<std::pair<std::string, size_t>> names;
};

struct FSMPragmaFile {
  std::string path;
  std::string text;
  SmallVector<size_t> lineStarts;
  SmallVector<FSMVectorPragma> vectors;
  SmallVector<FSMEnumPragma> enums;

  uint32_t lineOf(size_t offset) const {
    return static_cast<uint32_t>(llvm::upper_bound(lineStarts, offset) -
                                 lineStarts.begin());
  }
  uint32_t columnOf(size_t offset) const {
    return static_cast<uint32_t>(offset - lineStarts[lineOf(offset) - 1] + 1);
  }
};

} // namespace

static bool isIdentifierCharacter(char character) {
  return llvm::isAlnum(character) || character == '_' || character == '$';
}

/// Tokens of a pragma: identifiers, unsigned numbers, and single punctuators.
static SmallVector<StringRef> tokenizePragma(StringRef text) {
  SmallVector<StringRef> tokens;
  for (size_t index = 0; index < text.size();) {
    if (llvm::isSpace(text[index])) {
      ++index;
      continue;
    }
    size_t end = index + 1;
    if (isIdentifierCharacter(text[index]))
      while (end < text.size() && isIdentifierCharacter(text[end]))
        ++end;
    tokens.push_back(text.slice(index, end));
    index = end;
  }
  return tokens;
}

/// The leading identifier of each top-level comma-separated item of the
/// declaration that follows `offset`, up to its terminating semicolon.
static SmallVector<std::pair<std::string, size_t>>
declaredNames(StringRef text, size_t offset) {
  SmallVector<std::pair<std::string, size_t>> names;
  int depth = 0;
  bool expectName = true;
  for (size_t index = offset; index < text.size(); ++index) {
    char character = text[index];
    if (character == '/' && index + 1 < text.size() &&
        (text[index + 1] == '/' || text[index + 1] == '*')) {
      size_t close = text[index + 1] == '/' ? text.find('\n', index)
                                            : text.find("*/", index + 2);
      if (close == StringRef::npos)
        break;
      index = text[index + 1] == '/' ? close : close + 1;
      continue;
    }
    if (character == ';' && depth == 0)
      break;
    if (character == '(' || character == '[' || character == '{')
      ++depth;
    else if (character == ')' || character == ']' || character == '}')
      --depth;
    else if (character == ',' && depth == 0)
      expectName = true;
    else if (expectName && depth == 0 &&
             (llvm::isAlpha(character) || character == '_')) {
      size_t end = index;
      while (end < text.size() && isIdentifierCharacter(text[end]))
        ++end;
      names.push_back({text.slice(index, end).str(), index});
      expectName = false;
      index = end - 1;
    }
  }
  return names;
}

/// Read the IEEE 1800-2023 40.4 FSM pragmas of one source file. Pragmas work
/// in block and one-line comments (40.4.7).
static FailureOr<FSMPragmaFile> parseFSMPragmas(ModuleOp module,
                                                StringRef path) {
  FSMPragmaFile result;
  result.path = path.str();
  auto buffer = llvm::MemoryBuffer::getFile(path);
  if (!buffer)
    return result;
  result.text = (*buffer)->getBuffer().str();
  StringRef text = result.text;
  result.lineStarts.push_back(0);
  for (size_t index = 0; index != text.size(); ++index)
    if (text[index] == '\n')
      result.lineStarts.push_back(index + 1);

  auto parsePragma = [&](StringRef body, size_t begin,
                         size_t end) -> LogicalResult {
    SmallVector<StringRef> tokens = tokenizePragma(body);
    if (tokens.size() < 2 || tokens[0] != "tool")
      return success();
    auto malformed = [&]() {
      return module.emitError("malformed FSM coverage pragma at ")
             << path << ':' << result.lineOf(begin);
    };
    size_t next = 2;
    if (tokens[1] == "enum") {
      if (tokens.size() != 3)
        return malformed();
      FSMEnumPragma pragma;
      pragma.enumeration = tokens[2].str();
      // The declaration keyword precedes the pragma in the same statement.
      size_t statement = text.rfind(';', begin);
      statement = statement == StringRef::npos ? 0 : statement + 1;
      for (StringRef word : tokenizePragma(text.slice(statement, begin)))
        pragma.parameters |= word == "parameter" || word == "localparam";
      pragma.names = declaredNames(text, end);
      if (pragma.names.empty())
        return malformed();
      result.enums.push_back(std::move(pragma));
      return success();
    }
    if (tokens[1] != "state_vector" || tokens.size() < 3)
      return success();
    FSMVectorPragma pragma;
    pragma.offset = begin;
    auto isIdentifier = [](StringRef token) {
      return !token.empty() &&
             (llvm::isAlpha(token.front()) || token.front() == '_');
    };
    if (tokens[next] == "{") {
      // 40.4.3: a concatenation of whole signals.
      for (++next; next < tokens.size() && tokens[next] != "}"; ++next) {
        if (!isIdentifier(tokens[next]))
          return malformed();
        pragma.signals.push_back(tokens[next].str());
        if (next + 1 < tokens.size() && tokens[next + 1] == ",")
          ++next;
      }
      if (next == tokens.size() || pragma.signals.empty())
        return malformed();
      ++next;
    } else {
      if (!isIdentifier(tokens[next]))
        return malformed();
      pragma.signals.push_back(tokens[next++].str());
      // 40.4.2: a part-select of the signal.
      if (next < tokens.size() && tokens[next] == "[") {
        int64_t left = 0, right = 0;
        if (next + 4 >= tokens.size() || tokens[next + 2] != ":" ||
            tokens[next + 4] != "]" ||
            tokens[next + 1].getAsInteger(10, left) ||
            tokens[next + 3].getAsInteger(10, right))
          return malformed();
        pragma.range = {left, right};
        next += 5;
      }
    }
    if (next < tokens.size() && tokens[next] != "enum")
      pragma.name = tokens[next++].str();
    if (next < tokens.size()) {
      if (tokens[next] != "enum" || next + 2 != tokens.size())
        return malformed();
      pragma.enumeration = tokens[next + 1].str();
    }
    // A part-select or concatenation needs an FSM name and enumeration.
    if ((pragma.range || pragma.signals.size() != 1) &&
        (pragma.name.empty() || pragma.enumeration.empty()))
      return malformed();
    if (pragma.name.empty())
      pragma.name = pragma.signals.front();
    result.vectors.push_back(std::move(pragma));
    return success();
  };

  bool inString = false;
  for (size_t index = 0; index < text.size(); ++index) {
    char current = text[index];
    char next = index + 1 < text.size() ? text[index + 1] : '\0';
    if (inString) {
      if (current == '\\')
        ++index;
      else if (current == '"')
        inString = false;
      continue;
    }
    if (current == '"') {
      inString = true;
      continue;
    }
    if (current != '/' || (next != '/' && next != '*'))
      continue;
    size_t bodyBegin = index + 2;
    size_t close =
        next == '/' ? text.find('\n', bodyBegin) : text.find("*/", bodyBegin);
    if (close == StringRef::npos)
      close = text.size();
    size_t end = next == '/' ? close : std::min(close + 2, text.size());
    if (failed(parsePragma(text.slice(bodyBegin, close), index, end)))
      return failure();
    index = end == 0 ? 0 : end - 1;
  }
  return result;
}

void ObeliskSimPrepareCoveragePass::runOnOperation() {
  ModuleOp module = getOperation();
  simlowering::SemanticTypeIndex semanticTypes(module);
  simlowering::ScopedSemanticTypeIndex semanticTypeScope(semanticTypes);
  bool hasExecutableCovergroups = false;
  module.walk(
      [&](sim::SimCovergroupDeclOp) { hasExecutableCovergroups = true; });
  bool hasCoverageSystemCall = false;
  module.walk([&](ir::SVCallExpressionOp call) {
    if (hasCoverageSystemCall || !call.getIsSystemCall())
      return;
    hasCoverageSystemCall =
        llvm::StringSwitch<bool>(call.getCalleeName())
            .Cases({"$coverage_control", "$coverage_get_max", "$coverage_get",
                    "$coverage_merge", "$coverage_save",
                    "$set_coverage_db_name", "$load_coverage_db",
                    "$get_coverage"},
                   true)
            .Default(false);
  });
  // A system call needs the exact schema even when no metric was selected:
  // Clause 40 queries must return SV_COV_NOCOV rather than fail the runtime
  // lifecycle, instance arguments still need stable scope identities, and
  // functional database calls request persistence of otherwise-unused types.
  if (!cov::has<cov::Metrics>(module) && !hasExecutableCovergroups &&
      !hasCoverageSystemCall)
    return;

  llvm::StringMap<CoverageDirectiveFile> directiveFiles;
  auto parseDirectiveFile = [&](StringRef path) -> LogicalResult {
    std::string key = normalizeSeparators(path);
    auto [entry, inserted] = directiveFiles.try_emplace(key);
    if (!inserted)
      return success();
    return parseCoverageDirectives(module, path, entry->second);
  };
  auto parseDirectiveRange = [&](const SourceRange &range) -> LogicalResult {
    if (failed(parseDirectiveFile(range.startFile)))
      return failure();
    if (normalizeSeparators(range.startFile) !=
        normalizeSeparators(range.endFile))
      return parseDirectiveFile(range.endFile);
    return success();
  };

  bool invalidDirective = false;
  if (auto sourceFiles = cov::get<cov::SourceFiles>(module)) {
    for (Attribute attribute : sourceFiles) {
      auto path = dyn_cast<StringAttr>(attribute);
      if (!path || failed(parseDirectiveFile(path.getValue()))) {
        invalidDirective = true;
        break;
      }
    }
  }
  module.walk([&](Operation *operation) {
    if (invalidDirective)
      return;
    auto parseRangeAttribute = [&](StringRef name) {
      std::optional<SourceRange> range =
          decodeSourceRange(operation->getAttr(name));
      if (range && failed(parseDirectiveRange(*range)))
        invalidDirective = true;
    };
    parseRangeAttribute("source_range");
    parseRangeAttribute("original_source_range");
    if (auto path = operation->getAttrOfType<StringAttr>("source_file");
        path && failed(parseDirectiveFile(path.getValue())))
      invalidDirective = true;
    if (auto location = operation->getLoc()->findInstanceOf<FileLineColLoc>();
        location &&
        failed(parseDirectiveFile(location.getFilename().getValue())))
      invalidDirective = true;
    if (auto stack =
            operation->getAttrOfType<ArrayAttr>("macro_expansion_stack")) {
      for (Attribute attribute : stack) {
        auto frame = dyn_cast<DictionaryAttr>(attribute);
        if (!frame)
          continue;
        for (StringRef field : {"definition", "invocation"}) {
          std::optional<SourceRange> range =
              decodeSourceRange(frame.get(field));
          if (range && failed(parseDirectiveRange(*range)))
            invalidDirective = true;
        }
      }
    }
  });
  if (invalidDirective) {
    signalPassFailure();
    return;
  }

  FailureOr<CoverageConfiguration> coverageConfiguration =
      parseCoverageConfiguration(module);
  if (failed(coverageConfiguration)) {
    signalPassFailure();
    return;
  }

  SmallVector<PrefixMap> prefixMaps;
  if (auto maps = cov::get<cov::PrefixMaps>(module)) {
    for (Attribute attribute : maps) {
      auto value = dyn_cast<StringAttr>(attribute);
      if (!value)
        continue;
      auto [from, to] = value.getValue().split('=');
      prefixMaps.push_back(
          {normalizeSeparators(from), normalizeSeparators(to)});
    }
  }

  coverage::Database schema;
  schema.producer = "obelisk prototype";
  llvm::DenseMap<Operation *, ScopeInventory> scopeInventories;
  llvm::DenseMap<uint64_t, Operation *> scopeOwners;
  for (sim::SimDesignOp design : module.getOps<sim::SimDesignOp>()) {
    ScopeInventory inventory;
    SmallVector<sim::SimScopeDeclOp> declarations(
        design.getBody().front().getOps<sim::SimScopeDeclOp>());
    sortCoverageRecords(declarations, [](auto entry) {
      return coverageOrderKey(entry.getId());
    });
    for (sim::SimScopeDeclOp declaration : declarations) {
      std::string name =
          declaration.getId() == 0
              ? std::string("$root")
              : declaration.getHierarchicalName().value_or(StringRef{}).str();
      if (name.empty())
        name = ("scope." + Twine(declaration.getId())).str();
      std::string identity = design.getSymName().str();
      identity.push_back('\0');
      identity.append(name);
      uint64_t id = stableID("scope", identity);
      auto [owner, inserted] = scopeOwners.try_emplace(id, declaration);
      if (!inserted) {
        declaration.emitError("coverage scope ID hash collision")
            << " with " << owner->second->getLoc();
        signalPassFailure();
        return;
      }
      uint64_t parent = 0;
      if (std::optional<uint64_t> sourceParent = declaration.getParent()) {
        auto found = inventory.ids.find(*sourceParent);
        if (found == inventory.ids.end()) {
          declaration.emitError(
              "coverage scope parent has not been inventoried");
          signalPassFailure();
          return;
        }
        parent = found->second;
      }
      uint32_t kind = declaration.getVpiKind().value_or(uint32_t(0));
      inventory.ids[declaration.getId()] = id;
      inventory.hierarchies.try_emplace(id, name);
      inventory.kinds.try_emplace(id, kind);
      declaration.setCoverageId(id);
      schema.scopes.push_back(
          {id, parent, name, kind,
           declaration.getDefinitionName().value_or(StringRef{}).str()});
    }
    scopeInventories.try_emplace(design.getOperation(), std::move(inventory));
  }

  llvm::StringMap<uint64_t> fileIDs;
  llvm::DenseMap<uint64_t, std::string> filePaths;
  auto internFile = [&](StringRef originalPath) -> FailureOr<uint64_t> {
    std::string storedPath = applyPrefixMaps(originalPath, prefixMaps);
    if (auto found = fileIDs.find(storedPath); found != fileIDs.end())
      return found->second;
    uint64_t id = stableID("source", storedPath);
    auto [foundPath, inserted] = filePaths.try_emplace(id, storedPath);
    if (!inserted && foundPath->second != storedPath) {
      module.emitError("coverage source-file ID hash collision between '")
          << foundPath->second << "' and '" << storedPath << "'";
      return failure();
    }
    fileIDs[storedPath] = id;
    schema.sourceFiles.push_back({id, storedPath, digestFile(originalPath)});
    return id;
  };

  SmallVector<PointCandidate, 0> candidates;

  if (isLineMetricEnabled(module)) {
    for (sim::SimDesignOp design : module.getOps<sim::SimDesignOp>()) {
      ScopeInventory &scopeInventory =
          scopeInventories.find(design.getOperation())->second;
      auto &scopeIDs = scopeInventory.ids;
      auto &scopeHierarchies = scopeInventory.hierarchies;

      llvm::DenseMap<uint64_t, uint64_t> codeUnitScopes;
      for (sim::SimCodeUnitDeclOp declaration :
           design.getBody().front().getOps<sim::SimCodeUnitDeclOp>()) {
        auto scope = scopeIDs.find(declaration.getScopeId());
        if (scope != scopeIDs.end())
          codeUnitScopes.try_emplace(declaration.getId(), scope->second);
      }

      for (sim::SimFuncOp function :
           design.getBody().front().getOps<sim::SimFuncOp>()) {
        std::optional<uint64_t> codeUnit = function.getCodeUnitId();
        if (!codeUnit)
          continue;
        auto scope = codeUnitScopes.find(*codeUnit);
        if (scope == codeUnitScopes.end())
          continue;

        function.walk([&](Operation *operation) {
          if (!isExecutableStatement(operation))
            return;
          std::optional<SourceInfo> source = getSourceInfo(operation);
          if (!source)
            return;
          uint64_t node = 0;
          if (auto attr = operation->getAttrOfType<IntegerAttr>("node_id"))
            node = attr.getValue().getZExtValue();
          candidates.push_back({operation,
                                function,
                                std::move(*source),
                                scope->second,
                                *codeUnit,
                                node,
                                0,
                                false,
                                false,
                                scopeHierarchies.lookup(scope->second),
                                {}});
        });

        if (function.getEntryKind() != sim::EntryKind::Continuous ||
            function->hasAttr("internal"))
          continue;
        std::optional<SourceInfo> entrySource;
        Operation *source = nullptr;
        function.walk([&](Operation *operation) {
          if (entrySource)
            return WalkResult::interrupt();
          entrySource = getSourceInfo(operation);
          if (entrySource) {
            source = operation;
            return WalkResult::interrupt();
          }
          return WalkResult::advance();
        });
        if (entrySource)
          candidates.push_back({source,
                                function,
                                std::move(*entrySource),
                                scope->second,
                                *codeUnit,
                                0,
                                1,
                                true,
                                false,
                                scopeHierarchies.lookup(scope->second),
                                {}});
      }
    }

    llvm::DenseMap<uint64_t, Operation *> pointOwners;
    for (PointCandidate &candidate : candidates) {
      auto internRange = [&](const SourceRange &range) -> LogicalResult {
        return success(succeeded(internFile(range.startFile)) &&
                       succeeded(internFile(range.endFile)));
      };
      const SourceRange &expanded = candidate.source.expanded;
      FailureOr<uint64_t> startFile = internFile(expanded.startFile);
      FailureOr<uint64_t> endFile = internFile(expanded.endFile);
      if (failed(startFile) || failed(endFile)) {
        signalPassFailure();
        return;
      }
      if ((candidate.source.original &&
           failed(internRange(*candidate.source.original))) ||
          llvm::any_of(candidate.source.macroFrames,
                       [&](const MacroFrame &frame) {
                         return (frame.definition &&
                                 failed(internRange(*frame.definition))) ||
                                (frame.invocation &&
                                 failed(internRange(*frame.invocation)));
                       })) {
        signalPassFailure();
        return;
      }
      uint64_t hash = appendText(OBELISK_STABLE_HASH_OFFSET_BASIS, "line");
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.scope, 8);
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.codeUnit, 8);
      hash = appendSourceRange(hash, expanded, prefixMaps);
      hash = appendOptionalSourceRange(hash, candidate.source.original,
                                       prefixMaps);
      hash = obelisk_stable_hash_append_uint_le(
          hash, candidate.source.macroFrames.size(), 8);
      for (const MacroFrame &frame : candidate.source.macroFrames) {
        hash = obelisk_stable_hash_append_uint_le(hash, frame.node, 8);
        hash = appendText(hash, frame.name);
        hash = appendOptionalSourceRange(hash, frame.definition, prefixMaps);
        hash = appendOptionalSourceRange(hash, frame.invocation, prefixMaps);
      }
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.phase, 4);
      // Frontend node identity distinguishes separate expanded occurrences
      // with the same spelling range without depending on physical IR order.
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.node, 8);
      if (!hash)
        hash = 1;
      auto [owner, inserted] =
          pointOwners.try_emplace(hash, candidate.operation);
      if (!inserted) {
        candidate.operation->emitError("coverage line-point ID hash collision")
            << " with " << owner->second->getLoc();
        signalPassFailure();
        return;
      }
      candidate.point = {hash,
                         *startFile,
                         *endFile,
                         candidate.scope,
                         expanded.macroName,
                         expanded.startLine,
                         expanded.startColumn,
                         expanded.endLine,
                         expanded.endColumn,
                         candidate.phase,
                         0};

      const std::string storedPath =
          applyPrefixMaps(expanded.startFile, prefixMaps);
      const bool hasLineIncludes = llvm::any_of(
          coverageConfiguration->includes,
          [](const CoverageRule &rule) { return rule.metrics.test(0); });
      const bool included =
          !hasLineIncludes ||
          llvm::any_of(
              coverageConfiguration->includes, [&](const CoverageRule &rule) {
                return matchesRule(rule, 0, storedPath, candidate.hierarchy);
              });
      const CoverageRule *excludingRule = nullptr;
      for (const CoverageRule &rule : coverageConfiguration->excludes) {
        if (matchesRule(rule, 0, storedPath, candidate.hierarchy)) {
          excludingRule = &rule;
          break;
        }
      }
      std::optional<StringRef> sourceReason =
          directiveReason(directiveFiles, 0, candidate.source.expanded);
      if (!sourceReason && candidate.source.original)
        sourceReason =
            directiveReason(directiveFiles, 0, *candidate.source.original);
      for (const MacroFrame &frame : candidate.source.macroFrames) {
        if (!sourceReason && frame.definition)
          sourceReason = directiveReason(directiveFiles, 0, *frame.definition);
        if (!sourceReason && frame.invocation)
          sourceReason = directiveReason(directiveFiles, 0, *frame.invocation);
      }
      if (!included || excludingRule || sourceReason) {
        candidate.excluded = true;
        StringRef reason =
            sourceReason ? *sourceReason
            : excludingRule
                ? StringRef(excludingRule->reason)
                : StringRef("not selected by coverage include rules");
        schema.exclusions.push_back(
            {hash, coverage::MetricKind::Line, reason.str(), 0});
      }
    }

    sortCoverageRecords(candidates, [](const PointCandidate &entry) {
      return coverageOrderKey(entry.point.id);
    });
    schema.linePoints.reserve(candidates.size());
    Builder builder(&getContext());
    llvm::DenseMap<Operation *, SmallVector<FlatSymbolRefAttr>> keepalives;
    for (auto [index, candidate] : llvm::enumerate(candidates)) {
      schema.linePoints.push_back(candidate.point);
      Operation *target = candidate.functionEntry
                              ? candidate.function.getOperation()
                              : candidate.operation;
      if (!candidate.excluded) {
        cov::set<cov::LinePointIndex>(target, builder.getI64IntegerAttr(index));
        sim::SimDesignOp design =
            candidate.function->getParentOfType<sim::SimDesignOp>();
        if (!design) {
          candidate.function.emitError(
              "included coverage point has no enclosing design");
          signalPassFailure();
          return;
        }
        auto reference =
            FlatSymbolRefAttr::get(candidate.function.getSymNameAttr());
        SmallVectorImpl<FlatSymbolRefAttr> &references =
            keepalives[design.getOperation()];
        if (!llvm::is_contained(references, reference))
          references.push_back(reference);
      }
    }
    for (auto &[operation, references] : keepalives) {
      auto design = cast<sim::SimDesignOp>(operation);
      OpBuilder keepaliveBuilder(&getContext());
      keepaliveBuilder.setInsertionPointToStart(&design.getBody().front());
      for (FlatSymbolRefAttr reference : references)
        sim::SimCoverageKeepaliveOp::create(keepaliveBuilder, design.getLoc(),
                                            reference);
    }
    cov::set<cov::LinePointCount>(module,
                                  builder.getI64IntegerAttr(candidates.size()));
  }

  // IEEE 1800-2023 40.2.2: assertion coverage records, for each elaborated
  // assert, assume, cover, and expect statement, whether it has succeeded.
  if (isMetricEnabled(module, "assertion")) {
    struct AssertionCandidate {
      Operation *operation;
      sim::SimFuncOp function;
      SourceInfo source;
      uint64_t scope;
      uint64_t codeUnit;
      uint64_t node;
      std::string hierarchy;
      coverage::AssertionPoint point;
      bool excluded = false;
    };
    SmallVector<AssertionCandidate, 0> assertions;
    for (sim::SimDesignOp design : module.getOps<sim::SimDesignOp>()) {
      ScopeInventory &scopeInventory =
          scopeInventories.find(design.getOperation())->second;
      llvm::DenseMap<uint64_t, uint64_t> codeUnitScopes;
      for (sim::SimCodeUnitDeclOp declaration :
           design.getBody().front().getOps<sim::SimCodeUnitDeclOp>()) {
        auto scope = scopeInventory.ids.find(declaration.getScopeId());
        if (scope != scopeInventory.ids.end())
          codeUnitScopes.try_emplace(declaration.getId(), scope->second);
      }
      for (sim::SimFuncOp function :
           design.getBody().front().getOps<sim::SimFuncOp>()) {
        std::optional<uint64_t> codeUnit = function.getCodeUnitId();
        auto scope =
            codeUnit ? codeUnitScopes.find(*codeUnit) : codeUnitScopes.end();
        if (scope == codeUnitScopes.end())
          continue;
        function.walk([&](Operation *operation) {
          std::optional<ir::SVAssertionKind> sourceKind;
          if (auto concurrent =
                  dyn_cast<ir::SVConcurrentAssertionStatementOp>(operation))
            sourceKind = concurrent.getAssertionKind();
          else if (auto immediate =
                       dyn_cast<ir::SVImmediateAssertionStatementOp>(operation))
            sourceKind = immediate.getAssertionKind();
          // A restrict statement only constrains formal tools; it never
          // succeeds or fails in simulation.
          if (!sourceKind || *sourceKind == ir::SVAssertionKind::Restrict)
            return;
          std::optional<SourceInfo> source = getSourceInfo(operation);
          if (!source) {
            std::optional<SourceRange> site = getSiteRange(operation);
            if (!site)
              return;
            source = SourceInfo{std::move(*site), std::nullopt, {}};
          }
          coverage::AssertionKind kind = coverage::AssertionKind::Assert;
          if (*sourceKind == ir::SVAssertionKind::Assume)
            kind = coverage::AssertionKind::Assume;
          else if (*sourceKind == ir::SVAssertionKind::CoverProperty ||
                   *sourceKind == ir::SVAssertionKind::CoverSequence)
            kind = coverage::AssertionKind::Cover;
          // A statement label arrives as a synthetic named block.
          std::string name;
          if (Operation *parent = operation->getParentOp())
            if (auto path = parent->getAttrOfType<StringAttr>("block_path");
                path && isa<ir::SVBlockStatementOp>(parent))
              name = path.getValue().rsplit('.').second.str();
          uint64_t node = 0;
          if (auto attr = operation->getAttrOfType<IntegerAttr>("node_id"))
            node = attr.getValue().getZExtValue();
          AssertionCandidate candidate{
              operation,
              function,
              std::move(*source),
              scope->second,
              *codeUnit,
              node,
              scopeInventory.hierarchies.lookup(scope->second),
              {},
              false};
          candidate.point.name = std::move(name);
          candidate.point.kind = kind;
          assertions.push_back(std::move(candidate));
        });
      }
    }

    const bool hasAssertionIncludes = llvm::any_of(
        coverageConfiguration->includes,
        [](const CoverageRule &rule) { return rule.metrics.test(3); });
    llvm::DenseMap<uint64_t, Operation *> assertionOwners;
    for (AssertionCandidate &candidate : assertions) {
      const SourceRange &expanded = candidate.source.expanded;
      FailureOr<uint64_t> file = internFile(expanded.startFile);
      if (failed(file)) {
        signalPassFailure();
        return;
      }
      uint64_t hash = appendText(OBELISK_STABLE_HASH_OFFSET_BASIS, "assertion");
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.scope, 8);
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.codeUnit, 8);
      hash = appendSourceRange(hash, expanded, prefixMaps);
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.node, 8);
      if (!hash)
        hash = 1;
      auto [owner, inserted] =
          assertionOwners.try_emplace(hash, candidate.operation);
      if (!inserted) {
        candidate.operation->emitError(
            "coverage assertion-point ID hash collision")
            << " with " << owner->second->getLoc();
        signalPassFailure();
        return;
      }
      candidate.point.id = hash;
      candidate.point.scope = candidate.scope;
      candidate.point.file = *file;
      candidate.point.line = expanded.startLine;
      candidate.point.column = expanded.startColumn;
      candidate.point.endLine = expanded.endLine;
      candidate.point.endColumn = expanded.endColumn;

      const std::string storedPath =
          applyPrefixMaps(expanded.startFile, prefixMaps);
      const bool included =
          !hasAssertionIncludes ||
          llvm::any_of(
              coverageConfiguration->includes, [&](const CoverageRule &rule) {
                return matchesRule(rule, 3, storedPath, candidate.hierarchy);
              });
      const CoverageRule *excludingRule = nullptr;
      for (const CoverageRule &rule : coverageConfiguration->excludes)
        if (matchesRule(rule, 3, storedPath, candidate.hierarchy)) {
          excludingRule = &rule;
          break;
        }
      std::optional<StringRef> sourceReason =
          directiveReason(directiveFiles, 3, expanded);
      if (!sourceReason && candidate.source.original)
        sourceReason =
            directiveReason(directiveFiles, 3, *candidate.source.original);
      candidate.excluded = !included || excludingRule || sourceReason;
      if (candidate.excluded) {
        StringRef reason =
            sourceReason ? *sourceReason
            : excludingRule
                ? StringRef(excludingRule->reason)
                : StringRef("not selected by coverage include rules");
        schema.exclusions.push_back(
            {hash, coverage::MetricKind::Assertion, reason.str(), 0});
      }
    }

    sortCoverageRecords(assertions, [](const AssertionCandidate &entry) {
      return coverageOrderKey(entry.point.id);
    });
    // Assertion counters follow the line counters in the dense point space.
    uint64_t firstPoint = schema.linePoints.size();
    Builder builder(&getContext());
    llvm::DenseMap<Operation *, SmallVector<FlatSymbolRefAttr>> keepalives;
    for (auto [index, candidate] : llvm::enumerate(assertions)) {
      schema.assertionPoints.push_back(candidate.point);
      if (candidate.excluded)
        continue;
      cov::set<cov::AssertionPointIndex>(
          candidate.operation, builder.getI64IntegerAttr(firstPoint + index));
      auto design = candidate.function->getParentOfType<sim::SimDesignOp>();
      auto reference =
          FlatSymbolRefAttr::get(candidate.function.getSymNameAttr());
      SmallVectorImpl<FlatSymbolRefAttr> &references =
          keepalives[design.getOperation()];
      if (!llvm::is_contained(references, reference))
        references.push_back(reference);
    }
    for (auto &[operation, references] : keepalives) {
      auto design = cast<sim::SimDesignOp>(operation);
      OpBuilder keepaliveBuilder(&getContext());
      keepaliveBuilder.setInsertionPointToStart(&design.getBody().front());
      for (FlatSymbolRefAttr reference : references)
        sim::SimCoverageKeepaliveOp::create(keepaliveBuilder, design.getLoc(),
                                            reference);
    }
    cov::set<cov::AssertionPointCount>(
        module, builder.getI64IntegerAttr(assertions.size()));
  }

  // IEEE 1800-2023 40.2.2 and 40.4: FSM state coverage of the FSMs that
  // pragmas identify; automatic extraction is not required.
  if (isMetricEnabled(module, "fsm")) {
    llvm::StringMap<uint64_t> scopesByHierarchy;
    for (auto &[design, inventory] : scopeInventories)
      for (auto &[id, hierarchy] : inventory.hierarchies)
        scopesByHierarchy.try_emplace(hierarchy, id);
    llvm::StringMap<sim::SimStorageDeclOp> storagesByHierarchy;
    for (sim::SimDesignOp design : module.getOps<sim::SimDesignOp>())
      for (sim::SimStorageDeclOp declaration :
           design.getBody().front().getOps<sim::SimStorageDeclOp>())
        if (declaration.getLifetime() == sim::Lifetime::Design)
          if (std::optional<StringRef> name = declaration.getHierarchicalName())
            storagesByHierarchy.try_emplace(*name, declaration);
    llvm::DenseMap<StringRef, SmallVector<ir::SVInstanceBodySymbolOp>>
        instanceBodies;
    module.walk([&](ir::SVInstanceSymbolOp instance) {
      auto definition =
          instance->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      if (!definition)
        return;
      for (Operation *child : simlowering::getChildren(instance))
        if (auto body = dyn_cast<ir::SVInstanceBodySymbolOp>(child))
          instanceBodies[definition.getRootReference().getValue()].push_back(
              body);
    });

    struct FSMCandidate {
      coverage::FSMState state;
      std::string hierarchy;
      SourceRange site;
      bool excluded = false;
    };
    SmallVector<FSMCandidate, 0> fsmStates;
    llvm::MapVector<Operation *, SmallVector<Attribute>> fsmBindings;
    llvm::StringMap<FSMPragmaFile> pragmaFiles;
    WalkResult walked = module.walk([&](ir::SVDefinitionSymbolOp definition) {
      std::optional<SourceRange> range =
          decodeSourceRange(definition->getAttr("source_range"));
      if (!range)
        range = decodeSourceRange(definition->getAttr("original_source_range"));
      if (!range)
        return WalkResult::advance();
      auto [entry, inserted] = pragmaFiles.try_emplace(range->startFile);
      if (inserted) {
        FailureOr<FSMPragmaFile> parsed =
            parseFSMPragmas(module, range->startFile);
        if (failed(parsed))
          return WalkResult::interrupt();
        entry->second = std::move(*parsed);
      }
      const FSMPragmaFile &file = entry->second;
      auto inside = [&](size_t offset) {
        uint32_t line = file.lineOf(offset);
        return line >= range->startLine && line <= range->endLine;
      };
      // States of each enumeration, and the enumeration a current-state
      // signal declaration names (40.4.1, 40.4.5).
      llvm::StringMap<SmallVector<std::pair<std::string, size_t>>> states;
      llvm::StringMap<std::string> signalEnums;
      for (const FSMEnumPragma &pragma : file.enums) {
        if (!inside(pragma.names.front().second))
          continue;
        if (pragma.parameters)
          llvm::append_range(states[pragma.enumeration], pragma.names);
        else
          signalEnums.try_emplace(pragma.names.front().first,
                                  pragma.enumeration);
      }
      for (const FSMVectorPragma &vector : file.vectors) {
        if (!inside(vector.offset))
          continue;
        std::string enumeration = vector.enumeration;
        if (enumeration.empty())
          enumeration = signalEnums.lookup(vector.signals.front());
        auto names = states.find(enumeration);
        if (names == states.end())
          continue;
        for (ir::SVInstanceBodySymbolOp body :
             instanceBodies.lookup(definition.getSymName())) {
          auto path = body->getAttrOfType<StringAttr>("hierarchical_name");
          auto scope = path ? scopesByHierarchy.find(path.getValue())
                            : scopesByHierarchy.end();
          if (scope == scopesByHierarchy.end())
            continue;
          // Segments of the vector, least significant last.
          struct Segment {
            sim::SimStorageDeclOp storage;
            uint64_t low;
            uint64_t width;
          };
          SmallVector<Segment> segments;
          for (const std::string &signal : vector.signals) {
            auto storage = storagesByHierarchy.find(
                (path.getValue() + "." + signal).str());
            if (storage == storagesByHierarchy.end())
              break;
            Type type = storage->second.getType();
            std::optional<unsigned> width = sim::getPackedWidth(type);
            if (!width)
              break;
            uint64_t low = 0;
            uint64_t selected = *width;
            if (vector.range) {
              // A part-select indexes the declared packed range.
              int64_t left = static_cast<int64_t>(*width) - 1, right = 0;
              uint64_t element = 1;
              if (auto array = dyn_cast<sim::PackedArrayType>(type)) {
                left = array.getLeft();
                right = array.getRight();
                element = *width / sim::getAggregateNumElements(array);
              }
              auto offset = [&](int64_t index) -> std::optional<uint64_t> {
                int64_t position =
                    left >= right ? index - right : right - index;
                if (position < 0 ||
                    static_cast<uint64_t>(position) * element >= *width)
                  return std::nullopt;
                return static_cast<uint64_t>(position) * element;
              };
              std::optional<uint64_t> first = offset(vector.range->first);
              std::optional<uint64_t> second = offset(vector.range->second);
              if (!first || !second)
                break;
              low = std::min(*first, *second);
              selected = std::max(*first, *second) - low + element;
            }
            segments.push_back({storage->second, low, selected});
          }
          if (segments.size() != vector.signals.size())
            continue;
          uint64_t width = 0;
          for (const Segment &segment : segments)
            width += segment.width;
          if (width == 0 || width > 64)
            continue;
          uint64_t machine =
              appendText(OBELISK_STABLE_HASH_OFFSET_BASIS, "fsm");
          machine =
              obelisk_stable_hash_append_uint_le(machine, scope->second, 8);
          machine = appendText(machine, vector.name);
          if (!machine)
            machine = 1;
          size_t before = fsmStates.size();
          for (const auto &[name, offset] : names->second) {
            ir::SVParameterSymbolOp parameter;
            for (Operation *child : simlowering::getChildren(body))
              if (auto symbol = dyn_cast<ir::SVParameterSymbolOp>(child);
                  symbol && symbol->getAttrOfType<StringAttr>("name") &&
                  symbol->getAttrOfType<StringAttr>("name").getValue() == name)
                parameter = symbol;
            if (!parameter)
              continue;
            FailureOr<simlowering::ParsedConstant> parsed =
                simlowering::parseSVInteger(parameter.getConstantValue(), 64,
                                            parameter.getLoc());
            if (failed(parsed) || !parsed->unknown.isZero())
              continue;
            uint64_t value = parsed->value.getZExtValue();
            if (width < 64)
              value &= (uint64_t{1} << width) - 1;
            FailureOr<uint64_t> fileID = internFile(file.path);
            if (failed(fileID))
              return WalkResult::interrupt();
            uint64_t id =
                appendText(OBELISK_STABLE_HASH_OFFSET_BASIS, "fsm-state");
            id = obelisk_stable_hash_append_uint_le(id, machine, 8);
            id = appendText(id, name);
            if (!id)
              id = 1;
            FSMCandidate candidate;
            candidate.state = {id,
                               scope->second,
                               *fileID,
                               machine,
                               value,
                               vector.name,
                               name,
                               static_cast<uint32_t>(width),
                               file.lineOf(offset),
                               file.columnOf(offset),
                               0};
            candidate.hierarchy = path.getValue().str();
            candidate.site = {file.path,
                              file.path,
                              {},
                              file.lineOf(offset),
                              file.columnOf(offset),
                              file.lineOf(offset),
                              file.columnOf(offset)};
            fsmStates.push_back(std::move(candidate));
          }
          if (fsmStates.size() == before)
            continue;
          uint64_t shift = width;
          for (Segment &segment : segments) {
            shift -= segment.width;
            fsmBindings[segment.storage.getOperation()].push_back(
                cov::FsmBindingAttr::get(&getContext(), machine, shift,
                                         static_cast<int64_t>(segment.low),
                                         segment.width));
          }
        }
      }
      return WalkResult::advance();
    });
    if (walked.wasInterrupted()) {
      signalPassFailure();
      return;
    }

    const bool hasFSMIncludes = llvm::any_of(
        coverageConfiguration->includes,
        [](const CoverageRule &rule) { return rule.metrics.test(4); });
    llvm::DenseSet<uint64_t> fsmIDs;
    for (FSMCandidate &candidate : fsmStates) {
      if (!fsmIDs.insert(candidate.state.id).second) {
        module.emitError("coverage FSM-state ID hash collision for state '")
            << candidate.state.name << "' of " << candidate.hierarchy;
        signalPassFailure();
        return;
      }
      const std::string storedPath =
          applyPrefixMaps(candidate.site.startFile, prefixMaps);
      const bool included =
          !hasFSMIncludes ||
          llvm::any_of(
              coverageConfiguration->includes, [&](const CoverageRule &rule) {
                return matchesRule(rule, 4, storedPath, candidate.hierarchy);
              });
      const CoverageRule *excludingRule = nullptr;
      for (const CoverageRule &rule : coverageConfiguration->excludes)
        if (matchesRule(rule, 4, storedPath, candidate.hierarchy)) {
          excludingRule = &rule;
          break;
        }
      std::optional<StringRef> sourceReason =
          directiveReason(directiveFiles, 4, candidate.site);
      if (!included || excludingRule || sourceReason) {
        StringRef reason =
            sourceReason ? *sourceReason
            : excludingRule
                ? StringRef(excludingRule->reason)
                : StringRef("not selected by coverage include rules");
        schema.exclusions.push_back(
            {candidate.state.id, coverage::MetricKind::FSM, reason.str(), 0});
      }
    }
    sortCoverageRecords(fsmStates, [](const FSMCandidate &entry) {
      return coverageOrderKey(entry.state.id);
    });
    for (FSMCandidate &candidate : fsmStates)
      schema.fsmStates.push_back(std::move(candidate.state));
    for (auto &[operation, values] : fsmBindings) {
      cov::set<cov::FsmBindings>(operation,
                                 ArrayAttr::get(&getContext(), values));
      cov::mark<cov::ToggleObservable>(operation);
    }
  }

  if (isMetricEnabled(module, "toggle")) {
    SmallVector<ToggleCandidate, 0> toggles;
    for (sim::SimDesignOp design : module.getOps<sim::SimDesignOp>()) {
      ScopeInventory &scopeInventory =
          scopeInventories.find(design.getOperation())->second;
      llvm::DenseMap<uint64_t, Operation *> storages;
      llvm::DenseMap<uint64_t, Operation *> nets;
      auto addCandidate = [&](Operation *operation, Operation *canonical,
                              uint64_t scopeSourceID, uint64_t sourceLow,
                              Type type, StringRef hierarchy,
                              StringRef debugName) {
        auto scope = scopeInventory.ids.find(scopeSourceID);
        Type sourceType = type;
        if (auto sourceTypeAttr = cov::get<cov::SourceType>(operation))
          sourceType = sourceTypeAttr.getValue();
        FailureOr<ToggleTypePlan> plan = buildToggleTypePlan(type, sourceType);
        if (scope == scopeInventory.ids.end() || failed(plan) ||
            plan->fourState.size() > UINT32_MAX / 4 || hierarchy.empty())
          return;
        uint32_t scopeKind = scopeInventory.kinds.lookup(scope->second);
        using VPIKind = reflection::VPIObjectKind;
        // IEEE 1800-2017 6.21 and Clause 23 make variables and nets declared in
        // module, interface, and program scopes static design objects. Class,
        // package, and subroutine storage is outside toggle instrumentation.
        if (scopeKind != static_cast<uint16_t>(VPIKind::Module) &&
            scopeKind != static_cast<uint16_t>(VPIKind::Interface) &&
            scopeKind != static_cast<uint16_t>(VPIKind::Program))
          return;
        std::optional<SourceInfo> source = getSourceInfo(operation);
        if (!source) {
          std::optional<SourceRange> site = getSiteRange(operation);
          if (!site)
            return;
          source = SourceInfo{std::move(*site), std::nullopt, {}};
        }
        std::string name =
            debugName.empty() ? hierarchy.str() : debugName.str();
        toggles.push_back({operation,
                           canonical,
                           std::move(*source),
                           scope->second,
                           sourceLow,
                           plan->fourState.size(),
                           hierarchy.str(),
                           std::move(name),
                           type,
                           std::move(*plan),
                           false,
                           false,
                           {}});
        ToggleCandidate &candidate = toggles.back();
        candidate.fourState = llvm::any_of(
            candidate.plan.fourState, [](uint8_t value) { return value != 0; });
      };

      for (sim::SimStorageDeclOp declaration :
           design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
        storages[declaration.getId()] = declaration;
        if (declaration.getLifetime() != sim::Lifetime::Design ||
            sim::has<sim::metadata::SubroutineStorage>(declaration) ||
            !cov::has<cov::SourceAuthored>(declaration))
          continue;
        addCandidate(declaration, declaration, declaration.getScopeId(), 0,
                     declaration.getType(),
                     declaration.getHierarchicalName().value_or(StringRef{}),
                     declaration.getDebugName().value_or(StringRef{}));
      }
      for (sim::SimNetDeclOp declaration :
           design.getBody().front().getOps<sim::SimNetDeclOp>()) {
        nets[declaration.getId()] = declaration;
        if (declaration.getLifetime() != sim::Lifetime::Design ||
            declaration->hasAttr("internal") ||
            !cov::has<cov::SourceAuthored>(declaration))
          continue;
        addCandidate(declaration, declaration, declaration.getScopeId(), 0,
                     declaration.getType(),
                     declaration.getHierarchicalName().value_or(StringRef{}),
                     declaration.getDebugName().value_or(StringRef{}));
      }
      for (sim::SimPortDeclOp declaration :
           design.getBody().front().getOps<sim::SimPortDeclOp>()) {
        if (!cov::has<cov::SourceAuthored>(declaration))
          continue;
        Operation *canonical = declaration.getSourceIsNet()
                                   ? nets.lookup(declaration.getSourceId())
                                   : storages.lookup(declaration.getSourceId());
        if (!canonical)
          continue;
        addCandidate(declaration, canonical, declaration.getScopeId(),
                     declaration.getSourceLow(), declaration.getType(),
                     declaration.getHierarchicalName(),
                     declaration.getDebugName().value_or(StringRef{}));
      }
    }

    const bool hasToggleIncludes = llvm::any_of(
        coverageConfiguration->includes,
        [](const CoverageRule &rule) { return rule.metrics.test(1); });
    llvm::DenseMap<uint64_t, Operation *> toggleOwners;
    for (ToggleCandidate &candidate : toggles) {
      const SourceRange &expanded = candidate.source.expanded;
      FailureOr<uint64_t> file = internFile(expanded.startFile);
      if (failed(file)) {
        signalPassFailure();
        return;
      }
      uint64_t hash = appendText(OBELISK_STABLE_HASH_OFFSET_BASIS, "toggle");
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.scope, 8);
      hash = appendText(hash, candidate.hierarchy);
      hash = appendSourceRange(hash, expanded, prefixMaps);
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.sourceLow, 8);
      hash = obelisk_stable_hash_append_uint_le(hash, candidate.width, 8);
      hash = appendText(hash, candidate.operation->getName().getStringRef());
      if (!hash)
        hash = 1;
      auto [owner, inserted] =
          toggleOwners.try_emplace(hash, candidate.operation);
      if (!inserted) {
        candidate.operation->emitError(
            "coverage toggle-object ID hash collision")
            << " with " << owner->second->getLoc();
        signalPassFailure();
        return;
      }
      candidate.object = {hash,
                          candidate.scope,
                          *file,
                          candidate.name,
                          UINT32_MAX,
                          candidate.width,
                          expanded.startLine,
                          expanded.startColumn,
                          expanded.endLine,
                          expanded.endColumn,
                          candidate.fourState ? coverage::ToggleObjectFourState
                                              : uint32_t{0}};

      const std::string storedPath =
          applyPrefixMaps(expanded.startFile, prefixMaps);
      const bool included =
          !hasToggleIncludes ||
          llvm::any_of(
              coverageConfiguration->includes, [&](const CoverageRule &rule) {
                return matchesRule(rule, 1, storedPath, candidate.hierarchy);
              });
      const CoverageRule *excludingRule = nullptr;
      for (const CoverageRule &rule : coverageConfiguration->excludes)
        if (matchesRule(rule, 1, storedPath, candidate.hierarchy)) {
          excludingRule = &rule;
          break;
        }
      std::optional<StringRef> sourceReason =
          directiveReason(directiveFiles, 1, expanded);
      if (!sourceReason && candidate.source.original)
        sourceReason =
            directiveReason(directiveFiles, 1, *candidate.source.original);
      for (const MacroFrame &frame : candidate.source.macroFrames) {
        if (!sourceReason && frame.definition)
          sourceReason = directiveReason(directiveFiles, 1, *frame.definition);
        if (!sourceReason && frame.invocation)
          sourceReason = directiveReason(directiveFiles, 1, *frame.invocation);
      }
      candidate.excluded = !included || excludingRule || sourceReason;
      if (candidate.excluded) {
        StringRef reason =
            sourceReason ? *sourceReason
            : excludingRule
                ? StringRef(excludingRule->reason)
                : StringRef("not selected by coverage include rules");
        schema.exclusions.push_back(
            {hash, coverage::MetricKind::Toggle, reason.str(), 0});
      }
    }

    sortCoverageRecords(toggles, [](const ToggleCandidate &entry) {
      return coverageOrderKey(entry.object.id);
    });
    Builder builder(&getContext());
    llvm::DenseMap<Operation *, SmallVector<Attribute>> bindings;
    uint64_t base = 0;
    for (ToggleCandidate &candidate : toggles) {
      if (candidate.width > UINT64_MAX - base) {
        candidate.operation->emitError(
            "coverage toggle inventory is too large");
        signalPassFailure();
        return;
      }
      if (schema.toggleDimensions.size() > UINT32_MAX ||
          candidate.plan.dimensions.size() >
              UINT32_MAX - schema.toggleDimensions.size()) {
        candidate.operation->emitError(
            "coverage toggle type inventory is too large");
        signalPassFailure();
        return;
      }
      candidate.object.typeRoot = schema.toggleDimensions.size();
      schema.toggleObjects.push_back(candidate.object);
      uint32_t dimensionBase = schema.toggleDimensions.size();
      for (coverage::ToggleDimension dimension : candidate.plan.dimensions) {
        dimension.object = candidate.object.id;
        if (dimension.parent != UINT32_MAX)
          dimension.parent += dimensionBase;
        if (dimension.firstChild != UINT32_MAX)
          dimension.firstChild += dimensionBase;
        schema.toggleDimensions.push_back(std::move(dimension));
      }
      if (!candidate.excluded) {
        for (const ToggleBindingSegment &segment : candidate.plan.bindings) {
          if (segment.stateLow > UINT64_MAX - candidate.sourceLow) {
            candidate.operation->emitError(
                "coverage toggle binding offset is too large");
            signalPassFailure();
            return;
          }
          bindings[candidate.canonical].push_back(cov::ToggleBindingAttr::get(
              &getContext(), base + segment.objectLow,
              candidate.sourceLow + segment.stateLow, segment.width));
        }
        cov::mark<cov::ToggleObservable>(candidate.canonical);
      }
      base += candidate.width;
    }

    SmallVector<uint8_t> initialValue((base + 7) / 8, 0);
    SmallVector<uint8_t> initialUnknown((base + 7) / 8, 0);
    uint64_t initialBase = 0;
    for (const ToggleCandidate &candidate : toggles) {
      std::optional<sim::NetResolutionKind> resolution;
      if (auto net = dyn_cast<sim::SimNetDeclOp>(candidate.canonical))
        resolution = net.getResolutionKind();
      for (uint64_t bit = 0; bit != candidate.width; ++bit) {
        const bool fourState = candidate.plan.fourState[bit] != 0;
        bool value = false;
        bool unknown = fourState;
        if (resolution) {
          switch (*resolution) {
          case sim::NetResolutionKind::Tri0:
          case sim::NetResolutionKind::Supply0:
            unknown = false;
            break;
          case sim::NetResolutionKind::Tri1:
          case sim::NetResolutionKind::Supply1:
            value = true;
            unknown = false;
            break;
          case sim::NetResolutionKind::Wire:
          case sim::NetResolutionKind::Tri:
          case sim::NetResolutionKind::UWire:
          case sim::NetResolutionKind::WAnd:
          case sim::NetResolutionKind::WOr:
            // An undriven four-state net starts at Z. Two-state nets coerce
            // the same language value to zero.
            value = fourState;
            unknown = fourState;
            break;
          case sim::NetResolutionKind::TriReg:
            // An uninitialized charge store is X; two-state storage coerces
            // it to zero.
            value = false;
            unknown = fourState;
            break;
          }
        }
        setPackedBit(initialValue, initialBase + bit, value);
        setPackedBit(initialUnknown, initialBase + bit, unknown);
      }
      initialBase += candidate.width;
    }
    for (auto &[operation, values] : bindings)
      cov::set<cov::ToggleBindings>(operation, builder.getArrayAttr(values));
    cov::set<cov::ToggleBitCount>(module, builder.getI64IntegerAttr(base));
    cov::set<cov::ToggleInitialValue>(
        module,
        DenseI8ArrayAttr::get(&getContext(),
                              ArrayRef<int8_t>(reinterpret_cast<const int8_t *>(
                                                   initialValue.data()),
                                               initialValue.size())));
    cov::set<cov::ToggleInitialUnknown>(
        module,
        DenseI8ArrayAttr::get(&getContext(),
                              ArrayRef<int8_t>(reinterpret_cast<const int8_t *>(
                                                   initialUnknown.data()),
                                               initialUnknown.size())));
  }

  // Source-level functional coverage remains operational without a reporting
  // flag. Its typed execution plan is therefore required whenever an
  // executable covergroup declaration survives preparation; the metric flag
  // additionally controls persistence/unused-schema retention elsewhere.
  if (isMetricEnabled(module, "functional") || hasExecutableCovergroups) {
    uint32_t languageVersion = 2017;
    if (auto version = cov::get<cov::LanguageVersion>(module)) {
      uint64_t value = version.getValue().getZExtValue();
      if (value != 2017 && value != 2023) {
        module.emitError("functional coverage requires IEEE language version "
                         "2017 or 2023");
        signalPassFailure();
        return;
      }
      languageVersion = static_cast<uint32_t>(value);
    }
    const bool hasFunctionalIncludes = llvm::any_of(
        coverageConfiguration->includes,
        [](const CoverageRule &rule) { return rule.metrics.test(2); });
    llvm::DenseMap<uint64_t, Operation *> typeOwners;
    llvm::DenseMap<uint64_t, Operation *> itemOwners;
    llvm::DenseMap<uint64_t, Operation *> binOwners;
    llvm::DenseMap<uint64_t, Operation *> expressionOwners;
    llvm::DenseMap<uint64_t, Operation *> formalOwners;
    llvm::DenseMap<Type, std::pair<uint64_t, uint32_t>> aggregateTypeIDs;
    llvm::DenseMap<uint64_t, Type> aggregateTypesByID;
    llvm::DenseMap<Type, uint64_t> containerTypeIDs;
    llvm::DenseMap<uint64_t, Type> containerTypesByID;
    llvm::DenseMap<Type, uint64_t> classTypeIDs;
    llvm::DenseMap<uint64_t, Type> classTypesByID;
    llvm::DenseMap<Type, uint64_t> virtualTypeIDs;
    llvm::DenseMap<uint64_t, Type> virtualTypesByID;
    llvm::DenseMap<StringAttr, uint64_t> interfaceTypeIDs;
    llvm::DenseMap<uint64_t, StringAttr> interfaceNamesByID;
    llvm::DenseMap<StringAttr, uint64_t> runtimeClassIDs;
    module.walk([&](sim::SimClassDeclOp declaration) {
      runtimeClassIDs.try_emplace(declaration.getSymNameAttr(),
                                  declaration.getId());
    });
    llvm::DenseMap<StringAttr, uint64_t> covergroupTypeIDs;
    module.walk([&](sim::SimCovergroupDeclOp declaration) {
      covergroupTypeIDs.try_emplace(declaration.getSymNameAttr(),
                                    declaration.getSchemaType());
    });
    llvm::DenseMap<uint64_t, Operation *> selectorOwners;
    struct PendingValueSet {
      coverage::FunctionalValueSet set;
      SmallVector<coverage::FunctionalValueAtom> atoms;
      SmallVector<coverage::FunctionalValueLimb> limbs;
    };
    struct PendingCrossPlan {
      coverage::CrossPlan plan;
      SmallVector<coverage::CrossTarget> targets;
      SmallVector<coverage::CrossBinPlan> bins;
      SmallVector<coverage::CrossSelectorNode> selectors;
      SmallVector<coverage::CrossPredicateNode> predicates;
      SmallVector<uint64_t> predicateOperands;
      SmallVector<coverage::CrossSelectorOperand> selectorOperands;
    };
    struct PendingTransitionAlternative {
      coverage::TransitionAlternative alternative;
      SmallVector<coverage::TransitionStep> steps;
    };
    struct PendingTransitionProgram {
      coverage::TransitionProgram program;
      SmallVector<PendingTransitionAlternative> alternatives;
    };
    SmallVector<PendingValueSet> pendingValueSets;
    SmallVector<PendingCrossPlan, 0> pendingCrossPlans;
    SmallVector<PendingTransitionProgram, 0> pendingTransitionPrograms;
    llvm::DenseMap<uint64_t, Operation *> valueSetOwners;
    llvm::StringMap<ir::SVCovergroupTypeOp> semanticCovergroups;
    using EnumIdentity = std::pair<Type, Attribute>;
    llvm::DenseMap<EnumIdentity, SmallVector<ir::SVEnumValueSymbolOp>>
        enumDeclarations;
    auto enumIdentity = [](Operation *owner, Type type) -> EnumIdentity {
      return {type, owner->getAttr(simlowering::vpiSourceTypeIdentityAttrName)};
    };
    module.walk([&](ir::SVCovergroupTypeOp covergroup) {
      auto handle =
          dyn_cast<ir::CovergroupHandleType>(covergroup.getSemanticType());
      if (!handle)
        return;
      StringAttr symbol = simlowering::getSimulationCovergroupSymbol(
          handle.getCovergroupName());
      semanticCovergroups.try_emplace(symbol.getValue(), covergroup);
    });
    // Enumerators can be imported directly into their lexical scope, without
    // an enum-type AST wrapper. Display names can coincide across compilation
    // units, so use the exact source type identity when it is available.
    module.walk([&](ir::SVEnumValueSymbolOp enumerator) {
      auto type = enumerator->getAttrOfType<TypeAttr>("semantic_type");
      if (!type || !isa<ir::EnumType>(type.getValue()))
        return;
      auto &members =
          enumDeclarations[enumIdentity(enumerator, type.getValue())];
      if (llvm::none_of(members, [&](auto member) {
            return simlowering::getDebugName(member) ==
                   simlowering::getDebugName(enumerator);
          }))
        members.push_back(enumerator);
    });
    auto reportName = [](StringRef explicitName, StringRef hierarchy) {
      if (!explicitName.empty())
        return explicitName.str();
      size_t separator = hierarchy.find_last_of(".:");
      return hierarchy
          .drop_front(separator == StringRef::npos ? 0 : separator + 1)
          .str();
    };
    for (sim::SimDesignOp design : module.getOps<sim::SimDesignOp>()) {
      ScopeInventory &scopeInventory =
          scopeInventories.find(design.getOperation())->second;
      auto resolveScope = [&](StringRef hierarchy) -> uint64_t {
        uint64_t root = 0;
        uint64_t best = 0;
        size_t bestLength = 0;
        for (const auto &[id, name] : scopeInventory.hierarchies) {
          if (name == "$root")
            root = id;
          bool prefix = hierarchy == name;
          if (!prefix && hierarchy.starts_with(name) &&
              hierarchy.size() > name.size()) {
            StringRef remainder = hierarchy.drop_front(name.size());
            prefix = remainder.starts_with(".") || remainder.starts_with("::");
          }
          if (prefix && name.size() > bestLength) {
            best = id;
            bestLength = name.size();
          }
        }
        return best ? best : root;
      };

      for (sim::SimCovergroupDeclOp declaration :
           design.getBody().front().getOps<sim::SimCovergroupDeclOp>()) {
        const uint64_t typeID = declaration.getSchemaType();
        auto [typeOwner, insertedType] =
            typeOwners.try_emplace(typeID, declaration.getOperation());
        if (!insertedType) {
          declaration.emitError("functional coverage type ID collision")
              << " with " << typeOwner->second->getLoc();
          signalPassFailure();
          return;
        }
        auto source = semanticCovergroups.find(declaration.getSymName());
        if (source == semanticCovergroups.end()) {
          declaration.emitError(
              "has no semantic source for its embedded coverage schema");
          signalPassFailure();
          return;
        }
        ir::SVCovergroupTypeOp semanticCovergroup = source->second;
        if (typeID != simlowering::stableFunctionalTypeID(semanticCovergroup)) {
          declaration.emitError(
              "schema type ID does not match its semantic covergroup");
          signalPassFailure();
          return;
        }
        std::string typeHierarchy =
            simlowering::getHierarchyName(semanticCovergroup).str();
        if (typeHierarchy.empty())
          typeHierarchy = semanticCovergroup.getSymName().str();
        if (auto owner =
                semanticCovergroup->getParentOfType<ir::SVClassTypeOp>())
          for (Operation *member : simlowering::getChildren(owner)) {
            auto property = dyn_cast<ir::SVClassPropertySymbolOp>(member);
            if (property && property.getSemanticType() ==
                                semanticCovergroup.getSemanticType()) {
              StringRef propertyHierarchy =
                  simlowering::getHierarchyName(property);
              if (!propertyHierarchy.empty()) {
                typeHierarchy = propertyHierarchy.str();
                break;
              }
            }
          }
        uint64_t scope = resolveScope(typeHierarchy);
        if (!scope) {
          declaration.emitError(
              "cannot resolve an enclosing functional coverage scope");
          signalPassFailure();
          return;
        }
        // An embedded covergroup declares an anonymous type and an instance
        // variable (IEEE 1800-2023 19.4); reports name the type after that
        // variable, the last component of its hierarchy.
        schema.functionalTypes.push_back(
            {typeID, scope,
             reportName(declaration.getDebugName().value_or(""), typeHierarchy),
             0, languageVersion, typeHierarchy});
        if (auto base = declaration.getBaseAttr()) {
          schema.functionalTypes.back().base =
              covergroupTypeIDs.lookup(base.getRootReference());
          if (!schema.functionalTypes.back().base) {
            declaration.emitError("cannot resolve base covergroup schema type");
            signalPassFailure();
            return;
          }
        }

        uint32_t itemOrdinal = 0;
        llvm::DenseMap<Operation *, uint64_t> functionalItemIDs;
        llvm::DenseMap<Operation *, std::string> functionalItemHierarchies;
        llvm::StringMap<uint64_t> coverpointItemIDs;
        llvm::StringMap<Operation *> coverpointOperations;
        struct CoverpointBinTarget {
          uint64_t item = 0;
          uint64_t bin = 0;
          Operation *coverpoint = nullptr;
        };
        llvm::StringMap<CoverpointBinTarget> coverpointBinTargets;
        uint32_t sampleResultOrdinal = 0;
        uint32_t constructorResultOrdinal = 0;
        uint32_t eventResultOrdinal = 0;
        uint32_t optionResultOrdinal = 0;
        auto addFunctionalExpression =
            [&](Operation *expression, uint64_t owner,
                coverage::FunctionalExpressionOwnerKind ownerKind,
                coverage::FunctionalExpressionRole role,
                coverage::FunctionalExpressionEvaluationPhase phase,
                uint32_t ownerOrdinal, uint32_t ownerSubordinal,
                std::optional<coverage::FunctionalExpressionResultKind>
                    forcedResult = std::nullopt,
                std::optional<uint32_t> forcedBitWidth = std::nullopt,
                std::optional<coverage::CoverageSignedness> forcedSignedness =
                    std::nullopt,
                uint32_t expressionFlags = 0) -> LogicalResult {
          coverage::Digest semanticDigest =
              functionalSemanticDigest(expression);
          const uint64_t expressionID = stableFunctionalExpressionID(
              owner, ownerKind, role, ownerOrdinal, ownerSubordinal, expression,
              semanticDigest, prefixMaps);
          auto [previous, inserted] =
              expressionOwners.try_emplace(expressionID, expression);
          if (!inserted)
            return expression->emitError(
                       "functional coverage expression ID collision with ")
                   << previous->second->getLoc();

          coverage::FunctionalExpression descriptor;
          descriptor.id = expressionID;
          descriptor.owner = owner;
          descriptor.ownerKind = ownerKind;
          descriptor.role = role;
          descriptor.flags = expressionFlags;
          descriptor.semanticDigest = semanticDigest;
          descriptor.ownerOrdinal = ownerOrdinal;
          descriptor.ownerSubordinal = ownerSubordinal;
          descriptor.evaluationPhase = phase;
          switch (phase) {
          case coverage::FunctionalExpressionEvaluationPhase::Sample:
            descriptor.resultOrdinal = sampleResultOrdinal++;
            break;
          case coverage::FunctionalExpressionEvaluationPhase::Option:
            descriptor.resultOrdinal = optionResultOrdinal++;
            break;
          case coverage::FunctionalExpressionEvaluationPhase::Event:
            descriptor.resultOrdinal = eventResultOrdinal++;
            break;
          default:
            descriptor.resultOrdinal = constructorResultOrdinal++;
            break;
          }
          if (forcedResult) {
            descriptor.resultKind = *forcedResult;
            descriptor.signedness = coverage::CoverageSignedness::NotApplicable;
            if (*forcedResult ==
                coverage::FunctionalExpressionResultKind::Set) {
              FailureOr<Type> normalized =
                  simlowering::getNormalizedSemanticType(expression);
              Type element;
              if (succeeded(normalized)) {
                if (auto packed = dyn_cast<sim::PackedArrayType>(*normalized))
                  element = packed.getElementType();
                else if (auto fixed =
                             dyn_cast<sim::UnpackedArrayType>(*normalized))
                  element = fixed.getElementType();
                else if (auto dynamic =
                             dyn_cast<sim::DynamicArrayType>(*normalized))
                  element = dynamic.getElementType();
                else if (auto queue = dyn_cast<sim::QueueType>(*normalized))
                  element = queue.getElementType();
              }
              if (auto real = dyn_cast<FloatType>(element)) {
                if (real.getWidth() != 32 && real.getWidth() != 64)
                  return expression->emitError(
                      "functional coverage set expression has an unsupported "
                      "real element type");
                descriptor.bitWidth = real.getWidth();
              } else {
                std::optional<unsigned> width = sim::getPackedWidth(element);
                auto semanticType =
                    expression->getAttrOfType<TypeAttr>("semantic_type");
                Type semanticElement =
                    semanticType
                        ? getCoverageSetElementType(semanticType.getValue())
                        : Type{};
                if (!width || !*width || *width > UINT32_MAX ||
                    !semanticElement)
                  return expression->emitError(
                      "functional coverage set expression does not have a "
                      "supported element type");
                descriptor.bitWidth = static_cast<uint32_t>(*width);
                descriptor.signedness =
                    simlowering::isSignedSemanticType(semanticElement)
                        ? coverage::CoverageSignedness::Signed
                        : coverage::CoverageSignedness::Unsigned;
                if (isa<sim::LogicType>(sim::getPackedScalarType(element)))
                  descriptor.flags |=
                      coverage::FunctionalExpressionSetElementFourState;
              }
            }
          } else {
            FailureOr<Type> normalized =
                simlowering::getNormalizedSemanticType(expression);
            if (failed(normalized))
              return failure();
            if (isa<sim::StringType>(*normalized)) {
              descriptor.resultKind =
                  coverage::FunctionalExpressionResultKind::String;
              descriptor.signedness =
                  coverage::CoverageSignedness::NotApplicable;
            } else if (isa<Float32Type, Float64Type>(*normalized)) {
              descriptor.resultKind =
                  (*normalized).isF32()
                      ? coverage::FunctionalExpressionResultKind::ShortReal
                      : coverage::FunctionalExpressionResultKind::Real;
              descriptor.signedness =
                  coverage::CoverageSignedness::NotApplicable;
            } else {
              std::optional<unsigned> width = sim::getPackedWidth(*normalized);
              if (!width || !*width || *width > UINT32_MAX)
                return expression->emitError(
                    "functional coverage expression does not have a "
                    "supported integral, real, or string result type");
              descriptor.resultKind =
                  coverage::FunctionalExpressionResultKind::Integral;
              descriptor.bitWidth = static_cast<uint32_t>(*width);
              auto signedAttr =
                  expression->getAttrOfType<BoolAttr>("is_signed");
              auto semanticType =
                  expression->getAttrOfType<TypeAttr>("semantic_type");
              descriptor.signedness =
                  (signedAttr
                       ? signedAttr.getValue()
                       : semanticType && simlowering::isSignedSemanticType(
                                             semanticType.getValue()))
                      ? coverage::CoverageSignedness::Signed
                      : coverage::CoverageSignedness::Unsigned;
            }
          }
          if (forcedBitWidth || forcedSignedness) {
            if (!forcedBitWidth || !*forcedBitWidth || !forcedSignedness ||
                (descriptor.resultKind ==
                         coverage::FunctionalExpressionResultKind::Aggregate
                     ? *forcedSignedness !=
                           coverage::CoverageSignedness::NotApplicable
                     : *forcedSignedness ==
                               coverage::CoverageSignedness::NotApplicable ||
                           descriptor.resultKind !=
                               coverage::FunctionalExpressionResultKind::
                                   Integral))
              return expression->emitError(
                  "functional expression has an invalid forced "
                  "result type");
            descriptor.bitWidth = *forcedBitWidth;
            descriptor.signedness = *forcedSignedness;
          }
          if (role == coverage::FunctionalExpressionRole::CoverpointSample &&
              descriptor.resultKind ==
                  coverage::FunctionalExpressionResultKind::Integral) {
            auto type = expression->getAttrOfType<TypeAttr>("semantic_type");
            if (type && simlowering::isFourStateSemanticType(type.getValue()))
              descriptor.flags |=
                  coverage::FunctionalExpressionCoverpointFourState;
          }
          schema.functionalExpressions.push_back(descriptor);
          cov::set<cov::FunctionalExpressionId>(
              expression, IntegerAttr::get(IntegerType::get(&getContext(), 64),
                                           expressionID));
          cov::set<cov::FunctionalExpressionOrdinal>(
              expression, IntegerAttr::get(IntegerType::get(&getContext(), 32),
                                           descriptor.resultOrdinal));
          cov::set<cov::FunctionalExpressionKind>(
              expression,
              IntegerAttr::get(IntegerType::get(&getContext(), 32),
                               static_cast<uint32_t>(descriptor.resultKind)));
          cov::set<cov::FunctionalExpressionBitWidth>(
              expression, IntegerAttr::get(IntegerType::get(&getContext(), 32),
                                           descriptor.bitWidth));
          cov::set<cov::FunctionalExpressionSignedness>(
              expression,
              IntegerAttr::get(IntegerType::get(&getContext(), 32),
                               static_cast<uint32_t>(descriptor.signedness)));
          cov::set<cov::FunctionalExpressionPhase>(
              expression, IntegerAttr::get(IntegerType::get(&getContext(), 32),
                                           static_cast<uint32_t>(
                                               descriptor.evaluationPhase)));
          cov::set<cov::FunctionalExpressionRole>(
              expression,
              IntegerAttr::get(IntegerType::get(&getContext(), 32),
                               static_cast<uint32_t>(descriptor.role)));
          return success();
        };
        if (semanticCovergroup.getCoverageEventKind() ==
            ir::SVCoverageEventKind::Clocking) {
          Operation *control = nullptr;
          for (Operation *child : simlowering::getChildren(semanticCovergroup))
            if (isa<ir::SVSignalEventControlOp, ir::SVEventListControlOp>(
                    child)) {
              control = child;
              break;
            }
          SmallVector<ir::SVSignalEventControlOp> events;
          if (auto event =
                  dyn_cast_or_null<ir::SVSignalEventControlOp>(control))
            events.push_back(event);
          else if (control)
            for (Operation *child : simlowering::getChildren(control))
              if (auto event = dyn_cast<ir::SVSignalEventControlOp>(child))
                events.push_back(event);
          for (auto [ordinal, event] : llvm::enumerate(events))
            if (failed(addFunctionalExpression(
                    event, typeID,
                    coverage::FunctionalExpressionOwnerKind::Type,
                    coverage::FunctionalExpressionRole::SamplingEvent,
                    coverage::FunctionalExpressionEvaluationPhase::Event,
                    static_cast<uint32_t>(ordinal), 0,
                    coverage::FunctionalExpressionResultKind::Boolean))) {
              event.emitError(
                  "cannot inventory the typed clocking coverage event");
              signalPassFailure();
              return;
            }
        } else if (semanticCovergroup.getCoverageEventKind() ==
                   ir::SVCoverageEventKind::Block) {
          ir::SVBlockEventListControlOp control;
          for (Operation *child : simlowering::getChildren(semanticCovergroup))
            if ((control = dyn_cast<ir::SVBlockEventListControlOp>(child)))
              break;
          if (!control) {
            semanticCovergroup.emitError(
                "block-event covergroup has no typed event control");
            signalPassFailure();
            return;
          }
          SmallVector<Operation *> targets = simlowering::getChildren(control);
          ArrayAttr kinds = control.getEventKinds();
          if (targets.size() != kinds.size()) {
            control.emitError("block-event target and kind counts differ");
            signalPassFailure();
            return;
          }
          for (auto [ordinal, target] : llvm::enumerate(targets)) {
            auto kind = cast<ir::SVCoverageBlockEventKindAttr>(kinds[ordinal]);
            uint32_t flags = coverage::FunctionalExpressionSamplingEventBlock;
            if (kind.getValue() == ir::SVCoverageBlockEventKind::End)
              flags |= coverage::FunctionalExpressionSamplingEventEnd;
            if (failed(addFunctionalExpression(
                    target, typeID,
                    coverage::FunctionalExpressionOwnerKind::Type,
                    coverage::FunctionalExpressionRole::SamplingEvent,
                    coverage::FunctionalExpressionEvaluationPhase::Event,
                    static_cast<uint32_t>(ordinal), 0,
                    coverage::FunctionalExpressionResultKind::Boolean,
                    std::nullopt, std::nullopt, flags))) {
              target->emitError(
                  "cannot inventory the typed block coverage event");
              signalPassFailure();
              return;
            }
          }
        }
        auto addFunctionalOption =
            [&](ir::SVCoverageOptionOp option, uint64_t owner,
                coverage::FunctionalConfigurationOptionOwnerKind ownerKind,
                ir::SVCoverageOptionOwnerKind expectedOwnerKind)
            -> LogicalResult {
          std::optional<coverage::FunctionalConfigurationOptionKind> optionKind;
          switch (option.getOptionKind()) {
          case ir::SVCoverageOptionKind::Name:
            optionKind = coverage::FunctionalConfigurationOptionKind::Name;
            break;
          case ir::SVCoverageOptionKind::Comment:
            optionKind = coverage::FunctionalConfigurationOptionKind::Comment;
            break;
          case ir::SVCoverageOptionKind::Weight:
            optionKind = coverage::FunctionalConfigurationOptionKind::Weight;
            break;
          case ir::SVCoverageOptionKind::Goal:
            optionKind = coverage::FunctionalConfigurationOptionKind::Goal;
            break;
          case ir::SVCoverageOptionKind::AtLeast:
            optionKind = coverage::FunctionalConfigurationOptionKind::AtLeast;
            break;
          case ir::SVCoverageOptionKind::AutoBinMax:
            optionKind =
                coverage::FunctionalConfigurationOptionKind::AutoBinMax;
            break;
          case ir::SVCoverageOptionKind::CrossNumPrintMissing:
            optionKind = coverage::FunctionalConfigurationOptionKind::
                CrossNumPrintMissing;
            break;
          case ir::SVCoverageOptionKind::DetectOverlap:
            optionKind =
                coverage::FunctionalConfigurationOptionKind::DetectOverlap;
            break;
          case ir::SVCoverageOptionKind::PerInstance:
            optionKind =
                coverage::FunctionalConfigurationOptionKind::PerInstance;
            break;
          case ir::SVCoverageOptionKind::GetInstCoverage:
            optionKind =
                coverage::FunctionalConfigurationOptionKind::GetInstCoverage;
            break;
          case ir::SVCoverageOptionKind::MergeInstances:
            optionKind =
                coverage::FunctionalConfigurationOptionKind::MergeInstances;
            break;
          case ir::SVCoverageOptionKind::DistributeFirst:
            optionKind =
                coverage::FunctionalConfigurationOptionKind::DistributeFirst;
            break;
          case ir::SVCoverageOptionKind::CrossRetainAutoBins:
            optionKind = coverage::FunctionalConfigurationOptionKind::
                CrossRetainAutoBins;
            break;
          case ir::SVCoverageOptionKind::RealInterval:
            optionKind =
                coverage::FunctionalConfigurationOptionKind::RealInterval;
            break;
          case ir::SVCoverageOptionKind::Strobe:
            optionKind = coverage::FunctionalConfigurationOptionKind::Strobe;
            break;
          default:
            break;
          }
          SmallVector<Operation *> children = simlowering::getChildren(option);
          // A definition cannot assign the same owner/scope/option twice.
          // Use that semantic key for stable identity; resultOrdinal still
          // preserves definition order for construction-time evaluation.
          const uint32_t planOrdinal =
              optionKind ? static_cast<uint32_t>(*optionKind) : 0;
          const auto expressionOwnerKind =
              ownerKind ==
                      coverage::FunctionalConfigurationOptionOwnerKind::Group
                  ? coverage::FunctionalExpressionOwnerKind::Type
                  : coverage::FunctionalExpressionOwnerKind::Item;
          const coverage::FunctionalOptionScopeKind scope =
              option.getScopeKind() == ir::SVCoverageOptionScopeKind::Type
                  ? coverage::FunctionalOptionScopeKind::Type
                  : coverage::FunctionalOptionScopeKind::Instance;
          const std::optional<coverage::FunctionalExpressionResultKind>
              forcedResult =
                  optionKind &&
                          llvm::is_contained(
                              {coverage::FunctionalConfigurationOptionKind::
                                   DetectOverlap,
                               coverage::FunctionalConfigurationOptionKind::
                                   PerInstance,
                               coverage::FunctionalConfigurationOptionKind::
                                   GetInstCoverage,
                               coverage::FunctionalConfigurationOptionKind::
                                   MergeInstances,
                               coverage::FunctionalConfigurationOptionKind::
                                   DistributeFirst,
                               coverage::FunctionalConfigurationOptionKind::
                                   Strobe,
                               coverage::FunctionalConfigurationOptionKind::
                                   CrossRetainAutoBins},
                              *optionKind)
                      ? std::optional(
                            coverage::FunctionalExpressionResultKind::Boolean)
                      : std::nullopt;
          if (!optionKind || option.getOwnerKind() != expectedOwnerKind ||
              children.size() != 1 ||
              failed(addFunctionalExpression(
                  children.front(), owner, expressionOwnerKind,
                  coverage::FunctionalExpressionRole::OptionRHS,
                  coverage::FunctionalExpressionEvaluationPhase::Option,
                  planOrdinal, static_cast<uint32_t>(scope), forcedResult)))
            return option.emitError(
                "cannot build the typed functional option plan");

          const uint64_t expressionID =
              cov::get<cov::FunctionalExpressionId>(children.front())
                  .getValue()
                  .getZExtValue();
          schema.functionalOptionPlans.push_back({owner, expressionID,
                                                  ownerKind, scope, *optionKind,
                                                  planOrdinal, 0});
          return success();
        };
        uint32_t constructorFormalOrdinal = 0;
        uint32_t sampleFormalOrdinal = 0;
        for (Operation *child : simlowering::getChildren(semanticCovergroup)) {
          auto formal = dyn_cast<ir::SVFormalArgumentSymbolOp>(child);
          if (!formal)
            continue;
          const bool sample =
              formal.getIsCoverageSampleFormal().value_or(false);
          uint32_t ordinal =
              sample ? sampleFormalOrdinal++ : constructorFormalOrdinal++;
          const uint64_t formalID =
              stableFunctionalEntityID(sample ? "functional.sample-formal"
                                              : "functional.constructor-formal",
                                       typeID, formal);
          auto [previous, inserted] =
              formalOwners.try_emplace(formalID, formal.getOperation());
          if (!inserted) {
            formal.emitError("functional coverage formal ID collision")
                << " with " << previous->second->getLoc();
            signalPassFailure();
            return;
          }
          FailureOr<Type> normalized =
              simlowering::getNormalizedSemanticType(formal);
          if (failed(normalized)) {
            formal.emitError("cannot normalize functional formal type");
            signalPassFailure();
            return;
          }
          coverage::FunctionalFormal descriptor;
          descriptor.id = formalID;
          descriptor.type = typeID;
          descriptor.name = reportName(simlowering::getDebugName(formal),
                                       simlowering::getHierarchyName(formal));
          descriptor.kind = sample
                                ? coverage::FunctionalFormalKind::Sample
                                : coverage::FunctionalFormalKind::Constructor;
          descriptor.direction =
              formal.getDirection() == ir::SVArgumentDirection::Ref
                  ? coverage::FunctionalFormalDirection::Ref
                  : coverage::FunctionalFormalDirection::Input;
          descriptor.ordinal = ordinal;
          if (isa<sim::SemaphoreType>(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::Semaphore;
          } else if (auto handle =
                         dyn_cast<sim::VirtualInterfaceType>(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::VirtualInterface;
            auto [entry, inserted] = virtualTypeIDs.try_emplace(*normalized, 0);
            if (inserted) {
              auto [identity, fresh] =
                  interfaceTypeIDs.try_emplace(handle.getInterfaceName(), 0);
              if (fresh) {
                identity->second =
                    sim::getStableStorageTypeID(sim::VirtualInterfaceType::get(
                        formal.getContext(), handle.getInterfaceName(),
                        StringAttr::get(formal.getContext(), "")));
                auto [previousName, unique] = interfaceNamesByID.try_emplace(
                    identity->second, handle.getInterfaceName());
                if (!unique &&
                    previousName->second != handle.getInterfaceName()) {
                  formal.emitError("functional interface type ID collision");
                  signalPassFailure();
                  return;
                }
              }
              coverage::FunctionalVirtualInterfaceType layout{
                  sim::getStableStorageTypeID(*normalized), identity->second};
              auto [previousType, unique] =
                  virtualTypesByID.try_emplace(layout.id, *normalized);
              if (!unique && previousType->second != *normalized) {
                formal.emitError(
                    "functional virtual interface type ID collision");
                signalPassFailure();
                return;
              }
              entry->second = layout.id;
              schema.functionalVirtualInterfaceTypes.push_back(layout);
            }
            descriptor.valueType = entry->second;
          } else if (isa<sim::ProcessType>(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::Process;
          } else if (auto handle =
                         dyn_cast<sim::CovergroupHandleType>(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::Covergroup;
            descriptor.valueType = covergroupTypeIDs.lookup(
                handle.getCovergroupName().getLeafReference());
            if (!descriptor.valueType) {
              formal.emitError("functional covergroup type is unresolved");
              signalPassFailure();
              return;
            }
          } else if (isa<sim::EventType>(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::Event;
          } else if (isa<sim::ChandleType>(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::Chandle;
          } else if (isa<sim::DynamicArrayType, sim::QueueType,
                         sim::AssocArrayType, sim::MailboxType>(*normalized)) {
            descriptor.resultKind =
                isa<sim::MailboxType>(*normalized)
                    ? coverage::FunctionalExpressionResultKind::Mailbox
                    : coverage::FunctionalExpressionResultKind::Container;
            auto [entry, inserted] =
                containerTypeIDs.try_emplace(*normalized, 0);
            if (inserted) {
              coverage::FunctionalContainerType layout;
              layout.id = sim::getStableStorageTypeID(*normalized);
              auto [previousType, unique] =
                  containerTypesByID.try_emplace(layout.id, *normalized);
              if (!unique && previousType->second != *normalized) {
                formal.emitError("functional container type ID collision");
                signalPassFailure();
                return;
              }
              Type element;
              if (auto array = dyn_cast<sim::DynamicArrayType>(*normalized)) {
                layout.kind = coverage::FunctionalContainerKind::DynamicArray;
                element = array.getElementType();
              } else if (auto queue = dyn_cast<sim::QueueType>(*normalized)) {
                layout.kind = coverage::FunctionalContainerKind::Queue;
                layout.bound = queue.getBound();
                element = queue.getElementType();
              } else if (auto mailbox =
                             dyn_cast<sim::MailboxType>(*normalized)) {
                layout.kind = coverage::FunctionalContainerKind::Mailbox;
                element = mailbox.getElementType();
              } else {
                auto array = cast<sim::AssocArrayType>(*normalized);
                layout.kind =
                    coverage::FunctionalContainerKind::AssociativeArray;
                element = array.getElementType();
                std::optional<sim::AssocKeyLayout> key =
                    sim::getAssocKeyLayout(array);
                if (!key) {
                  formal.emitError("functional container key type has no "
                                   "runtime key representation");
                  signalPassFailure();
                  return;
                }
                // The coverage schema mirrors the runtime key kinds.
                layout.keyKind =
                    static_cast<coverage::FunctionalContainerKeyKind>(
                        static_cast<uint32_t>(key->kind));
                layout.keyWidth = key->width;
              }
              layout.elementType = sim::getStableStorageTypeID(element);
              if (auto mailbox = dyn_cast<sim::MailboxType>(*normalized))
                layout.elementType = mailbox.getMessageTypeID();
              entry->second = layout.id;
              schema.functionalContainerTypes.push_back(layout);
            }
            descriptor.valueType = entry->second;
          } else if (auto handle =
                         dyn_cast<sim::ClassHandleType>(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::ClassHandle;
            auto [entry, inserted] = classTypeIDs.try_emplace(*normalized, 0);
            if (inserted) {
              coverage::FunctionalClassType layout{
                  sim::getStableStorageTypeID(*normalized),
                  runtimeClassIDs.lookup(
                      handle.getClassName().getLeafReference())};
              auto [previousType, unique] =
                  classTypesByID.try_emplace(layout.id, *normalized);
              if (!layout.classType ||
                  (!unique && previousType->second != *normalized)) {
                formal.emitError("functional class type is unresolved or has "
                                 "an ID collision");
                signalPassFailure();
                return;
              }
              entry->second = layout.id;
              schema.functionalClassTypes.push_back(layout);
            }
            descriptor.valueType = entry->second;
          } else if (sim::isAggregateType(*normalized) &&
                     !sim::getPackedScalarType(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::Aggregate;
            descriptor.signedness = coverage::CoverageSignedness::NotApplicable;
            auto [entry, inserted] = aggregateTypeIDs.try_emplace(
                *normalized, std::pair<uint64_t, uint32_t>{});
            if (inserted) {
              auto element = simlowering::describeContainerElement(
                  *normalized, simlowering::getSemanticLocation(formal));
              auto width = sim::getProvenanceSpan(*normalized);
              if (failed(element) || !width || !*width || *width > UINT32_MAX) {
                formal.emitError(
                    "functional aggregate formal has no fixed layout");
                signalPassFailure();
                return;
              }
              coverage::FunctionalAggregateType layout{
                  sim::getStableStorageTypeID(*normalized),
                  element->typeID,
                  element->valueSize,
                  element->alignment,
                  static_cast<uint32_t>(*width),
                  static_cast<uint32_t>(element->flags)};
              auto [previousType, unique] =
                  aggregateTypesByID.try_emplace(layout.id, *normalized);
              if (!unique && previousType->second != *normalized) {
                formal.emitError("functional aggregate type ID collision");
                signalPassFailure();
                return;
              }
              entry->second = {layout.id, layout.bitWidth};
              schema.functionalAggregateTypes.push_back(layout);
            }
            descriptor.valueType = entry->second.first;
            descriptor.bitWidth = entry->second.second;
          } else if (isa<sim::StringType>(*normalized)) {
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::String;
            descriptor.signedness = coverage::CoverageSignedness::NotApplicable;
          } else if (isa<Float32Type, Float64Type>(*normalized)) {
            descriptor.resultKind =
                (*normalized).isF32()
                    ? coverage::FunctionalExpressionResultKind::ShortReal
                    : coverage::FunctionalExpressionResultKind::Real;
            descriptor.signedness = coverage::CoverageSignedness::NotApplicable;
          } else {
            std::optional<unsigned> width = sim::getPackedWidth(*normalized);
            if (!width || !*width || *width > UINT32_MAX) {
              formal.emitError("functional formal must have an integral, "
                               "real, or string typed v1 result");
              signalPassFailure();
              return;
            }
            descriptor.resultKind =
                coverage::FunctionalExpressionResultKind::Integral;
            descriptor.bitWidth = *width;
            descriptor.signedness =
                formal.getSemanticType() && simlowering::isSignedSemanticType(
                                                *formal.getSemanticType())
                    ? coverage::CoverageSignedness::Signed
                    : coverage::CoverageSignedness::Unsigned;
          }
          SmallVector<Operation *> defaultChildren =
              simlowering::getChildren(formal);
          if (!defaultChildren.empty()) {
            Operation *defaultExpression = defaultChildren.front();
            if (failed(addFunctionalExpression(
                    defaultExpression, formalID,
                    coverage::FunctionalExpressionOwnerKind::Formal,
                    coverage::FunctionalExpressionRole::FormalDefault,
                    sample
                        ? coverage::FunctionalExpressionEvaluationPhase::Sample
                        : coverage::FunctionalExpressionEvaluationPhase::
                              Constructor,
                    0, 0, descriptor.resultKind,
                    descriptor.bitWidth
                        ? std::optional<uint32_t>(descriptor.bitWidth)
                        : std::nullopt,
                    descriptor.bitWidth
                        ? std::optional<coverage::CoverageSignedness>(
                              descriptor.signedness)
                        : std::nullopt))) {
              formal.emitError("cannot inventory functional formal default");
              signalPassFailure();
              return;
            }
            descriptor.flags |= coverage::FunctionalFormalHasDefault;
            descriptor.defaultExpression =
                cov::get<cov::FunctionalExpressionId>(defaultExpression)
                    .getValue()
                    .getZExtValue();
          }
          schema.functionalFormals.push_back(std::move(descriptor));
          cov::set<cov::FunctionalFormalId>(
              formal,
              IntegerAttr::get(IntegerType::get(&getContext(), 64), formalID));
        }
        // Inventory item identities in source declaration order before
        // building plans. Cross targets are sibling symbol references, so a
        // complete item map avoids making their validity depend on whether a
        // target appears textually before or after the cross.
        for (Operation *child : simlowering::getChildren(semanticCovergroup)) {
          auto body = dyn_cast<ir::SVCovergroupBodySymbolOp>(child);
          if (!body)
            continue;
          for (Operation *member : simlowering::getChildren(body)) {
            const bool isPoint = isa<ir::SVCoverpointSymbolOp>(member);
            const bool isCross = isa<ir::SVCoverCrossSymbolOp>(member);
            if (!isPoint && !isCross)
              continue;
            std::string name =
                reportName(simlowering::getDebugName(member),
                           simlowering::getHierarchyName(member));
            uint32_t itemFlags = 0;
            if (cov::has<cov::InheritedOverridden>(member)) {
              itemFlags |= coverage::FunctionalItemNonAggregating;
              auto origin = cov::get<cov::InheritedOriginType>(member);
              name += (Twine("$inherited$") +
                       Twine(origin ? origin.getValue().getZExtValue() : 0))
                          .str();
            }
            const std::string hierarchy =
                (Twine(typeHierarchy) + "." + name).str();
            const uint64_t itemID = stableFunctionalEntityID(
                isPoint ? "functional.coverpoint" : "functional.cross", typeID,
                member);
            auto [itemOwner, insertedItem] =
                itemOwners.try_emplace(itemID, member);
            if (!insertedItem) {
              member->emitError("functional coverage item ID collision")
                  << " with " << itemOwner->second->getLoc();
              signalPassFailure();
              return;
            }
            functionalItemIDs.try_emplace(member, itemID);
            functionalItemHierarchies.try_emplace(member, hierarchy);
            if (auto point = dyn_cast<ir::SVCoverpointSymbolOp>(member)) {
              coverpointItemIDs.try_emplace(point.getSymName(), itemID);
              coverpointOperations.try_emplace(point.getSymName(), point);
              for (Operation *pointChild : simlowering::getChildren(point)) {
                auto bin = dyn_cast<ir::SVCoverageBinSymbolOp>(pointChild);
                if (!bin)
                  continue;
                const uint64_t binID =
                    stableFunctionalEntityID("functional.bin", itemID, bin);
                auto [position, inserted] = coverpointBinTargets.try_emplace(
                    bin.getSymName(),
                    CoverpointBinTarget{itemID, binID, point});
                if (!inserted) {
                  bin.emitError("duplicate functional coverage bin symbol")
                      << " with " << position->second.coverpoint->getLoc();
                  signalPassFailure();
                  return;
                }
              }
            }
            schema.functionalItems.push_back(
                {itemID, typeID, name,
                 isPoint ? coverage::FunctionalItemKind::Coverpoint
                         : coverage::FunctionalItemKind::Cross,
                 itemFlags, 100, 1, itemOrdinal++, hierarchy});
            cov::set<cov::FunctionalItemId>(
                member,
                IntegerAttr::get(IntegerType::get(&getContext(), 64), itemID));
          }
        }
        auto inventoryFunctionalBinSource =
            [&](Operation *sourceOwner, uint64_t binID,
                StringRef hierarchy) -> LogicalResult {
          std::optional<SourceInfo> source = getSourceInfo(sourceOwner);
          std::string storedPath;
          if (source) {
            auto persistRange = [&](coverage::FunctionalSourceRole role,
                                    uint32_t ordinal, const SourceRange &range,
                                    StringRef macroName = {}) {
              FailureOr<uint64_t> file = internFile(range.startFile);
              FailureOr<uint64_t> endFile = internFile(range.endFile);
              if (failed(file) || failed(endFile))
                return failure();
              schema.functionalSourceRanges.push_back(
                  {binID, role, ordinal, *file, *endFile,
                   macroName.empty() ? range.macroName : macroName.str(),
                   range.startLine, range.startColumn, range.endLine,
                   range.endColumn, 0});
              return success();
            };
            if (failed(parseDirectiveRange(source->expanded)) ||
                failed(persistRange(coverage::FunctionalSourceRole::Expanded, 0,
                                    source->expanded)) ||
                (source->original &&
                 (failed(parseDirectiveRange(*source->original)) ||
                  failed(persistRange(coverage::FunctionalSourceRole::Original,
                                      0, *source->original)))) ||
                llvm::any_of(
                    llvm::enumerate(source->macroFrames),
                    [&](auto indexedFrame) {
                      const MacroFrame &frame = indexedFrame.value();
                      uint32_t ordinal = indexedFrame.index();
                      return (frame.definition &&
                              (failed(parseDirectiveRange(*frame.definition)) ||
                               failed(persistRange(
                                   coverage::FunctionalSourceRole::
                                       MacroDefinition,
                                   ordinal, *frame.definition, frame.name)))) ||
                             (frame.invocation &&
                              (failed(parseDirectiveRange(*frame.invocation)) ||
                               failed(persistRange(
                                   coverage::FunctionalSourceRole::
                                       MacroInvocation,
                                   ordinal, *frame.invocation, frame.name))));
                    }))
              return failure();
            storedPath =
                applyPrefixMaps(source->expanded.startFile, prefixMaps);
          }
          const bool included =
              !hasFunctionalIncludes ||
              llvm::any_of(coverageConfiguration->includes,
                           [&](const CoverageRule &rule) {
                             return (!rule.file || source) &&
                                    matchesRule(rule, 2, storedPath, hierarchy);
                           });
          const CoverageRule *excludingRule = nullptr;
          for (const CoverageRule &rule : coverageConfiguration->excludes)
            if ((!rule.file || source) &&
                matchesRule(rule, 2, storedPath, hierarchy)) {
              excludingRule = &rule;
              break;
            }
          std::optional<StringRef> sourceReason;
          if (source) {
            sourceReason = directiveReason(directiveFiles, 2, source->expanded);
            if (!sourceReason && source->original)
              sourceReason =
                  directiveReason(directiveFiles, 2, *source->original);
            for (const MacroFrame &frame : source->macroFrames) {
              if (!sourceReason && frame.definition)
                sourceReason =
                    directiveReason(directiveFiles, 2, *frame.definition);
              if (!sourceReason && frame.invocation)
                sourceReason =
                    directiveReason(directiveFiles, 2, *frame.invocation);
            }
          }
          if (!included || excludingRule || sourceReason) {
            StringRef reason =
                sourceReason ? *sourceReason
                : excludingRule
                    ? StringRef(excludingRule->reason)
                    : StringRef("not selected by coverage include rules");
            schema.exclusions.push_back(
                {binID, coverage::MetricKind::Functional, reason.str(), 0});
          }
          return success();
        };
        for (Operation *child : simlowering::getChildren(semanticCovergroup)) {
          auto body = dyn_cast<ir::SVCovergroupBodySymbolOp>(child);
          if (!body)
            continue;
          for (Operation *member : simlowering::getChildren(body)) {
            if (auto option = dyn_cast<ir::SVCoverageOptionOp>(member)) {
              if (failed(addFunctionalOption(
                      option, typeID,
                      coverage::FunctionalConfigurationOptionOwnerKind::Group,
                      ir::SVCoverageOptionOwnerKind::Covergroup))) {
                signalPassFailure();
                return;
              }
              continue;
            }
            if (auto cross = dyn_cast<ir::SVCoverCrossSymbolOp>(member)) {
              const uint64_t itemID = functionalItemIDs.lookup(cross);
              if (!itemID) {
                cross.emitError("cross has no typed functional item identity");
                signalPassFailure();
                return;
              }
              ArrayAttr effectiveTargets =
                  cov::get<cov::EffectiveTargetSymbols>(cross);
              auto getEffectiveTarget = [&](size_t ordinal,
                                            Attribute targetAttr) -> StringRef {
                if (effectiveTargets && ordinal < effectiveTargets.size())
                  if (auto name =
                          dyn_cast<StringAttr>(effectiveTargets[ordinal]))
                    return name.getValue();
                if (auto target = dyn_cast<SymbolRefAttr>(targetAttr))
                  return target.getLeafReference();
                return {};
              };
              SmallVector<Operation *> crossChildren =
                  simlowering::getChildren(cross);
              uint64_t iffExpression = 0;
              if (cross.getHasIff()) {
                if (crossChildren.empty() ||
                    failed(addFunctionalExpression(
                        crossChildren.front(), itemID,
                        coverage::FunctionalExpressionOwnerKind::Item,
                        coverage::FunctionalExpressionRole::CrossIff,
                        coverage::FunctionalExpressionEvaluationPhase::Sample,
                        0, 0,
                        coverage::FunctionalExpressionResultKind::Boolean))) {
                  cross.emitError(
                      "cannot build the typed cross iff expression plan");
                  signalPassFailure();
                  return;
                }
                iffExpression =
                    cov::get<cov::FunctionalExpressionId>(crossChildren.front())
                        .getValue()
                        .getZExtValue();
              }
              for (Operation *crossChild : crossChildren) {
                auto option = dyn_cast<ir::SVCoverageOptionOp>(crossChild);
                if (!option)
                  continue;
                if (failed(addFunctionalOption(
                        option, itemID,
                        coverage::FunctionalConfigurationOptionOwnerKind::Item,
                        ir::SVCoverageOptionOwnerKind::Cross))) {
                  signalPassFailure();
                  return;
                }
              }

              PendingCrossPlan pending;
              pending.plan.item = itemID;
              pending.plan.retainAutoPolicy =
                  coverage::CrossRetainAutoPolicy::Deferred;
              pending.plan.iffExpression = iffExpression;
              Builder builder(&getContext());
              SmallVector<Attribute> tupleFields;
              tupleFields.reserve(cross.getTargetSymbols().size());
              for (auto [ordinal, targetAttr] :
                   llvm::enumerate(cross.getTargetSymbols())) {
                if (ordinal > UINT32_MAX) {
                  cross.emitError("cross target inventory exceeds v1 limits");
                  signalPassFailure();
                  return;
                }
                StringRef effectiveTarget =
                    getEffectiveTarget(ordinal, targetAttr);
                auto target = coverpointItemIDs.find(effectiveTarget);
                const uint64_t targetID =
                    target == coverpointItemIDs.end() ? 0 : target->second;
                if (!targetID) {
                  cross.emitError()
                      << "cross target " << ordinal
                      << " has no coverpoint item in the enclosing functional "
                         "type";
                  signalPassFailure();
                  return;
                }
                auto targetOperation =
                    coverpointOperations.find(effectiveTarget);
                auto point = targetOperation == coverpointOperations.end()
                                 ? ir::SVCoverpointSymbolOp{}
                                 : dyn_cast<ir::SVCoverpointSymbolOp>(
                                       targetOperation->second);
                FailureOr<Type> targetType =
                    point ? simlowering::getNormalizedSemanticType(point)
                          : FailureOr<Type>(failure());
                StringRef targetName =
                    point ? simlowering::getDebugName(point) : StringRef{};
                if (failed(targetType) || targetName.empty()) {
                  cross.emitError() << "cross target " << ordinal
                                    << " has no canonical CrossValType field";
                  signalPassFailure();
                  return;
                }
                tupleFields.push_back(sim::FieldAttr::get(
                    &getContext(), builder.getStringAttr(targetName),
                    *targetType, static_cast<uint32_t>(ordinal), 0));
                pending.targets.push_back(
                    {itemID, targetID, static_cast<uint32_t>(ordinal)});
              }
              auto encodedTuple = analysis::encodeNativeStorageLayout(
                  sim::UnpackedStructType::get(
                      &getContext(), builder.getArrayAttr(tupleFields)),
                  cross);
              if (failed(encodedTuple)) {
                signalPassFailure();
                return;
              }
              Type tupleType = *encodedTuple;
              FailureOr<simlowering::ContainerElementDescriptor>
                  tupleDescriptor = simlowering::describeContainerElement(
                      tupleType, simlowering::getSemanticLocation(cross));
              std::optional<uint64_t> tupleSpan =
                  sim::getProvenanceSpan(tupleType);
              if (failed(tupleDescriptor) || !tupleSpan || !*tupleSpan ||
                  tupleDescriptor->kind != sim::ElementKind::Aggregate ||
                  tupleDescriptor->typeID == 0 ||
                  tupleDescriptor->valueSize != (*tupleSpan + 7) / 8 ||
                  tupleDescriptor->bitWidth != tupleDescriptor->valueSize * 8) {
                cross.emitError(
                    "cross has no canonical CrossValType container layout");
                signalPassFailure();
                return;
              }
              pending.plan.tupleElementType = tupleDescriptor->typeID;
              pending.plan.tupleProvenanceSpan = *tupleSpan;
              pending.plan.tupleFlags =
                  sim::bitEnumContainsAny(tupleDescriptor->flags,
                                          sim::ElementFlags::FourState)
                      ? coverage::CrossTupleFourState
                      : 0;
              for (auto [ordinal, target] : llvm::enumerate(pending.targets)) {
                Type fieldType =
                    sim::getAggregateElementType(tupleType, ordinal);
                std::optional<std::pair<uint64_t, uint64_t>> fieldLayout =
                    sim::getAggregateProvenanceSubelement(tupleType, ordinal);
                std::optional<unsigned> packedWidth =
                    sim::getPackedWidth(fieldType);
                const bool real = isa<Float32Type, Float64Type>(fieldType);
                const uint32_t bitWidth =
                    real ? cast<FloatType>(fieldType).getWidth()
                    : packedWidth && *packedWidth <= UINT32_MAX
                        ? static_cast<uint32_t>(*packedWidth)
                        : 0;
                StringRef effectiveTarget = getEffectiveTarget(
                    ordinal, cross.getTargetSymbols()[ordinal]);
                auto targetOperation =
                    coverpointOperations.find(effectiveTarget);
                auto point = targetOperation == coverpointOperations.end()
                                 ? ir::SVCoverpointSymbolOp{}
                                 : dyn_cast<ir::SVCoverpointSymbolOp>(
                                       targetOperation->second);
                SmallVector<Operation *> pointChildren =
                    point ? simlowering::getChildren(point)
                          : SmallVector<Operation *>{};
                Operation *sourceExpression =
                    pointChildren.empty() ? nullptr : pointChildren.front();
                auto sourceType =
                    sourceExpression
                        ? sourceExpression->getAttrOfType<TypeAttr>(
                              "semantic_type")
                        : TypeAttr{};
                while (point && !point.getHasExplicitType() && sourceType &&
                       !isa<ir::EnumType>(sourceType.getValue())) {
                  auto conversion =
                      dyn_cast<ir::SVConversionExpressionOp>(sourceExpression);
                  auto implicit =
                      sourceExpression->getAttrOfType<BoolAttr>("is_implicit");
                  SmallVector<Operation *> children =
                      simlowering::getChildren(sourceExpression);
                  if (!conversion || !implicit || !implicit.getValue() ||
                      children.size() != 1)
                    break;
                  sourceExpression = children.front();
                  sourceType = sourceExpression->getAttrOfType<TypeAttr>(
                      "semantic_type");
                }
                const bool isSigned =
                    sourceType &&
                    simlowering::isSignedSemanticType(sourceType.getValue());
                const bool fourState =
                    packedWidth &&
                    isa<sim::LogicType>(sim::getPackedScalarType(fieldType));
                const bool domainFourState =
                    sourceType &&
                    simlowering::isFourStateSemanticType(sourceType.getValue());
                if (!fieldLayout || !bitWidth || !point ||
                    fieldLayout->second < bitWidth ||
                    fieldLayout->second > *tupleSpan ||
                    fieldLayout->first > *tupleSpan - fieldLayout->second) {
                  cross.emitError()
                      << "cross target " << ordinal
                      << " has no canonical CrossValType provenance layout";
                  signalPassFailure();
                  return;
                }
                target.tupleBitOffset = fieldLayout->first;
                target.tupleBitWidth = bitWidth;
                target.tupleResultKind =
                    real
                        ? (fieldType.isF32()
                               ? coverage::FunctionalExpressionResultKind::
                                     ShortReal
                               : coverage::FunctionalExpressionResultKind::Real)
                        : coverage::FunctionalExpressionResultKind::Integral;
                target.tupleSignedness =
                    real       ? coverage::CoverageSignedness::NotApplicable
                    : isSigned ? coverage::CoverageSignedness::Signed
                               : coverage::CoverageSignedness::Unsigned;
                target.tupleFlags =
                    (fourState ? coverage::CrossTupleFieldFourState : 0) |
                    (domainFourState ? coverage::CrossTargetDomainFourState
                                     : 0);
              }
              uint32_t crossBinOrdinal = 0;
              uint32_t selectorOrdinal = 0;
              std::function<FailureOr<uint64_t>(Operation *)> emitCrossSelector;
              emitCrossSelector =
                  [&](Operation *selector) -> FailureOr<uint64_t> {
                if (auto with =
                        dyn_cast_or_null<ir::SVBinSelectWithFilterExprOp>(
                            selector)) {
                  SmallVector<Operation *> operands =
                      simlowering::getChildren(with);
                  if (operands.size() != size_t{2} + with.getHasMatches()) {
                    with.emitError(
                        "cross selector with expression has malformed "
                        "children");
                    return failure();
                  }
                  FailureOr<uint64_t> subject =
                      emitCrossSelector(operands.front());
                  if (failed(subject))
                    return failure();
                  const uint64_t selectorID = stableFunctionalEntityID(
                      "functional.cross-selector", itemID, with);
                  auto [selectorOwner, insertedSelector] =
                      selectorOwners.try_emplace(selectorID,
                                                 with.getOperation());
                  if (!insertedSelector) {
                    with.emitError("functional cross selector ID collision")
                        << " with " << selectorOwner->second->getLoc();
                    return failure();
                  }
                  if (pending.selectors.size() >= UINT32_MAX ||
                      pending.selectorOperands.size() >= UINT32_MAX) {
                    with.emitError("functional cross selector inventory "
                                   "exceeds v1 limits");
                    return failure();
                  }

                  Operation *predicate = operands[1];
                  auto scalarAttr = cov::get<cov::CrossWithScalar>(predicate);
                  const bool scalarPredicate =
                      scalarAttr && scalarAttr.getValue();
                  auto targetPaths =
                      cov::get<cov::CrossWithTargetPaths>(predicate);
                  if (!targetPaths ||
                      targetPaths.size() != pending.targets.size())
                    return predicate->emitError(
                        "cross selector with has no validated iterator plan");
                  auto program =
                      scalarPredicate
                          ? std::nullopt
                          : planCrossPredicate(predicate, targetPaths);
                  uint64_t predicateRoot = 0, withExpression = 0;
                  if (program) {
                    llvm::DenseMap<CrossPredicateKey, uint64_t> ids;
                    uint32_t ordinal = 0;
                    for (const auto &entry : *program) {
                      const uint64_t id = stableID(
                          "functional.cross-predicate-node",
                          (Twine(selectorID) + "." + Twine(ordinal)).str());
                      coverage::CrossPredicateNode node;
                      node.id = id;
                      node.selector = selectorID;
                      node.kind = entry.kind;
                      node.bitWidth = entry.bitWidth;
                      node.signedness = entry.signedness;
                      node.ordinal = ordinal++;
                      node.flags = entry.flags;
                      node.elementWidth = entry.elementWidth;
                      node.indexBase = entry.indexBase;
                      if (pending.predicateOperands.size() > UINT32_MAX ||
                          entry.operandKeys.size() >
                              UINT32_MAX - pending.predicateOperands.size()) {
                        entry.expression->emitError(
                            "cross predicate operand inventory exceeds the "
                            "format limits");
                        return failure();
                      }
                      node.firstOperand = pending.predicateOperands.size();
                      node.operandCount = entry.operandKeys.size();
                      for (CrossPredicateKey operand : entry.operandKeys)
                        pending.predicateOperands.push_back(
                            ids.lookup(operand));
                      if (entry.kind == coverage::CrossPredicateKind::Target)
                        node.target =
                            pending.targets[entry.targetOrdinal].target;
                      if (entry.kind == coverage::CrossPredicateKind::Value) {
                        if (failed(addFunctionalExpression(
                                entry.expression, selectorID,
                                coverage::FunctionalExpressionOwnerKind::
                                    Selector,
                                coverage::FunctionalExpressionRole::
                                    PredicateValue,
                                coverage::FunctionalExpressionEvaluationPhase::
                                    Constructor,
                                node.ordinal, 0)))
                          return failure();
                        node.expression = cov::get<cov::FunctionalExpressionId>(
                                              entry.expression)
                                              .getValue()
                                              .getZExtValue();
                      }
                      ids.try_emplace(entry.key, id);
                      pending.predicates.push_back(node);
                      predicateRoot = id;
                    }
                  } else {
                    if (failed(addFunctionalExpression(
                            predicate, selectorID,
                            coverage::FunctionalExpressionOwnerKind::Selector,
                            coverage::FunctionalExpressionRole::SelectorWith,
                            coverage::FunctionalExpressionEvaluationPhase::
                                Constructor,
                            0, 0,
                            scalarPredicate
                                ? coverage::FunctionalExpressionResultKind::
                                      Boolean
                                : coverage::FunctionalExpressionResultKind::
                                      TupleQueue)))
                      return failure();
                    withExpression =
                        cov::get<cov::FunctionalExpressionId>(predicate)
                            .getValue()
                            .getZExtValue();
                    if (!scalarPredicate) {
                      cov::set<cov::CrossWithTupleType>(
                          predicate, TypeAttr::get(tupleType));
                      cov::set<cov::CrossWithSelectorId>(
                          predicate, builder.getI64IntegerAttr(selectorID));
                    }
                  }
                  coverage::CrossMatchesPolicy matchesPolicy =
                      coverage::CrossMatchesPolicy::Count;
                  uint64_t matchesCount = 1;
                  uint64_t matchesExpression = 0;
                  if (with.getHasMatches()) {
                    Operation *matches = operands.back();
                    matchesCount = 0;
                    if (isa<ir::SVUnboundedLiteralOp>(matches)) {
                      matchesPolicy = coverage::CrossMatchesPolicy::All;
                    } else if (std::optional<StringRef> spelling =
                                   simlowering::getConstantSpelling(matches)) {
                      FailureOr<Type> normalized =
                          simlowering::getNormalizedSemanticType(matches);
                      std::optional<unsigned> width =
                          succeeded(normalized)
                              ? sim::getPackedWidth(*normalized)
                              : std::nullopt;
                      FailureOr<simlowering::ParsedConstant> parsed =
                          width && *width
                              ? simlowering::parseSVInteger(
                                    *spelling, *width,
                                    simlowering::getSemanticLocation(matches))
                              : FailureOr<simlowering::ParsedConstant>(
                                    failure());
                      auto signedAttr =
                          matches->getAttrOfType<BoolAttr>("is_signed");
                      auto semanticType =
                          matches->getAttrOfType<TypeAttr>("semantic_type");
                      const bool isSigned =
                          signedAttr ? signedAttr.getValue()
                                     : semanticType &&
                                           simlowering::isSignedSemanticType(
                                               semanticType.getValue());
                      if (failed(parsed) || !parsed->unknown.isZero() ||
                          parsed->value.isZero() ||
                          (isSigned && parsed->value.isNegative())) {
                        matches->emitError("cross selector matches constant "
                                           "must be a positive v1 integer");
                        return failure();
                      }
                      if (parsed->value.getActiveBits() > 64) {
                        if (failed(addFunctionalExpression(
                                matches, selectorID,
                                coverage::FunctionalExpressionOwnerKind::
                                    Selector,
                                coverage::FunctionalExpressionRole::
                                    SelectorMatches,
                                coverage::FunctionalExpressionEvaluationPhase::
                                    Constructor,
                                0, 0)))
                          return failure();
                        matchesExpression =
                            cov::get<cov::FunctionalExpressionId>(matches)
                                .getValue()
                                .getZExtValue();
                      } else
                        matchesCount = parsed->value.getZExtValue();
                    } else {
                      if (failed(addFunctionalExpression(
                              matches, selectorID,
                              coverage::FunctionalExpressionOwnerKind::Selector,
                              coverage::FunctionalExpressionRole::
                                  SelectorMatches,
                              coverage::FunctionalExpressionEvaluationPhase::
                                  Constructor,
                              0, 0))) {
                        matches->emitError(
                            "cannot inventory cross selector matches "
                            "expression");
                        return failure();
                      }
                      matchesExpression =
                          cov::get<cov::FunctionalExpressionId>(matches)
                              .getValue()
                              .getZExtValue();
                    }
                  }
                  const uint32_t firstOperand = pending.selectorOperands.size();
                  pending.selectorOperands.push_back({selectorID, *subject, 0});
                  pending.selectors.push_back(
                      {selectorID, itemID, 0, 0, 0, withExpression, 0, 0,
                       matchesExpression, firstOperand, 1,
                       coverage::CrossSelectorKind::With, selectorOrdinal++,
                       scalarPredicate ? coverage::CrossSelectorScalarWith : 0,
                       matchesPolicy, matchesCount, predicateRoot});
                  return selectorID;
                }
                if (auto set = dyn_cast_or_null<ir::SVSetExprBinsSelectExprOp>(
                        selector)) {
                  SmallVector<Operation *> operands =
                      simlowering::getChildren(set);
                  if (operands.size() != size_t{1} + set.getHasMatches()) {
                    set.emitError(
                        "cross_set_expression selector has malformed children");
                    return failure();
                  }
                  const uint64_t selectorID = stableFunctionalEntityID(
                      "functional.cross-selector", itemID, set);
                  auto [selectorOwner, insertedSelector] =
                      selectorOwners.try_emplace(selectorID,
                                                 set.getOperation());
                  if (!insertedSelector) {
                    set.emitError("functional cross selector ID collision")
                        << " with " << selectorOwner->second->getLoc();
                    return failure();
                  }
                  if (pending.selectors.size() >= UINT32_MAX ||
                      pending.selectorOperands.size() > UINT32_MAX) {
                    set.emitError("functional cross selector inventory exceeds "
                                  "v1 limits");
                    return failure();
                  }

                  Operation *construction = operands.front();
                  if (failed(addFunctionalExpression(
                          construction, selectorID,
                          coverage::FunctionalExpressionOwnerKind::Selector,
                          coverage::FunctionalExpressionRole::BinSet,
                          coverage::FunctionalExpressionEvaluationPhase::
                              Constructor,
                          0, 0,
                          coverage::FunctionalExpressionResultKind::
                              TupleQueue))) {
                    construction->emitError(
                        "cannot inventory cross_set_expression");
                    return failure();
                  }
                  const uint64_t constructionExpression =
                      cov::get<cov::FunctionalExpressionId>(construction)
                          .getValue()
                          .getZExtValue();

                  coverage::CrossMatchesPolicy matchesPolicy =
                      coverage::CrossMatchesPolicy::Count;
                  uint64_t matchesCount = 1;
                  uint64_t matchesExpression = 0;
                  if (set.getHasMatches()) {
                    Operation *matches = operands.back();
                    matchesCount = 0;
                    if (isa<ir::SVUnboundedLiteralOp>(matches)) {
                      matchesPolicy = coverage::CrossMatchesPolicy::All;
                    } else if (std::optional<StringRef> spelling =
                                   simlowering::getConstantSpelling(matches)) {
                      FailureOr<Type> normalized =
                          simlowering::getNormalizedSemanticType(matches);
                      std::optional<unsigned> width =
                          succeeded(normalized)
                              ? sim::getPackedWidth(*normalized)
                              : std::nullopt;
                      FailureOr<simlowering::ParsedConstant> parsed =
                          width && *width
                              ? simlowering::parseSVInteger(
                                    *spelling, *width,
                                    simlowering::getSemanticLocation(matches))
                              : FailureOr<simlowering::ParsedConstant>(
                                    failure());
                      auto signedAttr =
                          matches->getAttrOfType<BoolAttr>("is_signed");
                      auto semanticType =
                          matches->getAttrOfType<TypeAttr>("semantic_type");
                      const bool isSigned =
                          signedAttr ? signedAttr.getValue()
                                     : semanticType &&
                                           simlowering::isSignedSemanticType(
                                               semanticType.getValue());
                      if (failed(parsed) || !parsed->unknown.isZero() ||
                          parsed->value.isZero() ||
                          (isSigned && parsed->value.isNegative())) {
                        matches->emitError(
                            "cross_set_expression matches constant must be a "
                            "positive v1 integer");
                        return failure();
                      }
                      if (parsed->value.getActiveBits() > 64) {
                        if (failed(addFunctionalExpression(
                                matches, selectorID,
                                coverage::FunctionalExpressionOwnerKind::
                                    Selector,
                                coverage::FunctionalExpressionRole::
                                    SelectorMatches,
                                coverage::FunctionalExpressionEvaluationPhase::
                                    Constructor,
                                0, 0)))
                          return failure();
                        matchesExpression =
                            cov::get<cov::FunctionalExpressionId>(matches)
                                .getValue()
                                .getZExtValue();
                      } else
                        matchesCount = parsed->value.getZExtValue();
                    } else {
                      if (failed(addFunctionalExpression(
                              matches, selectorID,
                              coverage::FunctionalExpressionOwnerKind::Selector,
                              coverage::FunctionalExpressionRole::
                                  SelectorMatches,
                              coverage::FunctionalExpressionEvaluationPhase::
                                  Constructor,
                              0, 0))) {
                        matches->emitError(
                            "cannot inventory cross_set_expression matches "
                            "expression");
                        return failure();
                      }
                      matchesExpression =
                          cov::get<cov::FunctionalExpressionId>(matches)
                              .getValue()
                              .getZExtValue();
                    }
                  }

                  pending.selectors.push_back(
                      {selectorID, itemID, 0, 0, 0, 0, constructionExpression,
                       0, matchesExpression,
                       static_cast<uint32_t>(pending.selectorOperands.size()),
                       0, coverage::CrossSelectorKind::Set, selectorOrdinal++,
                       0, matchesPolicy, matchesCount});
                  return selectorID;
                }
                if (auto allTuples =
                        dyn_cast_or_null<ir::SVCrossIdBinsSelectExprOp>(
                            selector)) {
                  if (!simlowering::getChildren(allTuples).empty()) {
                    allTuples.emitError(
                        "enclosing-cross selector must be a leaf");
                    return failure();
                  }
                  const uint64_t selectorID = stableFunctionalEntityID(
                      "functional.cross-selector", itemID, allTuples);
                  auto [selectorOwner, insertedSelector] =
                      selectorOwners.try_emplace(selectorID,
                                                 allTuples.getOperation());
                  if (!insertedSelector) {
                    allTuples.emitError("functional cross selector ID "
                                        "collision")
                        << " with " << selectorOwner->second->getLoc();
                    return failure();
                  }
                  if (pending.selectors.size() >= UINT32_MAX ||
                      pending.selectorOperands.size() > UINT32_MAX) {
                    allTuples.emitError(
                        "functional cross selector inventory exceeds v1 "
                        "limits");
                    return failure();
                  }
                  pending.selectors.push_back(
                      {selectorID, itemID, 0, 0, 0, 0, 0, 0, 0,
                       static_cast<uint32_t>(pending.selectorOperands.size()),
                       0, coverage::CrossSelectorKind::AllTuples,
                       selectorOrdinal++, 0, coverage::CrossMatchesPolicy::None,
                       0});
                  return selectorID;
                }
                if (auto condition =
                        dyn_cast_or_null<ir::SVConditionBinsSelectExprOp>(
                            selector)) {
                  auto effectiveTarget =
                      cov::get<cov::EffectiveTargetSymbol>(condition);
                  StringRef targetSymbol =
                      effectiveTarget
                          ? effectiveTarget.getValue()
                          : condition.getTargetSymbol().getLeafReference();
                  auto target = coverpointItemIDs.find(targetSymbol);
                  auto targetOperation =
                      coverpointOperations.find(targetSymbol);
                  auto targetBin = coverpointBinTargets.find(targetSymbol);
                  const uint64_t targetItemID =
                      target != coverpointItemIDs.end() ? target->second
                      : targetBin != coverpointBinTargets.end()
                          ? targetBin->second.item
                          : 0;
                  Operation *targetCoverpoint =
                      targetOperation != coverpointOperations.end()
                          ? targetOperation->second
                      : targetBin != coverpointBinTargets.end()
                          ? targetBin->second.coverpoint
                          : nullptr;
                  const uint64_t targetBinID =
                      targetBin != coverpointBinTargets.end()
                          ? targetBin->second.bin
                          : 0;
                  if (!targetItemID || !targetCoverpoint) {
                    condition.emitError("binsof target has no typed "
                                        "coverpoint or coverpoint-bin "
                                        "identity");
                    return failure();
                  }

                  const uint64_t conditionID = stableFunctionalEntityID(
                      "functional.cross-selector", itemID, condition);
                  auto [selectorOwner, insertedSelector] =
                      selectorOwners.try_emplace(conditionID,
                                                 condition.getOperation());
                  if (!insertedSelector) {
                    condition.emitError("functional cross selector ID "
                                        "collision")
                        << " with " << selectorOwner->second->getLoc();
                    return failure();
                  }

                  uint64_t valueSetID = 0;
                  if (condition.getIntersectCount()) {
                    SmallVector<Operation *> targetChildren =
                        simlowering::getChildren(targetCoverpoint);
                    FailureOr<Type> normalized =
                        targetChildren.empty()
                            ? FailureOr<Type>(failure())
                            : simlowering::getNormalizedSemanticType(
                                  targetChildren.front());
                    std::optional<unsigned> width =
                        succeeded(normalized) ? sim::getPackedWidth(*normalized)
                                              : std::nullopt;
                    const bool targetIsReal =
                        succeeded(normalized) &&
                        isa<Float32Type, Float64Type>(*normalized);
                    if (!targetIsReal &&
                        (!width || !*width || *width > UINT32_MAX)) {
                      condition.emitError("binsof intersect target must have "
                                          "an integral or real v1 type");
                      return failure();
                    }
                    valueSetID = stableFunctionalEntityID(
                        "functional.cross-selector-value-set", conditionID,
                        condition);
                    auto [valueSetOwner, insertedValueSet] =
                        valueSetOwners.try_emplace(valueSetID,
                                                   condition.getOperation());
                    if (!insertedValueSet) {
                      condition.emitError("functional cross selector "
                                          "value-set ID collision")
                          << " with " << valueSetOwner->second->getLoc();
                      return failure();
                    }
                    PendingValueSet selected;
                    selected.set.id = valueSetID;
                    selected.set.item = targetItemID;
                    selected.set.bitWidth = targetIsReal ? 64 : *width;
                    selected.set.kind =
                        targetIsReal
                            ? coverage::FunctionalValueSetKind::Real
                            : coverage::FunctionalValueSetKind::Integral;
                    auto targetSourceType =
                        targetChildren.front()->getAttrOfType<TypeAttr>(
                            "semantic_type");
                    selected.set.signedness =
                        targetIsReal
                            ? coverage::CoverageSignedness::NotApplicable
                        : targetSourceType && simlowering::isSignedSemanticType(
                                                  targetSourceType.getValue())
                            ? coverage::CoverageSignedness::Signed
                            : coverage::CoverageSignedness::Unsigned;
                    selected.set.flags =
                        coverage::FunctionalValueSetNeedsResolution;
                    SmallVector<Operation *> intersections =
                        simlowering::getChildren(condition);
                    if (intersections.size() != condition.getIntersectCount()) {
                      condition.emitError("binsof intersect child count is "
                                          "inconsistent");
                      return failure();
                    }
                    for (auto [atomOrdinal, value] :
                         llvm::enumerate(intersections)) {
                      auto valueRange =
                          dyn_cast<ir::SVValueRangeExpressionOp>(value);
                      SmallVector<Operation *> endpoints =
                          valueRange ? simlowering::getChildren(value)
                                     : SmallVector<Operation *>{};
                      Operation *lower =
                          endpoints.empty() ? value : endpoints.front();
                      Operation *upper =
                          endpoints.size() == 2 ? endpoints.back() : nullptr;
                      const bool lowerUnbounded =
                          simlowering::isUnboundedEndpoint(lower);
                      const bool upperUnbounded =
                          upper && simlowering::isUnboundedEndpoint(upper);
                      if ((lowerUnbounded || upperUnbounded) &&
                          (!upper || (lowerUnbounded && upperUnbounded))) {
                        condition.emitError(
                            "binsof intersect open range must have exactly "
                            "one bounded endpoint");
                        return failure();
                      }
                      lower = peelFunctionalValueCast(
                          lower, selected.set.bitWidth, *normalized);
                      if (upper)
                        upper = peelFunctionalValueCast(
                            upper, selected.set.bitWidth, *normalized);
                      coverage::FunctionalValueAtom atom;
                      atom.valueSet = valueSetID;
                      atom.ordinal = atomOrdinal;
                      atom.kind =
                          targetIsReal
                              ? coverage::FunctionalValueAtomKind::RealInterval
                          : upper
                              ? coverage::FunctionalValueAtomKind::IntegralRange
                              : coverage::FunctionalValueAtomKind::
                                    IntegralValue;
                      atom.flags = coverage::FunctionalValueAtomLowerInclusive |
                                   coverage::FunctionalValueAtomUpperInclusive;
                      if (targetIsReal && upper) {
                        atom.flags |= coverage::FunctionalValueAtomRealRange;
                      }
                      if (lowerUnbounded)
                        atom.flags |=
                            coverage::FunctionalValueAtomLowerUnbounded;
                      if (upperUnbounded)
                        atom.flags |=
                            coverage::FunctionalValueAtomUpperUnbounded;
                      if (!lowerUnbounded) {
                        if (failed(addFunctionalExpression(
                                lower, valueSetID,
                                coverage::FunctionalExpressionOwnerKind::
                                    ValueSet,
                                upper ? coverage::FunctionalExpressionRole::
                                            ValueAtomLower
                                      : coverage::FunctionalExpressionRole::
                                            ValueAtomSingleton,
                                coverage::FunctionalExpressionEvaluationPhase::
                                    Constructor,
                                atomOrdinal, 0))) {
                          condition.emitError(
                              "cannot inventory binsof intersect value");
                          return failure();
                        }
                        atom.lowerExpression =
                            cov::get<cov::FunctionalExpressionId>(lower)
                                .getValue()
                                .getZExtValue();
                      }
                      if (upper && !upperUnbounded) {
                        if (failed(addFunctionalExpression(
                                upper, valueSetID,
                                coverage::FunctionalExpressionOwnerKind::
                                    ValueSet,
                                coverage::FunctionalExpressionRole::
                                    ValueAtomUpper,
                                coverage::FunctionalExpressionEvaluationPhase::
                                    Constructor,
                                atomOrdinal, 0))) {
                          condition.emitError(
                              "cannot inventory binsof intersect range");
                          return failure();
                        }
                        atom.upperExpression =
                            cov::get<cov::FunctionalExpressionId>(upper)
                                .getValue()
                                .getZExtValue();
                      }
                      selected.atoms.push_back(atom);
                    }
                    pendingValueSets.push_back(std::move(selected));
                  }

                  if (pending.selectors.size() >= UINT32_MAX ||
                      pending.selectorOperands.size() > UINT32_MAX) {
                    condition.emitError(
                        "functional cross selector inventory exceeds v1 "
                        "limits");
                    return failure();
                  }
                  pending.selectors.push_back(
                      {conditionID, itemID, targetItemID, targetBinID,
                       valueSetID, 0, 0, 0, 0,
                       static_cast<uint32_t>(pending.selectorOperands.size()),
                       0, coverage::CrossSelectorKind::Binsof,
                       selectorOrdinal++, 0, coverage::CrossMatchesPolicy::None,
                       0});
                  return conditionID;
                }

                if (auto negation =
                        dyn_cast_or_null<ir::SVUnaryBinsSelectExprOp>(
                            selector)) {
                  SmallVector<Operation *> operands =
                      simlowering::getChildren(negation);
                  if (negation.getOperatorKind() !=
                          ir::SVCoverageSelectUnaryOperator::Negation ||
                      operands.size() != 1 ||
                      !isa<ir::SVConditionBinsSelectExprOp>(operands.front())) {
                    negation.emitError(
                        "expected negation of one binsof condition");
                    return failure();
                  }
                  FailureOr<uint64_t> child = emitCrossSelector(operands[0]);
                  if (failed(child))
                    return failure();
                  const uint64_t negationID = stableFunctionalEntityID(
                      "functional.cross-selector", itemID, negation);
                  auto [selectorOwner, insertedSelector] =
                      selectorOwners.try_emplace(negationID,
                                                 negation.getOperation());
                  if (!insertedSelector) {
                    negation.emitError("functional cross selector ID "
                                       "collision")
                        << " with " << selectorOwner->second->getLoc();
                    return failure();
                  }
                  if (pending.selectors.size() >= UINT32_MAX ||
                      pending.selectorOperands.size() >= UINT32_MAX) {
                    negation.emitError(
                        "functional cross selector inventory exceeds v1 "
                        "limits");
                    return failure();
                  }
                  const uint32_t firstOperand = pending.selectorOperands.size();
                  pending.selectorOperands.push_back({negationID, *child, 0});
                  pending.selectors.push_back(
                      {negationID, itemID, 0, 0, 0, 0, 0, 0, 0, firstOperand, 1,
                       coverage::CrossSelectorKind::Not, selectorOrdinal++, 0,
                       coverage::CrossMatchesPolicy::None, 0});
                  return negationID;
                }

                if (auto conjunction =
                        dyn_cast_or_null<ir::SVBinaryBinsSelectExprOp>(
                            selector)) {
                  SmallVector<Operation *> operands =
                      simlowering::getChildren(conjunction);
                  const auto operatorKind = conjunction.getOperatorKind();
                  if (!llvm::is_contained(
                          {ir::SVCoverageSelectBinaryOperator::And,
                           ir::SVCoverageSelectBinaryOperator::Or},
                          operatorKind) ||
                      operands.size() != 2) {
                    conjunction.emitError(
                        "expected a binary AND/OR cross selector");
                    return failure();
                  }
                  FailureOr<uint64_t> left = emitCrossSelector(operands[0]);
                  FailureOr<uint64_t> right = emitCrossSelector(operands[1]);
                  if (failed(left) || failed(right))
                    return failure();
                  const uint64_t conjunctionID = stableFunctionalEntityID(
                      "functional.cross-selector", itemID, conjunction);
                  auto [selectorOwner, insertedSelector] =
                      selectorOwners.try_emplace(conjunctionID,
                                                 conjunction.getOperation());
                  if (!insertedSelector) {
                    conjunction.emitError("functional cross selector ID "
                                          "collision")
                        << " with " << selectorOwner->second->getLoc();
                    return failure();
                  }
                  if (pending.selectors.size() >= UINT32_MAX ||
                      pending.selectorOperands.size() > UINT32_MAX - 2) {
                    conjunction.emitError(
                        "functional cross selector inventory exceeds v1 "
                        "limits");
                    return failure();
                  }
                  const uint32_t firstOperand = pending.selectorOperands.size();
                  pending.selectorOperands.push_back({conjunctionID, *left, 0});
                  pending.selectorOperands.push_back(
                      {conjunctionID, *right, 1});
                  pending.selectors.push_back(
                      {conjunctionID, itemID, 0, 0, 0, 0, 0, 0, 0, firstOperand,
                       2,
                       operatorKind == ir::SVCoverageSelectBinaryOperator::And
                           ? coverage::CrossSelectorKind::And
                           : coverage::CrossSelectorKind::Or,
                       selectorOrdinal++, 0, coverage::CrossMatchesPolicy::None,
                       0});
                  return conjunctionID;
                }

                if (selector)
                  selector->emitError("unsupported explicit cross selector");
                return failure();
              };
              WalkResult crossBinResult =
                  cross.walk([&](ir::SVCoverageBinSymbolOp bin) -> WalkResult {
                    FailureOr<simlowering::CoverageBinChildren> children =
                        simlowering::decodeCoverageBinChildren(bin);
                    Operation *selector =
                        succeeded(children) ? children->crossSelect : nullptr;
                    if (failed(children) || !selector) {
                      bin.emitError(
                          "explicit cross bin has no valid selector tree");
                      return WalkResult::interrupt();
                    }

                    const std::string binName =
                        reportName(simlowering::getDebugName(bin),
                                   simlowering::getHierarchyName(bin));
                    const auto itemHierarchy =
                        functionalItemHierarchies.find(cross);
                    if (itemHierarchy == functionalItemHierarchies.end()) {
                      cross.emitError(
                          "cross has no canonical functional item hierarchy");
                      return WalkResult::interrupt();
                    }
                    const std::string hierarchy =
                        (Twine(itemHierarchy->second) + "." + binName).str();
                    const uint64_t binID = stableFunctionalEntityID(
                        "functional.cross-bin", itemID, bin);
                    auto [binOwner, insertedBin] =
                        binOwners.try_emplace(binID, bin.getOperation());
                    if (!insertedBin) {
                      bin.emitError("functional cross bin ID collision")
                          << " with " << binOwner->second->getLoc();
                      return WalkResult::interrupt();
                    }
                    if (children->iff &&
                        failed(addFunctionalExpression(
                            children->iff, binID,
                            coverage::FunctionalExpressionOwnerKind::Bin,
                            coverage::FunctionalExpressionRole::BinIff,
                            coverage::FunctionalExpressionEvaluationPhase::
                                Sample,
                            0, 0,
                            coverage::FunctionalExpressionResultKind::
                                Boolean))) {
                      bin.emitError(
                          "cannot build the typed explicit cross-bin iff "
                          "expression plan");
                      return WalkResult::interrupt();
                    }

                    uint32_t binFlags = 0;
                    if (bin.getBinsKind() == ir::SVCoverageBinKind::IgnoreBins)
                      binFlags |= coverage::FunctionalBinIgnore;
                    if (bin.getBinsKind() == ir::SVCoverageBinKind::IllegalBins)
                      binFlags |= coverage::FunctionalBinIllegal;
                    schema.functionalBins.push_back(
                        {binID, itemID, binName,
                         coverage::FunctionalBinKind::Cross, binFlags, 1,
                         crossBinOrdinal++, hierarchy});
                    if (failed(inventoryFunctionalBinSource(bin, binID,
                                                            hierarchy))) {
                      bin.emitError(
                          "cannot inventory explicit cross bin source");
                      return WalkResult::interrupt();
                    }
                    FailureOr<uint64_t> rootSelectorID =
                        emitCrossSelector(selector);
                    if (failed(rootSelectorID))
                      return WalkResult::interrupt();
                    pending.bins.push_back({binID, itemID, *rootSelectorID, 0});
                    return WalkResult::advance();
                  });
              if (crossBinResult.wasInterrupted()) {
                signalPassFailure();
                return;
              }
              pendingCrossPlans.push_back(std::move(pending));
              continue;
            }
            auto coverpoint = dyn_cast<ir::SVCoverpointSymbolOp>(member);
            if (!coverpoint)
              continue;
            const uint64_t itemID = functionalItemIDs.lookup(coverpoint);
            if (!itemID) {
              coverpoint.emitError(
                  "coverpoint has no typed functional item identity");
              signalPassFailure();
              return;
            }
            const auto itemHierarchy =
                functionalItemHierarchies.find(coverpoint);
            if (itemHierarchy == functionalItemHierarchies.end()) {
              coverpoint.emitError(
                  "coverpoint has no canonical functional item hierarchy");
              signalPassFailure();
              return;
            }
            const StringRef pointHierarchy = itemHierarchy->second;
            SmallVector<Operation *> pointChildren =
                simlowering::getChildren(coverpoint);
            const unsigned pointExpressionCount =
                coverpoint.getHasIff() ? 2 : 1;
            if (pointChildren.size() < pointExpressionCount ||
                failed(addFunctionalExpression(
                    pointChildren.front(), itemID,
                    coverage::FunctionalExpressionOwnerKind::Item,
                    coverage::FunctionalExpressionRole::CoverpointSample,
                    coverage::FunctionalExpressionEvaluationPhase::Sample, 0,
                    0)) ||
                (coverpoint.getHasIff() &&
                 failed(addFunctionalExpression(
                     pointChildren[1], itemID,
                     coverage::FunctionalExpressionOwnerKind::Item,
                     coverage::FunctionalExpressionRole::CoverpointIff,
                     coverage::FunctionalExpressionEvaluationPhase::Sample, 0,
                     0, coverage::FunctionalExpressionResultKind::Boolean)))) {
              coverpoint.emitError(
                  "cannot build the typed functional sample expression plan");
              signalPassFailure();
              return;
            }
            for (Operation *child : pointChildren) {
              auto option = dyn_cast<ir::SVCoverageOptionOp>(child);
              if (!option)
                continue;
              if (failed(addFunctionalOption(
                      option, itemID,
                      coverage::FunctionalConfigurationOptionOwnerKind::Item,
                      ir::SVCoverageOptionOwnerKind::Coverpoint))) {
                signalPassFailure();
                return;
              }
            }
            FailureOr<Type> normalizedPointType =
                simlowering::getNormalizedSemanticType(pointChildren.front());
            const bool pointIsReal =
                succeeded(normalizedPointType) &&
                isa<Float32Type, Float64Type>(*normalizedPointType);
            uint64_t pointWidth = 0;
            if (succeeded(normalizedPointType))
              if (std::optional<unsigned> width =
                      sim::getPackedWidth(*normalizedPointType))
                pointWidth = *width;
            auto pointSourceType =
                pointChildren.front()->getAttrOfType<TypeAttr>("semantic_type");
            Operation *pointSourceExpression = pointChildren.front();
            while (!coverpoint.getHasExplicitType() && pointSourceType &&
                   !isa<ir::EnumType>(pointSourceType.getValue())) {
              auto conversion =
                  dyn_cast<ir::SVConversionExpressionOp>(pointSourceExpression);
              auto implicit =
                  pointSourceExpression->getAttrOfType<BoolAttr>("is_implicit");
              SmallVector<Operation *> conversionChildren =
                  simlowering::getChildren(pointSourceExpression);
              if (!conversion || !implicit || !implicit.getValue() ||
                  conversionChildren.size() != 1)
                break;
              pointSourceExpression = conversionChildren.front();
              pointSourceType = pointSourceExpression->getAttrOfType<TypeAttr>(
                  "semantic_type");
            }
            const bool pointIsSigned =
                pointSourceType &&
                simlowering::isSignedSemanticType(pointSourceType.getValue());
            uint32_t binOrdinal = 0;
            const bool hasNamedCoverageBin =
                llvm::any_of(pointChildren, [](Operation *candidate) {
                  auto bin = dyn_cast<ir::SVCoverageBinSymbolOp>(candidate);
                  return bin &&
                         bin.getBinsKind() == ir::SVCoverageBinKind::Bins;
                });
            if (!hasNamedCoverageBin && pointSourceType &&
                isa<ir::EnumType>(pointSourceType.getValue())) {
              const uint64_t pointBitWidth = pointWidth;
              auto declaration = enumDeclarations.find(enumIdentity(
                  pointSourceExpression, pointSourceType.getValue()));
              if (!pointBitWidth || pointBitWidth > UINT32_MAX ||
                  declaration == enumDeclarations.end()) {
                coverpoint.emitError(
                    "automatic enum bins require an integral enum "
                    "declaration inventory");
                signalPassFailure();
                return;
              }
              bool hasEnumerator = false;
              for (auto enumerator : declaration->second) {
                hasEnumerator = true;
                StringRef enumeratorName =
                    simlowering::getDebugName(enumerator);
                auto spelling =
                    enumerator->getAttrOfType<StringAttr>("constant_value");
                FailureOr<simlowering::ParsedConstant> parsed =
                    spelling
                        ? simlowering::parseSVInteger(
                              spelling.getValue(), pointBitWidth,
                              simlowering::getSemanticLocation(enumerator))
                        : FailureOr<simlowering::ParsedConstant>(failure());
                if (enumeratorName.empty() || failed(parsed)) {
                  enumerator.emitError(
                      "automatic enum bin has malformed declaration data");
                  signalPassFailure();
                  return;
                }
                const std::string binName =
                    (Twine("auto[") + enumeratorName + "]").str();
                const std::string hierarchy =
                    (Twine(pointHierarchy) + "." + binName).str();
                const uint64_t binID = stableFunctionalEntityID(
                    "functional.enum-automatic-bin", itemID, enumerator);
                auto [binOwner, insertedBin] =
                    binOwners.try_emplace(binID, enumerator.getOperation());
                if (!insertedBin) {
                  enumerator.emitError(
                      "functional coverage automatic enum bin ID collision")
                      << " with " << binOwner->second->getLoc();
                  signalPassFailure();
                  return;
                }
                const uint64_t valueSetID = stableFunctionalEntityID(
                    "functional.enum-automatic-value-set", binID, enumerator);
                auto [valueSetOwner, insertedValueSet] =
                    valueSetOwners.try_emplace(valueSetID,
                                               enumerator.getOperation());
                if (!insertedValueSet) {
                  enumerator.emitError(
                      "functional coverage automatic enum value-set ID "
                      "collision")
                      << " with " << valueSetOwner->second->getLoc();
                  signalPassFailure();
                  return;
                }
                schema.functionalBins.push_back(
                    {binID, itemID, binName, coverage::FunctionalBinKind::State,
                     coverage::FunctionalBinAutomatic, 1, binOrdinal,
                     hierarchy});
                PendingValueSet pending;
                pending.set.id = valueSetID;
                pending.set.item = itemID;
                pending.set.bitWidth = pointBitWidth;
                pending.set.signedness =
                    pointIsSigned ? coverage::CoverageSignedness::Signed
                                  : coverage::CoverageSignedness::Unsigned;
                // Section 19.5.3 names one bin for every enum member, but
                // automatic bins only consider two-state values. Preserve an
                // X/Z member as a zero-atom bin; resolution marks it empty.
                if (parsed->unknown.isZero()) {
                  coverage::FunctionalValueAtom atom;
                  atom.valueSet = valueSetID;
                  atom.limbCount = (pointBitWidth + 63) / 64;
                  atom.kind = coverage::FunctionalValueAtomKind::IntegralValue;
                  atom.flags = coverage::FunctionalValueAtomLowerInclusive |
                               coverage::FunctionalValueAtomUpperInclusive;
                  for (uint32_t limbOrdinal = 0; limbOrdinal != atom.limbCount;
                       ++limbOrdinal) {
                    uint64_t bitOffset64 = uint64_t{limbOrdinal} * 64;
                    unsigned bitOffset = static_cast<unsigned>(bitOffset64);
                    unsigned width = static_cast<unsigned>(
                        std::min<uint64_t>(64, pointBitWidth - bitOffset64));
                    uint64_t value =
                        parsed->value.extractBitsAsZExtValue(width, bitOffset);
                    pending.limbs.push_back(
                        {valueSetID, 0, limbOrdinal, value, 0, value, 0, 0});
                  }
                  pending.atoms.push_back(atom);
                }
                pendingValueSets.push_back(std::move(pending));
                schema.functionalBinPlans.push_back({binID, valueSetID});
                if (failed(inventoryFunctionalBinSource(coverpoint, binID,
                                                        hierarchy))) {
                  signalPassFailure();
                  return;
                }
                ++binOrdinal;
              }
              if (!hasEnumerator) {
                coverpoint.emitError(
                    "automatic enum bins require at least one enumerator");
                signalPassFailure();
                return;
              }
            }
            for (Operation *candidate : pointChildren) {
              auto bin = dyn_cast<ir::SVCoverageBinSymbolOp>(candidate);
              if (!bin)
                continue;
              const std::string binName =
                  reportName(simlowering::getDebugName(bin),
                             simlowering::getHierarchyName(bin));
              const std::string hierarchy =
                  (Twine(pointHierarchy) + "." + binName).str();
              const uint64_t binID =
                  stableFunctionalEntityID("functional.bin", itemID, bin);
              auto [binOwner, insertedBin] =
                  binOwners.try_emplace(binID, bin.getOperation());
              if (!insertedBin) {
                bin.emitError("functional coverage bin ID collision")
                    << " with " << binOwner->second->getLoc();
                signalPassFailure();
                return;
              }
              FailureOr<simlowering::CoverageBinChildren> binChildren =
                  simlowering::decodeCoverageBinChildren(bin);
              if (failed(binChildren)) {
                bin.emitError(
                    "cannot build the typed functional bin expression plan");
                signalPassFailure();
                return;
              }
              uint32_t binFlags = 0;
              if (bin.getIsDefault() && !bin.getIsDefaultSequence())
                binFlags |= coverage::FunctionalBinDefault;
              if (bin.getIsDefaultSequence())
                binFlags |= coverage::FunctionalBinDefaultSequence;
              if (bin.getBinsKind() == ir::SVCoverageBinKind::IgnoreBins)
                binFlags |= coverage::FunctionalBinIgnore;
              if (bin.getBinsKind() == ir::SVCoverageBinKind::IllegalBins)
                binFlags |= coverage::FunctionalBinIllegal;
              if (bin.getIsWildcard())
                binFlags |= coverage::FunctionalBinWildcard;
              const bool transition = bin.getTransitionSetCount() != 0 ||
                                      bin.getIsDefaultSequence();
              schema.functionalBins.push_back(
                  {binID, itemID, binName,
                   transition ? coverage::FunctionalBinKind::Transition
                              : coverage::FunctionalBinKind::State,
                   binFlags, 1, binOrdinal, hierarchy});
              if (binChildren->iff &&
                  failed(addFunctionalExpression(
                      binChildren->iff, binID,
                      coverage::FunctionalExpressionOwnerKind::Bin,
                      coverage::FunctionalExpressionRole::BinIff,
                      coverage::FunctionalExpressionEvaluationPhase::Sample, 0,
                      0, coverage::FunctionalExpressionResultKind::Boolean))) {
                bin.emitError("cannot inventory the functional bin iff");
                signalPassFailure();
                return;
              }

              uint64_t valueSetID = 0;
              if (transition) {
                const bool defaultSequence = bin.getIsDefaultSequence();
                if (pointIsReal ||
                    (defaultSequence && !binChildren->transitions.empty()) ||
                    (!defaultSequence && binChildren->transitions.empty())) {
                  bin.emitError("transition bins require an integral "
                                "coverpoint and default sequence must not "
                                "contain explicit sequences");
                  signalPassFailure();
                  return;
                }
                PendingTransitionProgram transitionProgram;
                transitionProgram.program.bin = binID;
                transitionProgram.program.item = itemID;
                for (auto [alternativeOrdinal, transitionSet] :
                     llvm::enumerate(binChildren->transitions)) {
                  if (alternativeOrdinal > UINT32_MAX ||
                      transitionSet.ranges.empty() ||
                      transitionSet.ranges.size() > UINT32_MAX) {
                    bin.emitError("transition sequence exceeds the v1 limits");
                    signalPassFailure();
                    return;
                  }
                  PendingTransitionAlternative transitionAlternative;
                  transitionAlternative.alternative.bin = binID;
                  transitionAlternative.alternative.ordinal =
                      static_cast<uint32_t>(alternativeOrdinal);
                  for (auto [stepOrdinal, transitionRange] :
                       llvm::enumerate(transitionSet.ranges)) {
                    const bool consecutive =
                        transitionRange.repeatKind ==
                        ir::SVCoverageTransitionRepeatKind::Consecutive;
                    const bool gotoRepetition =
                        transitionRange.repeatKind ==
                        ir::SVCoverageTransitionRepeatKind::GoTo;
                    const bool nonconsecutive =
                        transitionRange.repeatKind ==
                        ir::SVCoverageTransitionRepeatKind::Nonconsecutive;
                    if (stepOrdinal > UINT32_MAX ||
                        transitionRange.items.empty() ||
                        (transitionRange.repeatKind !=
                             ir::SVCoverageTransitionRepeatKind::None &&
                         !consecutive && !gotoRepetition && !nonconsecutive)) {
                      bin.emitError("this transition step is outside the "
                                    "bounded-repetition v1 slice");
                      signalPassFailure();
                      return;
                    }
                    const std::string valueSetKind =
                        (Twine("functional.transition-step-value-set.") +
                         Twine(alternativeOrdinal) + "." + Twine(stepOrdinal))
                            .str();
                    const uint64_t stepValueSetID = stableFunctionalEntityID(
                        valueSetKind, binID, transitionRange.items.front());
                    auto [valueSetOwner, insertedValueSet] =
                        valueSetOwners.try_emplace(
                            stepValueSetID, transitionRange.items.front());
                    if (!stepValueSetID || !insertedValueSet) {
                      auto diagnostic = bin.emitError(
                          "functional transition value-set ID collision");
                      if (!insertedValueSet)
                        diagnostic << " with "
                                   << valueSetOwner->second->getLoc();
                      signalPassFailure();
                      return;
                    }
                    PendingValueSet pending;
                    pending.set.id = stepValueSetID;
                    pending.set.item = itemID;
                    pending.set.bitWidth = pointWidth;
                    pending.set.kind =
                        coverage::FunctionalValueSetKind::Integral;
                    pending.set.signedness =
                        pointIsSigned ? coverage::CoverageSignedness::Signed
                                      : coverage::CoverageSignedness::Unsigned;
                    pending.set.flags =
                        coverage::FunctionalValueSetNeedsResolution;
                    for (auto [atomOrdinal, value] :
                         llvm::enumerate(transitionRange.items)) {
                      SmallVector<Operation *> endpoints =
                          simlowering::getChildren(value);
                      Operation *lower =
                          endpoints.empty() ? value : endpoints.front();
                      Operation *upper =
                          endpoints.size() == 2 ? endpoints.back() : nullptr;
                      auto peelEffectiveTypeCast = [&](Operation *expression) {
                        auto conversion =
                            dyn_cast<ir::SVConversionExpressionOp>(expression);
                        BoolAttr implicit =
                            expression->getAttrOfType<BoolAttr>("is_implicit");
                        if (!conversion || !implicit || !implicit.getValue())
                          return expression;
                        FailureOr<Type> normalized =
                            simlowering::getNormalizedSemanticType(expression);
                        std::optional<unsigned> width =
                            succeeded(normalized)
                                ? sim::getPackedWidth(*normalized)
                                : std::nullopt;
                        SmallVector<Operation *> children =
                            simlowering::getChildren(conversion);
                        return width && *width == pointWidth &&
                                       children.size() == 1
                                   ? children.front()
                                   : expression;
                      };
                      lower = peelEffectiveTypeCast(lower);
                      if (upper)
                        upper = peelEffectiveTypeCast(upper);
                      const bool lowerUnbounded =
                          simlowering::isUnboundedEndpoint(lower);
                      const bool upperUnbounded =
                          upper && simlowering::isUnboundedEndpoint(upper);
                      if ((lowerUnbounded || upperUnbounded) &&
                          (!upper || (lowerUnbounded && upperUnbounded))) {
                        bin.emitError("transition open range must have exactly "
                                      "one bounded endpoint");
                        signalPassFailure();
                        return;
                      }
                      coverage::FunctionalValueAtom atom;
                      atom.valueSet = stepValueSetID;
                      atom.ordinal = atomOrdinal;
                      atom.kind =
                          upper
                              ? coverage::FunctionalValueAtomKind::IntegralRange
                              : coverage::FunctionalValueAtomKind::
                                    IntegralValue;
                      atom.flags = coverage::FunctionalValueAtomLowerInclusive |
                                   coverage::FunctionalValueAtomUpperInclusive;
                      if (lowerUnbounded)
                        atom.flags |=
                            coverage::FunctionalValueAtomLowerUnbounded;
                      if (upperUnbounded)
                        atom.flags |=
                            coverage::FunctionalValueAtomUpperUnbounded;
                      if (!lowerUnbounded) {
                        if (failed(addFunctionalExpression(
                                lower, stepValueSetID,
                                coverage::FunctionalExpressionOwnerKind::
                                    ValueSet,
                                upper ? coverage::FunctionalExpressionRole::
                                            ValueAtomLower
                                      : coverage::FunctionalExpressionRole::
                                            ValueAtomSingleton,
                                coverage::FunctionalExpressionEvaluationPhase::
                                    Constructor,
                                atomOrdinal, 0))) {
                          bin.emitError("cannot inventory a transition value");
                          signalPassFailure();
                          return;
                        }
                        atom.lowerExpression =
                            cov::get<cov::FunctionalExpressionId>(lower)
                                .getValue()
                                .getZExtValue();
                      }
                      if (upper && !upperUnbounded) {
                        if (failed(addFunctionalExpression(
                                upper, stepValueSetID,
                                coverage::FunctionalExpressionOwnerKind::
                                    ValueSet,
                                coverage::FunctionalExpressionRole::
                                    ValueAtomUpper,
                                coverage::FunctionalExpressionEvaluationPhase::
                                    Constructor,
                                atomOrdinal, 0))) {
                          bin.emitError(
                              "cannot inventory a transition range bound");
                          signalPassFailure();
                          return;
                        }
                        atom.upperExpression =
                            cov::get<cov::FunctionalExpressionId>(upper)
                                .getValue()
                                .getZExtValue();
                      }
                      pending.atoms.push_back(atom);
                    }
                    pendingValueSets.push_back(std::move(pending));
                    coverage::TransitionStep step;
                    step.bin = binID;
                    step.valueSet = stepValueSetID;
                    step.alternativeOrdinal =
                        static_cast<uint32_t>(alternativeOrdinal);
                    step.ordinal = static_cast<uint32_t>(stepOrdinal);
                    if (consecutive || gotoRepetition || nonconsecutive) {
                      if (!transitionRange.repeatFrom) {
                        bin.emitError("transition repetition has no lower "
                                      "bound");
                        signalPassFailure();
                        return;
                      }
                      step.repetition =
                          consecutive
                              ? coverage::TransitionRepetitionKind::Consecutive
                          : gotoRepetition
                              ? coverage::TransitionRepetitionKind::Goto
                              : coverage::TransitionRepetitionKind::
                                    Nonconsecutive;
                      step.flags = coverage::TransitionStepNeedsResolution;
                      if (failed(addFunctionalExpression(
                              transitionRange.repeatFrom, binID,
                              coverage::FunctionalExpressionOwnerKind::Bin,
                              coverage::FunctionalExpressionRole::RepeatLower,
                              coverage::FunctionalExpressionEvaluationPhase::
                                  Constructor,
                              alternativeOrdinal, stepOrdinal))) {
                        bin.emitError("cannot inventory a transition repeat "
                                      "lower bound");
                        signalPassFailure();
                        return;
                      }
                      step.lowerExpression =
                          cov::get<cov::FunctionalExpressionId>(
                              transitionRange.repeatFrom)
                              .getValue()
                              .getZExtValue();
                      if (transitionRange.repeatTo) {
                        if (failed(addFunctionalExpression(
                                transitionRange.repeatTo, binID,
                                coverage::FunctionalExpressionOwnerKind::Bin,
                                coverage::FunctionalExpressionRole::RepeatUpper,
                                coverage::FunctionalExpressionEvaluationPhase::
                                    Constructor,
                                alternativeOrdinal, stepOrdinal))) {
                          bin.emitError("cannot inventory a transition repeat "
                                        "upper bound");
                          signalPassFailure();
                          return;
                        }
                        step.upperExpression =
                            cov::get<cov::FunctionalExpressionId>(
                                transitionRange.repeatTo)
                                .getValue()
                                .getZExtValue();
                      }
                    }
                    transitionAlternative.steps.push_back(step);
                  }
                  transitionAlternative.alternative.terminalValueSet =
                      transitionAlternative.steps.back().valueSet;
                  transitionProgram.alternatives.push_back(
                      std::move(transitionAlternative));
                }
                pendingTransitionPrograms.push_back(
                    std::move(transitionProgram));
              }
              if (!transition && !bin.getIsDefault()) {
                // Materialize the optional before validating and recording it.
                // Besides making the postcondition explicit, this avoids a
                // false maybe-uninitialized warning from some GCC versions
                // when the optional is dereferenced below the early return.
                const uint64_t pointBitWidth = pointIsReal ? 64 : pointWidth;
                if (!pointBitWidth || pointBitWidth > UINT32_MAX) {
                  bin.emitError("functional bin does not have a supported "
                                "coverpoint width");
                  signalPassFailure();
                  return;
                }
                valueSetID = stableFunctionalEntityID("functional.value-set",
                                                      binID, bin);
                auto [valueSetOwner, insertedValueSet] =
                    valueSetOwners.try_emplace(valueSetID, bin.getOperation());
                if (!insertedValueSet) {
                  bin.emitError("functional coverage value-set ID collision")
                      << " with " << valueSetOwner->second->getLoc();
                  signalPassFailure();
                  return;
                }
                PendingValueSet pending;
                pending.set.id = valueSetID;
                pending.set.item = itemID;
                pending.set.bitWidth = static_cast<uint32_t>(pointBitWidth);
                pending.set.kind =
                    pointIsReal ? coverage::FunctionalValueSetKind::Real
                                : coverage::FunctionalValueSetKind::Integral;
                pending.set.signedness =
                    pointIsReal ? coverage::CoverageSignedness::NotApplicable
                    : pointIsSigned ? coverage::CoverageSignedness::Signed
                                    : coverage::CoverageSignedness::Unsigned;
                pending.set.flags = coverage::FunctionalValueSetNeedsResolution;
                if (binChildren->with) {
                  auto candidates =
                      cov::get<cov::WithCandidateValues>(binChildren->with);
                  auto iteratorPath =
                      cov::get<cov::WithIteratorPath>(binChildren->with);
                  if (!candidates || !iteratorPath || candidates.empty() ||
                      candidates.size() >
                          coverage::MaxFunctionalWithCandidates) {
                    bin.emitError("functional bin with predicate has no "
                                  "validated finite candidate plan");
                    signalPassFailure();
                    return;
                  }
                  SmallVector<int64_t> expressionIDs;
                  SmallVector<int64_t> resultOrdinals;
                  expressionIDs.reserve(candidates.size());
                  resultOrdinals.reserve(candidates.size());
                  const coverage::Digest semanticDigest =
                      functionalSemanticDigest(binChildren->with);
                  for (auto [candidateOrdinal, candidate] :
                       llvm::enumerate(candidates)) {
                    auto integer = dyn_cast<IntegerAttr>(candidate);
                    auto integerType =
                        integer ? dyn_cast<IntegerType>(integer.getType())
                                : IntegerType{};
                    if (!integerType ||
                        integerType.getWidth() != pointBitWidth ||
                        candidateOrdinal > UINT32_MAX) {
                      bin.emitError("functional bin with predicate has a "
                                    "malformed candidate value");
                      signalPassFailure();
                      return;
                    }
                    const uint32_t ownerOrdinal =
                        static_cast<uint32_t>(candidateOrdinal);
                    const uint64_t expressionID = stableFunctionalExpressionID(
                        binID, coverage::FunctionalExpressionOwnerKind::Bin,
                        coverage::FunctionalExpressionRole::BinWith,
                        ownerOrdinal, 0, binChildren->with, semanticDigest,
                        prefixMaps);
                    auto [previous, inserted] = expressionOwners.try_emplace(
                        expressionID, binChildren->with);
                    if (!inserted) {
                      binChildren->with->emitError(
                          "functional coverage expression ID collision with ")
                          << previous->second->getLoc();
                      signalPassFailure();
                      return;
                    }

                    coverage::FunctionalExpression descriptor;
                    descriptor.id = expressionID;
                    descriptor.owner = binID;
                    descriptor.ownerKind =
                        coverage::FunctionalExpressionOwnerKind::Bin;
                    descriptor.role =
                        coverage::FunctionalExpressionRole::BinWith;
                    descriptor.resultKind =
                        coverage::FunctionalExpressionResultKind::Boolean;
                    descriptor.signedness =
                        coverage::CoverageSignedness::NotApplicable;
                    descriptor.semanticDigest = semanticDigest;
                    descriptor.ownerOrdinal = ownerOrdinal;
                    descriptor.ownerSubordinal = 0;
                    descriptor.evaluationPhase = coverage::
                        FunctionalExpressionEvaluationPhase::Constructor;
                    descriptor.resultOrdinal = constructorResultOrdinal++;
                    schema.functionalExpressions.push_back(descriptor);
                    expressionIDs.push_back(static_cast<int64_t>(expressionID));
                    resultOrdinals.push_back(descriptor.resultOrdinal);
                  }
                  cov::set<cov::WithExpressionIds>(
                      binChildren->with,
                      DenseI64ArrayAttr::get(&getContext(), expressionIDs));
                  cov::set<cov::WithExpressionOrdinals>(
                      binChildren->with,
                      DenseI64ArrayAttr::get(&getContext(), resultOrdinals));

                  // A BinWith root represents a candidate batch in the sole
                  // v1 contract. Never leave a misleading singular
                  // FunctionalExpression descriptor on that operation.
                  cov::remove<cov::FunctionalExpressionId>(binChildren->with);
                  cov::remove<cov::FunctionalExpressionOrdinal>(
                      binChildren->with);
                  cov::remove<cov::FunctionalExpressionKind>(binChildren->with);
                  cov::remove<cov::FunctionalExpressionBitWidth>(
                      binChildren->with);
                  cov::remove<cov::FunctionalExpressionSignedness>(
                      binChildren->with);
                  cov::remove<cov::FunctionalExpressionPhase>(
                      binChildren->with);
                  cov::remove<cov::FunctionalExpressionRole>(binChildren->with);
                }
                if (binChildren->setCoverage) {
                  if (failed(addFunctionalExpression(
                          binChildren->setCoverage, valueSetID,
                          coverage::FunctionalExpressionOwnerKind::ValueSet,
                          coverage::FunctionalExpressionRole::BinSet,
                          coverage::FunctionalExpressionEvaluationPhase::
                              Constructor,
                          0, 0,
                          coverage::FunctionalExpressionResultKind::Set))) {
                    bin.emitError("cannot inventory the functional bin set");
                    signalPassFailure();
                    return;
                  }
                  pending.set.setExpression =
                      cov::get<cov::FunctionalExpressionId>(
                          binChildren->setCoverage)
                          .getValue()
                          .getZExtValue();
                } else {
                  for (auto [atomOrdinal, value] :
                       llvm::enumerate(binChildren->values)) {
                    auto valueRange =
                        dyn_cast<ir::SVValueRangeExpressionOp>(value);
                    SmallVector<Operation *> endpoints =
                        valueRange ? simlowering::getChildren(value)
                                   : SmallVector<Operation *>{};
                    Operation *lower =
                        endpoints.empty() ? value : endpoints.front();
                    Operation *upper =
                        endpoints.size() == 2 ? endpoints.back() : nullptr;
                    const bool lowerUnbounded =
                        simlowering::isUnboundedEndpoint(lower);
                    const bool upperUnbounded =
                        upper && simlowering::isUnboundedEndpoint(upper);
                    if ((lowerUnbounded || upperUnbounded) &&
                        (!upper || (lowerUnbounded && upperUnbounded))) {
                      bin.emitError("functional bin open range must have "
                                    "exactly one bounded endpoint");
                      signalPassFailure();
                      return;
                    }
                    lower = peelFunctionalValueCast(
                        lower, pointBitWidth,
                        succeeded(normalizedPointType) ? *normalizedPointType
                                                       : Type{});
                    if (upper)
                      upper = peelFunctionalValueCast(
                          upper, pointBitWidth,
                          succeeded(normalizedPointType) ? *normalizedPointType
                                                         : Type{});
                    coverage::FunctionalValueAtom atom;
                    atom.valueSet = valueSetID;
                    atom.ordinal = atomOrdinal;
                    atom.kind =
                        pointIsReal
                            ? coverage::FunctionalValueAtomKind::RealInterval
                        : upper
                            ? coverage::FunctionalValueAtomKind::IntegralRange
                            : coverage::FunctionalValueAtomKind::IntegralValue;
                    atom.flags = coverage::FunctionalValueAtomLowerInclusive |
                                 coverage::FunctionalValueAtomUpperInclusive;
                    if (pointIsReal && valueRange) {
                      atom.flags |= coverage::FunctionalValueAtomRealRange;
                      if (valueRange.getRangeKind() ==
                          ir::SVValueRangeKind::AbsoluteTolerance)
                        atom.flags |=
                            coverage::FunctionalValueAtomAbsoluteTolerance;
                      else if (valueRange.getRangeKind() ==
                               ir::SVValueRangeKind::RelativeTolerance)
                        atom.flags |=
                            coverage::FunctionalValueAtomRelativeTolerance;
                    }
                    if (lowerUnbounded)
                      atom.flags |= coverage::FunctionalValueAtomLowerUnbounded;
                    if (upperUnbounded)
                      atom.flags |= coverage::FunctionalValueAtomUpperUnbounded;
                    if (!lowerUnbounded) {
                      if (failed(addFunctionalExpression(
                              lower, valueSetID,
                              coverage::FunctionalExpressionOwnerKind::ValueSet,
                              upper ? coverage::FunctionalExpressionRole::
                                          ValueAtomLower
                                    : coverage::FunctionalExpressionRole::
                                          ValueAtomSingleton,
                              coverage::FunctionalExpressionEvaluationPhase::
                                  Constructor,
                              atomOrdinal, 0))) {
                        bin.emitError(
                            "cannot inventory a functional bin value");
                        signalPassFailure();
                        return;
                      }
                      atom.lowerExpression =
                          cov::get<cov::FunctionalExpressionId>(lower)
                              .getValue()
                              .getZExtValue();
                    }
                    if (upper && !upperUnbounded) {
                      if (failed(addFunctionalExpression(
                              upper, valueSetID,
                              coverage::FunctionalExpressionOwnerKind::ValueSet,
                              coverage::FunctionalExpressionRole::
                                  ValueAtomUpper,
                              coverage::FunctionalExpressionEvaluationPhase::
                                  Constructor,
                              atomOrdinal, 0))) {
                        bin.emitError(
                            "cannot inventory a functional bin range bound");
                        signalPassFailure();
                        return;
                      }
                      atom.upperExpression =
                          cov::get<cov::FunctionalExpressionId>(upper)
                              .getValue()
                              .getZExtValue();
                    }
                    pending.atoms.push_back(atom);
                  }
                }
                pendingValueSets.push_back(std::move(pending));
              }
              uint64_t iffExpressionID = 0;
              if (binChildren->iff)
                iffExpressionID =
                    cov::get<cov::FunctionalExpressionId>(binChildren->iff)
                        .getValue()
                        .getZExtValue();
              coverage::FunctionalBinPlan plan;
              plan.bin = binID;
              plan.valueSet = valueSetID;
              plan.iffExpression = iffExpressionID;
              if (bin.getIsArray()) {
                if (binChildren->numberOfBins) {
                  if (failed(addFunctionalExpression(
                          binChildren->numberOfBins, binID,
                          coverage::FunctionalExpressionOwnerKind::Bin,
                          coverage::FunctionalExpressionRole::ArrayCardinality,
                          coverage::FunctionalExpressionEvaluationPhase::
                              Constructor,
                          0, 0))) {
                    bin.emitError(
                        "cannot inventory the functional bin-array count");
                    signalPassFailure();
                    return;
                  }
                  plan.cardinalityExpression =
                      cov::get<cov::FunctionalExpressionId>(
                          binChildren->numberOfBins)
                          .getValue()
                          .getZExtValue();
                  plan.arrayMode = coverage::FunctionalBinArrayMode::Fixed;
                  plan.distribution =
                      coverage::FunctionalBinDistributionKind::Uniform;
                } else {
                  plan.arrayMode = coverage::FunctionalBinArrayMode::Unsized;
                  plan.distribution =
                      coverage::FunctionalBinDistributionKind::PerValue;
                }
              }
              schema.functionalBinPlans.push_back(plan);

              if (failed(inventoryFunctionalBinSource(bin, binID, hierarchy))) {
                signalPassFailure();
                return;
              }
              ++binOrdinal;
            }
          }
        }
      }
    }
    sortCoverageRecords(pendingCrossPlans, [](const auto &entry) {
      return coverageOrderKey(entry.plan.item);
    });
    for (PendingCrossPlan &pending : pendingCrossPlans) {
      if (schema.crossTargets.size() > UINT32_MAX ||
          pending.targets.size() > UINT32_MAX - schema.crossTargets.size() ||
          schema.crossBins.size() > UINT32_MAX ||
          pending.bins.size() > UINT32_MAX - schema.crossBins.size() ||
          schema.crossSelectorNodes.size() > UINT32_MAX ||
          pending.selectors.size() >
              UINT32_MAX - schema.crossSelectorNodes.size() ||
          schema.crossPredicateNodes.size() > UINT32_MAX ||
          pending.predicates.size() >
              UINT32_MAX - schema.crossPredicateNodes.size() ||
          schema.crossPredicateOperands.size() > UINT32_MAX ||
          pending.predicateOperands.size() >
              UINT32_MAX - schema.crossPredicateOperands.size() ||
          schema.crossSelectorOperands.size() > UINT32_MAX ||
          pending.selectorOperands.size() >
              UINT32_MAX - schema.crossSelectorOperands.size()) {
        module.emitError("cross target inventory exceeds the v1 limits");
        signalPassFailure();
        return;
      }
      pending.plan.firstTarget = schema.crossTargets.size();
      pending.plan.targetCount = pending.targets.size();
      pending.plan.firstBin = schema.crossBins.size();
      pending.plan.binCount = pending.bins.size();
      schema.crossPlans.push_back(pending.plan);
      llvm::append_range(schema.crossTargets, pending.targets);
      llvm::append_range(schema.crossBins, pending.bins);
      const uint32_t operandBase = schema.crossSelectorOperands.size();
      for (coverage::CrossSelectorNode &selector : pending.selectors)
        selector.firstOperand += operandBase;
      llvm::append_range(schema.crossSelectorNodes, pending.selectors);
      const uint32_t predicateOperandBase =
          schema.crossPredicateOperands.size();
      for (coverage::CrossPredicateNode &node : pending.predicates)
        node.firstOperand += predicateOperandBase;
      llvm::append_range(schema.crossPredicateNodes, pending.predicates);
      llvm::append_range(schema.crossPredicateOperands,
                         pending.predicateOperands);
      llvm::append_range(schema.crossSelectorOperands,
                         pending.selectorOperands);
    }
    sortCoverageRecords(pendingTransitionPrograms, [](const auto &entry) {
      return coverageOrderKey(entry.program.bin);
    });
    for (PendingTransitionProgram &pending : pendingTransitionPrograms) {
      if (schema.transitionAlternatives.size() > UINT32_MAX ||
          pending.alternatives.size() >
              UINT32_MAX - schema.transitionAlternatives.size()) {
        module.emitError("transition alternative inventory exceeds the v1 "
                         "limits");
        signalPassFailure();
        return;
      }
      pending.program.firstAlternative = schema.transitionAlternatives.size();
      pending.program.alternativeCount = pending.alternatives.size();
      for (PendingTransitionAlternative &alternative : pending.alternatives) {
        if (schema.transitionSteps.size() > UINT32_MAX ||
            alternative.steps.size() >
                UINT32_MAX - schema.transitionSteps.size()) {
          module.emitError("transition step inventory exceeds the v1 limits");
          signalPassFailure();
          return;
        }
        alternative.alternative.firstStep = schema.transitionSteps.size();
        alternative.alternative.stepCount = alternative.steps.size();
        schema.transitionAlternatives.push_back(alternative.alternative);
        llvm::append_range(schema.transitionSteps, alternative.steps);
      }
      schema.transitionPrograms.push_back(pending.program);
    }
    // One shared row per relevant elaborated instance, independent of the
    // number of covergroups or modport views that reference its specialization.
    module.walk([&](sim::SimScopeDeclOp scope) {
      auto found = interfaceTypeIDs.find(scope.getInterfaceTypeAttr());
      if (found != interfaceTypeIDs.end())
        schema.functionalVirtualInterfaceScopes.push_back(
            {scope.getId(), found->second});
    });
    sortCoverageRecords(
        schema.functionalVirtualInterfaceTypes,
        [](const auto &entry) { return coverageOrderKey(entry.id); });
    sortCoverageRecords(
        schema.functionalVirtualInterfaceScopes,
        [](const auto &entry) { return coverageOrderKey(entry.id); });
    sortCoverageRecords(schema.functionalTypes, [](const auto &entry) {
      return coverageOrderKey(entry.id);
    });
    sortCoverageRecords(schema.functionalItems, [](const auto &entry) {
      return coverageOrderKey(entry.id);
    });
    sortCoverageRecords(schema.functionalBins, [](const auto &entry) {
      return coverageOrderKey(entry.id);
    });
    sortCoverageRecords(schema.functionalContainerTypes, [](const auto &entry) {
      return coverageOrderKey(entry.id);
    });
    sortCoverageRecords(schema.functionalAggregateTypes, [](const auto &entry) {
      return coverageOrderKey(entry.id);
    });
    sortCoverageRecords(schema.functionalClassTypes, [](const auto &entry) {
      return coverageOrderKey(entry.id);
    });
    sortCoverageRecords(schema.functionalFormals, [](const auto &entry) {
      return coverageOrderKey(entry.type, entry.kind, entry.ordinal);
    });
    sortCoverageRecords(schema.functionalExpressions, [](const auto &entry) {
      return coverageOrderKey(entry.id);
    });
    sortCoverageRecords(schema.functionalBinPlans, [](const auto &entry) {
      return coverageOrderKey(entry.bin);
    });
    sortCoverageRecords(schema.crossPredicateNodes, [](const auto &entry) {
      return coverageOrderKey(entry.selector, entry.ordinal);
    });
    // Sorting the nodes permutes their operand ranges. Repack the pooled
    // IDs once so every range stays canonical, without per-node allocation.
    auto oldPredicateOperands = std::move(schema.crossPredicateOperands);
    schema.crossPredicateOperands.clear();
    schema.crossPredicateOperands.reserve(oldPredicateOperands.size());
    for (coverage::CrossPredicateNode &node : schema.crossPredicateNodes) {
      uint32_t first = node.firstOperand;
      node.firstOperand = schema.crossPredicateOperands.size();
      for (uint32_t i = 0; i != node.operandCount; ++i)
        schema.crossPredicateOperands.push_back(
            oldPredicateOperands[first + i]);
    }
    sortCoverageRecords(schema.crossSelectorNodes, [](const auto &entry) {
      return coverageOrderKey(entry.cross, entry.ordinal);
    });
    sortCoverageRecords(schema.functionalOptionPlans, [](const auto &entry) {
      return coverageOrderKey(entry.ownerKind, entry.owner, entry.scope,
                              entry.option, entry.ordinal);
    });
    sortCoverageRecords(pendingValueSets, [](const auto &entry) {
      return coverageOrderKey(entry.set.id);
    });
    for (PendingValueSet &pending : pendingValueSets) {
      pending.set.firstAtom = schema.functionalValueAtoms.size();
      pending.set.atomCount = pending.atoms.size();
      schema.functionalValueSets.push_back(pending.set);
      for (coverage::FunctionalValueAtom &atom : pending.atoms) {
        atom.firstLimb += schema.functionalValueLimbs.size();
        schema.functionalValueAtoms.push_back(atom);
      }
      schema.functionalValueLimbs.insert(schema.functionalValueLimbs.end(),
                                         pending.limbs.begin(),
                                         pending.limbs.end());
    }
    sortCoverageRecords(schema.functionalSourceRanges, [](const auto &entry) {
      return coverageOrderKey(entry.bin, entry.role, entry.ordinal);
    });
  }

  sortCoverageRecords(schema.sourceFiles, [](const auto &entry) {
    return coverageOrderKey(entry.id);
  });
  sortCoverageRecords(schema.scopes, [](const auto &entry) {
    return coverageOrderKey(entry.id);
  });
  sortCoverageRecords(schema.exclusions, [](const auto &entry) {
    return coverageOrderKey(entry.metric, entry.entity);
  });

  module.walk(
      [](Operation *operation) { cov::remove<cov::SourceType>(operation); });

  std::vector<uint8_t> encoded;
  coverage::Diagnostic diagnostic;
  coverage::Status status = coverage::serialize(schema, encoded, &diagnostic);
  if (status != coverage::Status::Ok) {
    module.emitError() << "failed to serialize coverage schema: "
                       << coverage::statusName(status)
                       << (diagnostic.field ? " at field " : "")
                       << (diagnostic.field ? diagnostic.field : "")
                       << (diagnostic.detail.empty() ? "" : ": ")
                       << diagnostic.detail;
    signalPassFailure();
    return;
  }

  runtime::set<runtime::CoverageSchemaBlob>(
      module,
      DenseI8ArrayAttr::get(
          &getContext(),
          ArrayRef<int8_t>(reinterpret_cast<const int8_t *>(encoded.data()),
                           encoded.size())));
}

} // namespace obelisk
