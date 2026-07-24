//===- Prepare.cpp - Freeze semantic IR into isolated simulation units --===//
//
// Flattens the elaborated design into numeric descriptors and creates one
// isolated `obelisk_sim.func` shell per code unit, with every non-local
// resource it needs bound to an explicit entry argument. Everything that
// requires whole-design knowledge happens here, so the per-unit passes that
// follow can run concurrently.
//
//===----------------------------------------------------------------------===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"

#include <functional>
#include <limits>
#include <map>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPREPAREPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

static bool containsNestedOperation(Operation *root, Operation *nested) {
  for (Operation *current = nested; current; current = current->getParentOp())
    if (current == root)
      return true;
  return false;
}

/// Return true when `reference` contributes only the storage base of this
/// lvalue. Selection indices remain reads even though they are nested beneath
/// the assignment destination.
static bool isStorageBaseUse(Operation *lvalue, Operation *reference) {
  if (lvalue == reference)
    return isa<semantic::SVNamedValueExpressionOp,
               semantic::SVHierarchicalValueExpressionOp>(lvalue);
  SmallVector<Operation *> children = getChildren(lvalue);
  if (isa<semantic::SVMemberAccessExpressionOp,
          semantic::SVElementSelectExpressionOp,
          semantic::SVRangeSelectExpressionOp>(lvalue))
    return !children.empty() &&
           containsNestedOperation(children.front(), reference) &&
           isStorageBaseUse(children.front(), reference);
  if (isa<semantic::SVConcatenationExpressionOp>(lvalue))
    return llvm::any_of(children, [&](Operation *child) {
      return containsNestedOperation(child, reference) &&
             isStorageBaseUse(child, reference);
    });
  return false;
}

static bool isWriteOnlyReferenceUse(Operation *reference) {
  for (Operation *ancestor = reference->getParentOp(); ancestor;
       ancestor = ancestor->getParentOp()) {
    auto assignment =
        dyn_cast<semantic::SVAssignmentExpressionOp>(ancestor);
    if (!assignment)
      continue;
    if (assignment.getOperatorKind())
      return false;
    SmallVector<Operation *> children = getChildren(assignment);
    size_t destinationIndex = assignment.getHasTimingControl() ? 1u : 0u;
    return destinationIndex < children.size() &&
           containsNestedOperation(children[destinationIndex], reference) &&
           isStorageBaseUse(children[destinationIndex], reference);
  }
  return false;
}

/// A fixed foreach uses only elaborated range metadata to enumerate indices.
/// The collection value itself is not read unless the body references it.
static bool isFixedForeachCollectionUse(Operation *reference) {
  for (Operation *ancestor = reference->getParentOp(); ancestor;
       ancestor = ancestor->getParentOp()) {
    auto foreach =
        dyn_cast<semantic::SVForeachLoopStatementOp>(ancestor);
    if (!foreach)
      continue;
    SmallVector<Operation *> children = getChildren(foreach);
    return children.size() == 2 &&
           containsNestedOperation(children.front(), reference) &&
           !foreach_metadata::hasRuntimeDimension(
               foreach.getLoopDimensions());
  }
  return false;
}

static std::optional<uint64_t> getUnsigned64(IntegerAttr attribute) {
  if (!attribute || attribute.getValue().isNegative() ||
      attribute.getValue().getActiveBits() > 64)
    return std::nullopt;
  return attribute.getValue().getZExtValue();
}

/// FNV-1a over the elaborated hierarchical name. Keep the sign bit clear so
/// the stable unsigned identity has one canonical nonnegative i64 encoding.
static uint64_t getStableCodeUnitID(StringRef hierarchy) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (unsigned char byte : hierarchy.bytes()) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  hash &= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  return hash == 0 ? 1 : hash;
}

static uint32_t getStableImportID(StringRef cIdentifier) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (unsigned char byte : cIdentifier.bytes()) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32));
  return result == 0 ? 1 : result;
}

/// Node kinds whose semantics are declarative but that derive from a shared
/// generic base, so they cannot carry the SemanticDeclarativeNode trait.
static bool isDeclarativeLeafNode(Operation *op) {
  return isa<
      semantic::SVAssertionInstanceExpressionOp,
      semantic::SVCopyClassExpressionOp, semantic::SVNewClassExpressionOp,
      semantic::SVNewCovergroupExpressionOp,
      semantic::SVRandSequenceStatementOp, semantic::SVAssertionPortSymbolOp,
      semantic::SVClassPropertySymbolOp, semantic::SVCoverCrossSymbolOp,
      semantic::SVCoverCrossBodySymbolOp, semantic::SVCovergroupBodySymbolOp,
      semantic::SVCoverpointSymbolOp, semantic::SVLocalAssertionVarSymbolOp,
      semantic::SVRandSeqProductionSymbolOp, semantic::SVDPIOpenArrayTypeOp>(
      op);
}

struct DescriptorInfo {
  enum class Kind { Storage, Net, Driver, Event } kind;
  uint64_t id;
  uint64_t scopeId;
  Type type;
  sim::NetResolutionKind netKind = sim::NetResolutionKind::Wire;
  Type rootType;
  uint64_t viewOffset = 0;
  uint64_t packedViewOffset = 0;
  SmallVector<int64_t> viewIndices;
  Type aggregateViewType;
};

struct UnitInfo {
  enum class ObserverResult { None, Value, Truth, Event };

  Operation *source;
  uint64_t id;
  sim::EntryKind entryKind;
  std::string symbol;
  std::string hierarchy;
  sim::SimFuncOp function;
  ObserverResult observerResult = ObserverResult::None;
};

static bool isAddressableTimingExpression(Operation *op) {
  if (isa<semantic::SVNamedValueExpressionOp,
          semantic::SVHierarchicalValueExpressionOp>(op))
    return true;
  if (isa<semantic::SVMemberAccessExpressionOp>(op)) {
    SmallVector<Operation *> children = getChildren(op);
    return !children.empty() &&
           isAddressableTimingExpression(children.front());
  }
  if (!isa<semantic::SVElementSelectExpressionOp,
           semantic::SVRangeSelectExpressionOp>(op))
    return false;
  SmallVector<Operation *> children = getChildren(op);
  size_t expected =
      isa<semantic::SVElementSelectExpressionOp>(op) ? 2u : 3u;
  if (children.size() != expected ||
      !isAddressableTimingExpression(children.front()))
    return false;
  return llvm::all_of(ArrayRef<Operation *>(children).drop_front(),
                      [](Operation *index) {
                        return isa<semantic::SVIntegerLiteralOp,
                                   semantic::SVUnbasedUnsizedIntegerLiteralOp>(
                            index);
                      });
}

static FailureOr<sim::EntryKind> getEntryKind(Operation *op) {
  if (auto connection = dyn_cast<semantic::SVPortConnectionOp>(op)) {
    if (connection.getDirection() == semantic::SVArgumentDirection::Out)
      return sim::EntryKind::PortOutput;
    if (connection.getDirection() == semantic::SVArgumentDirection::In) {
      if (connection.getProvenance() ==
              semantic::SVPortConnectionKind::Default ||
          connection.getActualIsConstant())
        return sim::EntryKind::PortInitialize;
      return sim::EntryKind::PortInput;
    }
    emitError(getSemanticLocation(op))
        << "non-static inout and ref port connections cannot be spawned";
    return failure();
  }
  if (isa<semantic::SVContinuousAssignSymbolOp>(op))
    return sim::EntryKind::Continuous;
  if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(op))
    return !subroutine.getIsDpiImport().value_or(false) &&
                   subroutine.getSubroutineKind() ==
                       semantic::SVSubroutineKind::Task
               ? sim::EntryKind::Task
               : sim::EntryKind::Function;
  switch (cast<semantic::SVProceduralBlockSymbolOp>(op).getProcedureKind()) {
  case semantic::SVProceduralBlockKind::Initial:
    return sim::EntryKind::Initial;
  case semantic::SVProceduralBlockKind::Final:
    return sim::EntryKind::Final;
  case semantic::SVProceduralBlockKind::Always:
    return sim::EntryKind::Always;
  case semantic::SVProceduralBlockKind::AlwaysComb:
    return sim::EntryKind::AlwaysComb;
  case semantic::SVProceduralBlockKind::AlwaysLatch:
    return sim::EntryKind::AlwaysLatch;
  case semantic::SVProceduralBlockKind::AlwaysFF:
    return sim::EntryKind::AlwaysFF;
  }
  emitError(getSemanticLocation(op)) << "unknown procedural block kind";
  return failure();
}

static bool isProgramCodeUnit(Operation *op) {
  if (op->getParentOfType<semantic::SVAnonymousProgramSymbolOp>())
    return true;
  auto instance = op->getParentOfType<semantic::SVInstanceSymbolOp>();
  if (!instance)
    return false;
  auto reference =
      instance->getAttrOfType<SymbolRefAttr>("referenced_symbol");
  if (!reference)
    return false;
  auto definition =
      SymbolTable::lookupNearestSymbolFrom<semantic::SVDefinitionSymbolOp>(
          instance, reference);
  if (definition)
    return definition.getDefinitionKind() ==
           semantic::SVDefinitionKind::Program;

  // Elaborated instance references use the frontend's stable symbol spelling,
  // which may be flat even when parsed as a nested SymbolRefAttr. Resolve the
  // source definition name as a deterministic fallback.
  auto referencedPath =
      instance->getAttrOfType<StringAttr>("referenced_path");
  ModuleOp module = op->getParentOfType<ModuleOp>();
  bool program = false;
  if (referencedPath && module)
    module.walk([&](semantic::SVDefinitionSymbolOp candidate) {
      auto name = candidate->getAttrOfType<StringAttr>("name");
      if (name && name == referencedPath)
        program = candidate.getDefinitionKind() ==
                  semantic::SVDefinitionKind::Program;
    });
  return program;
}

static std::string getCodeUnitHierarchy(Operation *op) {
  if (auto connection = dyn_cast<semantic::SVPortConnectionOp>(op)) {
    Operation *instance = connection->getParentOp();
    return (getHierarchyName(instance) + ".$port_connection_" +
            Twine(connection.getFormalOrdinal()))
        .str();
  }
  StringRef lexical = getHierarchyName(op);
  if (isa<semantic::SVSubroutineSymbolOp>(op))
    return lexical.str();
  auto nodeID = op->getAttrOfType<IntegerAttr>("node_id");
  return (lexical + ".$code_unit_" +
          Twine(nodeID.getValue().getZExtValue()))
      .str();
}

static Operation *getSingleRegionRoot(Region &region) {
  if (region.empty() || region.front().empty())
    return nullptr;
  return &region.front().front();
}

static Operation *getPortActualLValue(semantic::SVPortConnectionOp connection) {
  Operation *actual = getSingleRegionRoot(connection.getActual());
  auto assignment = dyn_cast_or_null<semantic::SVAssignmentExpressionOp>(actual);
  if (!assignment)
    return actual;
  SmallVector<Operation *> children = getChildren(assignment);
  if (children.size() == 2 &&
      isa<semantic::SVEmptyArgumentExpressionOp>(children[1]))
    return children.front();
  return actual;
}

/// Automatic locals live in the owning unit's binding table instead of the
/// design descriptor inventory. A variable is automatic when it is declared
/// inside a statement block and not explicitly static.
static bool isAutomaticLocalSymbol(Operation *op) {
  if (isa<semantic::SVPatternVarSymbolOp>(op))
    return true;
  auto statementBlock =
      op->getParentOfType<semantic::SVStatementBlockSymbolOp>();
  auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op);
  if (!variable)
    return statementBlock != nullptr;
  if (variable.getLifetime() == semantic::SVVariableLifetime::Static)
    return false;
  // Zero-time function locals retain their established SSA treatment. Direct
  // task locals, on the other hand, need activation-owned storage because the
  // task may suspend after the declaration's lexical block has returned.
  if (auto subroutine =
          op->getParentOfType<semantic::SVSubroutineSymbolOp>();
      subroutine &&
      subroutine.getSubroutineKind() ==
          semantic::SVSubroutineKind::Function)
    return statementBlock != nullptr;
  return variable.getLifetime() ==
             semantic::SVVariableLifetime::Automatic ||
         statementBlock != nullptr;
}

static bool isStaticFormal(Operation *op) {
  auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(op);
  if (!formal || formal.getDirection() == semantic::SVArgumentDirection::Ref)
    return false;
  auto subroutine = op->getParentOfType<semantic::SVSubroutineSymbolOp>();
  return subroutine &&
         subroutine.getDefaultLifetime() ==
             semantic::SVVariableLifetime::Static;
}

static bool isNestedInCodeUnit(Operation *op) {
  for (Operation *parent = op->getParentOp(); parent;
       parent = parent->getParentOp())
    if (isCodeUnit(parent))
      return true;
  return false;
}

class ObeliskSimPreparePass
    : public impl::ObeliskSimPreparePassBase<ObeliskSimPreparePass> {
public:
  void runOnOperation() override;
};

