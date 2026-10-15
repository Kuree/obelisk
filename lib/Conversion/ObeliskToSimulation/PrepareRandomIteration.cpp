//===- PrepareRandomIteration.cpp - Frozen iterative constraints ----------===//

#include "PrepareRandomIteration.h"
#include "Detail.h"
#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/ScopeExit.h"

#include <cstdint>
#include <functional>
#include <string>

using namespace mlir;

namespace obelisk::simlowering {

/// Static foreach instances expanded into one randomize plan.
static constexpr uint64_t maxStaticForeachInstances =
    maxRandomContainerShadowElements;

LogicalResult
expandStaticRandomForeachConstraints(MutableArrayRef<Operation *> roots,
                                     Builder &builder) {
  struct Dimension {
    StringAttr symbol;
    int64_t left;
    int64_t right;
  };
  for (Operation *&root : roots) {
    WalkResult result = root->walk<
        WalkOrder::PostOrder>([&](semantic::SVForeachConstraintOp loop) {
      if (foreach_metadata::hasRuntimeDimension(loop.getLoopDimensions()))
        return WalkResult::advance();
      SmallVector<Operation *> children = getChildren(loop);
      if (children.size() != 2) {
        emitError(getSemanticLocation(loop))
            << "foreach constraint requires an array expression and body";
        return WalkResult::interrupt();
      }
      SmallVector<Dimension> dimensions;
      uint64_t count = 1;
      for (Attribute attribute : loop.getLoopDimensions()) {
        auto metadata = dyn_cast<DictionaryAttr>(attribute);
        auto iterator =
            metadata ? metadata.getAs<BoolAttr>(foreach_metadata::hasIterator)
                     : BoolAttr{};
        if (!iterator) {
          emitError(getSemanticLocation(loop))
              << "static foreach constraint metadata is malformed";
          return WalkResult::interrupt();
        }
        if (!iterator.getValue())
          continue;
        auto symbol =
            metadata.getAs<SymbolRefAttr>(foreach_metadata::iteratorSymbol);
        auto left = metadata.getAs<IntegerAttr>(foreach_metadata::left);
        auto right = metadata.getAs<IntegerAttr>(foreach_metadata::right);
        if (!symbol || !left || !right) {
          emitError(getSemanticLocation(loop))
              << "static foreach constraint requires iterator and range "
                 "metadata";
          return WalkResult::interrupt();
        }
        APInt distance = left.getValue().sext(65) - right.getValue().sext(65);
        if (distance.isNegative())
          distance = -distance;
        ++distance;
        if (distance.getActiveBits() > 32 ||
            distance.getZExtValue() > UINT32_MAX / count) {
          emitError(getSemanticLocation(loop))
              << "static foreach constraint exceeds the solver's 32-bit "
                 "instruction inventory";
          return WalkResult::interrupt();
        }
        count *= distance.getZExtValue();
        dimensions.push_back(
            {symbol.getLeafReference(), left.getInt(), right.getInt()});
      }
      // As for procedural foreach, omitting every iterator creates no body
      // instances. An empty conjunction is true.
      if (dimensions.empty())
        count = 0;
      // Each instance becomes part of the exact sampling plan, whose
      // construction and runtime counting grow faster than linearly in the
      // number of instances. Diagnose larger expansions instead of exhausting
      // compile or run time.
      if (count > maxStaticForeachInstances) {
        emitError(getSemanticLocation(loop))
            << "static foreach constraint expands to " << count
            << " instances; exact sampling plans support at most "
            << maxStaticForeachInstances;
        return WalkResult::interrupt();
      }

      NamedAttrList attributes(loop->getAttrs());
      attributes.erase("loop_dimensions");
      attributes.set("item_count", builder.getI64IntegerAttr(count));
      OperationState state(loop.getLoc(),
                           semantic::SVConstraintListOp::getOperationName());
      state.addAttributes(attributes);
      state.addRegion()->push_back(new Block());
      OpBuilder insertion(loop);
      Operation *expanded = insertion.create(state);
      Block &body = expanded->getRegion(0).front();
      llvm::DenseMap<StringAttr, int64_t> indices;
      std::function<void(unsigned)> emitDimension = [&](unsigned dimension) {
        if (dimension == dimensions.size()) {
          Operation *cloned = children.back()->clone();
          body.push_back(cloned);
          cloned->walk<WalkOrder::PostOrder>([&](Operation *expression) {
            if (!isa<semantic::SVNamedValueExpressionOp,
                     semantic::SVHierarchicalValueExpressionOp>(expression))
              return;
            auto symbol =
                expression->getAttrOfType<SymbolRefAttr>("referenced_symbol");
            if (!symbol)
              return;
            auto index = indices.find(symbol.getLeafReference());
            if (index == indices.end())
              return;
            NamedAttrList literalAttributes(expression->getAttrs());
            literalAttributes.erase("referenced_symbol");
            literalAttributes.erase("referenced_path");
            literalAttributes.set(
                "constant_value",
                builder.getStringAttr(std::to_string(index->second)));
            OperationState literalState(
                expression->getLoc(),
                semantic::SVIntegerLiteralOp::getOperationName());
            literalState.addAttributes(literalAttributes);
            literalState.addRegion()->push_back(new Block());
            OpBuilder literalBuilder(expression);
            literalBuilder.create(literalState);
            expression->erase();
          });
          return;
        }
        const Dimension &range = dimensions[dimension];
        int64_t value = range.left;
        for (;;) {
          indices[range.symbol] = value;
          emitDimension(dimension + 1);
          // Stop before incrementing an endpoint, including INT64_MIN/MAX.
          if (value == range.right)
            break;
          value += range.left <= range.right ? 1 : -1;
        }
        indices.erase(range.symbol);
      };
      if (count != 0)
        emitDimension(0);
      if (root == loop.getOperation())
        root = expanded;
      loop->erase();
      return WalkResult::advance();
    });
    if (result.wasInterrupted())
      return failure();
  }
  return success();
}

LogicalResult
expandStaticRandomUniquenessConstraints(MutableArrayRef<Operation *> roots,
                                        Builder &builder) {
  struct Dimension {
    Type element;
    int64_t left;
    int64_t right;
  };
  auto dimension = [](Type type) -> std::optional<Dimension> {
    if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type))
      return Dimension{array.getElementType(), array.getLeft(),
                       array.getRight()};
    if (auto array = dyn_cast<semantic::UnpackedArrayType>(type))
      return Dimension{array.getElementType(), 0,
                       static_cast<int64_t>(array.getSize()) - 1};
    return std::nullopt;
  };
  for (Operation *&root : roots) {
    WalkResult result = root->walk<
        WalkOrder::PostOrder>([&](semantic::SVUniquenessConstraintOp unique) {
      SmallVector<Operation *> children = getChildren(unique);
      bool hasArray = false;
      uint64_t count = 0;
      for (Operation *child : children) {
        auto attribute = child->getAttrOfType<TypeAttr>("semantic_type");
        if (!attribute) {
          emitError(getSemanticLocation(child))
              << "uniqueness operand has no semantic type";
          return WalkResult::interrupt();
        }
        Type type = attribute.getValue();
        uint64_t leaves = 1;
        while (auto array = dimension(type)) {
          hasArray = true;
          uint64_t extent = 0;
          if (auto sized = dyn_cast<semantic::UnpackedArrayType>(type)) {
            extent = sized.getSize();
          } else {
            APInt distance(65, static_cast<uint64_t>(array->left), true);
            distance -= APInt(65, static_cast<uint64_t>(array->right), true);
            if (distance.isNegative())
              distance = -distance;
            ++distance;
            if (distance.getActiveBits() > 32) {
              emitError(getSemanticLocation(child))
                  << "fixed uniqueness array exceeds the solver's 32-bit "
                     "instruction inventory";
              return WalkResult::interrupt();
            }
            extent = distance.getZExtValue();
          }
          if (extent != 0 && leaves > UINT32_MAX / extent) {
            emitError(getSemanticLocation(child))
                << "fixed uniqueness array exceeds the solver's 32-bit "
                   "instruction inventory";
            return WalkResult::interrupt();
          }
          leaves *= extent;
          type = array->element;
        }
        if (leaves > UINT32_MAX - count) {
          emitError(getSemanticLocation(unique))
              << "fixed uniqueness group exceeds the solver's 32-bit "
                 "instruction inventory";
          return WalkResult::interrupt();
        }
        count += leaves;
      }
      if (!hasArray)
        return WalkResult::advance();
      if (count > 1 && count * (count - 1) / 2 > UINT32_MAX) {
        emitError(getSemanticLocation(unique))
            << "fixed uniqueness comparisons exceed the solver's 32-bit "
               "instruction inventory";
        return WalkResult::interrupt();
      }

      NamedAttrList attributes(unique->getAttrs());
      attributes.set("item_count", builder.getI64IntegerAttr(count));
      OperationState state(
          unique.getLoc(),
          semantic::SVUniquenessConstraintOp::getOperationName());
      state.addAttributes(attributes);
      state.addRegion()->push_back(new Block());
      OpBuilder insertion(unique);
      Operation *expanded = insertion.create(state);
      Block &body = expanded->getRegion(0).front();
      Type indexType =
          semantic::IntegralType::get(builder.getContext(), 64, true, false, 63,
                                      0, semantic::SVIntegralFlavor::LongInt);
      std::function<void(Operation *, Type)> emitLeaves =
          [&](Operation *expression, Type type) {
            auto array = dimension(type);
            if (!array) {
              body.push_back(expression->clone());
              return;
            }
            if (auto sized = dyn_cast<semantic::UnpackedArrayType>(type))
              if (sized.getSize() == 0)
                return;
            int64_t index = array->left;
            for (;;) {
              OperationState selection(
                  expression->getLoc(),
                  semantic::SVElementSelectExpressionOp::getOperationName());
              selection.addAttribute("node_id", unique->getAttr("node_id"));
              selection.addAttribute("semantic_type",
                                     TypeAttr::get(array->element));
              Block *selectionBody = new Block();
              selection.addRegion()->push_back(selectionBody);
              selectionBody->push_back(expression->clone());
              OperationState literal(
                  expression->getLoc(),
                  semantic::SVIntegerLiteralOp::getOperationName());
              literal.addAttribute("node_id", unique->getAttr("node_id"));
              literal.addAttribute("semantic_type", TypeAttr::get(indexType));
              literal.addAttribute(
                  "constant_value",
                  builder.getStringAttr(std::to_string(index)));
              literal.addRegion()->push_back(new Block());
              selectionBody->push_back(Operation::create(literal));
              Operation *selected = Operation::create(selection);
              auto destroy = llvm::scope_exit([&] { selected->destroy(); });
              emitLeaves(selected, array->element);
              if (index == array->right)
                break;
              index += array->left <= array->right ? 1 : -1;
            }
          };
      for (Operation *child : children)
        emitLeaves(child,
                   child->getAttrOfType<TypeAttr>("semantic_type").getValue());
      if (root == unique.getOperation())
        root = expanded;
      unique->erase();
      return WalkResult::advance();
    });
    if (result.wasInterrupted())
      return failure();
  }
  return success();
}

