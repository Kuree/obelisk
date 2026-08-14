//===- PrepareRandomTemplates.cpp - Reusable constraint templates --------===//

#include "PrepareRandomTemplates.h"

#include "Detail.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"

#include <functional>
#include <limits>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

using EffectiveConstraintGroup =
    SmallVector<semantic::SVConstraintBlockSymbolOp, 2>;

LogicalResult
collectClassHierarchy(semantic::SVClassTypeOp leaf,
                      const PreparedClassDeclarations &classes,
                      SmallVectorImpl<semantic::SVClassTypeOp> &hierarchy) {
  llvm::SmallPtrSet<Operation *, 8> visiting;
  std::function<LogicalResult(semantic::SVClassTypeOp)> collect =
      [&](semantic::SVClassTypeOp current) -> LogicalResult {
    if (!visiting.insert(current).second)
      return failure();
    if (std::optional<Type> baseType = current.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? classes.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : classes.semanticClasses.end();
      if (base == classes.semanticClasses.end() ||
          failed(collect(base->second)))
        return failure();
    }
    hierarchy.push_back(current);
    return success();
  };
  return collect(leaf);
}

void collectEffectiveConstraints(
    ArrayRef<semantic::SVClassTypeOp> hierarchy,
    SmallVectorImpl<EffectiveConstraintGroup> &groups) {
  llvm::StringMap<unsigned> namedIndices;
  for (semantic::SVClassTypeOp classType : hierarchy) {
    for (Operation *member : getChildren(classType)) {
      auto constraint = dyn_cast<semantic::SVConstraintBlockSymbolOp>(member);
      if (!constraint)
        continue;
      std::optional<StringRef> name = constraint.getName();
      if (!name) {
        groups.push_back({constraint});
        continue;
      }
      auto [entry, inserted] =
          namedIndices.try_emplace(*name, static_cast<unsigned>(groups.size()));
      if (inserted) {
        groups.push_back({constraint});
        continue;
      }
      EffectiveConstraintGroup &group = groups[entry->second];
      if (!constraint.getIsExtends().value_or(false))
        group.clear();
      group.push_back(constraint);
    }
  }
}

class TemplateBuilder {
public:
  TemplateBuilder(
      sim::SimRandomConstraintTemplateOp templateOp,
      ArrayRef<semantic::SVClassTypeOp> hierarchy,
      const PreparedClassDeclarations &classes,
      const llvm::StringMap<Operation *> &semanticSymbols,
      const llvm::StringMap<DescriptorInfo> &descriptors,
      const llvm::DenseMap<Operation *, unsigned> &constraintIndices)
      : templateOp(templateOp), classes(classes),
        semanticSymbols(semanticSymbols), descriptors(descriptors),
        constraintIndices(constraintIndices), builder(templateOp.getContext()) {
    templateOp.getBody().push_back(new Block());
    builder.setInsertionPointToStart(&templateOp.getBody().front());
    for (semantic::SVClassTypeOp classType : hierarchy)
      visibleClasses.insert(classType);
  }

  LogicalResult compile(ArrayRef<semantic::SVConstraintBlockSymbolOp> ordered) {
    for (semantic::SVConstraintBlockSymbolOp constraint : ordered) {
      auto block = constraintIndices.find(constraint);
      if (block == constraintIndices.end() ||
          constraint.getIsExtern().value_or(false) ||
          constraint.getIsPure().value_or(false))
        return failure();
      bool foundBody = false;
      for (Operation *child : getChildren(constraint)) {
        auto list = dyn_cast<semantic::SVConstraintListOp>(child);
        if (!list)
          continue;
        foundBody = true;
        if (failed(compileConstraintList(list, block->second)))
          return failure();
      }
      if (!foundBody)
        return failure();
    }
    if (constraintCount == 0)
      return failure();
    if (!references.empty())
      templateOp.setReferencesAttr(builder.getArrayAttr(references));
    return success();
  }

private:
  FailureOr<unsigned> width(Operation *operation) const {
    auto type = operation->getAttrOfType<TypeAttr>("semantic_type");
    // Template dataflow is deliberately exact two-state integer IR.  A
    // four-state value needs both value and unknown planes and therefore must
    // stay on the legacy path until symbolic references model both planes.
    if (!type || isFourStateSemanticType(type.getValue()))
      return failure();
    std::optional<uint64_t> packed = getSemanticPackedWidth(type.getValue());
    if (!packed || *packed == 0 ||
        *packed > std::numeric_limits<unsigned>::max())
      return failure();
    return static_cast<unsigned>(*packed);
  }

