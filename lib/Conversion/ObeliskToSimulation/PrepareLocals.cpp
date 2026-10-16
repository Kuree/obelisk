//===- PrepareLocals.cpp - Automatic declaration inventory
//-----------------===//

#include "PrepareLocals.h"
#include "Detail.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
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
      if (auto path = routine.getReturnVariablePath()) {
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
    for (Candidate candidate : candidates) {
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
        continue;
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
          continue;
        // Existing static storage is the declaration identity. Events and
        // delegated inventories retain their own canonical source bindings.
        if (!isContextResolvableStorage(persistent->second))
          continue;
        auto storage = storages.lookup(persistent->second.id);
        if (!storage ||
            sim::has<sim::metadata::VpiIdentityDelegated>(storage) ||
            !reflection::vpiObjectSetContains(
                reflection::VPIObjectSetID::DataVariableObjects,
                sim::vpiKindForStorage(semantic)))
          continue;
        storage.setVpiTypeAttr(semantic);
        persistent->second.vpiType = semantic;
        if (auto identity = candidate.source->getAttrOfType<IntegerAttr>(
                vpiSourceTypeIdentityAttrName))
          sim::set<sim::metadata::VpiSourceTypeIdentity>(storage, identity);
        sim::SimVPIStaticLocalBindingOp::create(
            builder, getSemanticLocation(candidate.source), storage.getIdAttr(),
            owner.getIdAttr(), candidate.scope,
            builder.getI32IntegerAttr(sourceOrdinals[scope]++));
        continue;
      }
      const uint32_t kind = sim::vpiKindForClassMember(semantic);
      const auto *edge = reflection::findVPITraversal(
          static_cast<uint32_t>(reflection::VPIObjectKind::Frame),
          static_cast<uint32_t>(reflection::VPIRelationKind::AutomaticsRel),
          reflection::VPITraversalMode::Iterate);
      if (!edge || !reflection::vpiObjectSetContains(edge->targets, kind))
        continue;
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
    }
  }
  return success();
}
} // namespace obelisk::simlowering