LogicalResult
expandStaticRandomInsideExpressions(MutableArrayRef<Operation *> roots,
                                    Builder &builder) {
  struct Dimension {
    Type element;
    int64_t left;
    int64_t right;
  };
  struct Member {
    Operation *expression;
    SmallVector<Dimension> dimensions;
    uint64_t leaves = 1;
  };
  Type indexType =
      semantic::IntegralType::get(builder.getContext(), 64, true, false, 63, 0,
                                  semantic::SVIntegralFlavor::LongInt);
  for (Operation *&root : roots) {
    WalkResult result = root->walk<
        WalkOrder::PostOrder>([&](semantic::SVInsideExpressionOp inside) {
      auto children = getChildren(inside);
      if (children.empty() || inside.getItemCount() != children.size() - 1) {
        emitError(getSemanticLocation(inside))
            << "malformed inside item inventory";
        return WalkResult::interrupt();
      }
      SmallVector<Member> members;
      uint64_t count = 0;
      bool hasArray = false;
      for (Operation *child : ArrayRef(children).drop_front()) {
        Member member{child, {}, 1};
        auto attribute = child->getAttrOfType<TypeAttr>("semantic_type");
        Type type = attribute ? attribute.getValue() : Type{};
        for (;;) {
          Dimension dimension;
          uint64_t extent;
          if (auto array =
                  dyn_cast_or_null<semantic::RangedUnpackedArrayType>(type)) {
            dimension = {array.getElementType(), array.getLeft(),
                         array.getRight()};
            APInt distance(65, static_cast<uint64_t>(dimension.left), true);
            distance -= APInt(65, static_cast<uint64_t>(dimension.right), true);
            if (distance.isNegative())
              distance = -distance;
            ++distance;
            if (distance.getActiveBits() > 32) {
              emitError(getSemanticLocation(child))
                  << "fixed inside array exceeds the solver's 32-bit "
                     "instruction inventory";
              return WalkResult::interrupt();
            }
            extent = distance.getZExtValue();
          } else if (auto array =
                         dyn_cast_or_null<semantic::UnpackedArrayType>(type)) {
            dimension = {array.getElementType(), 0,
                         static_cast<int64_t>(array.getSize()) - 1};
            extent = array.getSize();
          } else {
            break;
          }
          hasArray = true;
          if (extent != 0 && member.leaves > UINT32_MAX / extent) {
            emitError(getSemanticLocation(child))
                << "fixed inside array exceeds the solver's 32-bit "
                   "instruction inventory";
            return WalkResult::interrupt();
          }
          member.leaves *= extent;
          member.dimensions.push_back(dimension);
          type = dimension.element;
        }
        if (member.leaves > UINT32_MAX - count) {
          emitError(getSemanticLocation(inside))
              << "fixed inside set exceeds the solver's 32-bit "
                 "instruction inventory";
          return WalkResult::interrupt();
        }
        count += member.leaves;
        members.push_back(std::move(member));
      }
      if (!hasArray)
        return WalkResult::advance();

      Operation *expanded = inside->cloneWithoutRegions();
      expanded->setAttr("item_count", builder.getI64IntegerAttr(count));
      expanded->getRegion(0).push_back(new Block());
      Block &body = expanded->getRegion(0).front();
      children.front()->moveBefore(&body, body.end());
      for (const Member &member : members) {
        if (member.dimensions.empty()) {
          member.expression->moveBefore(&body, body.end());
          continue;
        }
        SmallVector<int64_t> indices;
        for (const Dimension &dimension : member.dimensions)
          indices.push_back(dimension.left);
        for (uint64_t leaf = 0; leaf != member.leaves; ++leaf) {
          // Clone the base once per emitted leaf and wrap it by moving
          // each preceding selection. This costs the size of the lowered
          // leaf paths, including singleton dimensions, rather than
          // repeatedly cloning every prefix of a multidimensional path.
          Operation *selected = member.expression->clone();
          for (auto [dimension, index] :
               llvm::zip_equal(member.dimensions, indices)) {
            OperationState selection(
                member.expression->getLoc(),
                semantic::SVElementSelectExpressionOp::getOperationName());
            selection.addAttribute("node_id", inside->getAttr("node_id"));
            selection.addAttribute("semantic_type",
                                   TypeAttr::get(dimension.element));
            if (sim::has<sim::metadata::RandomFunctionExpression>(inside))
              selection.addAttribute(
                  sim::metadata::RandomFunctionExpression::getNameStr(),
                  builder.getUnitAttr());
            Block *selectionBody = new Block();
            selection.addRegion()->push_back(selectionBody);
            selectionBody->push_back(selected);
            OperationState literal(
                member.expression->getLoc(),
                semantic::SVIntegerLiteralOp::getOperationName());
            literal.addAttribute("node_id", inside->getAttr("node_id"));
            literal.addAttribute("semantic_type", TypeAttr::get(indexType));
            literal.addAttribute("constant_value",
                                 builder.getStringAttr(std::to_string(index)));
            literal.addRegion()->push_back(new Block());
            selectionBody->push_back(Operation::create(literal));
            selected = Operation::create(selection);
          }
          body.push_back(selected);
          // Stop before incrementing the final endpoint, including
          // INT64_MIN/MAX. The next leaf follows declaration order.
          if (leaf + 1 == member.leaves)
            break;
          for (size_t index = indices.size(); index != 0;) {
            --index;
            const Dimension &dimension = member.dimensions[index];
            if (indices[index] != dimension.right) {
              indices[index] += dimension.left <= dimension.right ? 1 : -1;
              break;
            }
            indices[index] = dimension.left;
          }
        }
      }
      inside->getBlock()->getOperations().insert(inside->getIterator(),
                                                 expanded);
      if (root == inside.getOperation())
        root = expanded;
      inside->erase();
      return WalkResult::advance();
    });
    if (result.wasInterrupted())
      return failure();
  }
  return success();
}

