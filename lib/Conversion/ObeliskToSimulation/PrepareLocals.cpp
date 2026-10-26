//===- PrepareLocals.cpp - Automatic declaration inventory
//-----------------===//

#include "PrepareLocals.h"
#include "Detail.h"
#include "PrepareExpressions.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Dialect/ForeachLoopMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace obelisk::simlowering {
LogicalResult
materializeProceduralLocals(sim::SimDesignOp design, const PreparedUnits &units,
                            const PreparedCaptures &captures,
                            llvm::StringMap<DescriptorInfo> &descriptors,
                            OpBuilder &builder) {
  llvm::DenseMap<uint64_t, sim::SimStatementDeclOp> statements;
  for (auto statement : design.getOps<sim::SimStatementDeclOp>())
    statements[statement.getId()] = statement;
  llvm::DenseMap<uint64_t, sim::SimStorageDeclOp> storages;
  for (auto storage : design.getOps<sim::SimStorageDeclOp>())
    storages[storage.getId()] = storage;
  SymbolTableCollection sourceSymbols;
  auto module = design->getParentOfType<ModuleOp>();
  uint64_t nextID = 1;
  for (const PreparedUnit &unit : units.units) {
    auto owner = units.declarations.lookup(unit.source);
    if (!owner || owner.getId() != unit.id || owner.getInternal())
      continue;
    // Only original source procedural activations own declarations. Observer,
    // initialization and outlined execution helpers never invent inventories.
    bool subroutine = isa<semantic::SVSubroutineSymbolOp>(unit.source);
    if (!subroutine && !isa<semantic::SVProceduralBlockSymbolOp>(unit.source))
      continue;
    struct Candidate {
      Operation *source;
      IntegerAttr scope;
    };
    SmallVector<Candidate> candidates;
    llvm::DenseSet<Operation *> seen;
    auto add = [&](Operation *source, IntegerAttr scope) {
      if (source && sim::get<sim::metadata::VpiDeclaredType>(source) &&
          seen.insert(source).second)
        candidates.push_back({source, scope});
    };
    if (subroutine) {
      for (Operation *child : getChildren(unit.source))
        if (isa<semantic::SVFormalArgumentSymbolOp>(child))
          add(child, {});
      auto routine = cast<semantic::SVSubroutineSymbolOp>(unit.source);
      if (auto reference = routine.getReturnVariableSymbolAttr()) {
        auto variable = dyn_cast_or_null<semantic::SVVariableSymbolOp>(
            sourceSymbols.lookupSymbolIn(module, reference));
        if (variable && variable->getParentOp() == routine)
          add(variable, {});
      } else if (auto path = routine.getReturnVariablePath()) {
        for (Operation *child : getChildren(unit.source))
          if (isa<semantic::SVVariableSymbolOp>(child) &&
              getHierarchyName(child) == *path)
            add(child, {});
        auto found = captures.locals.find(unit.source);
        if (found != captures.locals.end())
          for (const PreparedLocal &local : found->second)
            if (local.path == *path)
              add(local.source, {});
      }
    }
    unit.source->walk<WalkOrder::PreOrder>([&](Operation *operation) {
      // Nested executable declarations own separate activation inventories.
      if (operation != unit.source &&
          isa<semantic::SVSubroutineSymbolOp,
              semantic::SVProceduralBlockSymbolOp>(operation))
        return WalkResult::skip();
      auto declaration =
          dyn_cast<semantic::SVVariableDeclStatementOp>(operation);
      if (!declaration)
        return WalkResult::advance();
      auto reference = declaration.getReferencedSymbolAttr();
      Operation *source =
          reference ? sourceSymbols.lookupSymbolIn(module, reference) : nullptr;
      if (!source)
        return WalkResult::advance();
      IntegerAttr lexicalScope;
      for (Operation *parent = declaration->getParentOp();
           parent && parent != unit.source; parent = parent->getParentOp()) {
        auto id = sim::get<sim::metadata::VpiStatementId>(parent);
        if (!id)
          continue;
        auto statement = statements.lookup(id.getValue().getZExtValue());
        if (statement && statement.getIsScope() &&
            statement.getCodeUnitId() == unit.id) {
          lexicalScope = id;
          break;
        }
      }
      // A process has no variable-bearing VPI lexical object of its own. Its
      // declarations require a retained begin/fork scope from source syntax.
      if (lexicalScope || subroutine)
        add(source, lexicalScope);
      return WalkResult::advance();
    });
    llvm::DenseMap<uint64_t, uint64_t> ordinals, sourceOrdinals;
    auto materialize = [&](Candidate candidate) -> LogicalResult {
      auto semantic =
          sim::get<sim::metadata::VpiDeclaredType>(candidate.source);
      bool complete = true;
      AttrTypeWalker typeReferences;
      typeReferences.addWalk([&](sim::VPITypeSemanticsAttr current) {
        if (auto aliases = current.getTypedefAliases())
          for (Attribute alias : aliases)
            complete &=
                bool(sourceSymbols
                         .lookupNearestSymbolFrom<sim::SimVPITypespecDeclOp>(
                             design, cast<SymbolRefAttr>(alias)));
        if (current.getKind() == sim::VPITypeKind::Enum &&
            !current.getTypedefAliases()) {
          auto identity = candidate.source->getAttrOfType<IntegerAttr>(
              vpiSourceTypeIdentityAttrName);
          bool found = false;
          if (identity)
            for (auto declaration : design.getOps<sim::SimVPITypespecDeclOp>())
              found |= declaration.getSourceTypeIdentityAttr() == identity;
          complete &= found;
        }
      });
      typeReferences.walk(semantic);
      if (!complete)
        return success();
      auto type = getNormalizedSemanticType(candidate.source);
      if (failed(type))
        return failure();
      uint64_t scope =
          candidate.scope ? candidate.scope.getValue().getZExtValue() : 0;
      auto persistent = descriptors.find(getHierarchyName(candidate.source));
      if (persistent != descriptors.end()) {
        auto variable =
            dyn_cast<semantic::SVVariableSymbolOp>(candidate.source);
        bool staticSource =
            isStaticFormal(candidate.source) ||
            (variable &&
             variable.getLifetime() == semantic::SVVariableLifetime::Static);
        if (auto routine =
                dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
            variable && variable.getIsCompilerGenerated() && routine &&
            routine.getDefaultLifetime() ==
                semantic::SVVariableLifetime::Static)
          staticSource |= routine.getReturnVariablePath() ==
                          getHierarchyName(candidate.source);
        if (!staticSource)
          return success();
        // Existing static storage is the declaration identity. Events and
        // delegated inventories retain their own canonical source bindings.
        if (!isContextResolvableStorage(persistent->second))
          return success();
        auto storage = storages.lookup(persistent->second.id);
        if (!storage ||
            sim::has<sim::metadata::VpiIdentityDelegated>(storage) ||
            !reflection::vpiObjectSetContains(
                reflection::VPIObjectSetID::DataVariableObjects,
                sim::vpiKindForStorage(semantic)))
          return success();
        storage.setVpiTypeAttr(semantic);
        persistent->second.vpiType = semantic;
        if (auto identity = candidate.source->getAttrOfType<IntegerAttr>(
                vpiSourceTypeIdentityAttrName))
          sim::set<sim::metadata::VpiSourceTypeIdentity>(storage, identity);
        sim::SimVPIStaticLocalBindingOp::create(
            builder, getSemanticLocation(candidate.source), storage.getIdAttr(),
            owner.getIdAttr(), candidate.scope,
            builder.getI32IntegerAttr(sourceOrdinals[scope]++));
        return success();
      }
      const uint32_t kind = sim::vpiKindForClassMember(semantic);
      const auto *edge = reflection::findVPITraversal(
          static_cast<uint32_t>(reflection::VPIObjectKind::Frame),
          static_cast<uint32_t>(reflection::VPIRelationKind::AutomaticsRel),
          reflection::VPITraversalMode::Iterate);
      if (!edge || !reflection::vpiObjectSetContains(edge->targets, kind))
        return success();
      std::string symbol = "__obelisk_vpi_local_" + std::to_string(nextID);
      auto declaration = sim::SimVPILocalDeclOp::create(
          builder, getSemanticLocation(candidate.source),
          builder.getStringAttr(symbol), builder.getI64IntegerAttr(nextID++),
          owner.getIdAttr(), owner.getScopeIdAttr(), candidate.scope,
          builder.getI32IntegerAttr(ordinals[scope]++),
          builder.getStringAttr(getHierarchyName(candidate.source)),
          builder.getStringAttr(getDebugName(candidate.source)),
          TypeAttr::get(*type), semantic,
          builder.getI32IntegerAttr(sourceOrdinals[scope]++));
      sim::set<sim::metadata::VpiLocal>(
          candidate.source,
          FlatSymbolRefAttr::get(declaration.getSymNameAttr()));
      if (auto identity = candidate.source->getAttrOfType<IntegerAttr>(
              vpiSourceTypeIdentityAttrName))
        sim::set<sim::metadata::VpiSourceTypeIdentity>(declaration, identity);
      return success();
    };
    for (Candidate candidate : candidates)
      if (failed(materialize(candidate)))
        return failure();

    // The implicit return variable already has a declaration identity. Retain
    // its exact source-symbol relationship, independently of execution or IPO.
    if (auto routine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        routine &&
        routine.getSubroutineKind() == semantic::SVSubroutineKind::Function) {
      auto signature =
          routine.getSemanticType()
              ? dyn_cast<semantic::SubroutineType>(*routine.getSemanticType())
              : semantic::SubroutineType{};
      auto functionType = signature
                              ? dyn_cast<FunctionType>(signature.getSignature())
                              : FunctionType{};
      bool isVoid = functionType && functionType.getNumResults() == 1 &&
                    isa<semantic::VoidType>(functionType.getResult(0));
      if (isVoid) {
        SmallVector<Attribute> properties;
        if (auto existing =
                owner->getAttrOfType<sim::VPIPropertySetAttr>("vpi_properties"))
          llvm::append_range(properties, existing.getProperties());
        properties.push_back(sim::VPIPropertyAttr::get(
            builder.getContext(), builder.getI32IntegerAttr(4),
            builder.getI32IntegerAttr(0)));
        llvm::sort(properties, [](Attribute left, Attribute right) {
          return cast<sim::VPIPropertyAttr>(left).getSelector().getInt() <
                 cast<sim::VPIPropertyAttr>(right).getSelector().getInt();
        });
        owner->setAttr("vpi_properties", sim::VPIPropertySetAttr::get(
                                             builder.getContext(),
                                             builder.getArrayAttr(properties)));
      } else if (auto reference = routine.getReturnVariableSymbolAttr()) {
        auto variable = dyn_cast_or_null<semantic::SVVariableSymbolOp>(
            sourceSymbols.lookupSymbolIn(module, reference));
        auto anchor = sim::get<sim::metadata::VpiAnchor>(routine);
        sim::VPIObjectRefAttr target;
        if (variable && variable->getParentOp() == routine && anchor) {
          if (auto local = sim::get<sim::metadata::VpiLocal>(variable))
            target = sim::VPIObjectRefAttr::get(
                builder.getContext(), sim::VPIObjectRefKind::Symbol, {}, local);
          else if (auto found = descriptors.find(getHierarchyName(variable));
                   found != descriptors.end() &&
                   isContextResolvableStorage(found->second))
            for (auto binding :
                 design.getOps<sim::SimVPIStaticLocalBindingOp>())
              if (binding.getStorageId() == found->second.id &&
                  binding.getCodeUnitId() == owner.getId() &&
                  !binding.getStatementScopeIdAttr()) {
                target = sim::VPIObjectRefAttr::get(
                    builder.getContext(), sim::VPIObjectRefKind::Storage,
                    builder.getI64IntegerAttr(found->second.id));
                break;
              }
        }
        if (target)
          sim::SimVPIRelationDeclOp::create(
              builder, getSemanticLocation(routine),
              sim::VPIObjectRefAttr::get(builder.getContext(),
                                         sim::VPIObjectRefKind::Symbol, {},
                                         anchor),
              uint32_t(reflection::VPIRelationKind::ReturnRel),
              sim::VPIRelationMode::Handle, 0, target);
      }
    }

    // Foreach iterators are implicit symbols owned by the loop's scope, not
    // ordinary declaration statements or members of the indexed collection.
    VPIExpressionResolver resolver(design, descriptors);
    bool invalid = false;
    unit.source->walk<WalkOrder::PreOrder>([&](Operation *operation) {
      if (operation != unit.source &&
          isa<semantic::SVSubroutineSymbolOp,
              semantic::SVProceduralBlockSymbolOp>(operation))
        return WalkResult::skip();
      auto loop = dyn_cast<semantic::SVForeachLoopStatementOp>(operation);
      if (!loop)
        return WalkResult::advance();
      auto id = sim::get<sim::metadata::VpiStatementId>(loop);
      auto statement = id ? statements.lookup(id.getValue().getZExtValue())
                          : sim::SimStatementDeclOp{};
      auto children = getChildren(loop);
      if (!statement || statement.getCodeUnitId() != unit.id ||
          children.size() != 2 || loop.getLoopDimensions().empty())
        return WalkResult::advance();
      auto collection = resolver.resolve(children[0]);
      if (!collection ||
          (collection->kind !=
               static_cast<uint32_t>(reflection::VPIObjectKind::RegArray) &&
           collection->kind != static_cast<uint32_t>(
                                   reflection::VPIObjectKind::PackedArrayVar) &&
           collection->kind !=
               static_cast<uint32_t>(reflection::VPIObjectKind::BitVar) &&
           collection->kind !=
               static_cast<uint32_t>(reflection::VPIObjectKind::Reg)) ||
          (collection->reference.getKind() != sim::VPIObjectRefKind::Storage &&
           collection->reference.getKind() != sim::VPIObjectRefKind::Symbol))
        return WalkResult::advance();
      SmallVector<Operation *> indices;
      for (Attribute attribute : loop.getLoopDimensions()) {
        auto dimension = cast<DictionaryAttr>(attribute);
        if (!dimension.getAs<BoolAttr>(foreach_metadata::hasStaticRange)
                 .getValue())
          return WalkResult::advance();
        if (!dimension.getAs<BoolAttr>(foreach_metadata::hasIterator)
                 .getValue()) {
          indices.push_back(nullptr);
          continue;
        }
        auto source = sourceSymbols.lookupSymbolIn(
            module,
            dimension.getAs<SymbolRefAttr>(foreach_metadata::iteratorSymbol));
        auto semantic = source
                            ? sim::get<sim::metadata::VpiDeclaredType>(source)
                            : sim::VPITypeSemanticsAttr{};
        if (!isa_and_present<semantic::SVIteratorSymbolOp>(source) ||
            !semantic || semantic.getKind() != sim::VPITypeKind::Int)
          return WalkResult::advance();
        indices.push_back(source);
      }
      SmallVector<Attribute> slots;
      for (Operation *source : indices) {
        if (!source) {
          slots.push_back(builder.getUnitAttr());
          continue;
        }
        if (failed(materialize({source, id}))) {
          invalid = true;
          return WalkResult::interrupt();
        }
        auto declaration = sim::get<sim::metadata::VpiLocal>(source);
        if (!declaration) {
          source->emitError("retained foreach iterator did not materialize");
          invalid = true;
          return WalkResult::interrupt();
        }
        slots.push_back(declaration);
      }
      SmallVector<Attribute> dimensions(loop.getLoopDimensions().begin(),
                                        loop.getLoopDimensions().end());
      for (auto item : llvm::enumerate(slots)) {
        if (auto local = dyn_cast<FlatSymbolRefAttr>(item.value())) {
          NamedAttrList dimension(
              cast<DictionaryAttr>(dimensions[item.index()]));
          dimension.set(sim::metadata::VpiLocal::getNameStr(), local);
          dimensions[item.index()] =
              DictionaryAttr::get(loop.getContext(), dimension.getAttrs());
        }
      }
      loop->setAttr("loop_dimensions", builder.getArrayAttr(dimensions));
      sim::SimVPIForeachDeclOp::create(builder, getSemanticLocation(loop), id,
                                       collection->reference,
                                       builder.getArrayAttr(slots));
      return WalkResult::advance();
    });
    if (invalid)
      return failure();
  }
  return success();
}
} // namespace obelisk::simlowering
