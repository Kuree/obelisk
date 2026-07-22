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

#include <limits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPREPAREPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

static std::optional<uint64_t> getUnsigned64(IntegerAttr attribute) {
  if (!attribute || attribute.getValue().isNegative() ||
      attribute.getValue().getActiveBits() > 64)
    return std::nullopt;
  return attribute.getValue().getZExtValue();
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
  enum class Kind { Storage, Net, Driver } kind;
  uint64_t id;
  uint64_t scopeId;
  Type type;
};

struct UnitInfo {
  Operation *source;
  uint64_t id;
  sim::EntryKind entryKind;
  std::string symbol;
  sim::SimFuncOp function;
};

static FailureOr<sim::EntryKind> getEntryKind(Operation *op) {
  if (isa<semantic::SVContinuousAssignSymbolOp>(op))
    return sim::EntryKind::Continuous;
  if (isa<semantic::SVSubroutineSymbolOp>(op))
    return sim::EntryKind::Function;
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

/// Automatic locals live in the owning unit's binding table instead of the
/// design descriptor inventory. A variable is automatic when it is declared
/// inside a statement block and not explicitly static.
static bool isAutomaticLocalSymbol(Operation *op) {
  if (auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op))
    if (variable.getLifetime() == semantic::SVVariableLifetime::Static)
      return false;
  return op->getParentOfType<semantic::SVStatementBlockSymbolOp>() != nullptr;
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

  uint64_t designPrecisionFs = std::numeric_limits<uint64_t>::max();
  for (Operation *unit : sourceUnits) {
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
  uint64_t nextScopeId = 0;
  scopeIds[semanticRoot] = nextScopeId;
  sim::SimScopeDeclOp::create(
      builder, getSemanticLocation(semanticRoot), nextScopeId++, IntegerAttr{},
      builder.getStringAttr(getHierarchyName(semanticRoot)),
      builder.getStringAttr(getDebugName(semanticRoot)));
  semanticRoot->walk<WalkOrder::PreOrder>(
      [&](semantic::SVInstanceBodySymbolOp body) {
        Operation *parent = body->getParentOp();
        while (parent && !scopeIds.count(parent))
          parent = parent->getParentOp();
        uint64_t parentId = parent ? scopeIds.lookup(parent) : 0;
        uint64_t id = nextScopeId++;
        scopeIds[body] = id;
        sim::SimScopeDeclOp::create(
            builder, getSemanticLocation(body), id,
            builder.getI64IntegerAttr(parentId),
            builder.getStringAttr(getHierarchyName(body)),
            builder.getStringAttr(getDebugName(body)));
      });

  auto getScopeId = [&](Operation *op) {
    for (Operation *cursor = op; cursor; cursor = cursor->getParentOp())
      if (auto found = scopeIds.find(cursor); found != scopeIds.end())
        return found->second;
    return uint64_t{0};
  };

  // Resolve only identity port connections as aliases. Non-identity
  // connections need executable connection logic; rejecting them here avoids
  // silently collapsing an arbitrary expression to its first named operand.
  llvm::StringMap<std::string> aliases;
  semanticRoot->walk([&](semantic::SVInstanceSymbolOp instance) {
    SmallVector<Operation *> connections;
    Operation *instanceBody = nullptr;
    for (Operation *child : getChildren(instance)) {
      if (isa<semantic::SVInstanceBodySymbolOp>(child)) {
        instanceBody = child;
        continue;
      }
      connections.push_back(child);
    }
    if (!instanceBody)
      return;
    SmallVector<Operation *> ports;
    for (Operation *child : getChildren(instanceBody))
      if (isa<semantic::SVPortSymbolOp>(child))
        ports.push_back(child);
    if (connections.size() != ports.size()) {
      emitError(getSemanticLocation(instance))
          << "port connection inventory does not match elaborated ports";
      invalid = true;
      return;
    }
    for (auto [port, connection] : llvm::zip(ports, connections)) {
      Operation *identity = connection;
      if (isa<semantic::SVAssignmentExpressionOp>(connection)) {
        SmallVector<Operation *> assignmentChildren = getChildren(connection);
        if (assignmentChildren.size() == 2 &&
            isa<semantic::SVEmptyArgumentExpressionOp>(assignmentChildren[1]))
          identity = assignmentChildren.front();
      }
      auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(identity);
      if (!named) {
        emitError(getSemanticLocation(connection))
            << "non-identity port connections are not supported by the first "
               "simulation slice";
        invalid = true;
        continue;
      }
      StringRef portPath = getHierarchyName(port);
      StringRef connected = named.getReferencedPath();
      if (!portPath.empty() && !connected.empty() && portPath != connected)
        aliases[portPath] = connected.str();
    }
  });

  llvm::StringMap<DescriptorInfo> descriptors;
  uint64_t nextStorageId = 0;
  uint64_t nextNetId = 0;
  SmallVector<Operation *> designObjects;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (isNestedInCodeUnit(op))
      return;
    bool storage =
        isa<semantic::SVVariableSymbolOp>(op) && !isAutomaticLocalSymbol(op);
    if (storage || isa<semantic::SVNetSymbolOp>(op))
      designObjects.push_back(op);
  });
  auto emitDescriptor = [&](Operation *op) {
    bool storage = isa<semantic::SVVariableSymbolOp>(op);
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
    if (storage) {
      uint64_t id = nextStorageId++;
      descriptors[path] = {DescriptorInfo::Kind::Storage, id, scopeId, *type};
      sim::Lifetime lifetime =
          op->getParentOfType<semantic::SVStatementBlockSymbolOp>()
              ? sim::Lifetime::Static
              : sim::Lifetime::Design;
      sim::SimStorageDeclOp::create(builder, getSemanticLocation(op), id,
                                    scopeId, *type, lifetime, hierarchy, debug,
                                    sim::ComputeObservabilityKindAttr{});
    } else {
      uint64_t id = nextNetId++;
      descriptors[path] = {DescriptorInfo::Kind::Net, id, scopeId, *type};
      sim::SimNetDeclOp::create(builder, getSemanticLocation(op), id, scopeId,
                                *type, sim::Lifetime::Design, hierarchy, debug,
                                sim::ComputeObservabilityKindAttr{});
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
    bool cyclic = false;
    auto next = aliases.find(canonical);
    while (next != aliases.end()) {
      if (!seen.insert(canonical).second) {
        emitError(getSemanticLocation(op)) << "cyclic port alias for " << path;
        invalid = true;
        cyclic = true;
        break;
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
    descriptors[path] = target->second;
  }
  if (invalid)
    return abort();

  // A continuous assignment owns one immutable driver descriptor. The first
  // named value is the elaborated lvalue and must resolve to a net.
  uint64_t nextDriverId = 0;
  llvm::DenseMap<Operation *, DescriptorInfo> continuousDrivers;
  llvm::DenseMap<Operation *, std::string> continuousTargets;
  for (Operation *unit : sourceUnits) {
    if (!isa<semantic::SVContinuousAssignSymbolOp>(unit))
      continue;
    StringRef targetPath;
    unit->walk<WalkOrder::PreOrder>(
        [&](semantic::SVNamedValueExpressionOp named) {
          if (targetPath.empty())
            targetPath = named.getReferencedPath();
        });
    auto target = descriptors.find(targetPath);
    if (target == descriptors.end() ||
        target->second.kind != DescriptorInfo::Kind::Net) {
      emitError(getSemanticLocation(unit))
          << "continuous assignment target is not a flattened net: "
          << targetPath;
      invalid = true;
      continue;
    }
    uint64_t id = nextDriverId++;
    uint64_t scopeId = getScopeId(unit);
    DescriptorInfo info{DescriptorInfo::Kind::Driver, id, scopeId,
                        target->second.type};
    continuousDrivers[unit] = info;
    continuousTargets[unit] = targetPath.str();
    sim::SimDriverDeclOp::create(
        builder, getSemanticLocation(unit), id, scopeId, target->second.id,
        target->second.type, sim::Lifetime::Design,
        builder.getStringAttr(targetPath), builder.getStringAttr("continuous"));
  }
  if (invalid)
    return abort();

  SmallVector<UnitInfo> units;
  units.reserve(sourceUnits.size());
  llvm::StringMap<std::string> directCallees;
  llvm::StringMap<Operation *> directCalleeSources;
  for (auto [index, source] : llvm::enumerate(sourceUnits)) {
    FailureOr<sim::EntryKind> entryKind = getEntryKind(source);
    if (failed(entryKind)) {
      invalid = true;
      continue;
    }
    if (*entryKind == sim::EntryKind::Function) {
      auto subroutine = cast<semantic::SVSubroutineSymbolOp>(source);
      if (subroutine.getSubroutineKind() !=
              semantic::SVSubroutineKind::Function ||
          subroutine.getIsDpiImport().value_or(false)) {
        emitError(getSemanticLocation(source))
            << "only static zero-time SystemVerilog functions are supported";
        invalid = true;
        continue;
      }
      bool hasTiming = false;
      source->walk([&](Operation *nested) {
        hasTiming |=
            isa<semantic::SVDelayControlOp, semantic::SVSignalEventControlOp,
                semantic::SVEventListControlOp>(nested);
      });
      if (hasTiming) {
        emitError(getSemanticLocation(source))
            << "zero-time function contains a blocking timing control";
        invalid = true;
        continue;
      }
    }
    std::string symbol = llvm::formatv("unit_{0}", index).str();
    units.push_back(
        {source, static_cast<uint64_t>(index), *entryKind, symbol, {}});
    StringRef hierarchy = getHierarchyName(source);
    if (!hierarchy.empty()) {
      directCallees[hierarchy] = symbol;
      directCalleeSources[hierarchy] = source;
    }
  }
  if (invalid)
    return abort();

  llvm::DenseMap<Operation *,
                 SmallVector<std::pair<std::string, DescriptorInfo>>>
      unitCaptures;
  llvm::DenseMap<Operation *, SmallVector<std::pair<std::string, Type>>>
      unitLocals;
  for (UnitInfo &unit : units) {
    llvm::StringSet<> seenPaths;
    llvm::StringSet<> seenLocals;
    std::function<void(Operation *)> collectBinding = [&](Operation *nested) {
      StringRef path;
      SymbolRefAttr reference;
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested)) {
        path = named.getReferencedPath();
        reference = named.getReferencedSymbol();
      } else if (auto declaration =
                     dyn_cast<semantic::SVVariableDeclStatementOp>(nested)) {
        path = declaration.getReferencedPath();
        reference = declaration.getReferencedSymbol();
      } else {
        return;
      }
      auto descriptor = descriptors.find(path);
      if (descriptor != descriptors.end()) {
        if (seenPaths.insert(path).second)
          unitCaptures[unit.source].push_back({path.str(), descriptor->second});
        return;
      }
      if (!reference)
        return;
      auto symbol = semanticSymbols.find(reference.getLeafReference());
      if (symbol == semanticSymbols.end() ||
          !isa<semantic::SVVariableSymbolOp>(symbol->second))
        return;
      FailureOr<Type> type = getNormalizedSemanticType(symbol->second);
      if (failed(type)) {
        invalid = true;
        return;
      }
      if (seenLocals.insert(path).second) {
        unitLocals[unit.source].push_back({path.str(), *type});
        symbol->second->walk<WalkOrder::PreOrder>(
            [&](Operation *initializerNode) {
              collectBinding(initializerNode);
            });
      }
    };
    unit.source->walk<WalkOrder::PreOrder>(
        [&](Operation *nested) { collectBinding(nested); });
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
      return lhs.first < rhs.first;
    });
  }

  // Create the root shell first. Its body is filled after all process shells
  // exist, so every spawn uses an immutable precomputed flat name.
  SmallVector<DictionaryAttr> rootArgAttrs{
      captureMetadata(builder, sim::CaptureKind::Context)};
  auto rootType =
      FunctionType::get(context, {sim::ContextType::get(context)}, {});
  auto rootInitializer =
      sim::SimFuncOp::create(builder, module.getLoc(), "__obelisk_root",
                             rootType, sim::EntryKind::RootInitializer,
                             ArrayRef<NamedAttribute>{}, rootArgAttrs);

  for (UnitInfo &unit : units) {
    auto captures = unitCaptures.lookup(unit.source);
    auto locals = unitLocals.lookup(unit.source);
    SmallVector<Type> copyOutResultTypes;

    // A continuous assignment may read its own target. Keep the ordinary net
    // binding for reads and add a role-specific driver binding for writes.
    if (unit.entryKind == sim::EntryKind::Continuous &&
        continuousDrivers.count(unit.source)) {
      DescriptorInfo driver = continuousDrivers.lookup(unit.source);
      StringRef target = continuousTargets.find(unit.source)->second;
      captures.push_back({target.str(), driver});
    }

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
      }
      inputs.push_back(handleType);
      argAttrs.push_back(
          captureMetadata(builder, captureKind, capture.second.id));
      SmallVector<NamedAttribute> binding{
          builder.getNamedAttr("path", builder.getStringAttr(capture.first)),
          builder.getNamedAttr("argument",
                               builder.getI64IntegerAttr(captureIndex + 1)),
      };
      if (unit.entryKind == sim::EntryKind::Continuous &&
          capture.second.kind == DescriptorInfo::Kind::Driver &&
          capture.first == continuousTargets.lookup(unit.source))
        binding.push_back(
            builder.getNamedAttr("lvalue_only", builder.getUnitAttr()));
      bindings.push_back(builder.getDictionaryAttr(binding));
    }
    for (auto &[path, type] : locals)
      bindings.push_back(builder.getDictionaryAttr([&] {
        SmallVector<NamedAttribute> attrs{
            builder.getNamedAttr("path", builder.getStringAttr(path)),
            builder.getNamedAttr("local_type", TypeAttr::get(type)),
        };
        if (auto subroutine =
                dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
            subroutine && subroutine.getReturnVariablePath() == path)
          attrs.push_back(
              builder.getNamedAttr("is_return", builder.getUnitAttr()));
        return attrs;
      }()));

    // Function formals precede non-local captures in the public contract.
    // Output and inout formals use value arguments plus copy-out results; only
    // ref formals retain aliasing handles.
    if (unit.entryKind == sim::EntryKind::Function) {
      SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
      for (Operation *child : getChildren(unit.source))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          formals.push_back(formal);
      if (!formals.empty()) {
        SmallVector<Type> reordered{inputs.front()};
        SmallVector<DictionaryAttr> reorderedAttrs{argAttrs.front()};
        SmallVector<Attribute> formalBindings;
        for (semantic::SVFormalArgumentSymbolOp formal : formals) {
          FailureOr<Type> type = getNormalizedSemanticType(formal);
          if (failed(type)) {
            invalid = true;
            continue;
          }
          semantic::SVArgumentDirection direction = formal.getDirection();
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
            copyOutResultTypes.push_back(*type);
          }
          formalBindings.push_back(builder.getDictionaryAttr(formalBinding));
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
      std::optional<SymbolRefAttr> returnSymbol =
          subroutine.getReturnVariableSymbol();
      if (!returnSymbol) {
        emitError(getSemanticLocation(unit.source))
            << "function is missing its elaborated return variable";
        invalid = true;
        continue;
      }
      auto symbol = semanticSymbols.find(returnSymbol->getLeafReference());
      if (symbol == semanticSymbols.end()) {
        emitError(getSemanticLocation(unit.source))
            << "function return variable does not resolve";
        invalid = true;
        continue;
      }
      FailureOr<Type> resultType = getNormalizedSemanticType(symbol->second);
      if (failed(resultType)) {
        invalid = true;
        continue;
      }
      results.push_back(*resultType);
    }
    llvm::append_range(results, copyOutResultTypes);
    FunctionType type = FunctionType::get(context, inputs, results);
    NamedAttribute bindingAttr =
        builder.getNamedAttr(bindingsAttrName, builder.getArrayAttr(bindings));
    uint64_t timeUnitFs = 1'000'000;
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
    if (timeUnitFs < designPrecisionFs || timeUnitFs % designPrecisionFs != 0) {
      emitError(getSemanticLocation(unit.source))
          << "code unit time scale is incompatible with design precision";
      invalid = true;
      continue;
    }
    NamedAttribute delayScaleAttr = builder.getNamedAttr(
        delayScaleAttrName,
        builder.getI64IntegerAttr(timeUnitFs / designPrecisionFs));
    SmallVector<NamedAttribute> functionAttrs{bindingAttr, delayScaleAttr};
    StringRef hierarchy = getHierarchyName(unit.source);
    if (!hierarchy.empty())
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.hierarchical_name", builder.getStringAttr(hierarchy)));
    unit.function = sim::SimFuncOp::create(
        builder, getSemanticLocation(unit.source), unit.symbol, type,
        unit.entryKind, functionAttrs, argAttrs);
    SymbolTable::setSymbolVisibility(unit.function,
                                     SymbolTable::Visibility::Private);

    OpBuilder bodyBuilder =
        OpBuilder::atBlockEnd(&unit.function.getBody().front());
    for (Operation *child : getChildren(unit.source)) {
      if (isa<semantic::SVFormalArgumentSymbolOp, semantic::SVVariableSymbolOp>(
              child))
        continue;
      bodyBuilder.clone(*child);
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
        for (auto &capture : unitCaptures[targetSource])
          capturePaths.push_back(builder.getStringAttr(capture.first));
        call->setAttr(calleeCapturesAttrName,
                      builder.getArrayAttr(capturePaths));
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
          formals.push_back(builder.getDictionaryAttr({
              builder.getNamedAttr(
                  "direction", builder.getI64IntegerAttr(static_cast<int64_t>(
                                   formal.getDirection()))),
              builder.getNamedAttr("type", TypeAttr::get(*formalType)),
              builder.getNamedAttr(
                  "is_signed",
                  builder.getBoolAttr(semanticType &&
                                      isSignedSemanticType(*semanticType))),
          }));
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
    if (unit.entryKind != sim::EntryKind::Function) {
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
    if (unit.entryKind == sim::EntryKind::Function)
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
      case sim::CaptureKind::Storage:
        operands.push_back(
            sim::SimContextStorageOp::create(rootBuilder, loc, type, simContext,
                                             rootBuilder.getI64IntegerAttr(id))
                .getResult());
        break;
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