std::optional<APSInt> evaluateRandomConstantOperand(
    Operation *expression,
    function_ref<std::optional<StringRef>(Operation *)> referencedConstant) {
  auto typeAttr = expression->getAttrOfType<TypeAttr>("semantic_type");
  std::optional<uint64_t> width =
      typeAttr ? getSemanticPackedWidth(typeAttr.getValue()) : std::nullopt;
  if (!width || *width == 0 || *width > 64)
    return std::nullopt;
  bool isSigned = isSignedSemanticType(typeAttr.getValue());
  auto representable = [&](const APInt &value) -> std::optional<APSInt> {
    if (isSigned ? !value.isSignedIntN(*width)
                 : value.isNegative() || value.getActiveBits() > *width)
      return std::nullopt;
    return APSInt(value, /*isUnsigned=*/false);
  };
  std::optional<StringRef> spelling = getConstantSpelling(expression);
  if (!spelling)
    spelling = referencedConstant(expression);
  if (spelling) {
    FailureOr<ParsedConstant> parsed =
        parseSVInteger(*spelling, *width, getSemanticLocation(expression));
    if (failed(parsed) || !parsed->unknown.isZero())
      return std::nullopt;
    return representable(isSigned ? parsed->value.sext(randomConstantWidth)
                                  : parsed->value.zext(randomConstantWidth));
  }
  SmallVector<Operation *> operands = getChildren(expression);
  SmallVector<APSInt, 2> values;
  for (Operation *operand : operands) {
    std::optional<APSInt> value =
        evaluateRandomConstantOperand(operand, referencedConstant);
    if (!value)
      return std::nullopt;
    values.push_back(*value);
  }
  if (isa<semantic::SVConversionExpressionOp>(expression) && values.size() == 1)
    return representable(values.front());
  if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(expression);
      unary && values.size() == 1) {
    if (unary.getOperatorKind() == semantic::SVUnaryOperator::Plus)
      return representable(values.front());
    if (unary.getOperatorKind() == semantic::SVUnaryOperator::Minus)
      return representable(-values.front());
    return std::nullopt;
  }
  auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(expression);
  if (!binary || values.size() != 2)
    return std::nullopt;
  const APSInt &lhs = values[0], &rhs = values[1];
  using Binary = semantic::SVBinaryOperator;
  switch (binary.getOperatorKind()) {
  case Binary::Add:
    return representable(lhs + rhs);
  case Binary::Subtract:
    return representable(lhs - rhs);
  case Binary::Multiply:
    return representable(lhs * rhs);
  case Binary::Divide:
    return rhs.isZero() ? std::nullopt : representable(lhs.sdiv(rhs));
  case Binary::Mod:
    return rhs.isZero() ? std::nullopt : representable(lhs.srem(rhs));
  default:
    return std::nullopt;
  }
}

