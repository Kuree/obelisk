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
#include "PrepareDeclarations.h"
#include "PrepareNetTopology.h"
#include "PrepareTopology.h"
#include "PrepareValidation.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/ForeachLoopMetadata.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"
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
    auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(ancestor);
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
    auto foreach = dyn_cast<semantic::SVForeachLoopStatementOp>(ancestor);
    if (!foreach)
      continue;
    SmallVector<Operation *> children = getChildren(foreach);
    return children.size() == 2 &&
           containsNestedOperation(children.front(), reference) &&
           !foreach_metadata::hasRuntimeDimension(foreach.getLoopDimensions());
  }
  return false;
}

static std::optional<uint64_t> getUnsigned64(IntegerAttr attribute) {
  if (!attribute || attribute.getValue().isNegative() ||
      attribute.getValue().getActiveBits() > 64)
    return std::nullopt;
  return attribute.getValue().getZExtValue();
}

static uint32_t getStableImportID(StringRef cIdentifier) {
  uint64_t hash = obelisk_stable_hash(cIdentifier.data(), cIdentifier.size());
  uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32));
  return result == 0 ? 1 : result;
}

static semantic::SVClassTypeOp getOwningClass(Operation *member) {
  for (Operation *parent = member ? member->getParentOp() : nullptr; parent;
       parent = parent->getParentOp())
    if (auto classType = dyn_cast<semantic::SVClassTypeOp>(parent))
      return classType;
  return {};
}

struct UnitInfo {
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
    return !children.empty() && isAddressableTimingExpression(children.front());
  }
  if (!isa<semantic::SVElementSelectExpressionOp,
           semantic::SVRangeSelectExpressionOp>(op))
    return false;
  SmallVector<Operation *> children = getChildren(op);
  size_t expected = isa<semantic::SVElementSelectExpressionOp>(op) ? 2u : 3u;
  if (children.size() != expected ||
      !isAddressableTimingExpression(children.front()))
    return false;
  return llvm::all_of(
      ArrayRef<Operation *>(children).drop_front(), [](Operation *index) {
        return isa<semantic::SVIntegerLiteralOp,
                   semantic::SVUnbasedUnsizedIntegerLiteralOp>(index);
      });
}

static FailureOr<sim::EntryKind> getEntryKind(Operation *op) {
  if (isa<semantic::SVVariableSymbolOp>(op))
    return sim::EntryKind::Function;
  if (auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(op);
      property &&
      property.getLifetime() == semantic::SVVariableLifetime::Static)
    return sim::EntryKind::Function;
  if (isa<semantic::SVNetSymbolOp>(op))
    return sim::EntryKind::Continuous;
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
  auto reference = instance->getAttrOfType<SymbolRefAttr>("referenced_symbol");
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
  auto referencedPath = instance->getAttrOfType<StringAttr>("referenced_path");
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
  if (isa<semantic::SVVariableSymbolOp, semantic::SVClassPropertySymbolOp>(op))
    return (getHierarchyName(op) + ".$static_initializer").str();
  if (isa<semantic::SVNetSymbolOp>(op))
    return (getHierarchyName(op) + ".$net_initializer").str();
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
  return (lexical + ".$code_unit_" + Twine(nodeID.getValue().getZExtValue()))
      .str();
}

class ObeliskSimPreparePass
    : public impl::ObeliskSimPreparePassBase<ObeliskSimPreparePass> {
public:
  void runOnOperation() override;
};

