//===- PrepareExpressions.cpp - Immutable source expression relations
//------===//

#include "PrepareExpressions.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
#include "obelisk/Dialect/Simulation/SimulationVPIConstantValues.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "llvm/ADT/DenseSet.h"

using namespace mlir;

namespace obelisk::simlowering {
VPIExpressionResolver::VPIExpressionResolver(
    sim::SimDesignOp design, const llvm::StringMap<DescriptorInfo> &descriptors)
    : design(design), module(design->getParentOfType<ModuleOp>()),
      descriptors(descriptors) {
  for (auto declaration : design.getOps<sim::SimVPINetIdentityDeclOp>()) {
    auto [entry, inserted] = netIdentities.try_emplace(
        declaration.getHierarchicalName(), declaration);
    // Display paths need not distinguish compilation units. Ambiguity is not
    // permission to pick an arbitrary declaration.
    if (!inserted)
      entry->second = {};
  }
  for (auto declaration : design.getOps<sim::SimStorageDeclOp>())
    storages[declaration.getId()] = declaration;
  for (auto declaration : design.getOps<sim::SimNetDeclOp>())
    nets[declaration.getId()] = declaration;
}

std::optional<VPIExpressionEndpoint>
VPIExpressionResolver::resolve(Operation *expression) {
  if (!isa<semantic::SVNamedValueExpressionOp,
           semantic::SVHierarchicalValueExpressionOp>(expression))
    return std::nullopt;
  auto reference =
      expression->getAttrOfType<SymbolRefAttr>("referenced_symbol");
  Operation *source =
      reference && module ? symbols.lookupSymbolIn(module, reference) : nullptr;
  if (!source)
    return std::nullopt;
  // Ref declarations are expressions through their RefObj, even if later
  // preparation attaches an owned-local or anchor identity to the source.
  if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(source);
      formal && formal.getDirection() == semantic::SVArgumentDirection::Ref)
    return std::nullopt;
  if (auto port = dyn_cast<semantic::SVPortSymbolOp>(source);
      port && port.getDirection() == semantic::SVArgumentDirection::Ref)
    return std::nullopt;
  auto symbolic =
      [&](FlatSymbolRefAttr symbol) -> std::optional<VPIExpressionEndpoint> {
    if (!symbol)
      return std::nullopt;
    Operation *declaration = symbols.lookupSymbolIn(design, symbol);
    uint32_t kind = 0;
    if (auto local = dyn_cast_or_null<sim::SimVPILocalDeclOp>(declaration))
      kind = sim::vpiKindForClassMember(local.getVpiType());
    else if (auto anchor =
                 dyn_cast_or_null<sim::SimVPIObjectAnchorOp>(declaration))
      kind = anchor.getVpiKind();
    if (!kind)
      return std::nullopt;
    return VPIExpressionEndpoint{
        sim::VPIObjectRefAttr::get(expression->getContext(),
                                   sim::VPIObjectRefKind::Symbol, {}, symbol),
        kind};
  };
  if (auto member = sim::get<sim::metadata::VpiDefinitionMember>(source)) {
    if (failed(
            sim::verifyVPIDefinitionMemberRef(source, design, member, symbols)))
      return std::nullopt;
    auto declaration =
        symbols.lookupSymbolIn<sim::SimVPIDefinitionMemberDeclOp>(
            design, member.getSymbol());
    return VPIExpressionEndpoint{member, declaration.getVpiKind()};
  }
  if (auto local = sim::get<sim::metadata::VpiLocal>(source))
    return symbolic(local);
  if (auto anchor = sim::get<sim::metadata::VpiAnchor>(source))
    return symbolic(anchor);
  // Ref formals/ports require their RefObj identity. Do not expose an aliased
  // backing variable as the expression of the formal declaration (37.15).
  if (!isa<semantic::SVVariableSymbolOp, semantic::SVNetSymbolOp>(source))
    return std::nullopt;
  StringRef path = getHierarchyName(source);
  if (path.empty())
    return std::nullopt;
  auto found = descriptors.find(path);
  if (found == descriptors.end())
    return std::nullopt;
  const DescriptorInfo &descriptor = found->second;
  if (descriptor.viewOffset || descriptor.packedViewOffset ||
      !descriptor.viewIndices.empty() || descriptor.type != descriptor.rootType)
    return std::nullopt;
  Builder builder(expression->getContext());
  if (descriptor.kind == DescriptorInfo::Kind::Net) {
    auto declared = netIdentities.find(path);
    if (declared != netIdentities.end()) {
      auto identity = declared->second;
      if (!identity || identity.getBackingNetId() != descriptor.id)
        return std::nullopt;
      return VPIExpressionEndpoint{
          sim::VPIObjectRefAttr::get(expression->getContext(),
                                     sim::VPIObjectRefKind::NetIdentity,
                                     identity.getIdAttr()),
          sim::vpiKindForNet(identity.getVpiType())};
    }
    auto net = nets.lookup(descriptor.id);
    if (!net || !net.getVpiTypeAttr() || net.getHierarchicalName() != path)
      return std::nullopt;
    return VPIExpressionEndpoint{
        sim::VPIObjectRefAttr::get(expression->getContext(),
                                   sim::VPIObjectRefKind::Net,
                                   builder.getI64IntegerAttr(descriptor.id)),
        sim::vpiKindForNet(net.getVpiTypeAttr())};
  }
  if (descriptor.kind == DescriptorInfo::Kind::Storage) {
    auto storage = storages.lookup(descriptor.id);
    if (!storage || !storage.getVpiTypeAttr() ||
        storage.getHierarchicalName() != path)
      return std::nullopt;
    return VPIExpressionEndpoint{
        sim::VPIObjectRefAttr::get(expression->getContext(),
                                   sim::VPIObjectRefKind::Storage,
                                   builder.getI64IntegerAttr(descriptor.id)),
        sim::vpiKindForStorage(storage.getVpiTypeAttr())};
  }
  return std::nullopt;
}