Operation *peelValuePreservingConversions(Operation *expression,
                                          bool nonnegative) {
  auto layout =
      [](Operation *operation) -> std::optional<std::pair<uint64_t, bool>> {
    auto type = operation->getAttrOfType<TypeAttr>("semantic_type");
    std::optional<uint64_t> width =
        type ? getSemanticPackedWidth(type.getValue()) : std::nullopt;
    if (!width)
      return std::nullopt;
    return std::make_pair(*width, isSignedSemanticType(type.getValue()));
  };
  SmallVector<Operation *> conversions;
  Operation *operand = expression;
  while (isa<semantic::SVConversionExpressionOp>(operand)) {
    SmallVector<Operation *> children = getChildren(operand);
    if (children.size() != 1)
      return expression;
    conversions.push_back(operand);
    operand = children.front();
  }
  Operation *source = operand;
  for (Operation *conversion : llvm::reverse(conversions)) {
    auto from = layout(source), to = layout(conversion);
    if (!from || !to || to->first < from->first)
      return expression;
    if (from->second != to->second &&
        !(to->second ? to->first > from->first : nonnegative))
      return expression;
    source = conversion;
  }
  return operand;
}

std::optional<APSInt> getRandomUpperBound(
    Operation *constraint, function_ref<bool(Operation *)> isBounded,
    function_ref<std::optional<APSInt>(Operation *)> upperBound) {
  auto lesser = [](std::optional<APSInt> lhs, std::optional<APSInt> rhs) {
    if (!lhs || !rhs)
      return lhs ? lhs : rhs;
    return std::optional<APSInt>(*lhs < *rhs ? *lhs : *rhs);
  };
  auto greater = [](std::optional<APSInt> lhs, std::optional<APSInt> rhs) {
    if (!lhs || !rhs)
      return std::optional<APSInt>();
    return std::optional<APSInt>(*lhs < *rhs ? *rhs : *lhs);
  };
  std::function<std::optional<APSInt>(Operation *)> expressionBound =
      [&](Operation *expression) -> std::optional<APSInt> {
    SmallVector<Operation *> operands = getChildren(expression);
    if (auto inside = dyn_cast<semantic::SVInsideExpressionOp>(expression)) {
      if (operands.size() < 2 || !isBounded(operands.front()))
        return std::nullopt;
      std::optional<APSInt> bound;
      for (Operation *item : ArrayRef(operands).drop_front()) {
        std::optional<APSInt> value;
        if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item)) {
          SmallVector<Operation *> limits = getChildren(range);
          if (range.getRangeKind() == semantic::SVValueRangeKind::Simple &&
              limits.size() == 2)
            value = upperBound(limits.back());
        } else {
          value = upperBound(item);
        }
        if (!value)
          return std::nullopt;
        bound = bound ? greater(bound, value) : value;
      }
      return bound;
    }
    auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(expression);
    if (!binary || operands.size() != 2)
      return std::nullopt;
    using Binary = semantic::SVBinaryOperator;
    Binary kind = binary.getOperatorKind();
    if (kind == Binary::LogicalAnd)
      return lesser(expressionBound(operands[0]), expressionBound(operands[1]));
    if (kind == Binary::LogicalOr)
      return greater(expressionBound(operands[0]),
                     expressionBound(operands[1]));
    // Orient the comparison as operand <op> bounded value.
    bool boundedFirst = isBounded(operands[0]);
    if (!boundedFirst && !isBounded(operands[1]))
      return std::nullopt;
    std::optional<APSInt> value = upperBound(operands[boundedFirst ? 1 : 0]);
    if (!value)
      return std::nullopt;
    if (!boundedFirst) {
      if (kind == Binary::GreaterThanEqual)
        kind = Binary::LessThanEqual;
      else if (kind == Binary::GreaterThan)
        kind = Binary::LessThan;
      else if (kind == Binary::LessThanEqual || kind == Binary::LessThan)
        return std::nullopt;
    }
    switch (kind) {
    case Binary::Equality:
    case Binary::CaseEquality:
    case Binary::LessThanEqual:
      return value;
    case Binary::LessThan:
      return APSInt(*value - 1, /*isUnsigned=*/false);
    default:
      return std::nullopt;
    }
  };
  std::function<std::optional<APSInt>(Operation *)> constraintBound =
      [&](Operation *nested) -> std::optional<APSInt> {
    SmallVector<Operation *> children = getChildren(nested);
    if (auto expression = dyn_cast<semantic::SVExpressionConstraintOp>(nested))
      return !expression.getIsSoft() && children.size() == 1
                 ? expressionBound(children.front())
                 : std::nullopt;
    if (isa<semantic::SVConstraintListOp>(nested)) {
      std::optional<APSInt> bound;
      for (Operation *child : children)
        bound = lesser(bound, constraintBound(child));
      return bound;
    }
    // Each branch of an if-else constraint bounds the operand when it
    // applies.
    if (isa<semantic::SVConditionalConstraintOp>(nested) &&
        children.size() == 3)
      return greater(constraintBound(children[1]),
                     constraintBound(children[2]));
    return std::nullopt;
  };
  return constraintBound(constraint);
}