  bool isSigned(Operation *operation) const {
    auto type = operation->getAttrOfType<TypeAttr>("semantic_type");
    return type && isSignedSemanticType(type.getValue());
  }

  Value constant(Location location, const APInt &value) {
    Type type = IntegerType::get(builder.getContext(), value.getBitWidth());
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(type, value));
  }

  Value zero(Location location, unsigned width) {
    return constant(location, APInt::getZero(width));
  }

  Value ones(Location location, unsigned width) {
    return constant(location, APInt::getAllOnes(width));
  }

  FailureOr<Value> resize(Value value, unsigned resultWidth, bool signedInput,
                          Location location) {
    unsigned inputWidth = cast<IntegerType>(value.getType()).getWidth();
    if (inputWidth == resultWidth)
      return value;
    Type resultType = IntegerType::get(builder.getContext(), resultWidth);
    if (inputWidth > resultWidth)
      return arith::TruncIOp::create(builder, location, resultType, value)
          .getResult();
    if (signedInput)
      return arith::ExtSIOp::create(builder, location, resultType, value)
          .getResult();
    return arith::ExtUIOp::create(builder, location, resultType, value)
        .getResult();
  }

  Value truth(Value value, Location location) {
    unsigned inputWidth = cast<IntegerType>(value.getType()).getWidth();
    if (inputWidth == 1)
      return value;
    return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                 value, zero(location, inputWidth));
  }

  FailureOr<Value> reference(Operation *expression) {
    SymbolRefAttr reference;
    if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression))
      reference = named.getReferencedSymbol();
    else if (auto member =
                 dyn_cast<semantic::SVMemberAccessExpressionOp>(expression))
      reference = member.getReferencedSymbol();
    else if (auto hierarchical =
                 dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                     expression))
      reference = hierarchical.getReferencedSymbol();
    if (!reference)
      return failure();
    auto found = semanticSymbols.find(reference.getLeafReference());
    if (found == semanticSymbols.end())
      return failure();
    Operation *symbol = found->second;
    FailureOr<unsigned> valueWidth = width(expression);
    if (failed(valueWidth))
      return failure();

    sim::RandomValueReferenceAttr symbolic;
    if (auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(symbol)) {
      bool isStatic =
          property.getLifetime() == semantic::SVVariableLifetime::Static;
      auto owner = property->getParentOfType<semantic::SVClassTypeOp>();
      if (!isStatic && owner && visibleClasses.contains(owner)) {
        // Only an unqualified class-property name is proven to start at the
        // template owner.  Member and hierarchical expressions may denote a
        // nested or entirely different object; treating either as `this`
        // would silently bind the wrong state.
        if (!isa<semantic::SVNamedValueExpressionOp>(expression))
          return failure();
        FlatSymbolRefAttr field = classes.fieldSymbols.lookup(property);
        if (!field)
          return failure();
        symbolic = sim::RandomValueReferenceAttr::get(
            builder.getContext(), sim::RandomValueReferenceKind::ObjectField,
            ArrayRef<FlatSymbolRefAttr>{}, field, IntegerAttr{}, 0,
            *valueWidth);
      }
    }
    if (!symbolic) {
      StringRef path = getHierarchyName(symbol);
      auto descriptor = descriptors.find(path);
      if (descriptor == descriptors.end() ||
          descriptor->second.kind != DescriptorInfo::Kind::Storage)
        return failure();
      const DescriptorInfo &storage = descriptor->second;
      symbolic = sim::RandomValueReferenceAttr::get(
          builder.getContext(), sim::RandomValueReferenceKind::Storage,
          ArrayRef<FlatSymbolRefAttr>{}, FlatSymbolRefAttr{},
          builder.getI64IntegerAttr(storage.id), storage.packedViewOffset,
          *valueWidth);
    }

    auto [entry, inserted] = referenceIndices.try_emplace(
        symbolic, static_cast<unsigned>(references.size()));
    if (inserted)
      references.push_back(symbolic);
    Type type = IntegerType::get(builder.getContext(), *valueWidth);
    return sim::SimRandomConstraintValueOp::create(
               builder, getSemanticLocation(expression), type, entry->second)
        .getResult();
  }

  FailureOr<Value> expression(Operation *operation) {
    Location location = getSemanticLocation(operation);
    FailureOr<unsigned> resultWidth = width(operation);
    if (failed(resultWidth))
      return failure();

    if (std::optional<StringRef> spelling = getConstantSpelling(operation)) {
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(*spelling, *resultWidth, location);
      if (failed(parsed) || !parsed->unknown.isZero())
        return failure();
      return constant(location, parsed->value.zextOrTrunc(*resultWidth));
    }

    if (isa<semantic::SVNamedValueExpressionOp,
            semantic::SVMemberAccessExpressionOp,
            semantic::SVHierarchicalValueExpressionOp>(operation))
      return reference(operation);

    SmallVector<Operation *> children = getChildren(operation);
    if (isa<semantic::SVConversionExpressionOp>(operation)) {
      if (children.size() != 1)
        return failure();
      FailureOr<Value> input = expression(children.front());
      return succeeded(input)
                 ? resize(*input, *resultWidth, isSigned(operation), location)
                 : FailureOr<Value>(failure());
    }

    if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(operation)) {
      if (children.size() != 1)
        return failure();
      FailureOr<Value> input = expression(children.front());
      if (failed(input))
        return failure();
      FailureOr<Value> resized =
          resize(*input, *resultWidth, isSigned(children.front()), location);
      if (failed(resized))
        return failure();
      using Unary = semantic::SVUnaryOperator;
      switch (unary.getOperatorKind()) {
      case Unary::Plus:
        return *resized;
      case Unary::Minus:
        return arith::SubIOp::create(builder, location,
                                     zero(location, *resultWidth), *resized)
            .getResult();
      case Unary::BitwiseNot:
        return arith::XOrIOp::create(builder, location, *resized,
                                     ones(location, *resultWidth))
            .getResult();
      case Unary::LogicalNot: {
        Value predicate = truth(*input, location);
        return arith::XOrIOp::create(builder, location, predicate,
                                     ones(location, 1))
            .getResult();
      }
      case Unary::BitwiseAnd:
        return arith::CmpIOp::create(
                   builder, location, arith::CmpIPredicate::eq, *input,
                   ones(location,
                        cast<IntegerType>((*input).getType()).getWidth()))
            .getResult();
      case Unary::BitwiseNand: {
        Value reduced = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, *input,
            ones(location, cast<IntegerType>((*input).getType()).getWidth()));
        return arith::XOrIOp::create(builder, location, reduced,
                                     ones(location, 1))
            .getResult();
      }
      case Unary::BitwiseOr:
        return truth(*input, location);
      case Unary::BitwiseNor: {
        Value reduced = truth(*input, location);
        return arith::XOrIOp::create(builder, location, reduced,
                                     ones(location, 1))
            .getResult();
      }
      default:
        return failure();
      }
    }

    if (auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(operation)) {
      if (children.size() != 2)
        return failure();
      FailureOr<Value> left = expression(children[0]);
      FailureOr<Value> right = expression(children[1]);
      if (failed(left) || failed(right))
        return failure();
      using Binary = semantic::SVBinaryOperator;
      Binary kind = binary.getOperatorKind();

      if (kind == Binary::LogicalAnd || kind == Binary::LogicalOr ||
          kind == Binary::LogicalImplication ||
          kind == Binary::LogicalEquivalence) {
        Value lhs = truth(*left, location);
        Value rhs = truth(*right, location);
        switch (kind) {
        case Binary::LogicalAnd:
          return arith::AndIOp::create(builder, location, lhs, rhs).getResult();
        case Binary::LogicalOr:
          return arith::OrIOp::create(builder, location, lhs, rhs).getResult();
        case Binary::LogicalImplication: {
          Value notLeft =
              arith::XOrIOp::create(builder, location, lhs, ones(location, 1));
          return arith::OrIOp::create(builder, location, notLeft, rhs)
              .getResult();
        }
        case Binary::LogicalEquivalence:
          return arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::eq, lhs, rhs)
              .getResult();
        default:
          llvm_unreachable("filtered logical operator");
        }
      }

      bool comparison =
          kind == Binary::Equality || kind == Binary::Inequality ||
          kind == Binary::CaseEquality || kind == Binary::CaseInequality ||
          kind == Binary::WildcardEquality ||
          kind == Binary::WildcardInequality ||
          kind == Binary::GreaterThanEqual || kind == Binary::GreaterThan ||
          kind == Binary::LessThanEqual || kind == Binary::LessThan;

      bool shift = kind == Binary::LogicalShiftLeft ||
                   kind == Binary::ArithmeticShiftLeft ||
                   kind == Binary::LogicalShiftRight ||
                   kind == Binary::ArithmeticShiftRight;
      if (shift) {
        FailureOr<unsigned> shiftWidth = width(children[1]);
        std::optional<StringRef> spelling = getConstantSpelling(children[1]);
        FailureOr<ParsedConstant> amount =
            succeeded(shiftWidth) && spelling
                ? parseSVInteger(*spelling, *shiftWidth,
                                 getSemanticLocation(children[1]))
                : FailureOr<ParsedConstant>(failure());
        // arith shift operations are poison for an out-of-range amount,
        // whereas SystemVerilog defines an all-zero or sign-filled result.
        // Keep dynamic and out-of-range shifts on the legacy solver path.
        if (failed(amount) || !amount->unknown.isZero() ||
            amount->value.uge(*resultWidth) || *shiftWidth > *resultWidth)
          return failure();
        FailureOr<Value> lhs =
            resize(*left, *resultWidth, isSigned(children[0]), location);
        FailureOr<Value> rhs = resize(*right, *resultWidth, false, location);
        if (failed(lhs) || failed(rhs))
          return failure();
        if (kind == Binary::LogicalShiftLeft ||
            kind == Binary::ArithmeticShiftLeft)
          return arith::ShLIOp::create(builder, location, *lhs, *rhs)
              .getResult();
        if (kind == Binary::ArithmeticShiftRight && isSigned(children[0]))
          return arith::ShRSIOp::create(builder, location, *lhs, *rhs)
              .getResult();
        return arith::ShRUIOp::create(builder, location, *lhs, *rhs)
            .getResult();
      }

      unsigned operationWidth =
          comparison
              ? std::max(cast<IntegerType>((*left).getType()).getWidth(),
                         cast<IntegerType>((*right).getType()).getWidth())
              : *resultWidth;
      // IEEE 1800-2017 11.8.1 and 11.8.2: these operands affect one
      // another's common type; any unsigned operand makes both operands
      // unsigned before extension and evaluation.
      bool signedOperation = isSigned(children[0]) && isSigned(children[1]);
      FailureOr<Value> lhs =
          resize(*left, operationWidth, signedOperation, location);
      FailureOr<Value> rhs =
          resize(*right, operationWidth, signedOperation, location);
      if (failed(lhs) || failed(rhs))
        return failure();

      switch (kind) {
      case Binary::Add:
        return arith::AddIOp::create(builder, location, *lhs, *rhs).getResult();
      case Binary::Subtract:
        return arith::SubIOp::create(builder, location, *lhs, *rhs).getResult();
      case Binary::Multiply:
        return arith::MulIOp::create(builder, location, *lhs, *rhs).getResult();
      case Binary::Divide:
      case Binary::Mod: {
        FailureOr<unsigned> divisorWidth = width(children[1]);
        std::optional<StringRef> spelling = getConstantSpelling(children[1]);
        FailureOr<ParsedConstant> divisor =
            succeeded(divisorWidth) && spelling
                ? parseSVInteger(*spelling, *divisorWidth,
                                 getSemanticLocation(children[1]))
                : FailureOr<ParsedConstant>(failure());
        if (failed(divisor) || !divisor->unknown.isZero() ||
            divisor->value.isZero() ||
            (signedOperation && divisor->value.isAllOnes()))
          return failure();
        if (kind == Binary::Divide)
          return signedOperation
                     ? arith::DivSIOp::create(builder, location, *lhs, *rhs)
                           .getResult()
                     : arith::DivUIOp::create(builder, location, *lhs, *rhs)
                           .getResult();
        return signedOperation
                   ? arith::RemSIOp::create(builder, location, *lhs, *rhs)
                         .getResult()
                   : arith::RemUIOp::create(builder, location, *lhs, *rhs)
                         .getResult();
      }
      case Binary::BinaryAnd:
        return arith::AndIOp::create(builder, location, *lhs, *rhs).getResult();
      case Binary::BinaryOr:
        return arith::OrIOp::create(builder, location, *lhs, *rhs).getResult();
      case Binary::BinaryXor:
        return arith::XOrIOp::create(builder, location, *lhs, *rhs).getResult();
      case Binary::BinaryXnor: {
        Value value = arith::XOrIOp::create(builder, location, *lhs, *rhs);
        return arith::XOrIOp::create(builder, location, value,
                                     ones(location, operationWidth))
            .getResult();
      }
      case Binary::Equality:
      case Binary::CaseEquality:
      case Binary::WildcardEquality:
        return arith::CmpIOp::create(builder, location,
                                     arith::CmpIPredicate::eq, *lhs, *rhs)
            .getResult();
      case Binary::Inequality:
      case Binary::CaseInequality:
      case Binary::WildcardInequality:
        return arith::CmpIOp::create(builder, location,
                                     arith::CmpIPredicate::ne, *lhs, *rhs)
            .getResult();
      case Binary::GreaterThanEqual:
        return arith::CmpIOp::create(builder, location,
                                     signedOperation
                                         ? arith::CmpIPredicate::sge
                                         : arith::CmpIPredicate::uge,
                                     *lhs, *rhs)
            .getResult();
      case Binary::GreaterThan:
        return arith::CmpIOp::create(builder, location,
                                     signedOperation
                                         ? arith::CmpIPredicate::sgt
                                         : arith::CmpIPredicate::ugt,
                                     *lhs, *rhs)
            .getResult();
      case Binary::LessThanEqual:
        return arith::CmpIOp::create(builder, location,
                                     signedOperation
                                         ? arith::CmpIPredicate::sle
                                         : arith::CmpIPredicate::ule,
                                     *lhs, *rhs)
            .getResult();
      case Binary::LessThan:
        return arith::CmpIOp::create(builder, location,
                                     signedOperation
                                         ? arith::CmpIPredicate::slt
                                         : arith::CmpIPredicate::ult,
                                     *lhs, *rhs)
            .getResult();
      default:
        return failure();
      }
    }

    if (isa<semantic::SVConcatenationExpressionOp>(operation)) {
      if (children.empty())
        return failure();
      Value result = zero(location, *resultWidth);
      unsigned remaining = *resultWidth;
      for (Operation *child : children) {
        FailureOr<unsigned> childWidth = width(child);
        FailureOr<Value> childValue = expression(child);
        if (failed(childWidth) || failed(childValue) || *childWidth > remaining)
          return failure();
        remaining -= *childWidth;
        FailureOr<Value> extended =
            resize(*childValue, *resultWidth, false, location);
        if (failed(extended))
          return failure();
        Value shifted = *extended;
        if (remaining != 0)
          shifted = arith::ShLIOp::create(
              builder, location, shifted,
              constant(location, APInt(*resultWidth, remaining)));
        result = arith::OrIOp::create(builder, location, result, shifted);
      }
      return remaining == 0 ? FailureOr<Value>(result)
                            : FailureOr<Value>(failure());
    }

    if (auto conditional =
            dyn_cast<semantic::SVConditionalExpressionOp>(operation)) {
      uint64_t conditionCount = conditional.getConditionCount();
      ArrayRef<int64_t> patternFlags = conditional.getConditionPatternFlags();
      if (conditionCount == 0 || patternFlags.size() != conditionCount ||
          llvm::any_of(patternFlags,
                       [](int64_t value) { return value != 0; }) ||
          children.size() != conditionCount + 2)
        return failure();
      FailureOr<Value> predicate = expression(children.front());
      if (failed(predicate))
        return failure();
      Value combined = truth(*predicate, location);
      for (Operation *condition :
           ArrayRef(children).slice(1, conditionCount - 1)) {
        FailureOr<Value> next = expression(condition);
        if (failed(next))
          return failure();
        combined = arith::AndIOp::create(builder, location, combined,
                                         truth(*next, location));
      }
      FailureOr<Value> trueValue = expression(children[conditionCount]);
      FailureOr<Value> falseValue = expression(children[conditionCount + 1]);
      if (failed(trueValue) || failed(falseValue))
        return failure();
      FailureOr<Value> resizedTrue =
          resize(*trueValue, *resultWidth, isSigned(operation), location);
      FailureOr<Value> resizedFalse =
          resize(*falseValue, *resultWidth, isSigned(operation), location);
      if (failed(resizedTrue) || failed(resizedFalse))
        return failure();
      return arith::SelectOp::create(builder, location, combined, *resizedTrue,
                                     *resizedFalse)
          .getResult();
    }

    if (isa<semantic::SVInsideExpressionOp>(operation)) {
      if (children.size() < 2)
        return failure();
      Value matched;
      for (Operation *item : ArrayRef(children).drop_front()) {
        Value itemMatched;
        if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item)) {
          SmallVector<Operation *> endpoints = getChildren(range);
          if (endpoints.size() != 2)
            return failure();
          FailureOr<Value> value = expression(children.front());
          FailureOr<Value> low = expression(endpoints[0]);
          FailureOr<Value> high = expression(endpoints[1]);
          if (failed(value) || failed(low) || failed(high) ||
              (*value).getType() != (*low).getType() ||
              (*value).getType() != (*high).getType())
            return failure();
          bool signedRange = isSigned(children.front()) &&
                             isSigned(endpoints[0]) && isSigned(endpoints[1]);
          arith::CmpIPredicate ge = signedRange ? arith::CmpIPredicate::sge
                                                : arith::CmpIPredicate::uge;
          arith::CmpIPredicate le = signedRange ? arith::CmpIPredicate::sle
                                                : arith::CmpIPredicate::ule;
          Value above =
              arith::CmpIOp::create(builder, location, ge, *value, *low);
          Value below =
              arith::CmpIOp::create(builder, location, le, *value, *high);
          itemMatched = arith::AndIOp::create(builder, location, above, below);
        } else {
          FailureOr<Value> value = expression(children.front());
          FailureOr<Value> candidate = expression(item);
          if (failed(value) || failed(candidate) ||
              (*value).getType() != (*candidate).getType())
            return failure();
          itemMatched = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, *value, *candidate);
        }
        matched = matched ? arith::OrIOp::create(builder, location, matched,
                                                 itemMatched)
                          : itemMatched;
      }
      return matched ? FailureOr<Value>(matched) : FailureOr<Value>(failure());
    }

    return failure();
  }

  LogicalResult compileConstraintList(semantic::SVConstraintListOp list,
                                      unsigned block) {
    for (Operation *item : getChildren(list)) {
      auto constraint = dyn_cast<semantic::SVExpressionConstraintOp>(item);
      if (!constraint)
        return failure();
      SmallVector<Operation *> children = getChildren(constraint);
      if (children.size() != 1)
        return failure();
      FailureOr<Value> value = expression(children.front());
      if (failed(value))
        return failure();
      Value predicate = truth(*value, getSemanticLocation(constraint));
      if (constraint.getIsSoft()) {
        sim::SimRandomSoftConstraintOp::create(
            builder, getSemanticLocation(constraint), predicate, block,
            softPriority++);
      } else {
        sim::SimRandomHardConstraintOp::create(
            builder, getSemanticLocation(constraint), predicate, block);
      }
      ++constraintCount;
    }
    return success();
  }

  sim::SimRandomConstraintTemplateOp templateOp;
  const PreparedClassDeclarations &classes;
  const llvm::StringMap<Operation *> &semanticSymbols;
  const llvm::StringMap<DescriptorInfo> &descriptors;
  const llvm::DenseMap<Operation *, unsigned> &constraintIndices;
  OpBuilder builder;
  llvm::SmallPtrSet<Operation *, 8> visibleClasses;
  SmallVector<Attribute> references;
  llvm::DenseMap<Attribute, unsigned> referenceIndices;
  unsigned softPriority = 0;
  unsigned constraintCount = 0;
};

} // namespace