VPIExpressionMaterializer::VPIExpressionMaterializer(
    sim::SimDesignOp design, const llvm::StringMap<DescriptorInfo> &descriptors,
    OpBuilder &builder)
    : design(design), declarations(design, descriptors), builder(builder) {
  for (auto expression : design.getOps<sim::SimVPIExpressionDeclOp>())
    nextID = std::max(nextID, uint64_t(expression.getId()) + 1);
}

FailureOr<std::optional<VPIExpressionEndpoint>>
VPIExpressionMaterializer::resolve(Operation *expression,
                                   sim::VPIObjectRefAttr owner,
                                   uint32_t ownerKind, uint64_t scope,
                                   uint32_t selector, sim::VPIRelationMode mode,
                                   uint64_t ordinal) {
  return resolveImpl(expression, owner, ownerKind, scope, selector, mode,
                     ordinal, true);
}

FailureOr<std::optional<SmallVector<VPIExpressionEndpoint>>>
VPIExpressionMaterializer::resolveGroup(ArrayRef<Operation *> expressions,
                                        sim::VPIObjectRefAttr owner,
                                        uint32_t ownerKind, uint64_t scope,
                                        uint32_t selector) {
  for (auto [ordinal, expression] : llvm::enumerate(expressions)) {
    auto result = resolveImpl(expression, owner, ownerKind, scope, selector,
                              sim::VPIRelationMode::Iterate, ordinal, false);
    if (failed(result))
      return failure();
    if (!*result)
      return std::optional<SmallVector<VPIExpressionEndpoint>>{};
  }
  SmallVector<VPIExpressionEndpoint> targets;
  for (auto [ordinal, expression] : llvm::enumerate(expressions)) {
    auto result = resolveImpl(expression, owner, ownerKind, scope, selector,
                              sim::VPIRelationMode::Iterate, ordinal, true);
    if (failed(result) || !*result)
      return failure();
    targets.push_back(**result);
  }
  return std::optional<SmallVector<VPIExpressionEndpoint>>(std::move(targets));
}

