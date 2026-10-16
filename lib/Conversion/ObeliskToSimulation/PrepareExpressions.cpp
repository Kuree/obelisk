//===- PrepareExpressions.cpp - Immutable source expression relations
//------===//

#include "PrepareExpressions.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
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
    if (!net || net.getHierarchicalName() != path)
      return std::nullopt;
    return VPIExpressionEndpoint{
        sim::VPIObjectRefAttr::get(expression->getContext(),
                                   sim::VPIObjectRefKind::Net,
                                   builder.getI64IntegerAttr(descriptor.id)),
        sim::vpiKindForNet(net.getVpiTypeAttr())};
  }
  if (descriptor.kind == DescriptorInfo::Kind::Storage) {
    auto storage = storages.lookup(descriptor.id);
    if (!storage || storage.getHierarchicalName() != path)
      return std::nullopt;
    return VPIExpressionEndpoint{
        sim::VPIObjectRefAttr::get(expression->getContext(),
                                   sim::VPIObjectRefKind::Storage,
                                   builder.getI64IntegerAttr(descriptor.id)),
        sim::vpiKindForStorage(storage.getVpiTypeAttr())};
  }
  return std::nullopt;
}

LogicalResult materializeProceduralExpressionRelations(
    sim::SimDesignOp design, const PreparedUnits &units,
    const llvm::StringMap<DescriptorInfo> &descriptors, OpBuilder &builder) {
  using Relation = reflection::VPIRelationKind;
  using Mode = reflection::VPITraversalMode;
  llvm::DenseMap<uint64_t, sim::SimStatementDeclOp> statements;
  for (auto statement : design.getOps<sim::SimStatementDeclOp>())
    statements[statement.getId()] = statement;
  if (statements.empty())
    return success();
  VPIExpressionResolver resolver(design, descriptors);
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
        auto target = resolver.resolve(expression);
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
  return success();
}
} // namespace obelisk::simlowering