LogicalResult guardRandomContainerElements(
    Operation *&root,
    const llvm::DenseMap<Operation *, RandomContainerShadow> &shadows,
    Builder &builder) {
  MLIRContext *context = builder.getContext();
  Type bitType = semantic::IntegralType::get(context, 1, false, false, 0, 0,
                                             semantic::SVIntegralFlavor::Bit);
  auto create = [&](StringRef name, Location location, Attribute nodeID,
                    ArrayRef<NamedAttribute> attributes,
                    ArrayRef<Operation *> children) {
    OperationState state(location, name);
    state.addAttribute("node_id", nodeID);
    state.addAttributes(attributes);
    Block *body = new Block();
    state.addRegion()->push_back(body);
    for (Operation *child : children)
      body->push_back(child);
    return Operation::create(state);
  };
  // `index < size()` compared as unsigned, so that a negative index is
  // invalid too.
  auto createValidity = [&](Operation *index, Operation *sizeCall,
                            Attribute nodeID) -> FailureOr<Operation *> {
    Location location = index->getLoc();
    auto indexType = index->getAttrOfType<TypeAttr>("semantic_type");
    std::optional<uint64_t> indexWidth =
        indexType ? getSemanticPackedWidth(indexType.getValue()) : std::nullopt;
    if (!indexWidth || *indexWidth > 64) {
      emitError(getSemanticLocation(index))
          << "an element index of a random dynamic array or queue must be an "
             "integral value no wider than 64 bits";
      return failure();
    }
    unsigned width = *indexWidth <= 32 ? 32 : 64;
    Type compareType = semantic::IntegralType::get(
        context, width, false, false, width - 1, 0,
        width == 32 ? semantic::SVIntegralFlavor::Int
                    : semantic::SVIntegralFlavor::LongInt);
    auto convert = [&](Operation *operand) {
      return create(
          semantic::SVConversionExpressionOp::getOperationName(), location,
          nodeID,
          {builder.getNamedAttr("semantic_type", TypeAttr::get(compareType)),
           builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
           builder.getNamedAttr("is_implicit", builder.getBoolAttr(true))},
          {operand});
    };
    Operation *compare = create(
        semantic::SVBinaryExpressionOp::getOperationName(), location, nodeID,
        {builder.getNamedAttr("semantic_type", TypeAttr::get(bitType)),
         builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
         builder.getNamedAttr(
             "operator_kind",
             semantic::SVBinaryOperatorAttr::get(
                 context, semantic::SVBinaryOperator::LessThan))},
        {convert(index->clone()), convert(sizeCall->clone())});
    return create(semantic::SVExpressionConstraintOp::getOperationName(),
                  location, nodeID,
                  {builder.getNamedAttr("is_soft", builder.getBoolAttr(false))},
                  {compare});
  };
  // Replace `constraint` by a list of `prefix` followed by it.
  auto prepend = [&](Operation *constraint, ArrayRef<Operation *> prefix) {
    Operation *list =
        create(semantic::SVConstraintListOp::getOperationName(),
               constraint->getLoc(), constraint->getAttr("node_id"),
               {builder.getNamedAttr(
                   "item_count", builder.getI64IntegerAttr(prefix.size() + 1))},
               prefix);
    constraint->getBlock()->getOperations().insert(constraint->getIterator(),
                                                   list);
    constraint->moveBefore(&list->getRegion(0).front(),
                           list->getRegion(0).front().end());
    if (root == constraint)
      root = list;
  };

  // The constraint that creates each element read requires its validity.
  // Ordering constraints read no values.
  llvm::MapVector<Operation *, SmallVector<Operation *>> reads;
  SmallVector<semantic::SVForeachConstraintOp> loops;
  SmallVector<semantic::SVUniquenessConstraintOp> uniques;
  WalkResult result = root->walk([&](Operation *nested) {
    if (auto loop = dyn_cast<semantic::SVForeachConstraintOp>(nested)) {
      SmallVector<Operation *> children = getChildren(loop);
      if (!children.empty() && shadows.contains(children.front()))
        loops.push_back(loop);
      return WalkResult::advance();
    }
    if (auto unique = dyn_cast<semantic::SVUniquenessConstraintOp>(nested)) {
      if (llvm::any_of(getChildren(unique), [&](Operation *member) {
            return shadows.contains(member);
          }))
        uniques.push_back(unique);
      return WalkResult::advance();
    }
    auto select = dyn_cast<semantic::SVElementSelectExpressionOp>(nested);
    SmallVector<Operation *> children =
        select ? getChildren(select) : SmallVector<Operation *>();
    if (children.size() != 2 || !shadows.contains(children.front()))
      return WalkResult::advance();
    Operation *owner = select->getParentOp();
    while (owner && !owner->hasTrait<OpTrait::SemanticDeclarativeNode>())
      owner = owner->getParentOp();
    if (!owner) {
      emitError(getSemanticLocation(select))
          << "random container element read has no owning constraint";
      return WalkResult::interrupt();
    }
    if (!isa<semantic::SVSolveBeforeConstraintOp,
             semantic::SVDisableSoftConstraintOp>(owner))
      reads[owner].push_back(select);
    return WalkResult::advance();
  });
  if (result.wasInterrupted())
    return failure();

  for (auto &[constraint, selects] : reads) {
    SmallVector<Operation *> validity;
    for (Operation *select : selects) {
      SmallVector<Operation *> children = getChildren(select);
      FailureOr<Operation *> condition =
          createValidity(children[1], shadows.at(children[0]).sizeCall,
                         select->getAttr("node_id"));
      if (failed(condition)) {
        for (Operation *created : validity)
          created->destroy();
        return failure();
      }
      validity.push_back(*condition);
    }
    // A soft constraint prefers valid reads instead of requiring them, so it
    // never forces the size; a distribution must stay the whole expression.
    auto expression = dyn_cast<semantic::SVExpressionConstraintOp>(constraint);
    SmallVector<Operation *> operands =
        expression ? getChildren(expression) : SmallVector<Operation *>();
    if (!expression || !expression.getIsSoft() || operands.size() != 1 ||
        isa<semantic::SVDistExpressionOp>(operands.front())) {
      prepend(constraint, validity);
      continue;
    }
    Operation *conjunction = operands.front();
    conjunction->remove();
    for (Operation *condition : llvm::reverse(validity)) {
      Operation *compare = getChildren(condition).front();
      compare->remove();
      condition->destroy();
      conjunction =
          create(semantic::SVBinaryExpressionOp::getOperationName(),
                 compare->getLoc(), compare->getAttr("node_id"),
                 {builder.getNamedAttr("semantic_type", TypeAttr::get(bitType)),
                  builder.getNamedAttr("is_signed", builder.getBoolAttr(false)),
                  builder.getNamedAttr(
                      "operator_kind",
                      semantic::SVBinaryOperatorAttr::get(
                          context, semantic::SVBinaryOperator::LogicalAnd))},
                 {compare, conjunction});
    }
    expression->getRegion(0).front().push_back(conjunction);
  }

  // The condition `ordinal < size()` of one shadowed element.
  auto createGuard = [&](Operation *index, Operation *sizeCall,
                         Attribute nodeID) -> FailureOr<Operation *> {
    FailureOr<Operation *> guard = createValidity(index, sizeCall, nodeID);
    if (failed(guard))
      return failure();
    Operation *condition = getChildren(*guard).front();
    condition->remove();
    (*guard)->destroy();
    return condition;
  };
  Type ordinalType = semantic::IntegralType::get(
      context, 32, true, false, 31, 0, semantic::SVIntegralFlavor::Int);
  auto createOrdinal = [&](uint64_t ordinal, Location location,
                           Attribute nodeID) {
    return create(
        semantic::SVIntegerLiteralOp::getOperationName(), location, nodeID,
        {builder.getNamedAttr("semantic_type", TypeAttr::get(ordinalType)),
         builder.getNamedAttr("is_signed", builder.getBoolAttr(true)),
         builder.getNamedAttr("constant_value",
                              builder.getStringAttr(std::to_string(ordinal)))},
        {});
  };

  // IEEE 1800-2023 18.5.4: the members of a uniqueness group are its
  // singular variables and the leaf elements of its arrays, so a container
  // contributes the elements below its size. The other members keep their
  // own group; each element must differ from them and from every other
  // element that exists.
  for (semantic::SVUniquenessConstraintOp unique : uniques) {
    Location location = unique.getLoc();
    Attribute nodeID = unique->getAttr("node_id");
    SmallVector<Operation *> members = getChildren(unique);
    SmallVector<Operation *> others;
    struct Element {
      Operation *reference;
      uint64_t ordinal;
    };
    SmallVector<Element> elements;
    for (Operation *member : members) {
      auto shadow = shadows.find(member);
      if (shadow == shadows.end()) {
        others.push_back(member);
        continue;
      }
      for (uint64_t ordinal = 0; ordinal != shadow->second.count; ++ordinal)
        elements.push_back({member, ordinal});
    }
    auto createElement = [&](const Element &element) {
      auto type = cast<semantic::RangedUnpackedArrayType>(
          element.reference->getAttrOfType<TypeAttr>("semantic_type")
              .getValue());
      return create(semantic::SVElementSelectExpressionOp::getOperationName(),
                    location, nodeID,
                    {builder.getNamedAttr(
                        "semantic_type", TypeAttr::get(type.getElementType()))},
                    {element.reference->clone(),
                     createOrdinal(element.ordinal, location, nodeID)});
    };
    auto createUnique = [&](ArrayRef<Operation *> group) {
      NamedAttrList attributes(unique->getAttrs());
      attributes.erase("node_id");
      attributes.set("item_count", builder.getI64IntegerAttr(group.size()));
      return create(semantic::SVUniquenessConstraintOp::getOperationName(),
                    location, nodeID, attributes.getAttrs(), group);
    };
    // `ordinal < size()` of an element, then `guard -> body`.
    auto guarded = [&](const Element &element,
                       Operation *body) -> FailureOr<Operation *> {
      FailureOr<Operation *> condition =
          createGuard(createOrdinal(element.ordinal, location, nodeID),
                      shadows.at(element.reference).sizeCall, nodeID);
      if (failed(condition)) {
        body->destroy();
        return failure();
      }
      return create(semantic::SVImplicationConstraintOp::getOperationName(),
                    location, nodeID, {}, {*condition, body});
    };
    SmallVector<Operation *> constraints;
    auto destroyConstraints = [&] {
      for (Operation *constraint : constraints)
        constraint->destroy();
    };
    if (!others.empty())
      constraints.push_back(createUnique(llvm::map_to_vector(
          others, [](Operation *other) { return other->clone(); })));
    for (auto [index, element] : llvm::enumerate(elements)) {
      if (!others.empty()) {
        SmallVector<Operation *> group{createElement(element)};
        for (Operation *other : others)
          group.push_back(other->clone());
        FailureOr<Operation *> constraint =
            guarded(element, createUnique(group));
        if (failed(constraint)) {
          destroyConstraints();
          return failure();
        }
        constraints.push_back(*constraint);
      }
      // A later element of the same container exists only if this one does.
      for (const Element &earlier : ArrayRef(elements).take_front(index)) {
        FailureOr<Operation *> constraint = guarded(
            element,
            createUnique({createElement(earlier), createElement(element)}));
        if (succeeded(constraint) && earlier.reference != element.reference)
          constraint = guarded(earlier, *constraint);
        if (failed(constraint)) {
          destroyConstraints();
          return failure();
        }
        constraints.push_back(*constraint);
      }
    }
    Operation *list = create(
        semantic::SVConstraintListOp::getOperationName(), location, nodeID,
        {builder.getNamedAttr("item_count",
                              builder.getI64IntegerAttr(constraints.size()))},
        constraints);
    unique->getBlock()->getOperations().insert(unique->getIterator(), list);
    if (root == unique.getOperation())
      root = list;
    unique->erase();
  }

  for (semantic::SVForeachConstraintOp loop : loops) {
    SmallVector<Operation *> children = getChildren(loop);
    const RandomContainerShadow &shadow = shadows.at(children.front());
    ArrayAttr dimensions = loop.getLoopDimensions();
    auto first = dimensions.empty() ? DictionaryAttr()
                                    : dyn_cast<DictionaryAttr>(dimensions[0]);
    if (!first || children.size() != 2) {
      emitError(getSemanticLocation(loop))
          << "foreach constraint metadata is malformed";
      return failure();
    }
    NamedAttrList range(first);
    range.set(foreach_metadata::hasStaticRange, builder.getBoolAttr(true));
    range.set(foreach_metadata::left, builder.getI64IntegerAttr(0));
    range.set(
        foreach_metadata::right,
        builder.getI64IntegerAttr(static_cast<int64_t>(shadow.count) - 1));
    SmallVector<Attribute> updated(dimensions.begin(), dimensions.end());
    updated[0] = range.getDictionary(context);
    loop.setLoopDimensionsAttr(builder.getArrayAttr(updated));
    auto iterator = first.getAs<BoolAttr>(foreach_metadata::hasIterator);
    if (!iterator || !iterator.getValue())
      continue;
    auto symbol = first.getAs<SymbolRefAttr>(foreach_metadata::iteratorSymbol);
    auto path = first.getAs<StringAttr>(foreach_metadata::iteratorPath);
    auto type = first.getAs<TypeAttr>(foreach_metadata::iteratorType);
    if (!symbol || !path || !type) {
      emitError(getSemanticLocation(loop))
          << "foreach constraint metadata is malformed";
      return failure();
    }
    Attribute nodeID = loop->getAttr("node_id");
    Operation *index =
        create(semantic::SVNamedValueExpressionOp::getOperationName(),
               loop.getLoc(), nodeID,
               {builder.getNamedAttr("semantic_type", type),
                builder.getNamedAttr(
                    "is_signed",
                    builder.getBoolAttr(isSignedSemanticType(type.getValue()))),
                builder.getNamedAttr("referenced_symbol", symbol),
                builder.getNamedAttr("referenced_path", path)},
               {});
    // The guard of an element instance is its index being below size().
    FailureOr<Operation *> condition =
        createGuard(index, shadow.sizeCall, nodeID);
    index->destroy();
    if (failed(condition))
      return failure();
    Operation *body = children.back();
    Operation *implication =
        create(semantic::SVImplicationConstraintOp::getOperationName(),
               loop.getLoc(), nodeID, {}, {});
    body->getBlock()->getOperations().insert(body->getIterator(), implication);
    Block &implicationBody = implication->getRegion(0).front();
    implicationBody.push_back(*condition);
    body->moveBefore(&implicationBody, implicationBody.end());
  }
  return success();
}