FailureOr<std::optional<VPIExpressionEndpoint>>
VPIExpressionMaterializer::resolveImpl(Operation *expression,
                                       sim::VPIObjectRefAttr owner,
                                       uint32_t ownerKind, uint64_t scope,
                                       uint32_t selector,
                                       sim::VPIRelationMode mode,
                                       uint64_t ordinal, bool materialize) {
  using Kind = reflection::VPIObjectKind;
  using Rel = reflection::VPIRelationKind;
  auto unsupported = []() -> std::optional<VPIExpressionEndpoint> {
    return std::nullopt;
  };
  auto integral = [](Type type) {
    for (;;) {
      if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type))
        type = array.getElementType();
      else if (auto array = dyn_cast<semantic::PackedArrayType>(type))
        type = array.getElementType();
      else
        return isa<semantic::IntegralType, semantic::LogicType>(type);
    }
  };
  struct Planned {
    Operation *source;
    std::optional<VPIExpressionEndpoint> endpoint;
    uint32_t kind = 0, code = 0;
    Type type;
    sim::VPITypeSemanticsAttr semantic;
    sim::FrozenConstantAttr frozen;
    StringAttr identifier;
    SmallVector<unsigned> children;
    bool sourceOnly = false;
    bool unqualifiedEvent = false;
  };
  SmallVector<Planned> plan;
  plan.push_back({expression, {}});
  // The source AST is an owned tree. Each visit is a separate occurrence;
  // even repeated declaration leaves occupy distinct ordered operand slots.
  for (size_t index = 0; index != plan.size(); ++index) {
    Operation *source = plan[index].source;
    // Bound implicit integral conversions apply the surrounding context; they
    // are not explicit source cast occurrences. Exact child operations already
    // carry Slang's propagated type, while declaration leaves retain their
    // declared type and are resized by the operator. Unsized X/Z filling
    // remains unavailable until its context can be represented exactly.
    for (;;) {
      auto children = getChildren(source);
      if (auto signal = dyn_cast<semantic::SVSignalEventControlOp>(source);
          signal && signal.getEdgeKind() == semantic::EdgeKind::Change &&
          !signal.getHasIff() && children.size() == 1)
        source = children.front();
      else if (isa<semantic::SVEventListControlOp>(source) &&
               children.size() == 1)
        source = children.front();
      else if (auto conversion =
                   dyn_cast<semantic::SVConversionExpressionOp>(source);
               conversion && source->getAttrOfType<BoolAttr>("is_implicit") &&
               source->getAttrOfType<BoolAttr>("is_implicit").getValue() &&
               children.size() == 1) {
        auto type = source->getAttrOfType<TypeAttr>("semantic_type");
        auto childType =
            children.front()->getAttrOfType<TypeAttr>("semantic_type");
        if (!type || !childType || !integral(type.getValue()) ||
            !integral(childType.getValue()) ||
            getUnsizedUnknownFillWidth(children.front()))
          return unsupported();
        source = children.front();
      } else
        break;
    }
    plan[index].source = source;
    if (auto endpoint = declarations.resolve(source)) {
      auto type = source->getAttrOfType<TypeAttr>("semantic_type");
      if (index != 0 && (!type || !integral(type.getValue())))
        return unsupported();
      plan[index].endpoint = endpoint;
      plan[index].kind = endpoint->kind;
      if (isa<semantic::SVNamedValueExpressionOp>(source)) {
        auto reference =
            source->getAttrOfType<SymbolRefAttr>("referenced_symbol");
        SymbolTableCollection symbols;
        auto module = source->getParentOfType<ModuleOp>();
        auto *declaration = reference && module
                                ? symbols.lookupSymbolIn(module, reference)
                                : nullptr;
        auto name = declaration ? declaration->getAttrOfType<StringAttr>("name")
                                : StringAttr{};
        // Only an exact unqualified reference to a lexical ancestor's direct
        // declaration supplies a raw identifier. Imported/hierarchical names
        // need their complete source reference syntax in a later extension.
        bool lexical = false;
        for (auto *parent = source->getParentOp(); parent;
             parent = parent->getParentOp())
          lexical |= declaration && declaration->getParentOp() == parent;
        if (lexical && name && !name.empty())
          plan[index].identifier = name;
      }
      continue;
    }
    SmallVector<Operation *> children;
    uint32_t kind = 0, code = 0;
    bool sourceOnly = false;
    if (auto signal = dyn_cast<semantic::SVSignalEventControlOp>(source)) {
      auto syntax = getChildren(source);
      if (syntax.size() != (signal.getHasIff() ? 2u : 1u))
        return unsupported();
      auto edge = signal.getEdgeKind();
      if (edge != semantic::EdgeKind::Change &&
          edge != semantic::EdgeKind::Posedge &&
          edge != semantic::EdgeKind::Negedge)
        return unsupported();
      kind = uint32_t(Kind::Operation);
      sourceOnly = true;
      if (signal.getHasIff() && !plan[index].unqualifiedEvent) {
        code = 68; // vpiIffOp
        // A qualified edge owns a distinct edge operation below the iff.
        // Plan that occurrence from the same typed source, without cloning IR.
        children = {edge == semantic::EdgeKind::Change ? syntax.front()
                                                       : source,
                    syntax.back()};
      } else {
        if (edge == semantic::EdgeKind::Change)
          return unsupported();
        code = edge == semantic::EdgeKind::Posedge ? 39 : 40;
        children.push_back(syntax.front());
      }
    } else if (isa<semantic::SVEventListControlOp>(source)) {
      kind = uint32_t(Kind::Operation);
      code = 35; // vpiEventOrOp
      sourceOnly = true;
      children = getChildren(source);
      if (children.size() < 2)
        return unsupported();
    } else if (isa<semantic::SVIntegerLiteralOp,
                   semantic::SVUnbasedUnsizedIntegerLiteralOp>(source)) {
      auto value = source->getAttrOfType<StringAttr>("constant_value");
      if (!value || value.getValue() == "$")
        return unsupported();
      kind = uint32_t(Kind::Constant);
    } else if (isa<semantic::SVUnaryExpressionOp,
                   semantic::SVBinaryExpressionOp,
                   semantic::SVConditionalExpressionOp>(source)) {
      const reflection::VPISourceOperatorDescriptor *operation = nullptr;
      if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(source)) {
        auto name = semantic::stringifySVUnaryOperator(unary.getOperatorKind());
        operation = reflection::findVPISourceOperator(
            std::string_view(name.data(), name.size()), 1);
      } else if (auto binary =
                     dyn_cast<semantic::SVBinaryExpressionOp>(source)) {
        auto name =
            semantic::stringifySVBinaryOperator(binary.getOperatorKind());
        operation = reflection::findVPISourceOperator(
            std::string_view(name.data(), name.size()), 2);
      } else {
        auto conditional = cast<semantic::SVConditionalExpressionOp>(source);
        auto patterns = conditional.getConditionPatternFlags();
        if (conditional.getConditionCount() != 1 || patterns.size() != 1 ||
            patterns.front())
          return unsupported();
        operation = reflection::findVPISourceOperator("conditional", 3);
      }
      children = getChildren(source);
      if (!operation || children.size() != operation->arity ||
          reflection::integralOperatorRule(operation->value) ==
              reflection::VPIIntegralOperatorRule::Unsupported)
        return unsupported();
      kind = uint32_t(Kind::Operation);
      code = operation->value;
    } else if (isa<semantic::SVConversionExpressionOp>(source)) {
      auto implicit = source->getAttrOfType<BoolAttr>("is_implicit");
      auto constant = source->getAttrOfType<BoolAttr>("is_const_cast");
      children = getChildren(source);
      if (!implicit || implicit.getValue() ||
          (constant && constant.getValue()) || children.size() != 1)
        return unsupported();
      kind = uint32_t(Kind::Operation);
      code = 67; // vpiCastOp: the semantic root is its exact target Typespec.
    } else if (isa<semantic::SVConcatenationExpressionOp>(source)) {
      kind = uint32_t(Kind::Operation);
      code = 33;
      children = getChildren(source);
      if (children.empty())
        return unsupported();
    } else if (isa<semantic::SVReplicationExpressionOp>(source)) {
      kind = uint32_t(Kind::Operation);
      code = 34;
      auto syntax = getChildren(source);
      if (syntax.size() != 2 ||
          !isa<semantic::SVConcatenationExpressionOp>(syntax[1]))
        return unsupported();
      // 37.59 detail 1: multiplier followed by the expressions within the
      // replicated concatenation, without a fabricated nested Concat node.
      children.push_back(syntax[0]);
      auto operands = getChildren(syntax[1]);
      children.append(operands);
      if (children.size() < 2)
        return unsupported();
    } else if (isa<semantic::SVElementSelectExpressionOp>(source)) {
      children = getChildren(source);
      if (children.size() != 2)
        return unsupported();
      auto parent = declarations.resolve(children[0]);
      auto parentType = children[0]->getAttrOfType<TypeAttr>("semantic_type");
      auto layout = parentType
                        ? getSemanticSelectionLayout(parentType.getValue())
                        : std::nullopt;
      if (!parent || !layout || layout->elementWidth != 1 ||
          !(kind = reflection::vpiScalarBitSelectionKind(parent->kind)))
        return unsupported();
    } else if (auto range =
                   dyn_cast<semantic::SVRangeSelectExpressionOp>(source)) {
      children = getChildren(source);
      if (children.size() != 3)
        return unsupported();
      auto parentType = children[0]->getAttrOfType<TypeAttr>("semantic_type");
      auto layout = parentType
                        ? getSemanticSelectionLayout(parentType.getValue())
                        : std::nullopt;
      if (!layout || layout->elementWidth != 1)
        return unsupported();
      if (range.getSelectionKind() == semantic::SVRangeSelectionKind::Simple)
        kind = uint32_t(Kind::PartSelect);
      else {
        kind = uint32_t(Kind::IndexedPartSelect);
        code = range.getSelectionKind() ==
                       semantic::SVRangeSelectionKind::IndexedUp
                   ? 1
                   : 2;
      }
    } else
      return unsupported();
    plan[index].kind = kind;
    plan[index].code = code;
    plan[index].sourceOnly = sourceOnly;
    if (!sourceOnly) {
      auto sourceType = source->getAttrOfType<TypeAttr>("semantic_type");
      if (!sourceType || !integral(sourceType.getValue()))
        return unsupported();
      auto normalized = getNormalizedSemanticType(source);
      auto declared = sim::get<sim::metadata::VpiDeclaredType>(source);
      if (code == 67 && source->hasAttr(vpiTypedefLayersAttrName) && !declared)
        return unsupported();
      FailureOr<sim::VPITypeSemanticsAttr> semantic =
          declared
              ? declared
              : makeVPITypeSemantics(sourceType.getValue(),
                                     getSemanticLocation(source), {}, source);
      if (failed(normalized) || failed(semantic))
        return failure();
      if (!sim::getPackedScalarType(*normalized) ||
          !sim::getPackedWidth(*normalized).value_or(0))
        return unsupported();
      plan[index].type = *normalized;
      plan[index].semantic = *semantic;
      if (kind == uint32_t(Kind::Constant)) {
        auto frozen = freezeSemanticConstant(source);
        if (failed(frozen))
          return failure();
        plan[index].frozen = *frozen;
      }
    }
    for (Operation *child : children) {
      if (plan.size() >= std::numeric_limits<unsigned>::max()) {
        source->emitError(
            "VPI expression occurrence count exceeds the supported limit");
        return failure();
      }
      unsigned childIndex = static_cast<unsigned>(plan.size());
      plan.push_back({child, {}});
      plan.back().unqualifiedEvent = child == source;
      plan[index].children.push_back(childIndex);
    }
  }
  auto legal = [](uint32_t sourceKind, uint32_t selector,
                  sim::VPIRelationMode mode, uint32_t targetKind) {
    const auto *edge = reflection::findVPITraversal(
        sourceKind, selector,
        mode == sim::VPIRelationMode::Handle
            ? reflection::VPITraversalMode::Handle
            : reflection::VPITraversalMode::Iterate);
    return edge && !edge->statementContainment &&
           edge->automaticRelation == reflection::VPIAutomaticRelation::None &&
           reflection::vpiObjectSetContains(edge->targets, targetKind);
  };
  auto childSelector = [](const Planned &node, unsigned child) {
    if (node.kind == uint32_t(Kind::Operation))
      return uint32_t(Rel::OperandRel);
    if (child == 0)
      return uint32_t(Rel::ParentRel);
    if (reflection::isVPIBitSelection(node.kind))
      return uint32_t(Rel::IndexRel);
    bool part = node.kind == uint32_t(Kind::PartSelect);
    return uint32_t(child == 1
                        ? (part ? Rel::LeftRangeRel : Rel::BaseExprRel)
                        : (part ? Rel::RightRangeRel : Rel::WidthExprRel));
  };
  if (!legal(ownerKind, selector, mode, plan.front().kind))
    return unsupported();
  auto shape = [&](const Planned &node)
      -> std::optional<reflection::VPIIntegralExpressionShape> {
    auto type = node.source->getAttrOfType<TypeAttr>("semantic_type");
    if (!type || !integral(type.getValue()))
      return std::nullopt;
    auto normalized = getNormalizedSemanticType(node.source);
    auto semantic = makeVPITypeSemantics(
        type.getValue(), getSemanticLocation(node.source), {}, node.source);
    if (failed(normalized) || failed(semantic))
      return std::nullopt;
    auto width = sim::getPackedWidth(*normalized);
    if (!width)
      return std::nullopt;
    return reflection::VPIIntegralExpressionShape{
        *width, semantic->getIsSigned(), semantic->getIsFourState(), node.code};
  };
  // Prove immutable arithmetic from the exact source tree. These temporary
  // values are validation state only; the database retains the original
  // operations and contextual parameter references, never another value copy.
  SmallVector<std::optional<sim::VPIConstantValue>> constants(plan.size());
  SmallVector<std::optional<reflection::VPIIntegralExpressionShape>> shapes;
  for (const auto &node : plan)
    shapes.push_back(shape(node));
  for (size_t index = plan.size(); index-- != 0;) {
    const auto &node = plan[index];
    if (!shapes[index])
      continue;
    auto frozen = node.frozen;
    if (node.endpoint && (node.kind == uint32_t(Kind::Parameter) ||
                          node.kind == uint32_t(Kind::SpecParam) ||
                          node.kind == uint32_t(Kind::EnumConst))) {
      auto reference =
          node.source->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      SymbolTableCollection symbols;
      auto module = node.source->getParentOfType<ModuleOp>();
      auto *declaration = reference && module
                              ? symbols.lookupSymbolIn(module, reference)
                              : nullptr;
      if (declaration && declaration->hasAttr("constant_value")) {
        auto value = freezeSemanticConstant(declaration);
        if (failed(value))
          return failure();
        frozen = *value;
      }
    }
    if (frozen) {
      auto planes = dyn_cast<ArrayAttr>(frozen.getValue());
      if (!planes || planes.size() != 2)
        continue;
      auto value = dyn_cast<IntegerAttr>(planes[0]);
      auto unknown = dyn_cast<IntegerAttr>(planes[1]);
      if (value && unknown &&
          value.getValue().getBitWidth() == shapes[index]->width &&
          unknown.getValue().getBitWidth() == shapes[index]->width)
        constants[index] =
            sim::VPIConstantValue{value.getValue(), unknown.getValue()};
      continue;
    }
    if (node.kind != uint32_t(Kind::Operation) || node.sourceOnly)
      continue;
    SmallVector<sim::VPIConstantValue, 3> values;
    SmallVector<reflection::VPIIntegralExpressionShape, 3> operands;
    for (unsigned child : node.children) {
      if (!constants[child] || !shapes[child])
        break;
      values.push_back(*constants[child]);
      operands.push_back(*shapes[child]);
    }
    if (values.size() == node.children.size())
      constants[index] = sim::evaluateVPIIntegralConstant(
          node.code, *shapes[index], values, operands);
  }
  for (size_t nodeIndex = 0; nodeIndex != plan.size(); ++nodeIndex) {
    const auto &node = plan[nodeIndex];
    auto knownBound = [&](unsigned position) -> std::optional<int64_t> {
      if (position >= node.children.size())
        return std::nullopt;
      unsigned child = node.children[position];
      return constants[child] && shapes[child]
                 ? sim::knownVPIConstantInteger(*constants[child],
                                                shapes[child]->isSigned)
                 : std::nullopt;
    };
    if ((node.kind == uint32_t(Kind::PartSelect) &&
         (!knownBound(1) || !knownBound(2))) ||
        (node.kind == uint32_t(Kind::IndexedPartSelect) &&
         (!knownBound(2) || *knownBound(2) <= 0)) ||
        (node.kind == uint32_t(Kind::Operation) && node.code == 34 &&
         (!knownBound(0) || *knownBound(0) <= 0)))
      return unsupported();
    if (!node.sourceOnly && node.kind == uint32_t(Kind::Operation) &&
        reflection::integralOperatorRule(node.code) !=
            reflection::VPIIntegralOperatorRule::Unsupported) {
      auto resultShape = shapes[nodeIndex];
      SmallVector<reflection::VPIIntegralExpressionShape, 3> operands;
      for (unsigned child : node.children) {
        auto childShape = shapes[child];
        if (!childShape)
          return unsupported();
        operands.push_back(*childShape);
      }
      if (!resultShape ||
          !reflection::validVPIIntegralOperator(
              node.code, *resultShape, operands.data(), operands.size()))
        return unsupported();
    }
    auto childMode = node.kind == uint32_t(Kind::Operation)
                         ? sim::VPIRelationMode::Iterate
                         : sim::VPIRelationMode::Handle;
    for (auto [index, child] : llvm::enumerate(node.children))
      if (!legal(node.kind, childSelector(node, index), childMode,
                 plan[child].kind))
        return unsupported();
  }
  if (!materialize)
    return std::optional<VPIExpressionEndpoint>(
        VPIExpressionEndpoint{{}, plan.front().kind});
  uint64_t computed =
      llvm::count_if(plan, [](const Planned &node) { return !node.endpoint; });
  if (nextID > uint64_t(INT64_MAX) || computed > uint64_t(INT64_MAX) - nextID)
    return expression->emitError("VPI source expression ID space exhausted"),
           failure();
  // Assign all exact identities before materialization. No partial declarations
  // exist when unsupported syntax or a generated relation check rejects a plan.
  SmallVector<uint64_t> ids(plan.size());
  for (auto [index, node] : llvm::enumerate(plan)) {
    if (node.endpoint)
      continue;
    ids[index] = nextID++;
    std::string name =
        "__obelisk_vpi_rtl_expression_" + std::to_string(ids[index]);
    while (SymbolTable::lookupSymbolIn(design, name)) {
      if (nextID > uint64_t(INT64_MAX))
        return expression->emitError(
                   "VPI source expression ID space exhausted"),
               failure();
      ids[index] = nextID++;
      name = "__obelisk_vpi_rtl_expression_" + std::to_string(ids[index]);
    }
    node.endpoint = VPIExpressionEndpoint{
        sim::VPIObjectRefAttr::get(
            builder.getContext(),
            FlatSymbolRefAttr::get(builder.getContext(), name)),
        node.kind};
  }
  struct Incoming {
    sim::VPIObjectRefAttr owner;
    uint32_t selector;
    sim::VPIRelationMode mode;
    uint64_t ordinal;
  };
  SmallVector<Incoming> incoming(plan.size());
  incoming[0] = {owner, selector, mode, ordinal};
  for (const auto &node : plan)
    for (auto [index, child] : llvm::enumerate(node.children)) {
      bool operand = node.kind == uint32_t(Kind::Operation);
      incoming[child] = {node.endpoint->reference, childSelector(node, index),
                         operand ? sim::VPIRelationMode::Iterate
                                 : sim::VPIRelationMode::Handle,
                         operand ? index : 0};
    }
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&design.getBody().front());
  for (auto [index, node] : llvm::enumerate(plan)) {
    if (!node.type && !node.sourceOnly)
      continue;
    auto slot = incoming[index];
    sim::SimVPIExpressionDeclOp::create(
        builder, getSemanticLocation(node.source),
        node.endpoint->reference.getSymbol().getValue(), ids[index], node.kind,
        builder.getI64IntegerAttr(scope), slot.owner, slot.selector, slot.mode,
        slot.ordinal, node.type ? TypeAttr::get(node.type) : TypeAttr{},
        node.semantic, node.frozen,
        node.kind == uint32_t(Kind::Operation)
            ? builder.getI32IntegerAttr(node.code)
            : IntegerAttr{},
        node.kind == uint32_t(Kind::IndexedPartSelect)
            ? builder.getI32IntegerAttr(node.code)
            : IntegerAttr{},
        node.sourceOnly, FlatSymbolRefAttr{});
    for (unsigned child : node.children) {
      auto edge = incoming[child];
      auto relation = sim::SimVPIRelationDeclOp::create(
          builder, getSemanticLocation(plan[child].source), edge.owner,
          edge.selector, edge.mode, edge.ordinal,
          plan[child].endpoint->reference);
      if (plan[child].identifier)
        sim::set<sim::metadata::VpiExpressionIdentifier>(
            relation, plan[child].identifier);
    }
  }
  return plan.front().endpoint;
}

