//===- PrepareLocals.cpp - Automatic declaration inventory
//-----------------===//

#include "PrepareLocals.h"
#include "Detail.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace obelisk::simlowering {
LogicalResult materializeProceduralLocals(sim::SimDesignOp design,
                                          const PreparedUnits &units,
                                          const PreparedCaptures &captures,
                                          OpBuilder &builder) {
  llvm::DenseMap<uint64_t, sim::SimStatementDeclOp> statements;
  for (auto statement : design.getOps<sim::SimStatementDeclOp>())
    statements[statement.getId()] = statement;
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
    llvm::DenseMap<uint64_t, uint64_t> ordinals;
    for (Candidate candidate : candidates) {
      auto semantic =
          sim::get<sim::metadata::VpiDeclaredType>(candidate.source);
      auto type = getNormalizedSemanticType(candidate.source);
      if (failed(type))
        return failure();
      const uint32_t kind = sim::vpiKindForClassMember(semantic);
      const auto *edge = reflection::findVPITraversal(
          static_cast<uint32_t>(reflection::VPIObjectKind::Frame),
          static_cast<uint32_t>(reflection::VPIRelationKind::AutomaticsRel),
          reflection::VPITraversalMode::Iterate);
      if (!edge || !reflection::vpiObjectSetContains(edge->targets, kind))
        continue;
      uint64_t scope =
          candidate.scope ? candidate.scope.getValue().getZExtValue() : 0;
      std::string symbol = "__obelisk_vpi_local_" + std::to_string(nextID);
      auto declaration = sim::SimVPILocalDeclOp::create(
          builder, getSemanticLocation(candidate.source),
          builder.getStringAttr(symbol), builder.getI64IntegerAttr(nextID++),
          owner.getIdAttr(), owner.getScopeIdAttr(), candidate.scope,
          builder.getI32IntegerAttr(ordinals[scope]++),
          builder.getStringAttr(getHierarchyName(candidate.source)),
          builder.getStringAttr(getDebugName(candidate.source)),
          TypeAttr::get(*type), semantic);
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