LogicalResult materializeRandomConstraintTemplates(
    sim::SimDesignOp design, const PreparedClassDeclarations &classes,
    const llvm::StringMap<Operation *> &semanticSymbols,
    const llvm::StringMap<DescriptorInfo> &descriptors) {
  OpBuilder builder(design.getContext());
  builder.setInsertionPointToEnd(&design.getBody().front());

  for (semantic::SVClassTypeOp exactClass : classes.sources) {
    if (exactClass.getIsAbstract() || exactClass.getIsInterface())
      continue;
    SmallVector<semantic::SVClassTypeOp> hierarchy;
    if (failed(collectClassHierarchy(exactClass, classes, hierarchy)))
      return exactClass.emitError(
          "cannot construct random constraint template class hierarchy");

    SmallVector<EffectiveConstraintGroup> groups;
    collectEffectiveConstraints(hierarchy, groups);
    if (groups.empty() || groups.size() > 64)
      continue;

    SmallVector<Attribute> blockReferences;
    llvm::DenseMap<Operation *, unsigned> constraintIndices;
    llvm::SmallPtrSet<Operation *, 16> activeDeclarations;
    bool representable = true;
    for (auto [index, group] : llvm::enumerate(groups)) {
      if (group.empty()) {
        representable = false;
        break;
      }
      semantic::SVConstraintBlockSymbolOp effective = group.back();
      sim::RandomConstraintBlockReferenceAttr reference;
      if (effective.getIsStatic().value_or(false)) {
        auto storage = effective->getAttrOfType<IntegerAttr>(
            staticConstraintStorageAttrName);
        if (!storage) {
          representable = false;
          break;
        }
        reference = sim::RandomConstraintBlockReferenceAttr::get(
            design.getContext(),
            sim::RandomConstraintBlockReferenceKind::Storage, IntegerAttr{},
            storage);
      } else {
        reference = sim::RandomConstraintBlockReferenceAttr::get(
            design.getContext(),
            sim::RandomConstraintBlockReferenceKind::ObjectBlock,
            builder.getI32IntegerAttr(index), IntegerAttr{});
      }
      blockReferences.push_back(reference);
      for (semantic::SVConstraintBlockSymbolOp declaration : group) {
        constraintIndices[declaration] = index;
        activeDeclarations.insert(declaration);
      }
    }
    if (!representable)
      continue;

    // Block identities above follow first declaration so constraint_mode bits
    // remain stable. Body order is independently reconstructed from source
    // declaration order, as required for soft priority by IEEE 1800-2017
    // 18.5.14.1.
    SmallVector<semantic::SVConstraintBlockSymbolOp> ordered;
    for (semantic::SVClassTypeOp classType : hierarchy)
      for (Operation *member : getChildren(classType))
        if (auto constraint =
                dyn_cast<semantic::SVConstraintBlockSymbolOp>(member))
          if (activeDeclarations.contains(constraint))
            ordered.push_back(constraint);

    StringAttr owner = classes.symbols.lookup(exactClass);
    if (!owner)
      return exactClass.emitError(
          "cannot resolve random constraint template owner symbol");
    std::string symbol = (owner.getValue() + "_random_constraints").str();
    auto templateOp = sim::SimRandomConstraintTemplateOp::create(
        builder, getSemanticLocation(exactClass), symbol, owner.getValue(),
        ArrayAttr{}, builder.getArrayAttr(blockReferences));
    TemplateBuilder compiler(templateOp, hierarchy, classes, semanticSymbols,
                             descriptors, constraintIndices);
    if (failed(compiler.compile(ordered))) {
      templateOp.erase();
      continue;
    }
    sim::SimClassDeclOp declaration = classes.declarations.lookup(exactClass);
    if (!declaration) {
      templateOp.erase();
      return exactClass.emitError(
          "cannot resolve random constraint template class declaration");
    }
    declaration.setRandomConstraintTemplateAttr(
        FlatSymbolRefAttr::get(design.getContext(), symbol));
    SymbolTable::setSymbolVisibility(templateOp,
                                     SymbolTable::Visibility::Private);
  }
  return success();
}

} // namespace obelisk::simlowering