void ObeliskSimPreparePass::runOnOperation() {
  ModuleOp module = getOperation();
  MLIRContext *context = &getContext();

  FailureOr<ValidatedSemanticDesign> validated = validateSemanticDesign(module);
  if (failed(validated)) {
    signalPassFailure();
    return;
  }
  semantic::SVRootSymbolOp semanticRoot = validated->root;
  llvm::StringMap<Operation *> &semanticSymbols = validated->symbols;
  bool invalid = false;

  SmallVector<Operation *> sourceUnits;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(op);
        subroutine && subroutine.getIsBuiltin().value_or(false))
      return;
    auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(op);
    bool staticInitializer =
        property &&
        property.getLifetime() == semantic::SVVariableLifetime::Static &&
        !getChildren(property).empty();
    auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op);
    bool designInitializer = variable && !isNestedInCodeUnit(variable) &&
                             !isAutomaticLocalSymbol(variable) &&
                             !getChildren(variable).empty();
    auto net = dyn_cast<semantic::SVNetSymbolOp>(op);
    bool netInitializer = net && !getChildren(net).empty();
    if (isCodeUnit(op) || staticInitializer || designInitializer ||
        netInitializer)
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
    } else if (auto disable = dyn_cast<semantic::SVDisableStatementOp>(op)) {
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
        else if (auto disable = dyn_cast<semantic::SVDisableStatementOp>(op))
          path = disable.getTargetPathAttr();
      } else if (auto declaration =
                     dyn_cast<semantic::SVVariableDeclStatementOp>(op)) {
        path = StringAttr::get(context, declaration.getReferencedPath());
      }
      if (path)
        op->setAttr(attrName, IntegerAttr::get(IntegerType::get(context, 64),
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

  if (failed(materializeCovergroupDeclarations(semanticRoot, builder))) {
    abort();
    return;
  }

  FailureOr<PreparedClassDeclarations> classes = materializeClassDeclarations(
      module, design, semanticRoot, builder, semanticSymbols);
  if (failed(classes)) {
    abort();
    return;
  }
  auto &classSources = classes->sources;
  auto &classSymbols = classes->symbols;
  auto &classFieldSymbols = classes->fieldSymbols;
  auto &classMethodSymbols = classes->methodSymbols;
  auto &implicitConstructorSymbols = classes->implicitConstructorSymbols;
  auto &virtualMethodSlots = classes->virtualMethodSlots;
  auto &virtualMethodSignatures = classes->virtualMethodSignatures;
  auto &semanticClasses = classes->semanticClasses;

  FailureOr<PreparedScopeDeclarations> scopes = materializeScopeDeclarations(
      semanticRoot, sourceUnits, designPrecisionFs, builder);
  if (failed(scopes)) {
    abort();
    return;
  }
  auto getScopeId = [&](Operation *operation) {
    return scopes->lookup(operation);
  };

  FailureOr<PreparedPortAliases> portAliases = analyzePortAliases(semanticRoot);
  if (failed(portAliases)) {
    abort();
    return;
  }
  auto &portConnections = portAliases->connections;

  FailureOr<llvm::StringMap<DescriptorInfo>> preparedDescriptors =
      materializeDesignDescriptors(module, semanticRoot, *portAliases, *scopes,
                                   builder);
  if (failed(preparedDescriptors))
    return abort();
  llvm::StringMap<DescriptorInfo> &descriptors = *preparedDescriptors;

  FailureOr<ContinuousDriverMap> preparedNetTopology =
      materializeNetTopology(sourceUnits, portConnections, semanticSymbols,
                             descriptors, *scopes, builder);
  if (failed(preparedNetTopology))
    return abort();
  ContinuousDriverMap &continuousDrivers = *preparedNetTopology;

  SmallVector<UnitInfo> units;
  units.reserve(sourceUnits.size());
  llvm::StringMap<std::string> directCallees;
  llvm::StringMap<Operation *> directCalleeSources;
  llvm::DenseMap<Operation *, std::string> directCalleeNames;
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
    if ((*entryKind == sim::EntryKind::Function ||
         *entryKind == sim::EntryKind::Task) &&
        !isa<semantic::SVVariableSymbolOp, semantic::SVClassPropertySymbolOp>(
            source)) {
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
    uint64_t id = stableCodeUnitID(codeUnitHierarchy);
    auto [collision, inserted] = codeUnitIDs.try_emplace(id, source);
    if (!inserted) {
      emitError(getSemanticLocation(source))
          << "stable code-unit ID collision for '" << codeUnitHierarchy << "'";
      emitRemark(getSemanticLocation(collision->second))
          << "colliding code unit is here";
      invalid = true;
      continue;
    }
    units.push_back({source,
                     id,
                     *entryKind,
                     symbol,
                     std::move(codeUnitHierarchy),
                     {},
                     ObserverResult::None});
    if (!hierarchy.empty() &&
        !isa<semantic::SVPortConnectionOp, semantic::SVVariableSymbolOp,
             semantic::SVNetSymbolOp, semantic::SVClassPropertySymbolOp>(
            source)) {
      directCallees[hierarchy] = symbol;
      directCalleeSources[hierarchy] = source;
      directCalleeNames[source] = symbol;
    }
  }
  if (invalid)
    return abort();
  auto resolveDirectCallee =
      [&](semantic::SVCallExpressionOp call) -> Operation * {
    // Prefer the elaborator's symbol identity. Paths are retained as a
    // compatibility fallback, but can differ in spelling for defaulted
    // constructors and out-of-block class methods.
    if (SymbolRefAttr reference = call.getReferencedSymbolAttr()) {
      auto symbol = semanticSymbols.find(reference.getLeafReference());
      if (symbol != semanticSymbols.end() &&
          directCalleeNames.count(symbol->second))
        return symbol->second;
    }
    if (std::optional<StringRef> path = call.getReferencedPath()) {
      auto source = directCalleeSources.find(*path);
      if (source != directCalleeSources.end())
        return source->second;
    }
    return nullptr;
  };
  llvm::DenseMap<Operation *, uint64_t> unitIDs;
  for (const UnitInfo &unit : units)
    unitIDs[unit.source] = unit.id;

  // Give every potentially computed timing expression a private evaluator
  // identity while the complete semantic tree and collision set are still
  // available. Direct controls retain their existing lowering and the unused
  // evaluator is removed by symbol DCE.
  struct ObserverCandidate {
    Operation *expression;
    ObserverResult result;
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
          observerCandidates.push_back({children.front(), ObserverResult::Truth,
                                        "wait", unit.id, unit.hierarchy});
        return;
      }
      auto event = dyn_cast<semantic::SVSignalEventControlOp>(nested);
      if (!event)
        return;
      SmallVector<Operation *> children = getChildren(event);
      if (children.empty())
        return;
      ObserverResult primaryResult = ObserverResult::Value;
      FailureOr<Type> primaryType = getNormalizedSemanticType(children.front());
      if (succeeded(primaryType) && isa<sim::EventType>(*primaryType))
        primaryResult = ObserverResult::Event;
      observerCandidates.push_back({children.front(), primaryResult, "primary",
                                    unit.id, unit.hierarchy});
      if (event.getHasIff() && children.size() == 2)
        observerCandidates.push_back({children[1], ObserverResult::Truth, "iff",
                                      unit.id, unit.hierarchy});
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
    std::string hierarchy = (Twine(candidate.parentHierarchy) + ".$observer." +
                             Twine(ordinal) + "." + candidate.label)
                                .str();
    uint64_t id = stableCodeUnitID(hierarchy);
    auto [collision, inserted] =
        codeUnitIDs.try_emplace(id, candidate.expression);
    if (!inserted) {
      emitError(getSemanticLocation(candidate.expression))
          << "stable observer code-unit ID collision for '" << hierarchy << "'";
      emitRemark(getSemanticLocation(collision->second))
          << "colliding code unit is here";
      invalid = true;
      continue;
    }
    std::string symbol =
        llvm::formatv("observer_{0}_{1}", candidate.parentID, ordinal).str();
    candidate.expression->setAttr("obelisk_sim.observer",
                                  FlatSymbolRefAttr::get(context, symbol));
    candidate.expression->setAttr(
        observerResultAttrName,
        builder.getI32IntegerAttr(static_cast<uint32_t>(candidate.result)));
    units.push_back({candidate.expression,
                     id,
                     sim::EntryKind::Observer,
                     std::move(symbol),
                     std::move(hierarchy),
                     {},
                     candidate.result});
  }
  if (invalid)
    return abort();

  uint64_t rootCodeUnitID = stableCodeUnitID("__obelisk_root");
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
        fork &&
        fork.getBlockKind() != semantic::SVStatementBlockKind::Sequential) {
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
        uint64_t id = stableCodeUnitID(hierarchy);
        auto [collision, inserted] = codeUnitIDs.try_emplace(id, branch);
        if (!inserted) {
          emitError(getSemanticLocation(branch))
              << "stable fork code-unit ID collision for '" << hierarchy << "'";
          emitRemark(getSemanticLocation(collision->second))
              << "colliding code unit is here";
          invalid = true;
          continue;
        }
        branch->setAttr("obelisk_sim.fork_code_unit_id",
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
  struct ConstantInfo {
    std::string path;
    sim::FrozenConstantAttr value;
  };
  llvm::DenseMap<Operation *, SmallVector<LocalInfo>> unitLocals;
  llvm::DenseMap<Operation *, SmallVector<ConstantInfo>> unitConstants;
  llvm::DenseMap<Operation *, SmallVector<LocalInfo>> observerLocalCaptures;
  llvm::DenseMap<Operation *, llvm::StringSet<>> observerReadLocals;
  for (UnitInfo &unit : units) {
    llvm::StringSet<> seenPaths;
    llvm::StringSet<> seenLocals;
    llvm::StringSet<> seenConstants;
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
      Operation *referencedSymbol = nullptr;
      if (reference) {
        auto symbol = semanticSymbols.find(reference.getLeafReference());
        if (symbol != semanticSymbols.end())
          referencedSymbol = symbol->second;
      }
      // Function formals are already represented by the function's public
      // argument bindings. Capturing descriptor-backed static storage too
      // would create two providers for the same path. Static tasks retain
      // their descriptor capture because they may suspend and overlap; task
      // lowering copies each activation's formal into that shared storage.
      if (unit.entryKind == sim::EntryKind::Function &&
          isa_and_nonnull<semantic::SVFormalArgumentSymbolOp>(referencedSymbol))
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
      if (!referencedSymbol)
        return;
      if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
              semantic::SVSpecparamSymbolOp>(referencedSymbol)) {
        // An unbounded parameter has no runtime integer representation. It is
        // only meaningful to unevaluated inquiry functions such as
        // `$isunbounded`, which inspect the elaborated operand type directly.
        // Do not try to freeze its "$" spelling as an integer constant.
        if (auto type = nested->getAttrOfType<TypeAttr>("semantic_type");
            type && isa<ir::UnboundedType>(type.getValue()))
          return;
        if (!seenConstants.insert(path).second)
          return;
        FailureOr<sim::FrozenConstantAttr> value =
            freezeSemanticConstant(referencedSymbol);
        if (failed(value)) {
          invalid = true;
          return;
        }
        unitConstants[unit.source].push_back({path.str(), *value});
        return;
      }
      if (!isa<semantic::SVVariableSymbolOp, semantic::SVPatternVarSymbolOp>(
              referencedSymbol) &&
          !(unit.entryKind == sim::EntryKind::Observer &&
            isa<semantic::SVFormalArgumentSymbolOp>(referencedSymbol)))
        return;
      FailureOr<Type> type = getNormalizedSemanticType(referencedSymbol);
      if (failed(type)) {
        invalid = true;
        return;
      }
      if (seenLocals.insert(path).second) {
        auto &destination = unit.entryKind == sim::EntryKind::Observer
                                ? observerLocalCaptures[unit.source]
                                : unitLocals[unit.source];
        destination.push_back(
            {path.str(), *type, isAutomaticLocalSymbol(referencedSymbol),
             isa<semantic::SVPatternVarSymbolOp>(referencedSymbol)});
        referencedSymbol->walk<WalkOrder::PreOrder>(
            [&](Operation *initializerNode) {
              collectBinding(initializerNode);
            });
      }
      if (unit.entryKind == sim::EntryKind::Observer &&
          !isWriteOnlyReferenceUse(nested))
        observerReadLocals[unit.source].insert(path);
    };
    bool initializesDesignStorage =
        isa<semantic::SVVariableSymbolOp>(unit.source);
    if (auto property =
            dyn_cast<semantic::SVClassPropertySymbolOp>(unit.source))
      initializesDesignStorage =
          property.getLifetime() == semantic::SVVariableLifetime::Static;
    if (initializesDesignStorage) {
      StringRef path = getHierarchyName(unit.source);
      auto descriptor = descriptors.find(path);
      if (descriptor == descriptors.end()) {
        emitError(getSemanticLocation(unit.source))
            << "design initializer has no storage descriptor";
        invalid = true;
      } else if (seenPaths.insert(path).second) {
        unitCaptures[unit.source].push_back({path.str(), descriptor->second});
      }
    }
    unit.source->walk<WalkOrder::PreOrder>(
        [&](Operation *nested) { collectBinding(nested); });
    // Manual covergroup sampling evaluates the declaration's coverpoint and
    // iff expressions in the caller. Capture any enclosing design storage
    // those expressions read as part of the sampling unit.
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      if (call.getIsSystemCall() || call.getCalleeName() != "sample" ||
          !call.getReferencedSymbol())
        return;
      auto symbol =
          semanticSymbols.find(call.getReferencedSymbol()->getLeafReference());
      if (symbol == semanticSymbols.end())
        return;
      auto covergroup =
          symbol->second->getParentOfType<semantic::SVCovergroupTypeOp>();
      if (!covergroup)
        return;
      covergroup->walk<WalkOrder::PreOrder>(
          [&](Operation *nested) { collectBinding(nested); });
    });
    if (auto connection = dyn_cast<semantic::SVPortConnectionOp>(unit.source)) {
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
      Operation *target = resolveDirectCallee(call);
      if (target && targets.insert(target).second)
        callEdges[unit.source].push_back(target);
    });
  }
  llvm::DenseSet<Operation *> indirectRefTasks;
  for (UnitInfo &unit : units)
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      Operation *target = resolveDirectCallee(call);
      auto task = dyn_cast_or_null<semantic::SVSubroutineSymbolOp>(target);
      if (!task ||
          task.getSubroutineKind() != semantic::SVSubroutineKind::Task ||
          getOwningClass(task))
        return;
      SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
      for (Operation *child : getChildren(task))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          formals.push_back(formal);
      SmallVector<Operation *> actuals = getChildren(call);
      if (actuals.size() != formals.size())
        return;
      for (auto [actual, formal] : llvm::zip_equal(actuals, formals)) {
        if (formal.getDirection() != semantic::SVArgumentDirection::Ref)
          continue;
        auto select = dyn_cast<semantic::SVElementSelectExpressionOp>(actual);
        if (!select)
          continue;
        SmallVector<Operation *> selection = getChildren(select);
        if (selection.size() != 2)
          continue;
        FailureOr<Type> base = getNormalizedSemanticType(selection.front());
        if (succeeded(base) &&
            isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
                *base)) {
          indirectRefTasks.insert(target);
          return;
        }
      }
    });
  SmallVector<std::pair<Operation *, Operation *>> virtualOverrideEdges;
  for (semantic::SVClassTypeOp classType : classSources)
    for (Operation *child : getChildren(classType)) {
      semantic::SVSubroutineSymbolOp method = getClassMethod(child);
      if (!method || !method.getIsVirtual().value_or(false) ||
          !method.getOverrideSymbol())
        continue;
      auto overridden =
          semanticSymbols.find(method.getOverrideSymbol()->getLeafReference());
      if (overridden != semanticSymbols.end())
        virtualOverrideEdges.emplace_back(method, overridden->second);
    }
  bool changed;
  do {
    changed = false;
    auto mergeCaptures = [&](Operation *destination, Operation *source) {
      auto &captures = unitCaptures[destination];
      llvm::StringSet<> seen;
      for (auto &capture : captures)
        seen.insert(capture.first);
      for (auto &capture : unitCaptures[source])
        if (seen.insert(capture.first).second) {
          captures.push_back(capture);
          changed = true;
        }
      for (const auto &read : unitReadCaptures[source])
        changed |= unitReadCaptures[destination].insert(read.getKey()).second;
    };
    // Runtime substitution needs one capture ABI for an entire virtual family.
    // Propagate in both directions so a call through any base, derived, or
    // interface declaration supplies every lexical resource that one of its
    // possible implementations may use. Unused captures are ordinary dead
    // arguments and can be removed after devirtualization.
    for (auto [method, overridden] : virtualOverrideEdges) {
      mergeCaptures(method, overridden);
      mergeCaptures(overridden, method);
    }
    for (UnitInfo &unit : units) {
      for (Operation *target : callEdges[unit.source])
        mergeCaptures(unit.source, target);
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
    llvm::sort(
        unitConstants[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
    llvm::sort(
        observerLocalCaptures[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
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
  auto rootInitializer = sim::SimFuncOp::create(
      builder, module.getLoc(), "__obelisk_root", rootType,
      sim::EntryKind::RootInitializer, rootAttrs, rootArgAttrs);

  auto freezeCallContract = [&](semantic::SVCallExpressionOp call) {
    Operation *targetSource = resolveDirectCallee(call);
    if (!targetSource)
      return;
    auto target = directCalleeNames.find(targetSource);
    assert(target != directCalleeNames.end() &&
           "resolved direct callee has no frozen symbol");
    call->setAttr(calleeAttrName,
                  FlatSymbolRefAttr::get(context, target->second));
    if (auto targetSubroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
        targetSubroutine && getOwningClass(targetSubroutine) &&
        !targetSubroutine.getIsStatic().value_or(false)) {
      call->setAttr("obelisk_sim.class_instance", builder.getUnitAttr());
      if (call.getIsSuperClass())
        call->setAttr("obelisk_sim.class_super", builder.getUnitAttr());
      if (targetSubroutine.getIsVirtual().value_or(false) &&
          !call.getIsSuperClass())
        call->setAttr("obelisk_sim.class_virtual", builder.getUnitAttr());
      if (FlatSymbolRefAttr method =
              classMethodSymbols.lookup(targetSubroutine)) {
        call->setAttr("obelisk_sim.class_method", method);
        if (targetSubroutine.getIsVirtual().value_or(false)) {
          call->setAttr("obelisk_sim.class_slot",
                        builder.getI64IntegerAttr(
                            virtualMethodSlots.lookup(targetSubroutine)));
          call->setAttr("obelisk_sim.class_signature",
                        builder.getI64IntegerAttr(
                            virtualMethodSignatures.lookup(targetSubroutine)));
        }
      }
    }
    SmallVector<Attribute> capturePaths;
    bool dpiTarget = false;
    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      dpiTarget = true;
      StringAttr cIdentifier = subroutine.getDpiCIdentifierAttr();
      call->setAttr(
          "obelisk.dpi.import_id",
          builder.getI32IntegerAttr(getStableImportID(cIdentifier.getValue())));
      call->setAttr("obelisk.dpi.c_identifier", cIdentifier);
      call->setAttr("obelisk.dpi.scope_id",
                    builder.getI64IntegerAttr(getScopeId(targetSource)));
      call->setAttr(
          "obelisk.dpi.is_pure",
          builder.getBoolAttr(subroutine.getIsPure().value_or(false)));
      call->setAttr(
          "obelisk.dpi.is_context",
          builder.getBoolAttr(subroutine.getIsDpiContext().value_or(false)));
      call->setAttr("obelisk.dpi.is_task",
                    builder.getBoolAttr(subroutine.getSubroutineKind() ==
                                        semantic::SVSubroutineKind::Task));
    }
    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
        subroutine && !subroutine.getIsDpiImport().value_or(false) &&
        subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Task)
      call->setAttr("obelisk_sim.is_task", builder.getUnitAttr());
    SmallVector<Attribute> readCapturePaths;
    for (auto &capture : unitCaptures[targetSource]) {
      capturePaths.push_back(builder.getStringAttr(capture.first));
      if (unitReadCaptures[targetSource].contains(capture.first))
        readCapturePaths.push_back(builder.getStringAttr(capture.first));
    }
    call->setAttr(calleeCapturesAttrName, builder.getArrayAttr(capturePaths));
    call->setAttr(calleeReadCapturesAttrName,
                  builder.getArrayAttr(readCapturePaths));
    // One dictionary per callee formal keeps the direction, normalized type,
    // and signedness of the frozen signature together.
    SmallVector<Attribute> formals;
    for (Operation *targetChild : getChildren(targetSource)) {
      auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(targetChild);
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
              "direction", builder.getI64IntegerAttr(
                               static_cast<int64_t>(formal.getDirection()))),
          builder.getNamedAttr("type", TypeAttr::get(*formalType)),
          builder.getNamedAttr(
              "is_signed",
              builder.getBoolAttr(semanticType &&
                                  isSignedSemanticType(*semanticType))),
          builder.getNamedAttr(
              "argument_ref",
              builder.getBoolAttr(formal.getDirection() ==
                                      semantic::SVArgumentDirection::Ref &&
                                  indirectRefTasks.contains(targetSource))),
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
  };

  for (UnitInfo &unit : units) {
    auto captures = unitCaptures.lookup(unit.source);
    auto locals = unitLocals.lookup(unit.source);
    auto constants = unitConstants.lookup(unit.source);
    auto observerLocals = observerLocalCaptures.lookup(unit.source);
    SmallVector<Type> copyOutResultTypes;
    bool instanceClassMethod = false;
    Type classThisType;
    StringRef classThisPath;
    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source)) {
      auto owner = getOwningClass(subroutine);
      instanceClassMethod = owner && !subroutine.getIsStatic().value_or(false);
      if (instanceClassMethod) {
        FailureOr<Type> normalized = getNormalizedSemanticType(owner);
        std::optional<StringRef> path = subroutine.getThisVariablePath();
        bool pure = subroutine.getIsPure().value_or(false);
        if (failed(normalized) || (!path && !pure)) {
          emitError(getSemanticLocation(subroutine))
              << "instance method has no resolved this binding";
          invalid = true;
        } else {
          classThisType = *normalized;
          // Pure prototypes have no executable body and therefore no
          // elaborated `this` variable. They still need the same canonical
          // receiver position in their frozen virtual-method signature.
          classThisPath = path.value_or("__obelisk_pure_this");
        }
      }
    }

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
        metadataAttrs.push_back(
            builder.getNamedAttr(sim::metadata::descriptorRootType,
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
      const DriverInfo *plannedDriver = nullptr;
      if (capture.second.kind == DescriptorInfo::Kind::Driver)
        if (auto found = continuousDrivers.find(unit.source);
            found != continuousDrivers.end())
          if (auto planned = llvm::find_if(found->second,
                                           [&](const DriverInfo &driver) {
                                             return driver.descriptor.id ==
                                                    capture.second.id;
                                           });
              planned != found->second.end())
            plannedDriver = &*planned;
      IntegerAttr lvalueNode =
          plannedDriver && plannedDriver->nodeId
              ? builder.getI64IntegerAttr(*plannedDriver->nodeId)
              : IntegerAttr{};
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(capture.first), captureIndex + 1,
          plannedDriver ? sim::UnitArgumentKind::LValueOnly
                        : sim::UnitArgumentKind::Direct,
          /*copyOut=*/false, lvalueNode));
    }
    for (const LocalInfo &local : locals) {
      auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
      bool isReturn =
          subroutine && subroutine.getReturnVariablePath() == local.path;
      bindings.push_back(sim::LocalBindingAttr::get(
          context, builder.getStringAttr(local.path), local.type,
          local.automatic, local.patternVariable, isReturn));
    }
    for (const ConstantInfo &constant : constants)
      bindings.push_back(sim::ConstantBindingAttr::get(
          context, builder.getStringAttr(constant.path), constant.value));

    for (const LocalInfo &local : observerLocals) {
      unsigned argument = inputs.size();
      inputs.push_back(sim::RefType::get(context, local.type));
      argAttrs.push_back(captureMetadata(builder, sim::CaptureKind::Value));
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(local.path), argument,
          sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{}));
    }

    // Subroutine formals precede non-local captures in the public contract.
    // Function output and inout formals use copy-out results. Task copy-out
    // destinations are hidden reference arguments retained by the activation.
    // Only explicit ref formals otherwise preserve caller aliasing.
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && (unit.entryKind == sim::EntryKind::Function ||
                       unit.entryKind == sim::EntryKind::Task)) {
      bool dpiImport = subroutine.getIsDpiImport().value_or(false);
      bool directTask = unit.entryKind == sim::EntryKind::Task;
      SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
      for (Operation *child : getChildren(unit.source))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          formals.push_back(formal);
      if (instanceClassMethod || !formals.empty()) {
        SmallVector<Type> reordered{inputs.front()};
        SmallVector<DictionaryAttr> reorderedAttrs{argAttrs.front()};
        SmallVector<Attribute> formalBindings;
        if (instanceClassMethod) {
          unsigned argument = reordered.size();
          reordered.push_back(classThisType);
          reorderedAttrs.push_back(
              captureMetadata(builder, sim::CaptureKind::Formal));
          formalBindings.push_back(sim::ArgumentBindingAttr::get(
              context, builder.getStringAttr(classThisPath), argument,
              sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{}));
        }
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
          if (dpiImport && direction == semantic::SVArgumentDirection::Ref) {
            emitError(getSemanticLocation(formal))
                << "DPI ref formals are not supported; use input, output, or "
                   "inout";
            invalid = true;
            continue;
          }
          bool isRef = direction == semantic::SVArgumentDirection::Ref;
          bool indirectRef = indirectRefTasks.contains(unit.source);
          Type argumentType =
              isRef ? (directTask && !instanceClassMethod && !indirectRef
                           ? Type(sim::RefType::get(context, *type))
                           : Type(sim::ArgumentRefType::get(context, *type)))
                    : *type;
          unsigned argument = reordered.size();
          reordered.push_back(argumentType);
          reorderedAttrs.push_back(
              captureMetadata(builder, sim::CaptureKind::Formal));
          // Value formals are callee-local variables. Inputs copy in, outputs
          // and inouts copy out, and mem2reg removes the allocation whenever
          // the local does not escape. Only `ref` preserves caller aliasing.
          bool copyOut = direction == semantic::SVArgumentDirection::Out ||
                         direction == semantic::SVArgumentDirection::InOut;
          if (copyOut && !directTask)
            copyOutResultTypes.push_back(*type);
          formalBindings.push_back(sim::ArgumentBindingAttr::get(
              context, builder.getStringAttr(getHierarchyName(formal)),
              argument,
              isRef ? sim::UnitArgumentKind::Direct
                    : sim::UnitArgumentKind::FormalLocal,
              copyOut, IntegerAttr{}));
          if (directTask && copyOut) {
            unsigned destinationArgument = reordered.size();
            reordered.push_back(sim::RefType::get(context, *type));
            reorderedAttrs.push_back(
                captureMetadata(builder, sim::CaptureKind::Formal));
            formalBindings.push_back(sim::ArgumentBindingAttr::get(
                context, builder.getStringAttr(getHierarchyName(formal)),
                destinationArgument, sim::UnitArgumentKind::CopyOutDestination,
                /*copyOut=*/false, IntegerAttr{}));
          }
        }
        unsigned offset = reordered.size() - 1;
        reordered.append(inputs.begin() + 1, inputs.end());
        reorderedAttrs.append(argAttrs.begin() + 1, argAttrs.end());
        for (Attribute binding : bindings) {
          if (instanceClassMethod) {
            StringRef path =
                TypeSwitch<Attribute, StringRef>(binding)
                    .Case<sim::ArgumentBindingAttr, sim::LocalBindingAttr,
                          sim::ConstantBindingAttr>(
                        [](auto value) { return value.getPath().getValue(); })
                    .Default([](Attribute) { return StringRef{}; });
            if (path == classThisPath)
              continue;
          }
          auto argument = dyn_cast<sim::ArgumentBindingAttr>(binding);
          if (!argument) {
            formalBindings.push_back(binding);
            continue;
          }
          formalBindings.push_back(sim::ArgumentBindingAttr::get(
              context, argument.getPath(), argument.getArgument() + offset,
              argument.getKind(), argument.getCopyOut(),
              argument.getLvalueNode()));
        }
        inputs = std::move(reordered);
        argAttrs = std::move(reorderedAttrs);
        bindings = std::move(formalBindings);
      }
    }

    if (invalid)
      continue;
    SmallVector<Type> results;
    bool isVoidFunction = false;
    if (unit.entryKind == sim::EntryKind::Function) {
      auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
      if (!subroutine) {
        if (!isa<semantic::SVVariableSymbolOp,
                 semantic::SVClassPropertySymbolOp>(unit.source)) {
          emitError(getSemanticLocation(unit.source))
              << "synthetic zero-time function has an unsupported source";
          invalid = true;
        } else {
          isVoidFunction = true;
        }
      } else {
        bool dpiImport = subroutine.getIsDpiImport().value_or(false);
        if (subroutine.getSubroutineKind() ==
                semantic::SVSubroutineKind::Function &&
            !subroutine.getIsConstructor().value_or(false)) {
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
          } else {
            auto semanticType =
                unit.source->getAttrOfType<TypeAttr>("semantic_type");
            auto subroutineType = semanticType
                                      ? dyn_cast<semantic::SubroutineType>(
                                            semanticType.getValue())
                                      : semantic::SubroutineType{};
            auto signature =
                subroutineType
                    ? dyn_cast<FunctionType>(subroutineType.getSignature())
                    : FunctionType{};
            if (!signature || signature.getNumResults() != 1) {
              emitError(getSemanticLocation(unit.source))
                  << (dpiImport
                          ? "DPI function has no resolved return signature"
                          : "function is missing its elaborated return "
                            "variable "
                            "and resolved return signature");
              invalid = true;
              continue;
            }
            semanticResultType = signature.getResult(0);
            if (!isa<semantic::VoidType>(semanticResultType))
              resultType = normalizeSemanticType(
                  semanticResultType, getSemanticLocation(unit.source));
          }
          bool voidResult = !dpiImport && isa_and_nonnull<semantic::VoidType>(
                                              semanticResultType);
          isVoidFunction = voidResult;
          if (!voidResult && failed(resultType)) {
            invalid = true;
            continue;
          }
          if (!voidResult && dpiImport && !sim::getPackedWidth(*resultType)) {
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
          if (!voidResult)
            results.push_back(*resultType);
        }
      }
    } else if (unit.entryKind == sim::EntryKind::Observer) {
      Type resultType;
      if (unit.observerResult == ObserverResult::Truth ||
          unit.observerResult == ObserverResult::Event) {
        resultType = builder.getI1Type();
      } else {
        FailureOr<Type> normalized = getNormalizedSemanticType(unit.source);
        if (failed(normalized)) {
          invalid = true;
          continue;
        }
        resultType = isa<FloatType>(*normalized)
                         ? *normalized
                         : sim::getPackedScalarType(*normalized);
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
    if (instanceClassMethod)
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.this_argument", builder.getI32IntegerAttr(1)));
    if (isVoidFunction)
      functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.void_function",
                                                   builder.getUnitAttr()));
    if (isa<semantic::SVClassPropertySymbolOp>(unit.source))
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.static_initializer", builder.getUnitAttr()));
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsConstructor().value_or(false))
      functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.constructor",
                                                   builder.getUnitAttr()));
    if (unit.entryKind == sim::EntryKind::Observer)
      functionAttrs.push_back(
          builder.getNamedAttr(observerResultAttrName,
                               builder.getI32IntegerAttr(static_cast<uint32_t>(
                                   unit.observerResult))));
    if (unit.entryKind == sim::EntryKind::Observer) {
      std::optional<unsigned> width =
          results.empty() ? std::nullopt
          : isa<FloatType>(results.front())
              ? std::optional<unsigned>(
                    cast<FloatType>(results.front()).getWidth())
              : sim::getPackedWidth(results.front());
      if (!width) {
        emitError(getSemanticLocation(unit.source))
            << "observer result width is not fixed";
        invalid = true;
        continue;
      }
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.observer_width", builder.getI32IntegerAttr(*width)));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.observer_four_state",
          builder.getBoolAttr(isa<sim::LogicType>(results.front()))));
    }
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      SmallVector<Attribute> dpiInputs;
      SmallVector<Attribute> dpiCopyOuts;
      auto makeABI = [&](Type type, sim::DPIArgumentDirection direction,
                         Location location) -> FailureOr<sim::DPIABIAttr> {
        FailureOr<DPIABIType> classified = classifyDPIABIType(type, location);
        if (failed(classified))
          return failure();
        return sim::DPIABIAttr::get(
            context, static_cast<sim::DPIABIKind>(classified->kind), direction,
            classified->width, classified->fourState, classified->isSigned);
      };
      for (Operation *child : getChildren(unit.source)) {
        auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
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
              input->getWidth(), input->getFourState(), input->getIsSigned()));
      }
      SmallVector<Attribute> dpiSignature(dpiInputs);
      if (subroutine.getSubroutineKind() ==
          semantic::SVSubroutineKind::Function) {
        auto semanticType =
            unit.source->getAttrOfType<TypeAttr>("semantic_type");
        auto subroutineType =
            semanticType
                ? dyn_cast<semantic::SubroutineType>(semanticType.getValue())
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
          FailureOr<sim::DPIABIAttr> result = makeABI(
              sourceSignature.getResult(0), sim::DPIArgumentDirection::Result,
              getSemanticLocation(unit.source));
          if (failed(result))
            invalid = true;
          else
            dpiSignature.push_back(*result);
        }
      }
      llvm::append_range(dpiSignature, dpiCopyOuts);
      functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.dpi_import",
                                                   builder.getUnitAttr()));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_c_identifier", subroutine.getDpiCIdentifierAttr()));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_scope_id",
          builder.getI64IntegerAttr(getScopeId(unit.source))));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_import_id",
          builder.getI32IntegerAttr(getStableImportID(
              subroutine.getDpiCIdentifierAttr().getValue()))));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_abi_signature", builder.getArrayAttr(dpiSignature)));
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.dpi_logical_inputs",
                               builder.getI32IntegerAttr(dpiInputs.size())));
      sim::SimCodeUnitDeclOp declaration =
          codeUnitDeclarations.lookup(unit.source);
      declaration->setAttr("obelisk_sim.dpi_import", builder.getUnitAttr());
      declaration->setAttr("obelisk_sim.dpi_c_identifier",
                           subroutine.getDpiCIdentifierAttr());
      declaration->setAttr("obelisk_sim.dpi_import_id",
                           builder.getI32IntegerAttr(getStableImportID(
                               subroutine.getDpiCIdentifierAttr().getValue())));
      declaration->setAttr("obelisk_sim.dpi_abi_signature",
                           builder.getArrayAttr(dpiSignature));
      declaration->setAttr("obelisk_sim.dpi_logical_inputs",
                           builder.getI32IntegerAttr(dpiInputs.size()));
      if (subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Task)
        declaration->setAttr("obelisk_sim.dpi_task", builder.getUnitAttr());
      if (subroutine.getIsPure().value_or(false))
        functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.dpi_pure",
                                                     builder.getUnitAttr()));
      if (subroutine.getIsDpiContext().value_or(false))
        functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.dpi_context",
                                                     builder.getUnitAttr()));
      if (subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Task)
        functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.dpi_task",
                                                     builder.getUnitAttr()));
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
        "home_region", sim::EventRegionAttr::get(
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

    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      unit.function.getBody().getBlocks().clear();
      continue;
    }

    OpBuilder bodyBuilder =
        OpBuilder::atBlockEnd(&unit.function.getBody().front());
    if (unit.entryKind == sim::EntryKind::Observer ||
        isa<semantic::SVPortConnectionOp>(unit.source)) {
      bodyBuilder.clone(*unit.source);
    } else if (isa<semantic::SVVariableSymbolOp,
                   semantic::SVClassPropertySymbolOp>(unit.source)) {
      SmallVector<Operation *> initializer = getChildren(unit.source);
      auto memberOrdinals = unit.source->getAttrOfType<DenseI64ArrayAttr>(
          "obelisk.aggregate_member_initializer_ordinals");
      if (memberOrdinals &&
          initializer.size() != static_cast<size_t>(memberOrdinals.size())) {
        emitError(getSemanticLocation(unit.source))
            << "aggregate member initializer metadata has "
            << memberOrdinals.size() << " ordinals but " << initializer.size()
            << " expressions";
        invalid = true;
      } else if (memberOrdinals) {
        for (auto [expression, ordinal] :
             llvm::zip_equal(initializer, memberOrdinals.asArrayRef())) {
          Operation *cloned = bodyBuilder.clone(*expression);
          cloned->setAttr("obelisk_sim.initialize_static",
                          builder.getStringAttr(getHierarchyName(unit.source)));
          cloned->setAttr("obelisk_sim.initialize_subelement",
                          builder.getI64IntegerAttr(ordinal));
        }
      } else if (initializer.size() != 1) {
        emitError(getSemanticLocation(unit.source))
            << "design initializer must have one expression";
        invalid = true;
      } else {
        Operation *cloned = bodyBuilder.clone(*initializer.front());
        cloned->setAttr("obelisk_sim.initialize_static",
                        builder.getStringAttr(getHierarchyName(unit.source)));
      }
    } else if (isa<semantic::SVNetSymbolOp>(unit.source)) {
      SmallVector<Operation *> initializer = getChildren(unit.source);
      if (initializer.size() != 1) {
        emitError(getSemanticLocation(unit.source))
            << "net initializer must have one expression";
        invalid = true;
      } else {
        Operation *cloned = bodyBuilder.clone(*initializer.front());
        cloned->setAttr("obelisk_sim.initialize_net",
                        builder.getStringAttr(getHierarchyName(unit.source)));
      }
    } else {
      auto clonePropertyInitializers = [&] {
        auto owner = dyn_cast_or_null<semantic::SVClassTypeOp>(
            unit.source->getParentOp());
        if (!owner)
          return;
        for (Operation *member : getChildren(owner)) {
          auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
          if (!property ||
              property.getLifetime() == semantic::SVVariableLifetime::Static)
            continue;
          SmallVector<Operation *> initializer = getChildren(property);
          if (initializer.empty())
            continue;
          Operation *cloned = bodyBuilder.clone(*initializer.front());
          if (FlatSymbolRefAttr field = classFieldSymbols.lookup(property))
            cloned->setAttr("obelisk_sim.initialize_field", field);
        }
      };
      auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
      auto owner = subroutine ? dyn_cast_or_null<semantic::SVClassTypeOp>(
                                    unit.source->getParentOp())
                              : semantic::SVClassTypeOp{};
      bool constructor =
          subroutine && subroutine.getIsConstructor().value_or(false);
      bool initialized = false;
      if (constructor && owner && !owner.getBaseClass()) {
        clonePropertyInitializers();
        initialized = true;
      }
      for (Operation *child : getChildren(unit.source)) {
        if (isa<semantic::SVFormalArgumentSymbolOp,
                semantic::SVVariableSymbolOp,
                semantic::SVStatementBlockSymbolOp>(child))
          continue;
        bodyBuilder.clone(*child);
        if (constructor && owner && owner.getBaseClass() && !initialized) {
          bool containsSuper = false;
          child->walk([&](semantic::SVNewClassExpressionOp construct) {
            containsSuper |= construct.getIsSuperClass();
          });
          if (containsSuper) {
            clonePropertyInitializers();
            initialized = true;
          }
        }
      }
      if (constructor && !initialized)
        clonePropertyInitializers();
    }
    // Materialize declaration initializers before annotating calls. A walk is
    // not required to revisit operations inserted beneath the current node,
    // so doing both in one walk could leave calls in local initializers
    // without their frozen callee contract.
    unit.function.walk([&](semantic::SVVariableDeclStatementOp declaration) {
      auto symbol = semanticSymbols.find(
          declaration.getReferencedSymbol().getLeafReference());
      if (symbol == semanticSymbols.end())
        return;
      SmallVector<Operation *> initializer = getChildren(symbol->second);
      if (initializer.empty())
        return;
      OpBuilder declarationBuilder =
          OpBuilder::atBlockEnd(&declaration->getRegion(0).front());
      auto memberOrdinals = symbol->second->getAttrOfType<DenseI64ArrayAttr>(
          "obelisk.aggregate_member_initializer_ordinals");
      if (!memberOrdinals) {
        declarationBuilder.clone(*initializer.front());
        return;
      }
      if (initializer.size() != static_cast<size_t>(memberOrdinals.size())) {
        emitError(getSemanticLocation(symbol->second))
            << "aggregate member initializer metadata has "
            << memberOrdinals.size() << " ordinals but " << initializer.size()
            << " expressions";
        invalid = true;
        return;
      }
      declaration->setAttr("obelisk_sim.aggregate_member_initializers",
                           builder.getUnitAttr());
      for (auto [expression, ordinal] :
           llvm::zip_equal(initializer, memberOrdinals.asArrayRef())) {
        Operation *cloned = declarationBuilder.clone(*expression);
        cloned->setAttr("obelisk_sim.initialize_subelement",
                        builder.getI64IntegerAttr(ordinal));
      }
    });
    unit.function.walk([&](Operation *nested) {
      if (auto call = dyn_cast<semantic::SVCallExpressionOp>(nested)) {
        freezeCallContract(call);
        return;
      }
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested)) {
        auto symbol = semanticSymbols.find(
            named.getReferencedSymbol().getLeafReference());
        if (symbol == semanticSymbols.end())
          return;
        if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second))
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            named->setAttr("obelisk_sim.constant_value", constant);
        auto field = classFieldSymbols.find(symbol->second);
        auto property =
            dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second);
        if (field != classFieldSymbols.end() &&
            (!property ||
             property.getLifetime() != semantic::SVVariableLifetime::Static))
          named->setAttr("obelisk_sim.class_field", field->second);
        return;
      }
      if (auto hierarchical =
              dyn_cast<semantic::SVHierarchicalValueExpressionOp>(nested)) {
        auto symbol = semanticSymbols.find(
            hierarchical.getReferencedSymbol().getLeafReference());
        if (symbol != semanticSymbols.end() &&
            isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second))
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            hierarchical->setAttr("obelisk_sim.constant_value", constant);
        return;
      }
      if (auto member =
              dyn_cast<semantic::SVMemberAccessExpressionOp>(nested)) {
        auto symbol = semanticSymbols.find(
            member.getReferencedSymbol().getLeafReference());
        if (symbol == semanticSymbols.end())
          return;
        if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second)) {
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            member->setAttr("obelisk_sim.constant_value", constant);
          return;
        }
        auto field = classFieldSymbols.find(symbol->second);
        auto property =
            dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second);
        if (field != classFieldSymbols.end() &&
            (!property ||
             property.getLifetime() != semantic::SVVariableLifetime::Static))
          member->setAttr("obelisk_sim.class_field", field->second);
        return;
      }
    });
    if (unit.entryKind != sim::EntryKind::Function &&
        unit.entryKind != sim::EntryKind::Observer) {
      sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(unit.source),
                               ValueRange{});
    } else if (type.getNumResults() != 0) {
      auto placeholder = UnrealizedConversionCastOp::create(
          bodyBuilder, getSemanticLocation(unit.source), type.getResults(),
          ValueRange{});
      placeholder->setAttr(placeholderAttrName, builder.getUnitAttr());
      sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(unit.source),
                               placeholder.getResults());
    } else {
      sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(unit.source),
                               ValueRange{});
    }
  }
  if (invalid)
    return abort();

  llvm::DenseMap<Operation *, sim::SimFuncOp> unitFunctions;
  for (UnitInfo &unit : units)
    if (unit.function)
      unitFunctions[unit.source] = unit.function;

  auto constructorSymbolFor =
      [&](semantic::SVClassTypeOp classType) -> FlatSymbolRefAttr {
    if (FlatSymbolRefAttr implicit =
            implicitConstructorSymbols.lookup(classType))
      return implicit;
    for (Operation *child : getChildren(classType)) {
      semantic::SVSubroutineSymbolOp method = getClassMethod(child);
      if (!method || !method.getIsConstructor().value_or(false))
        continue;
      auto found = directCalleeNames.find(method);
      if (found != directCalleeNames.end())
        return FlatSymbolRefAttr::get(context, found->second);
    }
    return {};
  };

  // Slang omits an executable subroutine node for an implicit constructor.
  // Materialize that lifecycle edge explicitly so `new` without a declared
  // constructor still performs base construction and declaration-order
  // property initialization.
  for (semantic::SVClassTypeOp classType : classSources) {
    FlatSymbolRefAttr constructor =
        implicitConstructorSymbols.lookup(classType);
    if (!constructor)
      continue;
    Type receiverType = sim::ClassHandleType::get(
        context, FlatSymbolRefAttr::get(
                     context, classSymbols.lookup(classType).getValue()));
    FunctionType type = FunctionType::get(
        context, {sim::ContextType::get(context), receiverType}, {});
    SmallVector<DictionaryAttr> argAttrs{
        captureMetadata(builder, sim::CaptureKind::Context),
        captureMetadata(builder, sim::CaptureKind::Formal)};
    std::string hierarchy =
        (getHierarchyName(classType) + Twine("::new")).str();
    uint64_t codeUnitID = stableCodeUnitID(hierarchy);
    sim::SimCodeUnitDeclOp::create(
        builder, getSemanticLocation(classType), codeUnitID, uint64_t{0},
        sim::EntryKind::Function, builder.getStringAttr(hierarchy),
        builder.getStringAttr("implicit constructor"), UnitAttr{});
    SmallVector<NamedAttribute> attrs{
        builder.getNamedAttr("code_unit_id",
                             builder.getI64IntegerAttr(codeUnitID)),
        builder.getNamedAttr("obelisk_sim.this_argument",
                             builder.getI32IntegerAttr(1)),
        builder.getNamedAttr("obelisk_sim.constructor", builder.getUnitAttr()),
        builder.getNamedAttr(
            "home_region",
            sim::EventRegionAttr::get(context, sim::EventRegion::Active)),
        builder.getNamedAttr("domain",
                             sim::ExecutionDomainAttr::get(
                                 context, sim::ExecutionDomain::Design)),
        builder.getNamedAttr(sim::metadata::hierarchicalName,
                             builder.getStringAttr(hierarchy))};
    sim::SimFuncOp function = sim::SimFuncOp::create(
        builder, getSemanticLocation(classType), constructor.getValue(), type,
        sim::EntryKind::Function, attrs, argAttrs);
    SymbolTable::setSymbolVisibility(function,
                                     SymbolTable::Visibility::Private);
    OpBuilder bodyBuilder = OpBuilder::atBlockEnd(&function.getBody().front());
    Value receiver = function.getBody().front().getArgument(1);

    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : semanticClasses.end();
      FlatSymbolRefAttr baseConstructor =
          base == semanticClasses.end() ? FlatSymbolRefAttr{}
                                        : constructorSymbolFor(base->second);
      if (!baseConstructor) {
        emitError(getSemanticLocation(classType))
            << "implicit constructor cannot resolve its base constructor";
        invalid = true;
      } else {
        Type baseReceiverType = sim::ClassHandleType::get(
            context,
            FlatSymbolRefAttr::get(
                context, classSymbols.lookup(base->second).getValue()));
        semantic::SVCallExpressionOp baseCall;
        for (Operation *child : getChildren(classType)) {
          auto candidate = dyn_cast<semantic::SVCallExpressionOp>(child);
          Operation *target =
              candidate ? resolveDirectCallee(candidate) : nullptr;
          if (target &&
              directCalleeNames.lookup(target) == baseConstructor.getValue()) {
            baseCall = candidate;
            break;
          }
        }
        if (baseCall) {
          auto cloned =
              cast<semantic::SVCallExpressionOp>(bodyBuilder.clone(*baseCall));
          freezeCallContract(cloned);
          // An extends-clause constructor call is represented as an ordinary
          // semantic call. It still uses the current object as the base-class
          // receiver, just like an explicit super.new call.
          cloned->setAttr("obelisk_sim.class_super", builder.getUnitAttr());
        } else {
          Value baseReceiver = sim::SimClassCastOp::create(
              bodyBuilder, getSemanticLocation(classType), baseReceiverType,
              receiver);
          sim::SimClassDirectCallOp::create(
              bodyBuilder, getSemanticLocation(classType), TypeRange{},
              baseConstructor, baseReceiver, ValueRange{});
        }
      }
    }

    for (Operation *member : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
      if (!property ||
          property.getLifetime() == semantic::SVVariableLifetime::Static)
        continue;
      SmallVector<Operation *> initializer = getChildren(property);
      if (initializer.empty())
        continue;
      Operation *cloned = bodyBuilder.clone(*initializer.front());
      if (FlatSymbolRefAttr field = classFieldSymbols.lookup(property))
        cloned->setAttr("obelisk_sim.initialize_field", field);
    }
    sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(classType),
                             ValueRange{});
  }
  if (invalid)
    return abort();

  for (semantic::SVClassTypeOp classType : classSources) {
    for (Operation *child : getChildren(classType)) {
      auto method = getClassMethod(child);
      if (!method || method.getIsBuiltin().value_or(false))
        continue;
      bool isPure = method.getIsPure().value_or(false);
      sim::SimFuncOp typedImplementation = unitFunctions.lookup(method);
      if (!typedImplementation && !isPure) {
        method.emitError("concrete class method has no executable code unit");
        invalid = true;
        continue;
      }
      FlatSymbolRefAttr methodSymbol = classMethodSymbols.lookup(method);
      if (!methodSymbol) {
        method.emitError("class method has no frozen descriptor symbol");
        invalid = true;
        continue;
      }
      bool isVirtual = method.getIsVirtual().value_or(false);
      IntegerAttr slot =
          isVirtual
              ? builder.getI64IntegerAttr(virtualMethodSlots.lookup(method))
              : IntegerAttr{};
      IntegerAttr signatureID =
          isVirtual ? builder.getI64IntegerAttr(
                          virtualMethodSignatures.lookup(method))
                    : IntegerAttr{};
      sim::SimFuncOp implementation =
          isPure ? sim::SimFuncOp{} : typedImplementation;
      FlatSymbolRefAttr implementationRef =
          implementation
              ? FlatSymbolRefAttr::get(context, implementation.getSymName())
              : FlatSymbolRefAttr{};
      Type functionType = typedImplementation
                              ? Type(typedImplementation.getFunctionType())
                              : Type(FunctionType::get(context, {}, {}));
      sim::SimClassMethodDeclOp::create(
          builder, getSemanticLocation(method),
          builder.getStringAttr(methodSymbol.getValue()),
          FlatSymbolRefAttr::get(context,
                                 classSymbols.lookup(classType).getValue()),
          TypeAttr::get(functionType), slot, signatureID, implementationRef,
          builder.getBoolAttr(isVirtual), builder.getBoolAttr(isPure),
          builder.getBoolAttr(method.getIsStatic().value_or(false)),
          builder.getBoolAttr(method.getSubroutineKind() ==
                              semantic::SVSubroutineKind::Task),
          builder.getBoolAttr(method.getIsFinal().value_or(false)),
          builder.getStringAttr(getDebugName(method)));
    }
  }
  if (invalid)
    return abort();

  OpBuilder rootBuilder =
      OpBuilder::atBlockEnd(&rootInitializer.getBody().front());
  Value simContext = rootInitializer.getBody().front().getArgument(0);
  auto materializeRootOperands =
      [&](UnitInfo &unit) -> FailureOr<SmallVector<Value>> {
    SmallVector<Value> operands{simContext};
    for (unsigned index = 1; index < unit.function.getNumArguments(); ++index) {
      DictionaryAttr attrs = unit.function.getArgAttrDict(index);
      auto kind = dyn_cast_or_null<sim::CaptureKindAttr>(
          attrs ? attrs.get(captureKindAttrName) : Attribute{});
      auto descriptor = attrs ? attrs.getAs<IntegerAttr>(descriptorIdAttrName)
                              : IntegerAttr{};
      if (!kind || !descriptor) {
        unit.function.emitError() << "root-invoked argument #" << index
                                  << " has no descriptor capture metadata";
        return failure();
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
          auto low = attrs.getAs<IntegerAttr>(sim::metadata::descriptorLow);
          if (!low) {
            unit.function.emitError()
                << "view capture is missing its descriptor offset";
            return failure();
          }
          if (auto indices = attrs.getAs<DenseI64ArrayAttr>(
                  sim::metadata::descriptorIndices)) {
            auto aggregateType =
                attrs.getAs<TypeAttr>(sim::metadata::descriptorAggregateType);
            if (!aggregateType) {
              unit.function.emitError()
                  << "aggregate view capture is missing its result type";
              return failure();
            }
            Type resultType =
                sim::RefType::get(context, aggregateType.getValue());
            storage = sim::SimRefSubelementOp::create(
                          rootBuilder, loc, resultType, storage, indices)
                          .getResult();
          }
          if (storage.getType() != type) {
            auto packedLow =
                attrs.getAs<IntegerAttr>(sim::metadata::descriptorPackedLow);
            if (!packedLow) {
              unit.function.emitError()
                  << "packed view capture is missing its bit offset";
              return failure();
            }
            storage = sim::SimRefExtractOp::create(rootBuilder, loc, type,
                                                   storage, packedLow)
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
            << "root-invoked argument #" << index
            << " cannot be materialized by the root initializer";
        return failure();
      }
    }
    return operands;
  };

  // Design variable and static class-property initializers are zero-time
  // private functions. Run all of them before creating any process so initial
  // blocks observe fully initialized static state.
  for (UnitInfo &unit : units) {
    auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(unit.source);
    bool initializer = isa<semantic::SVVariableSymbolOp>(unit.source) ||
                       (property && property.getLifetime() ==
                                        semantic::SVVariableLifetime::Static);
    if (!initializer)
      continue;
    FailureOr<SmallVector<Value>> operands = materializeRootOperands(unit);
    if (failed(operands))
      return abort();
    sim::SimCallOp::create(rootBuilder, unit.function.getLoc(), TypeRange{},
                           FlatSymbolRefAttr::get(context, unit.symbol),
                           *operands, ArrayAttr{}, ArrayAttr{});
  }

  auto spawnRootUnit = [&](UnitInfo &unit) -> LogicalResult {
    FailureOr<SmallVector<Value>> operands = materializeRootOperands(unit);
    if (failed(operands))
      return failure();
    sim::SimSpawnOp::create(rootBuilder, unit.function.getLoc(),
                            sim::ProcessType::get(context),
                            FlatSymbolRefAttr::get(context, unit.symbol),
                            *operands, ArrayAttr{}, ArrayAttr{});
    return success();
  };
  auto isRootSpawned = [](const UnitInfo &unit) {
    return unit.entryKind != sim::EntryKind::Function &&
           unit.entryKind != sim::EntryKind::Task &&
           unit.entryKind != sim::EntryKind::Observer;
  };
  auto isRepeatingProcess = [](const UnitInfo &unit) {
    return unit.entryKind == sim::EntryKind::Always ||
           unit.entryKind == sim::EntryKind::AlwaysComb ||
           unit.entryKind == sim::EntryKind::AlwaysLatch ||
           unit.entryKind == sim::EntryKind::AlwaysFF;
  };

  // Establish repeating-process sensitivities before initial processes can
  // trigger events or mutate their watched values. The standard permits
  // either Active-region order at time zero; choosing this deterministic
  // order matches established simulator behavior and prevents a source-order
  // race from losing an event before an `always @(event)` has suspended.
  for (UnitInfo &unit : units)
    if (isRootSpawned(unit) && isRepeatingProcess(unit))
      if (failed(spawnRootUnit(unit)))
        return abort();
  for (UnitInfo &unit : units) {
    if (!isRootSpawned(unit) || isRepeatingProcess(unit))
      continue;
    if (failed(spawnRootUnit(unit)))
      return abort();
  }
  sim::SimReturnOp::create(rootBuilder, module.getLoc(), ValueRange{});
}

} // namespace
} // namespace obelisk