void ObeliskSimPreparePass::runOnOperation() {
  ModuleOp module = getOperation();
  MLIRContext *context = &getContext();

  semantic::SVRootSymbolOp semanticRoot;
  llvm::DenseMap<uint64_t, Operation *> nodeIds;
  bool invalid = false;
  module.walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (!isSemanticOp(op))
      return;
    if (auto root = dyn_cast<semantic::SVRootSymbolOp>(op)) {
      if (semanticRoot) {
        op->emitError("multiple elaborated semantic roots");
        invalid = true;
      }
      semanticRoot = root;
    }
    auto nodeId = op->getAttrOfType<IntegerAttr>("node_id");
    if (!nodeId) {
      op->emitError("semantic node is missing node_id");
      invalid = true;
      return;
    }
    uint64_t id = nodeId.getValue().getZExtValue();
    auto [it, inserted] = nodeIds.try_emplace(id, op);
    if (!inserted) {
      op->emitError() << "duplicate semantic node_id " << id;
      it->second->emitRemark("first node with this ID is here");
      invalid = true;
    }
  });
  if (!semanticRoot) {
    module.emitError(
        "obelisk-sim-prepare requires an elaborated obelisk.sv root");
    signalPassFailure();
    return;
  }

  // Resolve the entire semantic reference graph once, before isolated units
  // can be scheduled independently. Semantic symbols are isolated at every
  // scope, so nearest-symbol lookup cannot traverse these elaboration paths.
  // Node-prefixed symbol names are globally unique; validate every path
  // component directly against that frozen namespace.
  llvm::StringMap<Operation *> semanticSymbols;
  module.walk([&](Operation *op) {
    if (auto name =
            op->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName()))
      semanticSymbols.try_emplace(name.getValue(), op);
  });
  semanticRoot->walk([&](Operation *op) {
    for (NamedAttribute named : op->getAttrs()) {
      if (!named.getName().strref().ends_with("_symbol"))
        continue;
      named.getValue().walk([&](SymbolRefAttr reference) {
        bool resolved = semanticSymbols.count(reference.getRootReference());
        for (FlatSymbolRefAttr nested : reference.getNestedReferences())
          resolved &= semanticSymbols.count(nested.getValue());
        if (!resolved) {
          op->emitError() << "unresolved semantic reference " << reference;
          invalid = true;
        }
      });
    }
  });

  // Reject the declarative node families and the dynamic object types before
  // producing any target IR, so an unsupported construct never survives as
  // silently dropped semantics.
  module.walk([&](Operation *op) {
    if (!isSemanticOp(op))
      return;
    if (auto foreach = dyn_cast<semantic::SVForeachLoopStatementOp>(op);
        foreach &&
        foreach_metadata::hasRuntimeDimension(foreach.getLoopDimensions())) {
      emitError(getSemanticLocation(foreach))
          << "foreach over runtime-sized or associative collections is not "
             "supported";
      invalid = true;
    }
    if (op->hasTrait<OpTrait::SemanticDeclarativeNode>() ||
        isDeclarativeLeafNode(op)) {
      emitError(getSemanticLocation(op))
          << "unsupported semantic construct in the first simulation slice: "
          << op->getName();
      invalid = true;
    }
    for (NamedAttribute attr : op->getAttrs()) {
      attr.getValue().walk([&](Type type) {
        if (isa<semantic::DynArrayType, semantic::QueueType,
                semantic::AssocArrayType, semantic::ClassHandleType,
                semantic::ObjectType>(type)) {
          emitError(getSemanticLocation(op))
              << "unsupported dynamic or object type in the first simulation "
                 "slice: "
              << type;
          invalid = true;
        }
      });
    }
  });
  if (invalid) {
    signalPassFailure();
    return;
  }

  SmallVector<Operation *> sourceUnits;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (isCodeUnit(op))
      sourceUnits.push_back(op);
  });

  // Assign compact, collision-free IDs from sorted elaborated paths. These
  // IDs cross both native and bytecode ABIs, so unchecked truncated hashes
  // are not acceptable.
  llvm::StringSet<> controlPaths;
  llvm::StringSet<> staticPaths;
  semanticRoot->walk([&](Operation *op) {
    if (auto block = dyn_cast<semantic::SVBlockStatementOp>(op)) {
      if (auto path = block.getBlockPathAttr())
        controlPaths.insert(path.getValue());
    } else if (auto disable =
                   dyn_cast<semantic::SVDisableStatementOp>(op)) {
      if (auto path = disable.getTargetPathAttr())
        controlPaths.insert(path.getValue());
    } else if (auto declaration =
                   dyn_cast<semantic::SVVariableDeclStatementOp>(op)) {
      staticPaths.insert(declaration.getReferencedPath());
    }
  });
  auto assignPathIDs = [&](llvm::StringSet<> &paths, StringRef attrName) {
    SmallVector<StringRef> ordered;
    ordered.reserve(paths.size());
    for (const auto &path : paths)
      ordered.push_back(path.getKey());
    llvm::sort(ordered);
    llvm::StringMap<uint64_t> ids;
    for (auto [index, path] : llvm::enumerate(ordered))
      ids[path] = index + 1;
    semanticRoot->walk([&](Operation *op) {
      StringAttr path;
      if (attrName == "obelisk_sim.control_target_id") {
        if (auto block = dyn_cast<semantic::SVBlockStatementOp>(op))
          path = block.getBlockPathAttr();
        else if (auto disable =
                     dyn_cast<semantic::SVDisableStatementOp>(op))
          path = disable.getTargetPathAttr();
      } else if (auto declaration =
                     dyn_cast<semantic::SVVariableDeclStatementOp>(op)) {
        path = StringAttr::get(context, declaration.getReferencedPath());
      }
      if (path)
        op->setAttr(attrName,
                    IntegerAttr::get(IntegerType::get(context, 64),
                                     ids.lookup(path.getValue())));
    });
  };
  assignPathIDs(controlPaths, "obelisk_sim.control_target_id");
  assignPathIDs(staticPaths, "obelisk_sim.static_site_id");

  uint64_t designPrecisionFs = std::numeric_limits<uint64_t>::max();
  for (Operation *unit : sourceUnits) {
    if (auto assignment =
            dyn_cast<semantic::SVContinuousAssignSymbolOp>(unit)) {
      if (assignment.getUnsupportedStrength()) {
        emitError(getSemanticLocation(unit))
            << "continuous-assignment strengths are not supported: "
            << *assignment.getUnsupportedStrength();
        invalid = true;
      }
      if (assignment.getUnsupportedDelay()) {
        emitError(getSemanticLocation(unit))
            << "continuous-assignment delays are not supported: "
            << *assignment.getUnsupportedDelay();
        invalid = true;
      }
    }
    auto timeUnit = unit->getAttrOfType<IntegerAttr>("time_unit_fs");
    auto timePrecision = unit->getAttrOfType<IntegerAttr>("time_precision_fs");
    if (static_cast<bool>(timeUnit) != static_cast<bool>(timePrecision)) {
      emitError(getSemanticLocation(unit))
          << "code unit has an incomplete elaborated time scale";
      invalid = true;
      continue;
    }
    std::optional<uint64_t> unitFsValue =
        timeUnit ? getUnsigned64(timeUnit) : std::optional<uint64_t>(1'000'000);
    std::optional<uint64_t> precisionFsValue =
        timePrecision ? getUnsigned64(timePrecision)
                      : std::optional<uint64_t>(1'000'000);
    if (!unitFsValue || !precisionFsValue) {
      emitError(getSemanticLocation(unit))
          << "elaborated time scale does not fit an unsigned 64-bit value";
      invalid = true;
      continue;
    }
    uint64_t unitFs = *unitFsValue;
    uint64_t precisionFs = *precisionFsValue;
    if (unitFs == 0 || precisionFs == 0 || unitFs < precisionFs ||
        unitFs % precisionFs != 0) {
      emitError(getSemanticLocation(unit))
          << "invalid elaborated time scale " << unitFs << "fs/" << precisionFs
          << "fs";
      invalid = true;
      continue;
    }
    designPrecisionFs = std::min(designPrecisionFs, precisionFs);
  }
  if (designPrecisionFs == std::numeric_limits<uint64_t>::max())
    designPrecisionFs = 1'000'000;
  if (designPrecisionFs >
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    module.emitError("design time precision exceeds the simulation time ABI");
    signalPassFailure();
    return;
  }
  if (invalid) {
    signalPassFailure();
    return;
  }

  OpBuilder moduleBuilder(module.getBodyRegion());
  moduleBuilder.setInsertionPointToEnd(module.getBody());
  auto design = sim::SimDesignOp::create(
      moduleBuilder, module.getLoc(), "design",
      moduleBuilder.getI64IntegerAttr(designPrecisionFs),
      sim::ComputeGraphAttr{});
  design.getBody().push_back(new Block());
  OpBuilder builder(context);
  builder.setInsertionPointToStart(&design.getBody().front());

  // Any failure from here on leaves a partially built design behind, so every
  // exit erases it rather than emitting half-lowered IR.
  auto abort = [&] {
    design.erase();
    signalPassFailure();
  };

  llvm::DenseMap<Operation *, uint64_t> scopeIds;
  SmallVector<sim::SimScopeDeclOp> scopeDeclarations;
  uint64_t nextScopeId = 0;
  scopeIds[semanticRoot] = nextScopeId;
  scopeDeclarations.push_back(sim::SimScopeDeclOp::create(
      builder, getSemanticLocation(semanticRoot), nextScopeId++, IntegerAttr{},
      builder.getStringAttr(getHierarchyName(semanticRoot)),
      builder.getStringAttr(getDebugName(semanticRoot))));
  semanticRoot->walk<WalkOrder::PreOrder>(
      [&](semantic::SVInstanceBodySymbolOp body) {
        Operation *parent = body->getParentOp();
        while (parent && !scopeIds.count(parent))
          parent = parent->getParentOp();
        uint64_t parentId = parent ? scopeIds.lookup(parent) : 0;
        uint64_t id = nextScopeId++;
        scopeIds[body] = id;
        scopeDeclarations.push_back(sim::SimScopeDeclOp::create(
            builder, getSemanticLocation(body), id,
            builder.getI64IntegerAttr(parentId),
            builder.getStringAttr(getHierarchyName(body)),
            builder.getStringAttr(getDebugName(body))));
      });

  auto getScopeId = [&](Operation *op) {
    for (Operation *cursor = op; cursor; cursor = cursor->getParentOp())
      if (auto found = scopeIds.find(cursor); found != scopeIds.end())
        return found->second;
    return uint64_t{0};
  };
  for (Operation *unit : sourceUnits) {
    uint64_t scopeID = getScopeId(unit);
    if (scopeID >= scopeDeclarations.size())
      continue;
    uint64_t unitFs = 1'000'000;
    uint64_t precisionFs = 1'000'000;
    if (auto attr = unit->getAttrOfType<IntegerAttr>("time_unit_fs"))
      unitFs = attr.getValue().getZExtValue();
    if (auto attr = unit->getAttrOfType<IntegerAttr>("time_precision_fs"))
      precisionFs = attr.getValue().getZExtValue();
    sim::SimScopeDeclOp declaration = scopeDeclarations[scopeID];
    if (auto existing =
            declaration->getAttrOfType<IntegerAttr>(
                "dpi_unit_femtoseconds");
        existing && existing.getValue().getZExtValue() != unitFs) {
      emitError(getSemanticLocation(unit))
          << "DPI declaration scope has inconsistent time units";
      invalid = true;
      continue;
    }
    if (auto existing = declaration->getAttrOfType<IntegerAttr>(
            "dpi_precision_femtoseconds");
        existing && existing.getValue().getZExtValue() != precisionFs) {
      emitError(getSemanticLocation(unit))
          << "DPI declaration scope has inconsistent time precisions";
      invalid = true;
      continue;
    }
    declaration->setAttr("dpi_unit_femtoseconds",
                         builder.getI64IntegerAttr(unitFs));
    declaration->setAttr("dpi_precision_femtoseconds",
                         builder.getI64IntegerAttr(precisionFs));
  }
  for (sim::SimScopeDeclOp declaration : scopeDeclarations) {
    if (!declaration->hasAttr("dpi_unit_femtoseconds"))
      declaration->setAttr("dpi_unit_femtoseconds",
                           builder.getI64IntegerAttr(1'000'000));
    if (!declaration->hasAttr("dpi_precision_femtoseconds"))
      declaration->setAttr("dpi_precision_femtoseconds",
                           builder.getI64IntegerAttr(designPrecisionFs));
  }
  if (invalid) {
    abort();
    return;
  }

  // `ref` is the only variable-port association that aliases storage. Every
  // value port is frozen below either as static net topology or as an explicit
  // hidden connection unit.
  struct StaticStorageView {
    std::string path;
    Type rootType;
    Type viewType;
    uint64_t offset = 0;
    uint64_t packedOffset = 0;
    SmallVector<int64_t> indices;
    Type aggregateType;
  };
  std::function<FailureOr<StaticStorageView>(Operation *)> getStaticStorageView;
  getStaticStorageView = [&](Operation *expression)
      -> FailureOr<StaticStorageView> {
    StringRef path;
    if (auto named =
            dyn_cast<semantic::SVNamedValueExpressionOp>(expression))
      path = named.getReferencedPath();
    else if (auto hierarchical =
                 dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                     expression))
      path = hierarchical.getReferencedPath();
    if (!path.empty()) {
      FailureOr<Type> type = getNormalizedSemanticType(expression);
      if (failed(type))
        return failure();
      return StaticStorageView{path.str(), *type, *type, 0, 0, {}, *type};
    }

    SmallVector<Operation *> children = getChildren(expression);
    if (children.empty())
      return failure();
    FailureOr<StaticStorageView> base = getStaticStorageView(children.front());
    FailureOr<Type> resultType = getNormalizedSemanticType(expression);
    if (failed(base) || failed(resultType))
      return failure();

    if (auto member =
            dyn_cast<semantic::SVMemberAccessExpressionOp>(expression)) {
      // A declaration-order subelement cannot be represented after a packed
      // bit view. Keep the supported view grammar unambiguous instead of
      // silently applying the member to the wrong aggregate.
      if (base->packedOffset != 0)
        return failure();
      auto ordinal = member->getAttrOfType<IntegerAttr>("field_ordinal");
      if (!ordinal || ordinal.getValue().isNegative())
        return failure();
      auto subelement = sim::getAggregateProvenanceSubelement(
          base->viewType, ordinal.getValue().getZExtValue());
      if (!subelement ||
          subelement->first > UINT64_MAX - base->offset)
        return failure();
      base->offset += subelement->first;
      base->indices.push_back(ordinal.getValue().getZExtValue());
      base->viewType = *resultType;
      base->aggregateType = *resultType;
      return *base;
    }

    bool element = isa<semantic::SVElementSelectExpressionOp>(expression);
    if (!element && !isa<semantic::SVRangeSelectExpressionOp>(expression))
      return failure();
    if (children.size() < 2)
      return failure();
    auto literal = dyn_cast<semantic::SVIntegerLiteralOp>(children[1]);
    if (!literal)
      return failure();
    FailureOr<ParsedConstant> parsed = parseSVInteger(
        literal.getConstantValue(), 64, getSemanticLocation(children[1]));
    if (failed(parsed) || !parsed->unknown.isZero())
      return failure();
    int64_t first = parsed->value.getSExtValue();
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType)
      return failure();

    if (auto unpacked = dyn_cast<semantic::RangedUnpackedArrayType>(
            semanticType.getValue())) {
      if (!element || base->packedOffset != 0)
        return failure();
      llvm::APInt left(65, static_cast<uint64_t>(unpacked.getLeft()), true);
      llvm::APInt selected(65, static_cast<uint64_t>(first), true);
      llvm::APInt ordinal = unpacked.getLeft() >= unpacked.getRight()
                                ? left - selected
                                : selected - left;
      if (ordinal.isNegative() ||
          ordinal.ugt(llvm::APInt(
              65, std::numeric_limits<unsigned>::max())))
        return failure();
      auto subelement = sim::getAggregateProvenanceSubelement(
          base->viewType, static_cast<unsigned>(ordinal.getZExtValue()));
      if (!subelement ||
          subelement->first > UINT64_MAX - base->offset)
        return failure();
      base->offset += subelement->first;
      base->indices.push_back(
          static_cast<unsigned>(ordinal.getZExtValue()));
      base->viewType = *resultType;
      base->aggregateType = *resultType;
      return *base;
    }

    int64_t right;
    bool descending;
    if (auto integral =
            dyn_cast<semantic::IntegralType>(semanticType.getValue())) {
      right = integral.getRight();
      descending = integral.getLeft() >= integral.getRight();
    } else if (auto packed = dyn_cast<semantic::RangedPackedArrayType>(
                   semanticType.getValue())) {
      right = packed.getRight();
      descending = packed.getLeft() >= packed.getRight();
    } else {
      return failure();
    }
    auto physical = [&](int64_t index) -> std::optional<uint64_t> {
      llvm::APInt selected(65, static_cast<uint64_t>(index), true);
      llvm::APInt boundary(65, static_cast<uint64_t>(right), true);
      llvm::APInt offset =
          descending ? selected - boundary : boundary - selected;
      if (offset.isNegative() || offset.getActiveBits() > 64)
        return std::nullopt;
      return offset.getZExtValue();
    };
    std::optional<uint64_t> low = physical(first);
    if (!low)
      return failure();
    if (!element) {
      if (children.size() < 3)
        return failure();
      auto secondLiteral =
          dyn_cast<semantic::SVIntegerLiteralOp>(children[2]);
      if (!secondLiteral)
        return failure();
      FailureOr<ParsedConstant> second = parseSVInteger(
          secondLiteral.getConstantValue(), 64,
          getSemanticLocation(children[2]));
      if (failed(second) || !second->unknown.isZero())
        return failure();
      std::optional<uint64_t> other =
          physical(second->value.getSExtValue());
      if (!other)
        return failure();
      low = std::min(*low, *other);
    }
    if (!low || *low > UINT64_MAX - base->offset)
      return failure();
    base->offset += *low;
    if (*low > UINT64_MAX - base->packedOffset)
      return failure();
    base->packedOffset += *low;
    base->viewType = *resultType;
    return *base;
  };

  llvm::StringMap<std::string> aliases;
  llvm::StringMap<StaticStorageView> refViews;
  llvm::StringMap<std::string> interfaceAliases;
  SmallVector<semantic::SVPortConnectionOp> portConnections;
  semanticRoot->walk([&](semantic::SVPortConnectionOp connection) {
    portConnections.push_back(connection);
    if (connection.getDirection() != semantic::SVArgumentDirection::Ref)
      return;
    StringRef internal = connection.getInternalPath().value_or(StringRef{});
    Operation *actual = getSingleRegionRoot(connection.getActual());
    FailureOr<StaticStorageView> view =
        actual ? getStaticStorageView(actual)
               : FailureOr<StaticStorageView>(failure());
    if (internal.empty() || !actual || failed(view)) {
      emitError(getSemanticLocation(connection))
          << "ref port requires a static variable, member, packed selection, "
             "or fixed-array element association";
      invalid = true;
      return;
    }
    if (connection.getFormalType() !=
        actual->getAttrOfType<TypeAttr>("semantic_type").getValue()) {
      emitError(getSemanticLocation(connection))
          << "ref port association has a mismatched or converted type";
      invalid = true;
      return;
    }
    aliases[internal] = view->path;
    refViews[internal] = *view;
  });
  semanticRoot->walk([&](semantic::SVPortSymbolOp port) {
    std::optional<Type> type = port.getSemanticType();
    if (type && isa<semantic::RealType, semantic::RealtimeType>(*type)) {
      emitError(getSemanticLocation(port))
          << "real and realtime ports are not supported";
      invalid = true;
    }
  });
  semanticRoot->walk([&](semantic::SVModportPortSymbolOp port) {
    Operation *modport = port->getParentOp();
    Operation *interfaceBody = modport ? modport->getParentOp() : nullptr;
    StringRef path = getHierarchyName(port);
    StringRef base = getHierarchyName(interfaceBody);
    StringRef name = getDebugName(port);
    if (!path.empty() && !base.empty() && !name.empty())
      interfaceAliases[path] = (base + Twine(".") + name).str();
  });

  llvm::StringMap<DescriptorInfo> descriptors;
  uint64_t nextStorageId = 0;
  uint64_t nextNetId = 0;
  uint64_t nextEventId = 0;
  SmallVector<Operation *> designObjects;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *op) {
    auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op);
    bool staticVariable =
        variable &&
        variable.getLifetime() == semantic::SVVariableLifetime::Static;
    if (isNestedInCodeUnit(op) &&
        !staticVariable &&
        !isStaticFormal(op))
      return;
    bool storage =
        (isa<semantic::SVVariableSymbolOp>(op) &&
         !isAutomaticLocalSymbol(op)) ||
        isStaticFormal(op);
    if (storage || isa<semantic::SVNetSymbolOp>(op))
      designObjects.push_back(op);
  });
  auto emitDescriptor = [&](Operation *op) {
    bool storage = isa<semantic::SVVariableSymbolOp,
                       semantic::SVFormalArgumentSymbolOp>(op);
    StringRef path = getHierarchyName(op);
    if (path.empty()) {
      emitError(getSemanticLocation(op))
          << "design object is missing a hierarchy name";
      invalid = true;
      return;
    }
    if (descriptors.count(path))
      return;
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type)) {
      invalid = true;
      return;
    }
    uint64_t scopeId = getScopeId(op);
    StringAttr hierarchy = builder.getStringAttr(path);
    StringAttr debug = builder.getStringAttr(getDebugName(op));
    if (storage && isa<sim::EventType>(*type)) {
      if (!getChildren(op).empty()) {
        emitError(getSemanticLocation(op))
            << "initialized event variables require event-cell lowering";
        invalid = true;
        return;
      }
      uint64_t id = nextEventId++;
      descriptors[path] = {DescriptorInfo::Kind::Event, id, scopeId, *type,
                           sim::NetResolutionKind::Wire};
      descriptors[path].rootType = *type;
      return;
    }
    if (storage) {
      uint64_t id = nextStorageId++;
      descriptors[path] = {DescriptorInfo::Kind::Storage, id, scopeId, *type,
                           sim::NetResolutionKind::Wire};
      descriptors[path].rootType = *type;
      sim::Lifetime lifetime =
          (op->getParentOfType<semantic::SVStatementBlockSymbolOp>() ||
           isStaticFormal(op))
              ? sim::Lifetime::Static
              : sim::Lifetime::Design;
      sim::SimStorageDeclOp::create(builder, getSemanticLocation(op), id,
                                    scopeId, *type, lifetime, hierarchy, debug,
                                    sim::ComputeObservabilityKindAttr{});
    } else {
      if ((*type).isF64()) {
        emitError(getSemanticLocation(op))
            << "real and realtime nets are not supported";
        invalid = true;
        return;
      }
      auto net = cast<semantic::SVNetSymbolOp>(op);
      sim::NetResolutionKind resolution;
      switch (net.getNetKind()) {
      case semantic::SVNetKind::Wire:
        resolution = sim::NetResolutionKind::Wire;
        break;
      case semantic::SVNetKind::Tri:
        resolution = sim::NetResolutionKind::Tri;
        break;
      case semantic::SVNetKind::UWire:
        resolution = sim::NetResolutionKind::UWire;
        break;
      default:
        emitError(getSemanticLocation(op))
            << "unsupported net resolution kind "
            << semantic::stringifySVNetKind(net.getNetKind());
        invalid = true;
        return;
      }
      if (net.getUnsupportedStrength()) {
        emitError(getSemanticLocation(op))
            << "net strengths are not supported: "
            << *net.getUnsupportedStrength();
        invalid = true;
        return;
      }
      if (net.getUnsupportedDelay()) {
        emitError(getSemanticLocation(op))
            << "net delays are not supported: " << *net.getUnsupportedDelay();
        invalid = true;
        return;
      }
      uint64_t id = nextNetId++;
      descriptors[path] = {DescriptorInfo::Kind::Net, id, scopeId, *type,
                           resolution};
      descriptors[path].rootType = *type;
      sim::SimNetDeclOp::create(builder, getSemanticLocation(op), id, scopeId,
                                *type, sim::Lifetime::Design, hierarchy, debug,
                                sim::ComputeObservabilityKindAttr{}, resolution,
                                UnitAttr{});
    }
  };
  // Materialize canonical objects first so alias resolution is independent of
  // semantic-tree traversal order.
  for (Operation *op : designObjects)
    if (!aliases.count(getHierarchyName(op)))
      emitDescriptor(op);
  for (Operation *op : designObjects) {
    StringRef path = getHierarchyName(op);
    auto alias = aliases.find(path);
    if (alias == aliases.end())
      continue;
    llvm::StringSet<> seen;
    StringRef canonical = alias->second;
    uint64_t viewOffset = 0;
    uint64_t packedViewOffset = 0;
    SmallVector<const StaticStorageView *> viewChain;
    if (auto view = refViews.find(path); view != refViews.end()) {
      viewOffset = view->second.offset;
      packedViewOffset = view->second.packedOffset;
      viewChain.push_back(&view->second);
    }
    bool cyclic = false;
    auto next = aliases.find(canonical);
    while (next != aliases.end()) {
      if (!seen.insert(canonical).second) {
        emitError(getSemanticLocation(op)) << "cyclic port alias for " << path;
        invalid = true;
        cyclic = true;
        break;
      }
      if (auto view = refViews.find(canonical); view != refViews.end()) {
        if (view->second.offset > UINT64_MAX - viewOffset) {
          emitError(getSemanticLocation(op))
              << "ref port view offset overflows for " << path;
          invalid = true;
          cyclic = true;
          break;
        }
        viewOffset += view->second.offset;
        if (view->second.packedOffset > UINT64_MAX - packedViewOffset) {
          emitError(getSemanticLocation(op))
              << "ref port packed view offset overflows for " << path;
          invalid = true;
          cyclic = true;
          break;
        }
        packedViewOffset += view->second.packedOffset;
        viewChain.push_back(&view->second);
      }
      canonical = next->second;
      next = aliases.find(canonical);
    }
    if (cyclic)
      continue;
    auto target = descriptors.find(canonical);
    if (target == descriptors.end()) {
      emitError(getSemanticLocation(op))
          << "port alias target has no flattened descriptor: " << canonical;
      invalid = true;
      continue;
    }
    if (refViews.count(path) &&
        target->second.kind != DescriptorInfo::Kind::Storage) {
      emitError(getSemanticLocation(op))
          << "ref port cannot alias a net or driver";
      invalid = true;
      continue;
    }
    descriptors[path] = target->second;
    if (auto view = refViews.find(path); view != refViews.end()) {
      SmallVector<int64_t> viewIndices;
      for (const StaticStorageView *component : llvm::reverse(viewChain))
        viewIndices.append(component->indices);
      Type aggregateViewType = target->second.rootType;
      for (int64_t index : viewIndices) {
        if (index < 0 || static_cast<uint64_t>(index) >
                             std::numeric_limits<unsigned>::max()) {
          aggregateViewType = {};
          break;
        }
        aggregateViewType = sim::getAggregateElementType(
            aggregateViewType, static_cast<unsigned>(index));
        if (!aggregateViewType)
          break;
      }
      if (!aggregateViewType) {
        emitError(getSemanticLocation(op))
            << "ref port has an invalid composed storage view for " << path;
        invalid = true;
        continue;
      }
      descriptors[path].type = view->second.viewType;
      descriptors[path].rootType = target->second.rootType;
      descriptors[path].viewOffset = viewOffset;
      descriptors[path].packedViewOffset = packedViewOffset;
      descriptors[path].viewIndices = std::move(viewIndices);
      descriptors[path].aggregateViewType = aggregateViewType;
    }
  }
  for (const auto &[path, targetPath] : interfaceAliases) {
    auto target = descriptors.find(targetPath);
    if (target == descriptors.end()) {
      emitError(module.getLoc())
          << "interface modport member has no flattened target: " << path;
      invalid = true;
      continue;
    }
    descriptors[path] = target->second;
  }
  if (invalid)
    return abort();

  struct NetRun {
    DescriptorInfo descriptor;
    uint64_t offset;
    uint64_t width;
    std::string path;
    std::optional<uint64_t> nodeId;
  };
  std::function<bool(Operation *, SmallVectorImpl<NetRun> &)> flattenNetExpr;
  flattenNetExpr = [&](Operation *expression,
                       SmallVectorImpl<NetRun> &runs) -> bool {
    if (!expression)
      return false;
    if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression)) {
      auto descriptor = descriptors.find(named.getReferencedPath());
      if (descriptor == descriptors.end() ||
          descriptor->second.kind != DescriptorInfo::Kind::Net)
        return false;
      std::optional<unsigned> width = sim::getPackedWidth(descriptor->second.type);
      if (!width)
        return false;
      std::optional<uint64_t> nodeId;
      if (auto id = named->getAttrOfType<IntegerAttr>("node_id"))
        nodeId = id.getValue().getZExtValue();
      runs.push_back({descriptor->second, 0, *width,
                      named.getReferencedPath().str(), nodeId});
      return true;
    }
    if (auto hierarchical =
            dyn_cast<semantic::SVHierarchicalValueExpressionOp>(expression)) {
      auto descriptor = descriptors.find(hierarchical.getReferencedPath());
      if (descriptor == descriptors.end() ||
          descriptor->second.kind != DescriptorInfo::Kind::Net)
        return false;
      std::optional<unsigned> width =
          sim::getPackedWidth(descriptor->second.type);
      if (!width)
        return false;
      std::optional<uint64_t> nodeId;
      if (auto id = hierarchical->getAttrOfType<IntegerAttr>("node_id"))
        nodeId = id.getValue().getZExtValue();
      runs.push_back({descriptor->second, 0, *width,
                      hierarchical.getReferencedPath().str(), nodeId});
      return true;
    }
    if (isa<semantic::SVConcatenationExpressionOp>(expression)) {
      SmallVector<Operation *> children = getChildren(expression);
      for (Operation *child : llvm::reverse(children))
        if (!flattenNetExpr(child, runs))
          return false;
      return true;
    }
    if (auto member =
            dyn_cast<semantic::SVMemberAccessExpressionOp>(expression)) {
      SmallVector<Operation *> children = getChildren(expression);
      if (children.empty())
        return false;
      SmallVector<NetRun> base;
      if (!flattenNetExpr(children.front(), base) || base.size() != 1)
        return false;
      FailureOr<Type> sourceType =
          getNormalizedSemanticType(children.front());
      auto ordinal = member->getAttrOfType<IntegerAttr>("field_ordinal");
      if (failed(sourceType) || !ordinal || ordinal.getValue().isNegative() ||
          ordinal.getValue().getActiveBits() > 32)
        return false;
      auto subelement = sim::getAggregateProvenanceSubelement(
          *sourceType, static_cast<unsigned>(ordinal.getInt()));
      FailureOr<Type> resultType = getNormalizedSemanticType(expression);
      std::optional<unsigned> resultWidth =
          succeeded(resultType) ? sim::getPackedWidth(*resultType)
                                : std::nullopt;
      if (!subelement || !resultWidth || subelement->first > base.front().width ||
          *resultWidth > base.front().width - subelement->first)
        return false;
      runs.push_back({base.front().descriptor,
                      base.front().offset + subelement->first, *resultWidth,
                      base.front().path, base.front().nodeId});
      return true;
    }
    if (isa<semantic::SVElementSelectExpressionOp,
            semantic::SVRangeSelectExpressionOp>(expression)) {
      SmallVector<Operation *> children = getChildren(expression);
      if (children.size() < 2)
        return false;
      SmallVector<NetRun> base;
      if (!flattenNetExpr(children.front(), base) || base.size() != 1)
        return false;
      auto literalValue = [&](Operation *node) -> std::optional<int64_t> {
        StringAttr spelling = node->getAttrOfType<StringAttr>("constant_value");
        if (!spelling)
          if (auto reference =
                  node->getAttrOfType<SymbolRefAttr>("referenced_symbol"))
            if (auto symbol =
                    semanticSymbols.find(reference.getLeafReference());
                symbol != semanticSymbols.end())
              spelling = symbol->second->getAttrOfType<StringAttr>(
                  "constant_value");
        if (!spelling)
          return std::nullopt;
        FailureOr<ParsedConstant> value = parseSVInteger(
            spelling.getValue(), 64, getSemanticLocation(node));
        if (failed(value) || !value->unknown.isZero() ||
            !value->value.isSignedIntN(64))
          return std::nullopt;
        return value->value.getSExtValue();
      };
      std::optional<int64_t> first = literalValue(children[1]);
      if (!first)
        return false;
      FailureOr<Type> normalizedSource =
          getNormalizedSemanticType(children.front());
      FailureOr<Type> resultType = getNormalizedSemanticType(expression);
      std::optional<unsigned> width =
          succeeded(resultType) ? sim::getPackedWidth(*resultType)
                                : std::nullopt;
      if (failed(normalizedSource) || !width)
        return false;

      if (isa<semantic::SVElementSelectExpressionOp>(expression) &&
          isa<sim::PackedArrayType>(*normalizedSource)) {
        std::optional<unsigned> ordinal =
            sim::getArrayElementOrdinal(*normalizedSource, *first);
        if (!ordinal)
          return false;
        auto subelement = sim::getAggregateProvenanceSubelement(
            *normalizedSource, *ordinal);
        if (!subelement || subelement->first > base.front().width ||
            *width > base.front().width - subelement->first)
          return false;
        runs.push_back({base.front().descriptor,
                        base.front().offset + subelement->first, *width,
                        base.front().path, base.front().nodeId});
        return true;
      }

      auto sourceType = children.front()->getAttrOfType<TypeAttr>("semantic_type");
      if (!sourceType)
        return false;
      std::optional<int64_t> right;
      bool descending = true;
      if (auto integral = dyn_cast<semantic::IntegralType>(sourceType.getValue())) {
        right = integral.getRight();
        descending = integral.getLeft() >= integral.getRight();
      } else if (auto packed = dyn_cast<semantic::RangedPackedArrayType>(
                     sourceType.getValue())) {
        right = packed.getRight();
        descending = packed.getLeft() >= packed.getRight();
      }
      if (!right)
        return false;
      uint64_t elementSpan = 1;
      if (auto packed = dyn_cast<sim::PackedArrayType>(*normalizedSource)) {
        std::optional<uint64_t> span =
            sim::getProvenanceSpan(packed.getElementType());
        if (!span || *span == 0)
          return false;
        elementSpan = *span;
      }
      auto physical = [&](int64_t index) -> std::optional<uint64_t> {
        llvm::APInt selected(65, static_cast<uint64_t>(index), true);
        llvm::APInt boundary(65, static_cast<uint64_t>(*right), true);
        llvm::APInt offset =
            descending ? selected - boundary : boundary - selected;
        if (offset.isNegative() || offset.getActiveBits() > 64)
          return std::nullopt;
        uint64_t scalarOffset = offset.getZExtValue();
        if (scalarOffset != 0 &&
            elementSpan > UINT64_MAX / scalarOffset)
          return std::nullopt;
        return scalarOffset * elementSpan;
      };
      std::optional<uint64_t> low = physical(*first);
      if (!low)
        return false;
      auto range = dyn_cast<semantic::SVRangeSelectExpressionOp>(expression);
      if (range && range.getSelectionKind() ==
                       semantic::SVRangeSelectionKind::Simple) {
        std::optional<int64_t> second = literalValue(children[2]);
        if (!second)
          return false;
        std::optional<uint64_t> other = physical(*second);
        if (!other)
          return false;
        low = std::min(*low, *other);
      } else if (range) {
        bool baseNamesHighBit =
            (descending &&
             range.getSelectionKind() ==
                 semantic::SVRangeSelectionKind::IndexedDown) ||
            (!descending &&
             range.getSelectionKind() ==
                 semantic::SVRangeSelectionKind::IndexedUp);
        if (baseNamesHighBit) {
          if (*width < elementSpan || *low < *width - elementSpan)
            return false;
          *low -= *width - elementSpan;
        }
      }
      if (!low || !width || *low > base.front().width ||
          *width > base.front().width - *low)
        return false;
      runs.push_back({base.front().descriptor, base.front().offset + *low,
                      *width, base.front().path, base.front().nodeId});
      return true;
    }
    return false;
  };

  using StaticEdgeKey =
      std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;
  struct StaticEdgeMetadata {
    uint64_t scopeId;
    std::string provenance;
    Location location;
  };
  std::map<StaticEdgeKey, StaticEdgeMetadata> staticEdges;
  auto appendStaticConnections = [&](semantic::SVPortConnectionOp connection,
                                     ArrayRef<NetRun> lhs,
                                     ArrayRef<NetRun> rhs) {
    size_t lhsIndex = 0, rhsIndex = 0;
    uint64_t lhsConsumed = 0, rhsConsumed = 0;
    while (lhsIndex != lhs.size() && rhsIndex != rhs.size()) {
      const NetRun &left = lhs[lhsIndex];
      const NetRun &right = rhs[rhsIndex];
      uint64_t width =
          std::min(left.width - lhsConsumed, right.width - rhsConsumed);
      uint64_t leftOffset = left.offset + lhsConsumed;
      uint64_t rightOffset = right.offset + rhsConsumed;
      if ((left.descriptor.netKind == sim::NetResolutionKind::UWire) !=
          (right.descriptor.netKind == sim::NetResolutionKind::UWire)) {
        emitError(getSemanticLocation(connection))
            << "connected component mixes uwire with resolved wire/tri nets";
        invalid = true;
        return;
      }
      for (uint64_t bit = 0; bit != width; ++bit) {
        StaticEdgeKey edge{left.descriptor.id, leftOffset + bit,
                           right.descriptor.id, rightOffset + bit};
        StaticEdgeKey reverse{right.descriptor.id, rightOffset + bit,
                              left.descriptor.id, leftOffset + bit};
        if (reverse < edge)
          edge = reverse;
        if (std::get<0>(edge) == std::get<2>(edge) &&
            std::get<1>(edge) == std::get<3>(edge))
          continue;
        StaticEdgeMetadata metadata{
            getScopeId(connection),
            semantic::stringifySVPortConnectionKind(
                connection.getProvenance())
                .str(),
            getSemanticLocation(connection)};
        auto [found, inserted] = staticEdges.try_emplace(edge, metadata);
        if (!inserted &&
            std::tie(metadata.scopeId, metadata.provenance) <
                std::tie(found->second.scopeId, found->second.provenance))
          found->second = std::move(metadata);
      }
      lhsConsumed += width;
      rhsConsumed += width;
      if (lhsConsumed == left.width) {
        ++lhsIndex;
        lhsConsumed = 0;
      }
      if (rhsConsumed == right.width) {
        ++rhsIndex;
        rhsConsumed = 0;
      }
    }
    if (lhsIndex != lhs.size() || rhsIndex != rhs.size()) {
      emitError(getSemanticLocation(connection))
          << "static net connection has incompatible endpoint widths";
      invalid = true;
    }
  };

  // Classify every resolved connection exactly once. Pure net associations
  // become topology; ref and interfaces are aliases; the remainder become
  // hidden executable entries consumed by the ordinary per-unit lowering.
  for (semantic::SVPortConnectionOp connection : portConnections) {
    if (connection.getDirection() == semantic::SVArgumentDirection::Ref)
      continue;
    StringRef internalPath =
        connection.getInternalPath().value_or(StringRef{});
    auto internalDescriptor = descriptors.find(internalPath);
    if (internalDescriptor == descriptors.end()) {
      // Interface ports have no scalar state descriptor and their elaborated
      // member references already name the connected interface instance.
      if (connection.getInterfaceInstanceSymbol() ||
          isa<semantic::UntypedType>(connection.getFormalType()))
        continue;
      emitError(getSemanticLocation(connection))
          << "port internal endpoint has no flattened descriptor";
      invalid = true;
      continue;
    }
    Operation *actual = getPortActualLValue(connection);
    if (!actual) {
      // Open inputs retain their declaration default; open outputs and inouts
      // intentionally perform no work.
      continue;
    }

    SmallVector<NetRun> lhs, rhs;
    Operation *internalExpression =
        getSingleRegionRoot(connection.getInternal());
    bool internalNet = false;
    if (internalExpression) {
      internalNet = flattenNetExpr(internalExpression, lhs);
    } else if (internalDescriptor->second.kind == DescriptorInfo::Kind::Net) {
      if (std::optional<unsigned> width =
              sim::getPackedWidth(internalDescriptor->second.type)) {
        lhs.push_back({internalDescriptor->second, 0, *width,
                       internalPath.str(), std::nullopt});
        internalNet = true;
      }
    }
    bool actualNet = flattenNetExpr(actual, rhs);
    if (internalNet && actualNet) {
      appendStaticConnections(connection, lhs, rhs);
      continue;
    }
    if (connection.getDirection() == semantic::SVArgumentDirection::InOut) {
      emitError(getSemanticLocation(connection))
          << "inout port requires a representation-compatible static net "
             "connection";
      invalid = true;
      continue;
    }
    sourceUnits.push_back(connection);
  }
  if (invalid)
    return abort();

  // Canonical scalar equivalences above make overlap partitioning and
  // duplicate elimination exact. Reassemble adjacent mappings into the
  // deterministic interval representation consumed by both backends.
  uint64_t nextConnectionId = 0;
  for (auto edge = staticEdges.begin(); edge != staticEdges.end();) {
    auto [lhsNet, lhsOffset, rhsNet, rhsOffset] = edge->first;
    const StaticEdgeMetadata metadata = edge->second;
    uint64_t width = 1;
    int direction = 0;
    auto next = std::next(edge);
    while (next != staticEdges.end()) {
      auto [nextLhsNet, nextLhsOffset, nextRhsNet, nextRhsOffset] =
          next->first;
      if (next->second.scopeId != metadata.scopeId ||
          next->second.provenance != metadata.provenance ||
          nextLhsNet != lhsNet || nextRhsNet != rhsNet ||
          nextLhsOffset != lhsOffset + width)
        break;
      int candidateDirection = 0;
      if (nextRhsOffset == rhsOffset + width)
        candidateDirection = 1;
      else if (rhsOffset >= width && nextRhsOffset == rhsOffset - width)
        candidateDirection = -1;
      if (candidateDirection == 0 ||
          (direction != 0 && candidateDirection != direction))
        break;
      direction = candidateDirection;
      ++width;
      ++next;
    }
    sim::SimNetConnectDeclOp::create(
        builder, metadata.location, nextConnectionId++, metadata.scopeId,
        lhsNet, lhsOffset, rhsNet, rhsOffset, width, direction < 0,
        builder.getStringAttr(metadata.provenance));
    edge = next;
  }

  // Each syntactic net sink owns an immutable driver descriptor. Keeping
  // repeated appearances distinct is observable for concatenation lvalues
  // that short a bit to more than one RHS bit, while exact driven ranges let
  // unresolved nets enforce their one-driver-per-component rule.
  struct DriverInfo {
    std::string path;
    DescriptorInfo descriptor;
    std::optional<uint64_t> nodeId;
    uint64_t drivenLow;
    uint64_t drivenWidth;
  };
  uint64_t nextDriverId = 0;
  llvm::DenseMap<Operation *, SmallVector<DriverInfo>> continuousDrivers;
  std::function<void(Operation *, SmallVectorImpl<NetRun> &)>
      collectDriverRuns;
  collectDriverRuns = [&](Operation *expression,
                          SmallVectorImpl<NetRun> &runs) {
    if (!expression)
      return;
    if (isa<semantic::SVConcatenationExpressionOp>(expression)) {
      for (Operation *child : getChildren(expression))
        collectDriverRuns(child, runs);
      return;
    }
    SmallVector<NetRun> exact;
    if (flattenNetExpr(expression, exact)) {
      llvm::append_range(runs, exact);
      return;
    }
    // A dynamic selection can retarget any bit of its base. Preserve a full
    // conservative range so uwire validation never misses a possible clash.
    for (Operation *child : getChildren(expression)) {
      size_t before = runs.size();
      collectDriverRuns(child, runs);
      if (runs.size() != before)
        return;
    }
  };
  for (Operation *unit : sourceUnits) {
    bool continuous = isa<semantic::SVContinuousAssignSymbolOp>(unit);
    auto connection = dyn_cast<semantic::SVPortConnectionOp>(unit);
    if (!continuous && !connection)
      continue;
    SmallVector<NetRun> sinks;
    if (connection &&
        connection.getDirection() == semantic::SVArgumentDirection::In) {
      Operation *internal = getSingleRegionRoot(connection.getInternal());
      if (internal) {
        collectDriverRuns(internal, sinks);
      } else {
        StringRef path = connection.getInternalPath().value_or(StringRef{});
        auto target = descriptors.find(path);
        if (target == descriptors.end()) {
          emitError(getSemanticLocation(unit))
              << "connection target is not a flattened design object: "
              << path;
          invalid = true;
          continue;
        }
        if (target->second.kind == DescriptorInfo::Kind::Net) {
          std::optional<unsigned> width =
              sim::getPackedWidth(target->second.type);
          if (!width) {
            emitError(getSemanticLocation(unit))
                << "net connection target has no fixed packed width";
            invalid = true;
            continue;
          }
          sinks.push_back(
              {target->second, 0, *width, path.str(), std::nullopt});
        }
      }
    } else {
      Operation *assignmentRoot = nullptr;
      if (connection) {
        assignmentRoot = getSingleRegionRoot(connection.getActual());
      } else {
        SmallVector<Operation *> roots = getChildren(unit);
        if (!roots.empty())
          assignmentRoot = roots.front();
      }
      auto assignment =
          dyn_cast_or_null<semantic::SVAssignmentExpressionOp>(assignmentRoot);
      SmallVector<Operation *> children =
          assignment ? getChildren(assignment) : SmallVector<Operation *>{};
      if (!assignment || children.size() != 2) {
        emitError(getSemanticLocation(unit))
            << "connection source has no resolved assignment lvalue";
        invalid = true;
        continue;
      }
      collectDriverRuns(children.front(), sinks);
    }

    for (const NetRun &sink : sinks) {
      uint64_t id = nextDriverId++;
      uint64_t scopeId = getScopeId(unit);
      DescriptorInfo info{DescriptorInfo::Kind::Driver, id, scopeId,
                          sink.descriptor.type, sink.descriptor.netKind};
      info.rootType = sink.descriptor.type;
      continuousDrivers[unit].push_back(
          {sink.path, info, sink.nodeId, sink.offset, sink.width});
      sim::SimDriverDeclOp::create(
          builder, getSemanticLocation(unit), id, scopeId, sink.descriptor.id,
          sink.descriptor.type, sim::Lifetime::Design,
          builder.getStringAttr(sink.path),
          builder.getStringAttr(connection ? "port connection" : "continuous"),
          builder.getI64IntegerAttr(sink.offset),
          builder.getI64IntegerAttr(sink.width));
    }
  }
  if (invalid)
    return abort();

  SmallVector<UnitInfo> units;
  units.reserve(sourceUnits.size());
  llvm::StringMap<std::string> directCallees;
  llvm::StringMap<Operation *> directCalleeSources;
  llvm::DenseMap<uint64_t, Operation *> codeUnitIDs;
  for (auto [index, source] : llvm::enumerate(sourceUnits)) {
    if (auto exported =
            source->getAttrOfType<StringAttr>("dpi_export_c_identifier")) {
      emitError(getSemanticLocation(source))
          << "DPI export '" << exported.getValue()
          << "' is not supported by simulation lowering";
      invalid = true;
      continue;
    }
    FailureOr<sim::EntryKind> entryKind = getEntryKind(source);
    if (failed(entryKind)) {
      invalid = true;
      continue;
    }
    if (*entryKind == sim::EntryKind::Function ||
        *entryKind == sim::EntryKind::Task) {
      auto subroutine = cast<semantic::SVSubroutineSymbolOp>(source);
      bool dpiImport = subroutine.getIsDpiImport().value_or(false);
      if (*entryKind == sim::EntryKind::Function && !dpiImport &&
          subroutine.getSubroutineKind() !=
              semantic::SVSubroutineKind::Function) {
        emitError(getSemanticLocation(source))
            << "only static zero-time SystemVerilog functions are supported";
        invalid = true;
        continue;
      }
      if (dpiImport && !subroutine.getDpiCIdentifierAttr()) {
        emitError(getSemanticLocation(source))
            << "DPI import is missing its resolved C identifier";
        invalid = true;
        continue;
      }
      bool hasTiming = false;
      source->walk([&](Operation *nested) {
        hasTiming |=
            isa<semantic::SVDelayControlOp, semantic::SVSignalEventControlOp,
                semantic::SVEventListControlOp>(nested);
      });
      if (*entryKind == sim::EntryKind::Function && !dpiImport && hasTiming) {
        emitError(getSemanticLocation(source))
            << "zero-time function contains a blocking timing control";
        invalid = true;
        continue;
      }
    }
    std::string symbol = llvm::formatv("unit_{0}", index).str();
    StringRef hierarchy = isa<semantic::SVPortConnectionOp>(source)
                              ? getHierarchyName(source->getParentOp())
                              : getHierarchyName(source);
    if (hierarchy.empty()) {
      emitError(getSemanticLocation(source))
          << "code unit has no elaborated hierarchical name";
      invalid = true;
      continue;
    }
    std::string codeUnitHierarchy = getCodeUnitHierarchy(source);
    uint64_t id = getStableCodeUnitID(codeUnitHierarchy);
    auto [collision, inserted] = codeUnitIDs.try_emplace(id, source);
    if (!inserted) {
      emitError(getSemanticLocation(source))
          << "stable code-unit ID collision for '" << codeUnitHierarchy
          << "'";
      emitRemark(getSemanticLocation(collision->second))
          << "colliding code unit is here";
      invalid = true;
      continue;
    }
    units.push_back({source, id, *entryKind, symbol,
                     std::move(codeUnitHierarchy), {},
                     UnitInfo::ObserverResult::None});
    if (!hierarchy.empty() && !isa<semantic::SVPortConnectionOp>(source)) {
      directCallees[hierarchy] = symbol;
      directCalleeSources[hierarchy] = source;
    }
  }
  if (invalid)
    return abort();

  // Give every potentially computed timing expression a private evaluator
  // identity while the complete semantic tree and collision set are still
  // available. Direct controls retain their existing lowering and the unused
  // evaluator is removed by symbol DCE.
  struct ObserverCandidate {
    Operation *expression;
    UnitInfo::ObserverResult result;
    std::string label;
    uint64_t parentID;
    std::string parentHierarchy;
  };
  SmallVector<ObserverCandidate> observerCandidates;
  const size_t ordinaryUnitCount = units.size();
  for (size_t unitIndex = 0; unitIndex != ordinaryUnitCount; ++unitIndex) {
    UnitInfo &unit = units[unitIndex];
    unit.source->walk<WalkOrder::PreOrder>([&](Operation *nested) {
      if (auto wait = dyn_cast<semantic::SVWaitStatementOp>(nested)) {
        SmallVector<Operation *> children = getChildren(wait);
        if (children.size() == 2 &&
            !isAddressableTimingExpression(children.front()))
          observerCandidates.push_back(
              {children.front(), UnitInfo::ObserverResult::Truth, "wait",
               unit.id, unit.hierarchy});
        return;
      }
      auto event = dyn_cast<semantic::SVSignalEventControlOp>(nested);
      if (!event)
        return;
      SmallVector<Operation *> children = getChildren(event);
      if (children.empty())
        return;
      UnitInfo::ObserverResult primaryResult =
          UnitInfo::ObserverResult::Value;
      FailureOr<Type> primaryType =
          getNormalizedSemanticType(children.front());
      if (succeeded(primaryType) && isa<sim::EventType>(*primaryType))
        primaryResult = UnitInfo::ObserverResult::Event;
      observerCandidates.push_back(
          {children.front(), primaryResult, "primary", unit.id,
           unit.hierarchy});
      if (event.getHasIff() && children.size() == 2)
        observerCandidates.push_back(
            {children[1], UnitInfo::ObserverResult::Truth, "iff", unit.id,
             unit.hierarchy});
    });
  }
  llvm::DenseSet<Operation *> outlinedObservers;
  for (ObserverCandidate &candidate : observerCandidates) {
    if (!outlinedObservers.insert(candidate.expression).second)
      continue;
    auto nodeID = candidate.expression->getAttrOfType<IntegerAttr>("node_id");
    if (!nodeID) {
      candidate.expression->emitError(
          "timing observer expression is missing node_id");
      invalid = true;
      continue;
    }
    uint64_t ordinal = nodeID.getValue().getZExtValue();
    std::string hierarchy =
        (Twine(candidate.parentHierarchy) + ".$observer." +
         Twine(ordinal) + "." + candidate.label)
            .str();
    uint64_t id = getStableCodeUnitID(hierarchy);
    auto [collision, inserted] =
        codeUnitIDs.try_emplace(id, candidate.expression);
    if (!inserted) {
      emitError(getSemanticLocation(candidate.expression))
          << "stable observer code-unit ID collision for '" << hierarchy
          << "'";
      emitRemark(getSemanticLocation(collision->second))
          << "colliding code unit is here";
      invalid = true;
      continue;
    }
    std::string symbol =
        llvm::formatv("observer_{0}_{1}", candidate.parentID, ordinal).str();
    candidate.expression->setAttr(
        "obelisk_sim.observer",
        FlatSymbolRefAttr::get(context, symbol));
    candidate.expression->setAttr(
        "obelisk_sim.observer_result",
        builder.getI32IntegerAttr(
            static_cast<uint32_t>(candidate.result)));
    units.push_back({candidate.expression, id, sim::EntryKind::Observer,
                     std::move(symbol), std::move(hierarchy), {},
                     candidate.result});
  }
  if (invalid)
    return abort();

  uint64_t rootCodeUnitID = getStableCodeUnitID("__obelisk_root");
  if (auto collision = codeUnitIDs.find(rootCodeUnitID);
      collision != codeUnitIDs.end()) {
    emitError(getSemanticLocation(collision->second))
        << "stable code-unit ID collides with the root initializer";
    return abort();
  }
  codeUnitIDs[rootCodeUnitID] = semanticRoot;

  // Fork branches are private code units too. Assign their stable identities
  // while the full semantic design is still available, and reject collisions
  // before per-unit lowering can run concurrently.
  std::function<void(Operation *, StringRef)> assignForkCodeUnits;
  assignForkCodeUnits = [&](Operation *operation, StringRef parentHierarchy) {
    if (auto fork = dyn_cast<semantic::SVBlockStatementOp>(operation);
        fork && fork.getBlockKind() !=
                    semantic::SVStatementBlockKind::Sequential) {
      SmallVector<Operation *> branches = getChildren(fork);
      if (branches.size() == 1 &&
          isa<semantic::SVStatementListOp>(branches.front()))
        branches = getChildren(branches.front());
      while (!branches.empty() &&
             isa<semantic::SVVariableDeclStatementOp>(branches.front()))
        branches.erase(branches.begin());
      auto nodeID = fork->getAttrOfType<IntegerAttr>("node_id");
      for (auto [index, branch] : llvm::enumerate(branches)) {
        std::string hierarchy =
            (Twine(parentHierarchy) + ".$fork." +
             Twine(nodeID.getValue().getZExtValue()) + "." + Twine(index))
                .str();
        uint64_t id = getStableCodeUnitID(hierarchy);
        auto [collision, inserted] = codeUnitIDs.try_emplace(id, branch);
        if (!inserted) {
          emitError(getSemanticLocation(branch))
              << "stable fork code-unit ID collision for '" << hierarchy
              << "'";
          emitRemark(getSemanticLocation(collision->second))
              << "colliding code unit is here";
          invalid = true;
          continue;
        }
        branch->setAttr(
            "obelisk_sim.fork_code_unit_id",
            IntegerAttr::get(IntegerType::get(context, 64), id));
        assignForkCodeUnits(branch, hierarchy);
      }
      return;
    }
    for (Operation *child : getChildren(operation))
      assignForkCodeUnits(child, parentHierarchy);
  };
  for (UnitInfo &unit : units)
    assignForkCodeUnits(unit.source, unit.hierarchy);
  if (invalid)
    return abort();

  sim::SimCodeUnitDeclOp::create(
      builder, module.getLoc(), rootCodeUnitID, uint64_t{0},
      sim::EntryKind::RootInitializer, builder.getStringAttr("__obelisk_root"),
      builder.getStringAttr("root initializer"), UnitAttr{});
  llvm::DenseMap<Operation *, sim::SimCodeUnitDeclOp> codeUnitDeclarations;
  for (UnitInfo &unit : units) {
    auto declaration = sim::SimCodeUnitDeclOp::create(
        builder, getSemanticLocation(unit.source), unit.id,
        getScopeId(unit.source), unit.entryKind,
        builder.getStringAttr(unit.hierarchy),
        builder.getStringAttr(getDebugName(unit.source)),
        isa<semantic::SVPortConnectionOp>(unit.source) ? builder.getUnitAttr()
                                                       : UnitAttr{});
    codeUnitDeclarations[unit.source] = declaration;
  }

  llvm::DenseMap<Operation *,
                 SmallVector<std::pair<std::string, DescriptorInfo>>>
      unitCaptures;
  llvm::DenseMap<Operation *, llvm::StringSet<>> unitReadCaptures;
  struct LocalInfo {
    std::string path;
    Type type;
    bool automatic = false;
    bool patternVariable = false;
  };
  llvm::DenseMap<Operation *, SmallVector<LocalInfo>> unitLocals;
  llvm::DenseMap<Operation *, SmallVector<LocalInfo>> observerLocalCaptures;
  llvm::DenseMap<Operation *, llvm::StringSet<>> observerReadLocals;
  for (UnitInfo &unit : units) {
    llvm::StringSet<> seenPaths;
    llvm::StringSet<> seenLocals;
    std::function<void(Operation *)> collectBinding = [&](Operation *nested) {
      StringRef path;
      SymbolRefAttr reference;
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested)) {
        path = named.getReferencedPath();
        reference = named.getReferencedSymbol();
      } else if (auto hierarchical =
                     dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                         nested)) {
        path = hierarchical.getReferencedPath();
        reference = hierarchical.getReferencedSymbol();
      } else if (auto declaration =
                     dyn_cast<semantic::SVVariableDeclStatementOp>(nested)) {
        path = declaration.getReferencedPath();
        reference = declaration.getReferencedSymbol();
      } else {
        return;
      }
      if (!isa<semantic::SVVariableDeclStatementOp>(nested) &&
          isFixedForeachCollectionUse(nested))
        return;
      auto descriptor = descriptors.find(path);
      if (descriptor != descriptors.end()) {
        if (seenPaths.insert(path).second)
          unitCaptures[unit.source].push_back({path.str(), descriptor->second});
        if (!isa<semantic::SVVariableDeclStatementOp>(nested) &&
            !isWriteOnlyReferenceUse(nested))
          unitReadCaptures[unit.source].insert(path);
        return;
      }
      if (!reference)
        return;
      auto symbol = semanticSymbols.find(reference.getLeafReference());
      if (symbol == semanticSymbols.end() ||
          (!isa<semantic::SVVariableSymbolOp>(symbol->second) &&
           !isa<semantic::SVPatternVarSymbolOp>(symbol->second) &&
           !(unit.entryKind == sim::EntryKind::Observer &&
             isa<semantic::SVFormalArgumentSymbolOp>(symbol->second))))
        return;
      FailureOr<Type> type = getNormalizedSemanticType(symbol->second);
      if (failed(type)) {
        invalid = true;
        return;
      }
      if (seenLocals.insert(path).second) {
        auto &destination =
            unit.entryKind == sim::EntryKind::Observer
                ? observerLocalCaptures[unit.source]
                : unitLocals[unit.source];
        destination.push_back(
            {path.str(), *type, isAutomaticLocalSymbol(symbol->second),
             isa<semantic::SVPatternVarSymbolOp>(symbol->second)});
        symbol->second->walk<WalkOrder::PreOrder>(
            [&](Operation *initializerNode) {
              collectBinding(initializerNode);
            });
      }
      if (unit.entryKind == sim::EntryKind::Observer &&
          !isWriteOnlyReferenceUse(nested))
        observerReadLocals[unit.source].insert(path);
    };
    unit.source->walk<WalkOrder::PreOrder>(
        [&](Operation *nested) { collectBinding(nested); });
    if (auto connection =
            dyn_cast<semantic::SVPortConnectionOp>(unit.source)) {
      StringRef internal = connection.getInternalPath().value_or(StringRef{});
      if (!internal.empty())
        if (auto descriptor = descriptors.find(internal);
            descriptor != descriptors.end() &&
            seenPaths.insert(internal).second)
          unitCaptures[unit.source].push_back(
              {internal.str(), descriptor->second});
    }
  }
  if (invalid)
    return abort();

  // A caller must explicitly receive every non-local resource required by a
  // direct callee. Compute the transitive closure before signatures are
  // frozen, which also makes recursive and forward call graphs deterministic.
  // The call edges are collected once; only the capture sets iterate.
  llvm::DenseMap<Operation *, SmallVector<Operation *>> callEdges;
  for (UnitInfo &unit : units) {
    llvm::SmallDenseSet<Operation *> targets;
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      std::optional<StringRef> path = call.getReferencedPath();
      if (!path)
        return;
      auto target = directCalleeSources.find(*path);
      if (target != directCalleeSources.end() &&
          targets.insert(target->second).second)
        callEdges[unit.source].push_back(target->second);
    });
  }
  bool changed;
  do {
    changed = false;
    for (UnitInfo &unit : units) {
      auto &captures = unitCaptures[unit.source];
      llvm::StringSet<> seen;
      for (auto &capture : captures)
        seen.insert(capture.first);
      for (Operation *target : callEdges[unit.source])
        for (auto &capture : unitCaptures[target])
          if (seen.insert(capture.first).second) {
            captures.push_back(capture);
            changed = true;
          }
      for (Operation *target : callEdges[unit.source])
        for (const auto &read : unitReadCaptures[target])
          changed |=
              unitReadCaptures[unit.source].insert(read.getKey()).second;
    }
  } while (changed);
  for (UnitInfo &unit : units) {
    llvm::sort(unitCaptures[unit.source], [](const auto &lhs, const auto &rhs) {
      if (lhs.second.kind != rhs.second.kind)
        return lhs.second.kind < rhs.second.kind;
      if (lhs.second.id != rhs.second.id)
        return lhs.second.id < rhs.second.id;
      return lhs.first < rhs.first;
    });
    llvm::sort(unitLocals[unit.source], [](const auto &lhs, const auto &rhs) {
      return lhs.path < rhs.path;
    });
    llvm::sort(observerLocalCaptures[unit.source],
               [](const auto &lhs, const auto &rhs) {
                 return lhs.path < rhs.path;
               });
    if (unit.entryKind == sim::EntryKind::Observer) {
      SmallVector<Attribute> captures;
      SmallVector<Attribute> dependencies;
      for (auto &capture : unitCaptures[unit.source]) {
        captures.push_back(builder.getStringAttr(capture.first));
        if (unitReadCaptures[unit.source].contains(capture.first))
          dependencies.push_back(builder.getStringAttr(capture.first));
      }
      for (const LocalInfo &local : observerLocalCaptures[unit.source]) {
        captures.push_back(builder.getStringAttr(local.path));
        if (observerReadLocals[unit.source].contains(local.path))
          dependencies.push_back(builder.getStringAttr(local.path));
      }
      unit.source->setAttr("obelisk_sim.observer_captures",
                           builder.getArrayAttr(captures));
      unit.source->setAttr("obelisk_sim.observer_dependencies",
                           builder.getArrayAttr(dependencies));
    }
  }

  // Create the root shell first. Its body is filled after all process shells
  // exist, so every spawn uses an immutable precomputed flat name.
  SmallVector<DictionaryAttr> rootArgAttrs{
      captureMetadata(builder, sim::CaptureKind::Context)};
  auto rootType =
      FunctionType::get(context, {sim::ContextType::get(context)}, {});
  SmallVector<NamedAttribute> rootAttrs{builder.getNamedAttr(
      "code_unit_id", builder.getI64IntegerAttr(rootCodeUnitID))};
  rootAttrs.push_back(builder.getNamedAttr(
      "home_region",
      sim::EventRegionAttr::get(context, sim::EventRegion::Active)));
  rootAttrs.push_back(builder.getNamedAttr(
      "domain",
      sim::ExecutionDomainAttr::get(context, sim::ExecutionDomain::Design)));
  auto rootInitializer =
      sim::SimFuncOp::create(builder, module.getLoc(), "__obelisk_root",
                             rootType, sim::EntryKind::RootInitializer,
                             rootAttrs, rootArgAttrs);

  for (UnitInfo &unit : units) {
    auto captures = unitCaptures.lookup(unit.source);
    auto locals = unitLocals.lookup(unit.source);
    auto observerLocals = observerLocalCaptures.lookup(unit.source);
    SmallVector<Type> copyOutResultTypes;

    // A continuous assignment may read its own target. Keep the ordinary net
    // bindings for reads and add role-specific driver bindings for each
    // syntactic net sink.
    if (auto found = continuousDrivers.find(unit.source);
        found != continuousDrivers.end())
      for (const DriverInfo &driver : found->second)
        captures.push_back({driver.path, driver.descriptor});

    SmallVector<Type> inputs{sim::ContextType::get(context)};
    SmallVector<DictionaryAttr> argAttrs{
        captureMetadata(builder, sim::CaptureKind::Context)};
    SmallVector<Attribute> bindings;
    for (auto [captureIndex, capture] : llvm::enumerate(captures)) {
      sim::CaptureKind captureKind = sim::CaptureKind::Storage;
      Type handleType;
      switch (capture.second.kind) {
      case DescriptorInfo::Kind::Storage:
        captureKind = sim::CaptureKind::Storage;
        handleType = sim::RefType::get(context, capture.second.type);
        break;
      case DescriptorInfo::Kind::Net:
        captureKind = sim::CaptureKind::Net;
        handleType = sim::NetType::get(context, capture.second.type);
        break;
      case DescriptorInfo::Kind::Driver:
        captureKind = sim::CaptureKind::Driver;
        handleType = sim::DriverType::get(context, capture.second.type);
        break;
      case DescriptorInfo::Kind::Event:
        captureKind = sim::CaptureKind::Event;
        handleType = sim::EventType::get(context);
        break;
      }
      inputs.push_back(handleType);
      DictionaryAttr metadata =
          captureMetadata(builder, captureKind, capture.second.id);
      SmallVector<NamedAttribute> metadataAttrs(metadata.begin(),
                                                metadata.end());
      if (capture.second.rootType &&
          (capture.second.viewOffset != 0 ||
           capture.second.rootType != capture.second.type)) {
        metadataAttrs.push_back(builder.getNamedAttr(
            sim::metadata::descriptorRootType,
            TypeAttr::get(capture.second.rootType)));
        metadataAttrs.push_back(builder.getNamedAttr(
            sim::metadata::descriptorLow,
            builder.getI64IntegerAttr(capture.second.viewOffset)));
        if (!capture.second.viewIndices.empty())
          metadataAttrs.push_back(builder.getNamedAttr(
              sim::metadata::descriptorIndices,
              builder.getDenseI64ArrayAttr(capture.second.viewIndices)));
        if (capture.second.aggregateViewType)
          metadataAttrs.push_back(builder.getNamedAttr(
              sim::metadata::descriptorAggregateType,
              TypeAttr::get(capture.second.aggregateViewType)));
        if (capture.second.packedViewOffset != 0 ||
            capture.second.aggregateViewType != capture.second.type)
          metadataAttrs.push_back(builder.getNamedAttr(
              sim::metadata::descriptorPackedLow,
              builder.getI64IntegerAttr(capture.second.packedViewOffset)));
      }
      argAttrs.push_back(builder.getDictionaryAttr(metadataAttrs));
      SmallVector<NamedAttribute> binding{
          builder.getNamedAttr("path", builder.getStringAttr(capture.first)),
          builder.getNamedAttr("argument",
                               builder.getI64IntegerAttr(captureIndex + 1)),
      };
      const DriverInfo *plannedDriver = nullptr;
      if (capture.second.kind == DescriptorInfo::Kind::Driver)
        if (auto found = continuousDrivers.find(unit.source);
            found != continuousDrivers.end())
          if (auto planned = llvm::find_if(
                  found->second, [&](const DriverInfo &driver) {
                    return driver.descriptor.id == capture.second.id;
                  });
              planned != found->second.end())
            plannedDriver = &*planned;
      if (plannedDriver) {
        binding.push_back(
            builder.getNamedAttr("lvalue_only", builder.getUnitAttr()));
        if (plannedDriver->nodeId)
          binding.push_back(builder.getNamedAttr(
              "lvalue_node_id",
              builder.getI64IntegerAttr(*plannedDriver->nodeId)));
      }
      bindings.push_back(builder.getDictionaryAttr(binding));
    }
    for (const LocalInfo &local : locals)
      bindings.push_back(builder.getDictionaryAttr([&] {
        SmallVector<NamedAttribute> attrs{
            builder.getNamedAttr("path",
                                 builder.getStringAttr(local.path)),
            builder.getNamedAttr("local_type", TypeAttr::get(local.type)),
        };
        if (local.automatic)
          attrs.push_back(
              builder.getNamedAttr("automatic", builder.getUnitAttr()));
        if (local.patternVariable)
          attrs.push_back(builder.getNamedAttr("pattern_variable",
                                                builder.getUnitAttr()));
        if (auto subroutine =
                dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
            subroutine &&
            subroutine.getReturnVariablePath() == local.path)
          attrs.push_back(
              builder.getNamedAttr("is_return", builder.getUnitAttr()));
        return attrs;
      }()));

    for (const LocalInfo &local : observerLocals) {
      unsigned argument = inputs.size();
      inputs.push_back(sim::RefType::get(context, local.type));
      argAttrs.push_back(
          captureMetadata(builder, sim::CaptureKind::Value));
      bindings.push_back(builder.getDictionaryAttr({
          builder.getNamedAttr("path", builder.getStringAttr(local.path)),
          builder.getNamedAttr("argument",
                               builder.getI64IntegerAttr(argument)),
      }));
    }

    // Subroutine formals precede non-local captures in the public contract.
    // Function output and inout formals use copy-out results. Task copy-out
    // destinations are hidden reference arguments retained by the activation.
    // Only explicit ref formals otherwise preserve caller aliasing.
    if (unit.entryKind == sim::EntryKind::Function ||
        unit.entryKind == sim::EntryKind::Task) {
      auto subroutine = cast<semantic::SVSubroutineSymbolOp>(unit.source);
      bool dpiImport = subroutine.getIsDpiImport().value_or(false);
      bool directTask = unit.entryKind == sim::EntryKind::Task;
      SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
      for (Operation *child : getChildren(unit.source))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          formals.push_back(formal);
      if (!formals.empty()) {
        SmallVector<Type> reordered{inputs.front()};
        SmallVector<DictionaryAttr> reorderedAttrs{argAttrs.front()};
        SmallVector<Attribute> formalBindings;
        for (semantic::SVFormalArgumentSymbolOp formal : formals) {
          if (dpiImport) {
            std::optional<Type> semanticType = formal.getSemanticType();
            if (!semanticType ||
                failed(getDPIABIKind(*semanticType,
                                     getSemanticLocation(formal)))) {
              invalid = true;
              continue;
            }
          }
          FailureOr<Type> type = getNormalizedSemanticType(formal);
          if (failed(type)) {
            invalid = true;
            continue;
          }
          if (dpiImport && !sim::getPackedWidth(*type)) {
            emitError(getSemanticLocation(formal))
                << "DPI import formal type is unsupported by the initial "
                   "integral ABI";
            invalid = true;
            continue;
          }
          semantic::SVArgumentDirection direction = formal.getDirection();
          if (dpiImport &&
              direction == semantic::SVArgumentDirection::Ref) {
            emitError(getSemanticLocation(formal))
                << "DPI ref formals are not supported; use input, output, or "
                   "inout";
            invalid = true;
            continue;
          }
          bool isRef = direction == semantic::SVArgumentDirection::Ref;
          Type argumentType =
              isRef ? Type(sim::RefType::get(context, *type)) : *type;
          unsigned argument = reordered.size();
          reordered.push_back(argumentType);
          reorderedAttrs.push_back(
              captureMetadata(builder, sim::CaptureKind::Formal));
          SmallVector<NamedAttribute> formalBinding{
              builder.getNamedAttr(
                  "path", builder.getStringAttr(getHierarchyName(formal))),
              builder.getNamedAttr("argument",
                                   builder.getI64IntegerAttr(argument)),
              builder.getNamedAttr(
                  "formal_direction",
                  builder.getI64IntegerAttr(static_cast<int64_t>(direction))),
          };
          // Value formals are callee-local variables. Inputs copy in, outputs
          // and inouts copy out, and mem2reg removes the allocation whenever
          // the local does not escape. Only `ref` preserves caller aliasing.
          if (!isRef)
            formalBinding.push_back(
                builder.getNamedAttr("formal_local", builder.getUnitAttr()));
          if (direction == semantic::SVArgumentDirection::Out ||
              direction == semantic::SVArgumentDirection::InOut) {
            formalBinding.push_back(
                builder.getNamedAttr("copy_out", builder.getUnitAttr()));
            if (!directTask)
              copyOutResultTypes.push_back(*type);
          }
          formalBindings.push_back(builder.getDictionaryAttr(formalBinding));
          if (directTask &&
              (direction == semantic::SVArgumentDirection::Out ||
               direction == semantic::SVArgumentDirection::InOut)) {
            unsigned destinationArgument = reordered.size();
            reordered.push_back(sim::RefType::get(context, *type));
            reorderedAttrs.push_back(
                captureMetadata(builder, sim::CaptureKind::Formal));
            formalBindings.push_back(builder.getDictionaryAttr({
                builder.getNamedAttr(
                    "path",
                    builder.getStringAttr(getHierarchyName(formal))),
                builder.getNamedAttr(
                    "argument",
                    builder.getI64IntegerAttr(destinationArgument)),
                builder.getNamedAttr("copy_out_destination",
                                     builder.getUnitAttr()),
            }));
          }
        }
        unsigned offset = reordered.size() - 1;
        reordered.append(inputs.begin() + 1, inputs.end());
        reorderedAttrs.append(argAttrs.begin() + 1, argAttrs.end());
        for (Attribute binding : bindings) {
          auto dictionary = cast<DictionaryAttr>(binding);
          auto argument = dictionary.getAs<IntegerAttr>("argument");
          if (!argument) {
            formalBindings.push_back(binding);
            continue;
          }
          SmallVector<NamedAttribute> attrs(dictionary.begin(),
                                            dictionary.end());
          uint64_t old = argument.getValue().getZExtValue();
          for (NamedAttribute &attr : attrs)
            if (attr.getName() == "argument")
              attr.setValue(builder.getI64IntegerAttr(old + offset));
          formalBindings.push_back(builder.getDictionaryAttr(attrs));
        }
        inputs = std::move(reordered);
        argAttrs = std::move(reorderedAttrs);
        bindings = std::move(formalBindings);
      }
    }

    if (invalid)
      continue;
    SmallVector<Type> results;
    if (unit.entryKind == sim::EntryKind::Function) {
      auto subroutine = cast<semantic::SVSubroutineSymbolOp>(unit.source);
      bool dpiImport = subroutine.getIsDpiImport().value_or(false);
      if (subroutine.getSubroutineKind() ==
          semantic::SVSubroutineKind::Function) {
        std::optional<SymbolRefAttr> returnSymbol =
            subroutine.getReturnVariableSymbol();
        FailureOr<Type> resultType = failure();
        Type semanticResultType;
        if (returnSymbol) {
          auto symbol =
              semanticSymbols.find(returnSymbol->getLeafReference());
          if (symbol == semanticSymbols.end()) {
            emitError(getSemanticLocation(unit.source))
                << "function return variable does not resolve";
            invalid = true;
            continue;
          }
          resultType = getNormalizedSemanticType(symbol->second);
          if (auto attr =
                  symbol->second->getAttrOfType<TypeAttr>("semantic_type"))
            semanticResultType = attr.getValue();
        } else if (dpiImport) {
          auto semanticType =
              unit.source->getAttrOfType<TypeAttr>("semantic_type");
          auto subroutineType =
              semanticType
                  ? dyn_cast<semantic::SubroutineType>(
                        semanticType.getValue())
                  : semantic::SubroutineType{};
          auto signature =
              subroutineType
                  ? dyn_cast<FunctionType>(subroutineType.getSignature())
                  : FunctionType{};
          if (!signature || signature.getNumResults() != 1) {
            emitError(getSemanticLocation(unit.source))
                << "DPI function has no resolved return signature";
            invalid = true;
            continue;
          }
          semanticResultType = signature.getResult(0);
          resultType = normalizeSemanticType(
              signature.getResult(0), getSemanticLocation(unit.source));
        } else {
          emitError(getSemanticLocation(unit.source))
              << "function is missing its elaborated return variable";
          invalid = true;
          continue;
        }
        if (failed(resultType)) {
          invalid = true;
          continue;
        }
        if (dpiImport && !sim::getPackedWidth(*resultType)) {
          emitError(getSemanticLocation(unit.source))
              << "DPI import return type is unsupported by the initial "
                 "integral ABI";
          invalid = true;
          continue;
        }
        if (dpiImport &&
            (!semanticResultType ||
             failed(getDPIABIKind(semanticResultType,
                                  getSemanticLocation(unit.source))))) {
          invalid = true;
          continue;
        }
        results.push_back(*resultType);
      }
    } else if (unit.entryKind == sim::EntryKind::Observer) {
      Type resultType;
      if (unit.observerResult == UnitInfo::ObserverResult::Truth ||
          unit.observerResult == UnitInfo::ObserverResult::Event) {
        resultType = builder.getI1Type();
      } else {
        FailureOr<Type> normalized =
            getNormalizedSemanticType(unit.source);
        if (failed(normalized)) {
          invalid = true;
          continue;
        }
        resultType = sim::getPackedScalarType(*normalized);
        if (!resultType) {
          emitError(getSemanticLocation(unit.source))
              << "observer expression does not have a packed scalar result";
          invalid = true;
          continue;
        }
      }
      results.push_back(resultType);
    }
    llvm::append_range(results, copyOutResultTypes);
    FunctionType type = FunctionType::get(context, inputs, results);
    NamedAttribute bindingAttr =
        builder.getNamedAttr(bindingsAttrName, builder.getArrayAttr(bindings));
    uint64_t timeUnitFs = 1'000'000;
    uint64_t timePrecisionFs = 1'000'000;
    if (auto attr = unit.source->getAttrOfType<IntegerAttr>("time_unit_fs")) {
      std::optional<uint64_t> value = getUnsigned64(attr);
      if (!value) {
        emitError(getSemanticLocation(unit.source))
            << "code unit time scale does not fit an unsigned 64-bit value";
        invalid = true;
        continue;
      }
      timeUnitFs = *value;
    }
    if (auto attr =
            unit.source->getAttrOfType<IntegerAttr>("time_precision_fs")) {
      std::optional<uint64_t> value = getUnsigned64(attr);
      if (!value) {
        emitError(getSemanticLocation(unit.source))
            << "code unit time precision does not fit an unsigned 64-bit "
               "value";
        invalid = true;
        continue;
      }
      timePrecisionFs = *value;
    }
    if (timeUnitFs < designPrecisionFs || timeUnitFs % designPrecisionFs != 0) {
      emitError(getSemanticLocation(unit.source))
          << "code unit time scale is incompatible with design precision";
      invalid = true;
      continue;
    }
    NamedAttribute delayScaleAttr = builder.getNamedAttr(
        delayScaleAttrName,
        builder.getI64IntegerAttr(timeUnitFs / designPrecisionFs));
    NamedAttribute delayQuantumAttr = builder.getNamedAttr(
        delayQuantumAttrName,
        builder.getI64IntegerAttr(timePrecisionFs / designPrecisionFs));
    SmallVector<NamedAttribute> functionAttrs{
        bindingAttr, delayScaleAttr, delayQuantumAttr,
        builder.getNamedAttr("code_unit_id",
                             builder.getI64IntegerAttr(unit.id))};
    if (unit.entryKind == sim::EntryKind::Observer)
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.observer_result",
          builder.getI32IntegerAttr(
              static_cast<uint32_t>(unit.observerResult))));
    if (unit.entryKind == sim::EntryKind::Observer) {
      std::optional<unsigned> width =
          results.empty() ? std::nullopt
                          : sim::getPackedWidth(results.front());
      if (!width) {
        emitError(getSemanticLocation(unit.source))
            << "observer result width is not fixed";
        invalid = true;
        continue;
      }
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.observer_width",
          builder.getI32IntegerAttr(*width)));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.observer_four_state",
          builder.getBoolAttr(isa<sim::LogicType>(results.front()))));
    }
    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      SmallVector<Attribute> dpiInputs;
      SmallVector<Attribute> dpiCopyOuts;
      auto makeABI = [&](Type type, sim::DPIArgumentDirection direction,
                         Location location) -> FailureOr<sim::DPIABIAttr> {
        FailureOr<DPIABIType> classified =
            classifyDPIABIType(type, location);
        if (failed(classified))
          return failure();
        return sim::DPIABIAttr::get(
            context, static_cast<sim::DPIABIKind>(classified->kind),
            direction, classified->width, classified->fourState,
            classified->isSigned);
      };
      for (Operation *child : getChildren(unit.source)) {
        auto formal =
            dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
        if (!formal)
          continue;
        std::optional<Type> semanticType = formal.getSemanticType();
        if (!semanticType) {
          formal.emitError("DPI formal has no semantic ABI type");
          invalid = true;
          continue;
        }
        sim::DPIArgumentDirection direction =
            static_cast<sim::DPIArgumentDirection>(formal.getDirection());
        FailureOr<sim::DPIABIAttr> input =
            makeABI(*semanticType, direction, getSemanticLocation(formal));
        if (failed(input)) {
          invalid = true;
          continue;
        }
        dpiInputs.push_back(*input);
        if (direction != sim::DPIArgumentDirection::Input)
          dpiCopyOuts.push_back(sim::DPIABIAttr::get(
              context, input->getKind(), sim::DPIArgumentDirection::Output,
              input->getWidth(), input->getFourState(),
              input->getIsSigned()));
      }
      SmallVector<Attribute> dpiSignature(dpiInputs);
      if (subroutine.getSubroutineKind() ==
          semantic::SVSubroutineKind::Function) {
        auto semanticType =
            unit.source->getAttrOfType<TypeAttr>("semantic_type");
        auto subroutineType =
            semanticType
                ? dyn_cast<semantic::SubroutineType>(
                      semanticType.getValue())
                : semantic::SubroutineType{};
        auto sourceSignature =
            subroutineType
                ? dyn_cast<FunctionType>(subroutineType.getSignature())
                : FunctionType{};
        if (!sourceSignature || sourceSignature.getNumResults() != 1) {
          emitError(getSemanticLocation(unit.source))
              << "DPI function has no resolved result signature";
          invalid = true;
        } else {
          FailureOr<sim::DPIABIAttr> result =
              makeABI(sourceSignature.getResult(0),
                      sim::DPIArgumentDirection::Result,
                      getSemanticLocation(unit.source));
          if (failed(result))
            invalid = true;
          else
            dpiSignature.push_back(*result);
        }
      }
      llvm::append_range(dpiSignature, dpiCopyOuts);
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.dpi_import",
                               builder.getUnitAttr()));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_c_identifier",
          subroutine.getDpiCIdentifierAttr()));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_scope_id",
          builder.getI64IntegerAttr(getScopeId(unit.source))));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_import_id",
          builder.getI32IntegerAttr(
              getStableImportID(
                  subroutine.getDpiCIdentifierAttr().getValue()))));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_abi_signature",
          builder.getArrayAttr(dpiSignature)));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_logical_inputs",
          builder.getI32IntegerAttr(dpiInputs.size())));
      sim::SimCodeUnitDeclOp declaration =
          codeUnitDeclarations.lookup(unit.source);
      declaration->setAttr("obelisk_sim.dpi_import",
                           builder.getUnitAttr());
      declaration->setAttr("obelisk_sim.dpi_c_identifier",
                           subroutine.getDpiCIdentifierAttr());
      declaration->setAttr(
          "obelisk_sim.dpi_import_id",
          builder.getI32IntegerAttr(
              getStableImportID(
                  subroutine.getDpiCIdentifierAttr().getValue())));
      declaration->setAttr("obelisk_sim.dpi_abi_signature",
                           builder.getArrayAttr(dpiSignature));
      declaration->setAttr("obelisk_sim.dpi_logical_inputs",
                           builder.getI32IntegerAttr(dpiInputs.size()));
      if (subroutine.getSubroutineKind() ==
          semantic::SVSubroutineKind::Task)
        declaration->setAttr("obelisk_sim.dpi_task",
                             builder.getUnitAttr());
      if (subroutine.getIsPure().value_or(false))
        functionAttrs.push_back(builder.getNamedAttr(
            "obelisk_sim.dpi_pure", builder.getUnitAttr()));
      if (subroutine.getIsDpiContext().value_or(false))
        functionAttrs.push_back(builder.getNamedAttr(
            "obelisk_sim.dpi_context", builder.getUnitAttr()));
      if (subroutine.getSubroutineKind() ==
          semantic::SVSubroutineKind::Task)
        functionAttrs.push_back(builder.getNamedAttr(
            "obelisk_sim.dpi_task", builder.getUnitAttr()));
    }
    if (isa<semantic::SVPortConnectionOp>(unit.source))
      functionAttrs.push_back(
          builder.getNamedAttr("internal", builder.getUnitAttr()));
    StringRef hierarchy = isa<semantic::SVPortConnectionOp>(unit.source)
                              ? getHierarchyName(unit.source->getParentOp())
                              : getHierarchyName(unit.source);
    if (!hierarchy.empty())
      functionAttrs.push_back(builder.getNamedAttr(
          sim::metadata::hierarchicalName, builder.getStringAttr(hierarchy)));
    bool programDomain = isProgramCodeUnit(unit.source);
    functionAttrs.push_back(builder.getNamedAttr(
        "home_region",
        sim::EventRegionAttr::get(
            context, programDomain ? sim::EventRegion::Reactive
                                   : sim::EventRegion::Active)));
    functionAttrs.push_back(builder.getNamedAttr(
        "domain", sim::ExecutionDomainAttr::get(
                      context, programDomain ? sim::ExecutionDomain::Program
                                             : sim::ExecutionDomain::Design)));
    unit.function = sim::SimFuncOp::create(
        builder, getSemanticLocation(unit.source), unit.symbol, type,
        unit.entryKind, functionAttrs, argAttrs);
    SymbolTable::setSymbolVisibility(unit.function,
                                     SymbolTable::Visibility::Private);

    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      unit.function.getBody().getBlocks().clear();
      continue;
    }

    OpBuilder bodyBuilder =
        OpBuilder::atBlockEnd(&unit.function.getBody().front());
    if (unit.entryKind == sim::EntryKind::Observer ||
        isa<semantic::SVPortConnectionOp>(unit.source)) {
      bodyBuilder.clone(*unit.source);
    } else {
      for (Operation *child : getChildren(unit.source)) {
        if (isa<semantic::SVFormalArgumentSymbolOp,
                semantic::SVVariableSymbolOp,
                semantic::SVStatementBlockSymbolOp>(child))
          continue;
        bodyBuilder.clone(*child);
      }
    }
    unit.function.walk([&](Operation *nested) {
      if (auto call = dyn_cast<semantic::SVCallExpressionOp>(nested)) {
        std::optional<StringRef> path = call.getReferencedPath();
        if (!path)
          return;
        auto target = directCallees.find(*path);
        if (target == directCallees.end())
          return;
        call->setAttr(calleeAttrName,
                      FlatSymbolRefAttr::get(context, target->second));
        SmallVector<Attribute> capturePaths;
        Operation *targetSource = directCalleeSources[*path];
        bool dpiTarget = false;
        if (auto subroutine =
                dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
            subroutine && subroutine.getIsDpiImport().value_or(false)) {
          dpiTarget = true;
          StringAttr cIdentifier = subroutine.getDpiCIdentifierAttr();
          call->setAttr("obelisk.dpi.import_id",
                        builder.getI32IntegerAttr(
                            getStableImportID(cIdentifier.getValue())));
          call->setAttr("obelisk.dpi.c_identifier", cIdentifier);
          call->setAttr("obelisk.dpi.scope_id",
                        builder.getI64IntegerAttr(getScopeId(targetSource)));
          call->setAttr(
              "obelisk.dpi.is_pure",
              builder.getBoolAttr(subroutine.getIsPure().value_or(false)));
          call->setAttr("obelisk.dpi.is_context",
                        builder.getBoolAttr(
                            subroutine.getIsDpiContext().value_or(false)));
          call->setAttr(
              "obelisk.dpi.is_task",
              builder.getBoolAttr(subroutine.getSubroutineKind() ==
                                  semantic::SVSubroutineKind::Task));
        }
        if (auto subroutine =
                dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
            subroutine && !subroutine.getIsDpiImport().value_or(false) &&
            subroutine.getSubroutineKind() ==
                semantic::SVSubroutineKind::Task)
          call->setAttr("obelisk_sim.is_task", builder.getUnitAttr());
        SmallVector<Attribute> readCapturePaths;
        for (auto &capture : unitCaptures[targetSource]) {
          capturePaths.push_back(builder.getStringAttr(capture.first));
          if (unitReadCaptures[targetSource].contains(capture.first))
            readCapturePaths.push_back(
                builder.getStringAttr(capture.first));
        }
        call->setAttr(calleeCapturesAttrName,
                      builder.getArrayAttr(capturePaths));
        call->setAttr(calleeReadCapturesAttrName,
                      builder.getArrayAttr(readCapturePaths));
        // One dictionary per callee formal keeps the direction, normalized
        // type, and signedness of the frozen signature together.
        SmallVector<Attribute> formals;
        for (Operation *targetChild : getChildren(targetSource)) {
          auto formal =
              dyn_cast<semantic::SVFormalArgumentSymbolOp>(targetChild);
          if (!formal)
            continue;
          FailureOr<Type> formalType = getNormalizedSemanticType(formal);
          if (failed(formalType)) {
            invalid = true;
            continue;
          }
          std::optional<Type> semanticType = formal.getSemanticType();
          SmallVector<NamedAttribute> formalAttrs{
              builder.getNamedAttr(
                  "direction", builder.getI64IntegerAttr(static_cast<int64_t>(
                                   formal.getDirection()))),
              builder.getNamedAttr("type", TypeAttr::get(*formalType)),
              builder.getNamedAttr(
                  "is_signed",
                  builder.getBoolAttr(semanticType &&
                                      isSignedSemanticType(*semanticType))),
          };
          if (dpiTarget && semanticType) {
            FailureOr<DPIABIKind> category =
                getDPIABIKind(*semanticType, getSemanticLocation(formal));
            if (failed(category)) {
              invalid = true;
              continue;
            }
            formalAttrs.push_back(builder.getNamedAttr(
                "dpi_category",
                builder.getI32IntegerAttr(static_cast<uint32_t>(*category))));
          }
          formals.push_back(builder.getDictionaryAttr(formalAttrs));
        }
        call->setAttr(calleeFormalsAttrName, builder.getArrayAttr(formals));
        return;
      }
      if (auto declaration =
              dyn_cast<semantic::SVVariableDeclStatementOp>(nested)) {
        auto symbol = semanticSymbols.find(
            declaration.getReferencedSymbol().getLeafReference());
        if (symbol == semanticSymbols.end())
          return;
        SmallVector<Operation *> initializer = getChildren(symbol->second);
        if (initializer.empty())
          return;
        OpBuilder declarationBuilder =
            OpBuilder::atBlockEnd(&declaration->getRegion(0).front());
        declarationBuilder.clone(*initializer.front());
      }
    });
    if (unit.entryKind != sim::EntryKind::Function &&
        unit.entryKind != sim::EntryKind::Observer) {
      sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(unit.source),
                               ValueRange{});
    } else {
      auto placeholder = UnrealizedConversionCastOp::create(
          bodyBuilder, getSemanticLocation(unit.source), type.getResults(),
          ValueRange{});
      placeholder->setAttr(placeholderAttrName, builder.getUnitAttr());
      sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(unit.source),
                               placeholder.getResults());
    }
  }
  if (invalid)
    return abort();

  OpBuilder rootBuilder =
      OpBuilder::atBlockEnd(&rootInitializer.getBody().front());
  Value simContext = rootInitializer.getBody().front().getArgument(0);
  for (UnitInfo &unit : units) {
    if (unit.entryKind == sim::EntryKind::Function ||
        unit.entryKind == sim::EntryKind::Task ||
        unit.entryKind == sim::EntryKind::Observer)
      continue;
    SmallVector<Value> operands{simContext};
    for (unsigned index = 1; index < unit.function.getNumArguments(); ++index) {
      DictionaryAttr attrs = unit.function.getArgAttrDict(index);
      auto kind = dyn_cast_or_null<sim::CaptureKindAttr>(
          attrs ? attrs.get(captureKindAttrName) : Attribute{});
      auto descriptor = attrs ? attrs.getAs<IntegerAttr>(descriptorIdAttrName)
                              : IntegerAttr{};
      if (!kind || !descriptor) {
        unit.function.emitError()
            << "process entry argument #" << index
            << " has no descriptor capture metadata to spawn with";
        invalid = true;
        break;
      }
      uint64_t id = descriptor.getValue().getZExtValue();
      Type type = unit.function.getArgumentTypes()[index];
      Location loc = unit.function.getLoc();
      switch (kind.getValue()) {
      case sim::CaptureKind::Storage: {
        auto rootTypeAttr =
            attrs.getAs<TypeAttr>(sim::metadata::descriptorRootType);
        Type contextType =
            rootTypeAttr
                ? Type(sim::RefType::get(context, rootTypeAttr.getValue()))
                : type;
        Value storage = sim::SimContextStorageOp::create(
                            rootBuilder, loc, contextType, simContext,
                            rootBuilder.getI64IntegerAttr(id))
                            .getResult();
        if (rootTypeAttr) {
          auto low =
              attrs.getAs<IntegerAttr>(sim::metadata::descriptorLow);
          if (!low) {
            unit.function.emitError()
                << "view capture is missing its descriptor offset";
            invalid = true;
            break;
          }
          if (auto indices =
                  attrs.getAs<DenseI64ArrayAttr>(
                      sim::metadata::descriptorIndices)) {
            auto aggregateType = attrs.getAs<TypeAttr>(
                sim::metadata::descriptorAggregateType);
            if (!aggregateType) {
              unit.function.emitError()
                  << "aggregate view capture is missing its result type";
              invalid = true;
              break;
            }
            Type resultType =
                sim::RefType::get(context, aggregateType.getValue());
            storage = sim::SimRefSubelementOp::create(
                          rootBuilder, loc, resultType, storage, indices)
                          .getResult();
          }
          if (storage.getType() != type) {
            auto packedLow = attrs.getAs<IntegerAttr>(
                sim::metadata::descriptorPackedLow);
            if (!packedLow) {
              unit.function.emitError()
                  << "packed view capture is missing its bit offset";
              invalid = true;
              break;
            }
            storage = sim::SimRefExtractOp::create(
                          rootBuilder, loc, type, storage, packedLow)
                          .getResult();
          }
        }
        operands.push_back(storage);
        break;
      }
      case sim::CaptureKind::Net:
        operands.push_back(
            sim::SimContextNetOp::create(rootBuilder, loc, type, simContext,
                                         rootBuilder.getI64IntegerAttr(id))
                .getResult());
        break;
      case sim::CaptureKind::Driver:
        operands.push_back(
            sim::SimContextDriverOp::create(rootBuilder, loc, type, simContext,
                                            rootBuilder.getI64IntegerAttr(id))
                .getResult());
        break;
      case sim::CaptureKind::Event:
        operands.push_back(
            sim::SimContextEventOp::create(rootBuilder, loc, type, simContext,
                                           rootBuilder.getI64IntegerAttr(id))
                .getResult());
        break;
      case sim::CaptureKind::Context:
      case sim::CaptureKind::Formal:
      case sim::CaptureKind::Value:
        unit.function.emitError()
            << "process entry argument #" << index
            << " cannot be materialized by the root initializer";
        invalid = true;
        break;
      }
    }
    if (invalid)
      return abort();
    sim::SimSpawnOp::create(rootBuilder, unit.function.getLoc(),
                            sim::ProcessType::get(context),
                            FlatSymbolRefAttr::get(context, unit.symbol),
                            operands, ArrayAttr{}, ArrayAttr{});
  }
  sim::SimReturnOp::create(rootBuilder, module.getLoc(), ValueRange{});
}

} // namespace
} // namespace obelisk