void replaceInvalidRandomContainerReads(
    MutableArrayRef<Operation *> roots,
    function_ref<bool(Operation *)> isShadowReference,
    function_ref<std::optional<APSInt>(Operation *)> evaluate,
    Builder &builder) {
  SmallVector<Operation *> invalid;
  for (Operation *root : roots)
    root->walk([&](semantic::SVElementSelectExpressionOp select) {
      SmallVector<Operation *> children = getChildren(select);
      if (children.size() != 2 || !isShadowReference(children.front()))
        return;
      auto shadow = cast<semantic::RangedUnpackedArrayType>(
          children.front()
              ->getAttrOfType<TypeAttr>("semantic_type")
              .getValue());
      std::optional<APSInt> index = evaluate(children[1]);
      if (index && (index->isNegative() || *index > shadow.getRight()))
        invalid.push_back(select);
    });
  for (Operation *select : invalid) {
    NamedAttrList attributes;
    attributes.set("node_id", select->getAttr("node_id"));
    attributes.set("semantic_type", select->getAttr("semantic_type"));
    if (Attribute isSigned = select->getAttr("is_signed"))
      attributes.set("is_signed", isSigned);
    attributes.set("constant_value", builder.getStringAttr("0"));
    OperationState literal(select->getLoc(),
                           semantic::SVIntegerLiteralOp::getOperationName());
    literal.addAttributes(attributes);
    literal.addRegion()->push_back(new Block());
    OpBuilder insertion(select);
    insertion.create(literal);
    select->erase();
  }
}

} // namespace obelisk::simlowering
