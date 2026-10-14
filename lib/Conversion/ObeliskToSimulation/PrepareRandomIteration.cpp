//===- PrepareRandomIteration.cpp - Frozen iterative constraints ----------===//

#include "PrepareRandomIteration.h"
#include "Detail.h"
#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"

#include <cstdint>
#include <functional>
#include <string>

using namespace mlir;

namespace obelisk::simlowering {

/// Static foreach instances expanded into one randomize plan.
static constexpr uint64_t maxStaticForeachInstances = 256;

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

} // namespace obelisk::simlowering