LogicalResult materializeProceduralExpressionRelations(
    sim::SimDesignOp design, const PreparedUnits &units,
    const llvm::StringMap<DescriptorInfo> &descriptors, OpBuilder &builder) {
  using Relation = reflection::VPIRelationKind;
  using Mode = reflection::VPITraversalMode;
  llvm::DenseMap<uint64_t, sim::SimStatementDeclOp> statements;
  for (auto statement : design.getOps<sim::SimStatementDeclOp>())
    statements[statement.getId()] = statement;
  llvm::DenseMap<uint64_t, Operation *> unitSources;
  for (const PreparedUnit &unit : units.units)
    unitSources[unit.id] = unit.source;
  llvm::StringMap<sim::SimVPICallDeclOp> calls;
  for (auto call : design.getOps<sim::SimVPICallDeclOp>())
    calls[call.getSymName()] = call;
  if (statements.empty() && calls.empty())
    return success();
  llvm::DenseMap<std::pair<uint64_t, uint64_t>, uint64_t> caseItems;
  for (auto relation : design.getOps<sim::SimVPIStatementRelationDeclOp>())
    if (relation.getSourceKind() == sim::VPIStatementSourceKind::Statement &&
        relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIObjectKind::CaseItem))
      caseItems[{relation.getSourceId(), relation.getOrdinal()}] =
          relation.getTargetStatementId();
  VPIExpressionMaterializer materializer(design, descriptors, builder);
  bool invalid = false;
  llvm::DenseMap<Attribute, SmallVector<Attribute>> uses;
  for (auto relation : design.getOps<sim::SimVPIRelationDeclOp>())
    if (relation.getSelector() == static_cast<uint32_t>(Relation::UseRel) &&
        relation.getMode() == sim::VPIRelationMode::Iterate)
      uses[relation.getSource()].push_back(relation.getTarget());
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&design.getBody().front());
  llvm::DenseSet<uint64_t> visited;
  for (const PreparedUnit &unit : units.units) {
    if (!isa<semantic::SVProceduralBlockSymbolOp,
             semantic::SVSubroutineSymbolOp>(unit.source))
      continue;
    unit.source->walk<WalkOrder::PreOrder>([&](Operation *source) {
      if (source != unit.source && isa<semantic::SVProceduralBlockSymbolOp,
                                       semantic::SVSubroutineSymbolOp>(source))
        return WalkResult::skip();
      if (auto call = dyn_cast<semantic::SVCallExpressionOp>(source)) {
        auto site = sim::get<sim::metadata::VpiCallSite>(call);
        auto declaration =
            site ? calls.lookup(site.getValue()) : sim::SimVPICallDeclOp{};
        using Kind = reflection::VPIObjectKind;
        auto defaulted = call.getDefaultedArguments();
        auto arguments = getChildren(call);
        bool supported =
            declaration &&
            (declaration.getVpiKind() == uint32_t(Kind::FuncCall) ||
             declaration.getVpiKind() == uint32_t(Kind::TaskCall)) &&
            declaration.getCalleeCodeUnitId() && !call.getHasThisClass() &&
            !call.getHasOutputArguments() && defaulted &&
            defaulted->size() == arguments.size() &&
            call.getArgumentCount() == arguments.size() &&
            llvm::all_of(*defaulted, [](int64_t flag) { return flag == 0; });
        Operation *callee =
            supported ? unitSources.lookup(*declaration.getCalleeCodeUnitId())
                      : nullptr;
        unsigned formalCount = 0;
        if (callee)
          for (Operation *child : getChildren(callee))
            if (auto formal =
                    dyn_cast<semantic::SVFormalArgumentSymbolOp>(child)) {
              ++formalCount;
              supported &=
                  formal.getDirection() == semantic::SVArgumentDirection::In;
            }
        supported &= callee && formalCount == arguments.size();
        if (supported) {
          auto owner = sim::VPIObjectRefAttr::get(builder.getContext(), site);
          auto group = materializer.resolveGroup(
              arguments, owner, declaration.getVpiKind(),
              declaration.getScopeId(), uint32_t(Relation::ArgumentRel));
          if (failed(group))
            invalid = true;
          else if (*group)
            for (auto [ordinal, target] : llvm::enumerate(**group)) {
              auto location = getSemanticLocation(arguments[ordinal]);
              sim::SimVPIRelationDeclOp::create(
                  builder, location, owner, uint32_t(Relation::ArgumentRel),
                  sim::VPIRelationMode::Iterate, ordinal, target.reference);
              const auto *edge = reflection::findVPITraversal(
                  target.kind, uint32_t(Relation::UseRel), Mode::Iterate);
              if (!edge || !reflection::vpiObjectSetContains(
                               edge->targets, declaration.getVpiKind()))
                continue;
              auto &targets = uses[target.reference];
              if (llvm::is_contained(targets, owner))
                continue;
              sim::SimVPIRelationDeclOp::create(
                  builder, location, target.reference,
                  uint32_t(Relation::UseRel), sim::VPIRelationMode::Iterate,
                  targets.size(), owner);
              targets.push_back(owner);
            }
        }
      }
      auto identity = sim::get<sim::metadata::VpiStatementId>(source);
      if (!identity)
        return WalkResult::advance();
      uint64_t id = identity.getValue().getZExtValue();
      auto statement = statements.lookup(id);
      if (!statement || !visited.insert(id).second)
        return WalkResult::advance();
      auto sourceRef = sim::VPIObjectRefAttr::get(
          builder.getContext(), sim::VPIObjectRefKind::Statement, identity);
      auto emit = [&](Relation selector, Operation *expression) {
        auto resolved = materializer.resolve(
            expression, sourceRef, statement.getVpiKind(),
            statement.getScopeId(), static_cast<uint32_t>(selector));
        if (failed(resolved)) {
          invalid = true;
          return;
        }
        auto target = *resolved;
        const auto *edge = reflection::findVPITraversal(
            statement.getVpiKind(), static_cast<uint32_t>(selector),
            Mode::Handle);
        if (!target || !edge || edge->statementContainment ||
            edge->automaticRelation != reflection::VPIAutomaticRelation::None ||
            !reflection::vpiObjectSetContains(edge->targets, target->kind))
          return;
        sim::SimVPIRelationDeclOp::create(
            builder, getSemanticLocation(expression), sourceRef,
            static_cast<uint32_t>(selector), sim::VPIRelationMode::Handle, 0,
            target->reference);
        const auto *use = reflection::findVPITraversal(
            target->kind, static_cast<uint32_t>(Relation::UseRel),
            Mode::Iterate);
        if (!use || !reflection::vpiObjectSetContains(use->targets,
                                                      statement.getVpiKind()))
          return;
        auto &targets = uses[target->reference];
        if (llvm::is_contained(targets, sourceRef))
          return;
        sim::SimVPIRelationDeclOp::create(
            builder, getSemanticLocation(expression), target->reference,
            static_cast<uint32_t>(Relation::UseRel),
            sim::VPIRelationMode::Iterate, targets.size(), sourceRef);
        targets.push_back(sourceRef);
      };
      auto children = getChildren(source);
      if (auto caseStatement = dyn_cast<semantic::SVCaseStatementOp>(source)) {
        size_t next = 1;
        for (auto [itemOrdinal, count] :
             llvm::enumerate(caseStatement.getItemLabelCounts())) {
          auto found = caseItems.find({id, itemOrdinal});
          if (found == caseItems.end() || count < 0 ||
              next >= children.size() ||
              static_cast<uint64_t>(count) >= children.size() - next) {
            invalid = true;
            source->emitError(
                "case expression group has no exact retained item");
            return WalkResult::advance();
          }
          auto item = statements.lookup(found->second);
          auto owner = sim::VPIObjectRefAttr::get(
              builder.getContext(), sim::VPIObjectRefKind::Statement,
              builder.getI64IntegerAttr(found->second));
          auto labels = ArrayRef(children).slice(next, count);
          auto group = materializer.resolveGroup(
              labels, owner, item.getVpiKind(), item.getScopeId(),
              static_cast<uint32_t>(Relation::ExprRel));
          if (failed(group))
            invalid = true;
          else if (*group)
            for (auto [ordinal, target] : llvm::enumerate(**group)) {
              auto location = getSemanticLocation(labels[ordinal]);
              sim::SimVPIRelationDeclOp::create(
                  builder, location, owner,
                  static_cast<uint32_t>(Relation::ExprRel),
                  sim::VPIRelationMode::Iterate, ordinal, target.reference);
              const auto *use = reflection::findVPITraversal(
                  target.kind, static_cast<uint32_t>(Relation::UseRel),
                  Mode::Iterate);
              if (!use || !reflection::vpiObjectSetContains(
                              use->targets, statement.getVpiKind()))
                continue;
              auto &targets = uses[target.reference];
              if (llvm::is_contained(targets, sourceRef))
                continue;
              sim::SimVPIRelationDeclOp::create(
                  builder, location, target.reference,
                  static_cast<uint32_t>(Relation::UseRel),
                  sim::VPIRelationMode::Iterate, targets.size(), sourceRef);
              targets.push_back(sourceRef);
            }
          next += count + 1;
        }
      }
      if (statement.getVpiKind() ==
          static_cast<uint32_t>(reflection::VPIObjectKind::EventControl)) {
        Operation *control = source;
        if (isa<semantic::SVTimedStatementOp>(source) && children.size() == 2)
          control = children.front();
        emit(Relation::ConditionRel, control);
        return WalkResult::advance();
      }
      if (isa<semantic::SVExpressionStatementOp>(source) &&
          children.size() == 1 &&
          isa<semantic::SVAssignmentExpressionOp>(children.front())) {
        source = children.front();
        children = getChildren(source);
      }
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(source)) {
        size_t prefix = assignment.getHasTimingControl() ? 1 : 0;
        if (children.size() == prefix + 2) {
          emit(Relation::LhsRel, children[prefix]);
          emit(Relation::RhsRel, children[prefix + 1]);
        }
      } else if (auto conditional =
                     dyn_cast<semantic::SVConditionalStatementOp>(source)) {
        if (conditional.getConditionCount() == 1 &&
            conditional.getConditionPatternFlags().front() == 0)
          emit(Relation::ConditionRel, children.front());
      } else if (isa<semantic::SVCaseStatementOp,
                     semantic::SVPatternCaseStatementOp,
                     semantic::SVWhileLoopStatementOp,
                     semantic::SVRepeatLoopStatementOp,
                     semantic::SVWaitStatementOp,
                     semantic::SVReturnStatementOp>(source)) {
        if (!children.empty())
          emit(Relation::ConditionRel, children.front());
      } else if (isa<semantic::SVDoWhileLoopStatementOp>(source)) {
        if (children.size() == 2)
          emit(Relation::ConditionRel, children.front());
      } else if (auto loop = dyn_cast<semantic::SVForLoopStatementOp>(source)) {
        if (loop.getHasCondition())
          emit(Relation::ConditionRel, children[loop.getInitializerCount()]);
      }
      return WalkResult::advance();
    });
  }
  return failure(invalid);
}
} // namespace obelisk::simlowering
