//===- PrepareUnits.cpp - Simulation code-unit planning -------------------===//

#include "PrepareUnits.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/FormatVariadic.h"

#include <functional>

using namespace mlir;

namespace obelisk::simlowering {

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
  if (isa<semantic::SVSystemTimingCheckSymbolOp>(op) &&
      op->hasAttr("obelisk.basic_timing_check"))
    return sim::EntryKind::Always;
  if (op->hasAttr(sequenceEndpointEventAttrName))
    return sim::EntryKind::Always;
  if (isa<semantic::SVClockingBlockSymbolOp>(op) &&
      (op->hasAttr(clockingEventMonitorRequiredAttrName) ||
       op->hasAttr(clockingEventListAttrName)))
    return sim::EntryKind::Always;
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
  if (isa<semantic::SVContinuousAssignSymbolOp,
          semantic::SVPrimitiveInstanceSymbolOp>(op))
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

static std::string getCodeUnitHierarchy(Operation *op) {
  if (isa<semantic::SVSystemTimingCheckSymbolOp>(op)) {
    auto nodeID = op->getAttrOfType<IntegerAttr>("node_id");
    return (getHierarchyName(op) + ".$timing_check_" +
            Twine(nodeID.getValue().getZExtValue()))
        .str();
  }
  if (op->hasAttr(sequenceEndpointEventAttrName))
    return (getHierarchyName(op) + ".$sequence_endpoint").str();
  if (isa<semantic::SVClockingBlockSymbolOp>(op) &&
      (op->hasAttr(clockingEventMonitorRequiredAttrName) ||
       op->hasAttr(clockingEventListAttrName)))
    return (getHierarchyName(op) + ".$event_monitor").str();
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

Operation *PreparedUnits::resolveDirectCallee(
    semantic::SVCallExpressionOp call,
    const llvm::StringMap<Operation *> &symbols) const {
  auto resolveExtern = [&](Operation *source) {
    if (externCalleeTargets.empty())
      return source;
    auto target = externCalleeTargets.find(source);
    return target == externCalleeTargets.end() ? source : target->second;
  };
  // Prefer the elaborator's symbol identity. Paths can differ in spelling for
  // defaulted constructors and out-of-block class methods.
  if (SymbolRefAttr reference = call.getReferencedSymbolAttr()) {
    auto symbol = symbols.find(reference.getLeafReference());
    if (symbol != symbols.end()) {
      Operation *source = resolveExtern(symbol->second);
      if (directCalleeNames.count(source))
        return source;
    }
  }
  if (std::optional<StringRef> path = call.getReferencedPath()) {
    auto source = directCalleeSources.find(*path);
    if (source != directCalleeSources.end()) {
      Operation *target = resolveExtern(source->second);
      if (directCalleeNames.count(target))
        return target;
    }
  }
  return nullptr;
}

SmallVector<const PreparedVirtualInterfaceCallee *>
PreparedUnits::resolveVirtualInterfaceCallees(
    semantic::SVCallExpressionOp call) const {
  SmallVector<Operation *> children = getChildren(call);
  if (!call.getHasThisClass() || children.empty())
    return {};
  auto typeAttr = children.front()->getAttrOfType<TypeAttr>("semantic_type");
  auto interface =
      typeAttr ? dyn_cast<semantic::VirtualInterfaceType>(typeAttr.getValue())
               : semantic::VirtualInterfaceType{};
  if (!interface)
    return {};
  SymbolRefAttr identity = interface.getInterfaceName();
  StringRef selectedModport = interface.getModport().getValue();
  auto topInstance = [](Operation *operation) {
    semantic::SVInstanceSymbolOp result;
    for (Operation *parent = operation; parent; parent = parent->getParentOp())
      if (auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(parent))
        result = instance;
    return result;
  };
  semantic::SVInstanceSymbolOp callerDesign = topInstance(call);
  if (!callerDesign)
    return {};
  StringRef design = getHierarchyName(callerDesign);
  SmallVector<const PreparedVirtualInterfaceCallee *> result;
  for (const PreparedVirtualInterfaceCallee &candidate :
       virtualInterfaceCallees) {
    if (candidate.method != call.getCalleeName() ||
        candidate.interfaceIdentity != identity || candidate.design != design)
      continue;
    // An import modport consumes either an ordinary interface method or an
    // extern implemented by a provider's export modport. An export access can
    // only name the latter.
    bool selected = selectedModport.empty() ||
                    call->hasAttr("virtual_interface_call_import") ||
                    (candidate.externExport &&
                     call->hasAttr("virtual_interface_call_export"));
    if (selected)
      result.push_back(&candidate);
  }
  return result;
}

FailureOr<PreparedUnits> materializeCodeUnitDeclarations(
    ModuleOp module, semantic::SVRootSymbolOp semanticRoot,
    ArrayRef<Operation *> sourceUnits,
    const llvm::StringMap<Operation *> &semanticSymbols,
    const PreparedScopeDeclarations &scopes, OpBuilder &builder) {
  MLIRContext *context = builder.getContext();
  bool invalid = false;
  PreparedUnits result;
  result.units.reserve(sourceUnits.size());
  llvm::DenseMap<uint64_t, Operation *> codeUnitIDs;
  SmallVector<semantic::SVSubroutineSymbolOp> interfaceExterns;

  for (auto [index, source] : llvm::enumerate(sourceUnits)) {
    if (auto exported =
            source->getAttrOfType<StringAttr>("dpi_export_c_identifier")) {
      auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(source);
      if (!subroutine || exported.getValue().empty()) {
        emitError(getSemanticLocation(source))
            << "DPI export is missing its resolved function and C identifier";
        invalid = true;
        continue;
      }
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
      source->walk<WalkOrder::PreOrder>([&](Operation *nested) {
        if (auto block = dyn_cast<semantic::SVBlockStatementOp>(nested);
            block && block.getBlockKind() !=
                         semantic::SVStatementBlockKind::Sequential) {
          // IEEE 1800 permits fork...join_none in a function because the
          // function itself returns without blocking. Its branches become
          // independent fork code units below, so their timing controls do
          // not make the enclosing function a suspending code unit.
          if (block.getBlockKind() == semantic::SVStatementBlockKind::JoinNone)
            return WalkResult::skip();

          // fork...join and fork...join_any block the caller and therefore
          // remain illegal in a zero-time function even if a particular set
          // of branches happens to complete in the current time slot.
          hasTiming = true;
          return WalkResult::skip();
        }
        if (isa<semantic::SVDelayControlOp, semantic::SVSignalEventControlOp,
                semantic::SVEventListControlOp>(nested))
          hasTiming = true;
        return hasTiming ? WalkResult::interrupt() : WalkResult::advance();
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
    result.units.push_back({source,
                            id,
                            *entryKind,
                            symbol,
                            std::move(codeUnitHierarchy),
                            {},
                            ObserverResult::None});
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(source)) {
      if (subroutine.getIsInterfaceExtern().value_or(false))
        interfaceExterns.push_back(subroutine);
      auto body =
          subroutine->getParentOfType<semantic::SVInstanceBodySymbolOp>();
      if (body && !body->hasAttr("is_virtual_interface_type_instance") &&
          !subroutine.getIsInterfaceExtern().value_or(false)) {
        auto identity =
            body->getAttrOfType<SymbolRefAttr>("virtual_interface_identity");
        semantic::SVInstanceSymbolOp top;
        for (Operation *parent = subroutine; parent;
             parent = parent->getParentOp())
          if (auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(parent))
            top = instance;
        if (identity && top)
          result.virtualInterfaceCallees.push_back(
              {source, source, identity,
               subroutine.getName().value_or("").str(),
               getHierarchyName(top).str(), false});
      }
    }
    if (!isa<semantic::SVPortConnectionOp, semantic::SVVariableSymbolOp,
             semantic::SVNetSymbolOp, semantic::SVClassPropertySymbolOp,
             semantic::SVSequenceSymbolOp, semantic::SVClockingBlockSymbolOp>(
            source)) {
      result.directCalleeSources[hierarchy] = source;
      result.directCalleeNames[source] = symbol;
    }
  }

  auto compatibleABI = [](semantic::SVSubroutineSymbolOp lhs,
                          semantic::SVSubroutineSymbolOp rhs) {
    if (lhs.getSemanticType() != rhs.getSemanticType() ||
        lhs.getSubroutineKind() != rhs.getSubroutineKind())
      return false;
    SmallVector<semantic::SVFormalArgumentSymbolOp> lhsFormals;
    SmallVector<semantic::SVFormalArgumentSymbolOp> rhsFormals;
    for (Operation *child : getChildren(lhs))
      if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
        lhsFormals.push_back(formal);
    for (Operation *child : getChildren(rhs))
      if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
        rhsFormals.push_back(formal);
    if (lhsFormals.size() != rhsFormals.size())
      return false;
    return llvm::all_of(llvm::zip_equal(lhsFormals, rhsFormals), [](auto pair) {
      auto [lhsFormal, rhsFormal] = pair;
      return lhsFormal.getDirection() == rhsFormal.getDirection() &&
             lhsFormal.getSemanticType() == rhsFormal.getSemanticType();
    });
  };

  auto resolveExternImplementation =
      [&](semantic::SVMethodPrototypeSymbolOp prototype,
          semantic::SVSubroutineSymbolOp stub, Attribute symbolAttr,
          Attribute pathAttr) -> FailureOr<semantic::SVSubroutineSymbolOp> {
    auto implementationRef = dyn_cast<SymbolRefAttr>(symbolAttr);
    auto implementationPath = dyn_cast<StringAttr>(pathAttr);
    auto implementation =
        implementationRef
            ? semanticSymbols.find(implementationRef.getLeafReference())
            : semanticSymbols.end();
    auto target =
        implementation == semanticSymbols.end()
            ? semantic::SVSubroutineSymbolOp{}
            : dyn_cast<semantic::SVSubroutineSymbolOp>(implementation->second);
    auto pathTarget =
        implementationPath
            ? result.directCalleeSources.find(implementationPath.getValue())
            : result.directCalleeSources.end();
    if (!target || pathTarget == result.directCalleeSources.end() ||
        pathTarget->second != target ||
        !result.directCalleeNames.count(target)) {
      emitError(getSemanticLocation(prototype))
          << "modport-exported interface extern implementation does not "
             "resolve by matching symbol and path to one executable "
             "subroutine";
      return failure();
    }
    if (!compatibleABI(stub, target)) {
      emitError(getSemanticLocation(target))
          << "modport-exported interface extern implementation has an "
             "incompatible subroutine ABI";
      emitRemark(getSemanticLocation(prototype)) << "extern prototype is here";
      return failure();
    }
    return target;
  };

  auto addExternVirtualCandidate =
      [&](semantic::SVSubroutineSymbolOp stub, Operation *source,
          semantic::SVMethodPrototypeSymbolOp prototype) -> LogicalResult {
    auto body = prototype->getParentOfType<semantic::SVInstanceBodySymbolOp>();
    auto identity =
        body ? body->getAttrOfType<SymbolRefAttr>("virtual_interface_identity")
             : SymbolRefAttr{};
    semantic::SVInstanceSymbolOp top;
    for (Operation *parent = prototype; parent; parent = parent->getParentOp())
      if (auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(parent))
        top = instance;
    if (!identity || !top) {
      emitError(getSemanticLocation(prototype))
          << "modport-exported interface extern has no interface identity";
      return failure();
    }
    std::string method = stub.getName().value_or("").str();
    std::string design = getHierarchyName(top).str();
    bool duplicate =
        llvm::any_of(result.virtualInterfaceCallees,
                     [&](const PreparedVirtualInterfaceCallee &candidate) {
                       return candidate.dispatchSource == stub.getOperation() &&
                              candidate.interfaceIdentity == identity &&
                              candidate.method == method &&
                              candidate.design == design;
                     });
    if (!duplicate)
      result.virtualInterfaceCallees.push_back(PreparedVirtualInterfaceCallee{
          source, stub.getOperation(), identity, std::move(method),
          std::move(design), true});
    return success();
  };

  // A modport export provides the implementation of an interface extern from
  // the connected module. Slang freezes the exact elaborated implementation
  // inventory on the prototype. A sole ordinary extern redirects directly to
  // that code unit. A fork/join extern retains its stub as a compile-time
  // aggregator; preparation later materializes one ordinary task-call branch
  // per provider and joins those branches without runtime dispatch.
  for (semantic::SVSubroutineSymbolOp stub : interfaceExterns) {
    auto prototype = dyn_cast_or_null<semantic::SVMethodPrototypeSymbolOp>(
        stub->getParentOp());
    if (!prototype) {
      emitError(getSemanticLocation(stub))
          << "interface extern subroutine has no method prototype";
      invalid = true;
      continue;
    }
    int64_t count = prototype.getExternImplementationCount();
    ArrayAttr symbols = prototype.getExternImplementationSymbols();
    ArrayAttr paths = prototype.getExternImplementationPaths();
    bool forkJoin = prototype.getIsForkJoin().value_or(false);
    if (forkJoin) {
      if (stub.getSubroutineKind() != semantic::SVSubroutineKind::Task) {
        emitError(getSemanticLocation(prototype))
            << "only an interface extern task may use fork/join aggregation";
        invalid = true;
        continue;
      }
      if (count < 0 || !symbols || !paths ||
          static_cast<int64_t>(symbols.size()) != count ||
          static_cast<int64_t>(paths.size()) != count) {
        emitError(getSemanticLocation(prototype))
            << "modport-exported interface extern fork/join task has "
               "inconsistent implementation metadata";
        invalid = true;
        continue;
      }
      // A compile-time-only virtual-interface type inventory is not an
      // executable receiver. A real zero-provider instance remains legal and
      // receives the Clause 25.7.4 runtime-error aggregator below.
      if (count == 0 && isCompileTimeOnlyInstanceMember(stub))
        continue;
      SmallVector<Operation *> targets;
      llvm::SmallPtrSet<Operation *, 4> seenTargets;
      bool aggregationInvalid = false;
      for (int64_t index = 0; index != count; ++index) {
        FailureOr<semantic::SVSubroutineSymbolOp> target =
            resolveExternImplementation(prototype, stub, symbols[index],
                                        paths[index]);
        if (failed(target)) {
          invalid = true;
          aggregationInvalid = true;
          break;
        }
        if (!seenTargets.insert(target->getOperation()).second) {
          emitError(getSemanticLocation(prototype))
              << "modport-exported interface extern fork/join task repeats "
                 "one implementation";
          invalid = true;
          aggregationInvalid = true;
          break;
        }
        targets.push_back(target->getOperation());
      }
      if (aggregationInvalid)
        continue;
      llvm::sort(targets, [](Operation *lhs, Operation *rhs) {
        return getHierarchyName(lhs) < getHierarchyName(rhs);
      });
      result.externForkJoinTargets[stub] = std::move(targets);
      if (failed(addExternVirtualCandidate(stub, stub, prototype)))
        invalid = true;
      continue;
    }
    if (count == 0) {
      // Compile-time-only virtual-interface type inventories do not represent
      // an executable interface instance. Every real non-fork/join extern
      // must have one provider, even when no call names it.
      if (!isCompileTimeOnlyInstanceMember(stub)) {
        emitError(getSemanticLocation(prototype))
            << "modport-exported interface extern has no implementation";
        invalid = true;
      }
      continue;
    }
    if (count != 1) {
      emitError(getSemanticLocation(prototype))
          << "modport-exported interface extern has " << count
          << " implementations; exactly one is executable";
      invalid = true;
      continue;
    }
    if (!symbols || symbols.size() != 1 || !paths || paths.size() != 1) {
      emitError(getSemanticLocation(prototype))
          << "modport-exported interface extern has inconsistent "
             "implementation metadata";
      invalid = true;
      continue;
    }
    FailureOr<semantic::SVSubroutineSymbolOp> target =
        resolveExternImplementation(prototype, stub, symbols[0], paths[0]);
    if (failed(target)) {
      invalid = true;
      continue;
    }
    result.externCalleeTargets[stub] = *target;
    if (failed(addExternVirtualCandidate(stub, *target, prototype)))
      invalid = true;
  }
  for (auto [index, lhsRecord] :
       llvm::enumerate(result.virtualInterfaceCallees)) {
    auto lhs = cast<semantic::SVSubroutineSymbolOp>(lhsRecord.source);
    for (const PreparedVirtualInterfaceCallee &rhsRecord :
         ArrayRef<PreparedVirtualInterfaceCallee>(
             result.virtualInterfaceCallees)
             .drop_front(index + 1)) {
      if (lhsRecord.interfaceIdentity != rhsRecord.interfaceIdentity ||
          lhsRecord.method != rhsRecord.method ||
          lhsRecord.design != rhsRecord.design)
        continue;
      auto rhs = cast<semantic::SVSubroutineSymbolOp>(rhsRecord.source);
      if (!compatibleABI(lhs, rhs)) {
        emitError(getSemanticLocation(rhs))
            << "virtual-interface call candidates have incompatible "
               "subroutine ABIs";
        emitRemark(getSemanticLocation(lhs)) << "other candidate is here";
        invalid = true;
      }
    }
  }
  if (invalid)
    return failure();

  struct ObserverCandidate {
    Operation *expression;
    ObserverResult result;
    std::string label;
    uint64_t parentID;
    std::string parentHierarchy;
    bool sampled = false;
  };
  SmallVector<ObserverCandidate> observerCandidates;
  auto isManagedMemberExpression = [&](Operation *expression) {
    auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(expression);
    SmallVector<Operation *> children =
        member ? getChildren(member) : SmallVector<Operation *>{};
    if (!member || children.size() != 1)
      return false;
    FailureOr<Type> receiver = getNormalizedSemanticType(children.front());
    return succeeded(receiver) && isa<sim::ClassHandleType>(*receiver);
  };
  const size_t ordinaryUnitCount = result.units.size();
  for (size_t unitIndex = 0; unitIndex != ordinaryUnitCount; ++unitIndex) {
    PreparedUnit &unit = result.units[unitIndex];
    unit.source->walk<WalkOrder::PreOrder>([&](Operation *nested) {
      if (auto override =
              dyn_cast<semantic::SVProceduralAssignStatementOp>(nested)) {
        SmallVector<Operation *> statementChildren = getChildren(override);
        auto assignment = statementChildren.size() == 1
                              ? dyn_cast<semantic::SVAssignmentExpressionOp>(
                                    statementChildren.front())
                              : semantic::SVAssignmentExpressionOp{};
        SmallVector<Operation *> assignmentChildren =
            assignment ? getChildren(assignment) : SmallVector<Operation *>{};
        if (assignmentChildren.size() != 2) {
          emitError(getSemanticLocation(override))
              << "procedural force/assign has no binary assignment expression";
          invalid = true;
          return;
        }
        FailureOr<Type> targetType =
            getNormalizedSemanticType(assignmentChildren.front());
        if (failed(targetType)) {
          invalid = true;
          return;
        }
        Type observerType = *targetType;
        if (!analysis::getSimulationStorageBitWidth(observerType)) {
          emitError(getSemanticLocation(override))
              << "procedural override target has no fixed executable value";
          invalid = true;
          return;
        }
        Operation *rhs = assignmentChildren.back();
        rhs->setAttr(observerCoercedTypeAttrName, TypeAttr::get(observerType));
        observerCandidates.push_back({rhs, ObserverResult::Value,
                                      "override_rhs", unit.id, unit.hierarchy});
        return;
      }
      if (auto assertion =
              dyn_cast<semantic::SVConcurrentAssertionStatementOp>(nested)) {
        SmallVector<Operation *> children = getChildren(assertion);
        if (assertion.getHasDefaultDisable() && !children.empty())
          observerCandidates.push_back({children.front(), ObserverResult::Truth,
                                        "disable", unit.id, unit.hierarchy});
        return;
      }
      if (auto disabled =
              dyn_cast<semantic::SVDisableIffAssertionExprOp>(nested)) {
        SmallVector<Operation *> children = getChildren(disabled);
        if (!children.empty())
          observerCandidates.push_back({children.front(), ObserverResult::Truth,
                                        "disable", unit.id, unit.hierarchy});
        return;
      }
      if (auto abort = dyn_cast<semantic::SVAbortAssertionExprOp>(nested)) {
        SmallVector<Operation *> children = getChildren(abort);
        if (!abort.getIsSynchronous() && !children.empty())
          observerCandidates.push_back({children.front(), ObserverResult::Truth,
                                        "abort", unit.id, unit.hierarchy});
        return;
      }
      if (auto wait = dyn_cast<semantic::SVWaitStatementOp>(nested)) {
        SmallVector<Operation *> children = getChildren(wait);
        if (children.size() == 2 &&
            (!isAddressableTimingExpression(children.front()) ||
             isManagedMemberExpression(children.front()) ||
             !storageDecidesTruth(children.front())))
          observerCandidates.push_back({children.front(), ObserverResult::Truth,
                                        "wait", unit.id, unit.hierarchy});
        return;
      }
      bool staticClockingVariableIff =
          nested->hasAttr(clockingVariableAttrName) &&
          nested->hasAttr(clockingEventHasIffAttrName);
      bool virtualClockingVariableIff =
          nested->hasAttr("virtual_interface_clocking") &&
          nested->hasAttr("virtual_interface_clock_event_has_iff");
      bool staticClockingSource = nested->hasAttr(clockingVariableAttrName) &&
                                  nested->hasAttr("clocking_source_expression");
      bool virtualClockingSource =
          nested->hasAttr("virtual_interface_clocking") &&
          nested->hasAttr("virtual_interface_clocking_source_expression");
      if (staticClockingVariableIff || virtualClockingVariableIff ||
          staticClockingSource || virtualClockingSource) {
        SmallVector<Operation *> children = getChildren(nested);
        bool virtualClocking =
            virtualClockingVariableIff || virtualClockingSource;
        size_t expressionOffset = virtualClocking ? 1 : 0;
        size_t sourceCount =
            static_cast<size_t>(staticClockingSource || virtualClockingSource);
        size_t iffCount = static_cast<size_t>(staticClockingVariableIff ||
                                              virtualClockingVariableIff) *
                          2;
        if (children.size() != expressionOffset + sourceCount + iffCount) {
          emitError(getSemanticLocation(nested))
              << "clocking variable has an invalid frozen source/event "
                 "expression inventory";
          invalid = true;
          return;
        }
        if (sourceCount) {
          bool sampled =
              nested->hasAttr(clockingInputSkewOneStepAttrName) ||
              nested->hasAttr("virtual_interface_clock_input_skew_one_step");
          observerCandidates.push_back(
              {children[expressionOffset], ObserverResult::Value,
               "clocking_source", unit.id, unit.hierarchy, sampled});
          expressionOffset += sourceCount;
        }
        if (iffCount) {
          observerCandidates.push_back(
              {children[expressionOffset], ObserverResult::Value,
               "clocking_primary", unit.id, unit.hierarchy});
          observerCandidates.push_back({children[expressionOffset + 1],
                                        ObserverResult::Truth, "clocking_iff",
                                        unit.id, unit.hierarchy});
        }
        return;
      }
      if (auto cycle = dyn_cast<semantic::SVCycleDelayControlOp>(nested)) {
        if (!cycle->hasAttr(clockingEventHasIffAttrName))
          return;
        SmallVector<Operation *> children = getChildren(cycle);
        if (children.size() != 3) {
          emitError(getSemanticLocation(cycle))
              << "cycle delay with iff has no frozen clock and condition "
                 "expressions";
          invalid = true;
          return;
        }
        observerCandidates.push_back({children[1], ObserverResult::Value,
                                      "clocking_primary", unit.id,
                                      unit.hierarchy});
        observerCandidates.push_back({children[2], ObserverResult::Truth,
                                      "clocking_iff", unit.id, unit.hierarchy});
        return;
      }
      auto event = dyn_cast<semantic::SVSignalEventControlOp>(nested);
      if (!event)
        return;
      SmallVector<Operation *> children = getChildren(event);
      if (children.empty())
        return;
      if (auto instance = dyn_cast<semantic::SVAssertionInstanceExpressionOp>(
              children.front()))
        if (auto type = instance->getAttrOfType<TypeAttr>("semantic_type");
            type && isa<semantic::SequenceType>(type.getValue()))
          return;
      // A clocking-block event is lowered directly to its selected clock
      // descriptor. Its void-typed surface expression is therefore never a
      // value observer. A monitored event with an additional iff only needs
      // an observer for that additional condition; lowering supplies the
      // event-primary evaluator over the selected descriptor.
      if (children.front()->hasAttr("virtual_interface_clocking_block_event") ||
          children.front()->hasAttr(clockingBlockEventAttrName)) {
        bool monitoredClockingEvent =
            children.front()->hasAttr(clockingEventMonitorRequiredAttrName) ||
            children.front()->hasAttr(clockingEventListAttrName) ||
            children.front()->hasAttr(
                "virtual_interface_clock_event_monitor") ||
            children.front()->hasAttr("virtual_interface_clock_event_list");
        if (monitoredClockingEvent && event.getHasIff()) {
          if (children.size() != 2) {
            emitError(getSemanticLocation(event))
                << "monitored clocking-block event with iff has no condition";
            invalid = true;
            return;
          }
          observerCandidates.push_back({children[1], ObserverResult::Truth,
                                        "clocking_event_iff", unit.id,
                                        unit.hierarchy});
          return;
        }
        bool virtualClockingIff =
            children.front()->hasAttr("virtual_interface_clock_event_has_iff");
        bool staticClockingIff =
            children.front()->hasAttr(clockingEventHasIffAttrName);
        if (virtualClockingIff || staticClockingIff) {
          SmallVector<Operation *> clockingChildren =
              getChildren(children.front());
          size_t expressionOffset = virtualClockingIff ? 1 : 0;
          if (clockingChildren.size() != expressionOffset + 2) {
            emitError(getSemanticLocation(children.front()))
                << "clocking-block event with iff has no frozen clock and "
                   "condition expressions";
            invalid = true;
            return;
          }
          observerCandidates.push_back(
              {clockingChildren[expressionOffset], ObserverResult::Value,
               "clocking_primary", unit.id, unit.hierarchy});
          observerCandidates.push_back({clockingChildren[expressionOffset + 1],
                                        ObserverResult::Truth, "clocking_iff",
                                        unit.id, unit.hierarchy});
        }
        return;
      }
      ObserverResult primaryResult = ObserverResult::Value;
      FailureOr<Type> primaryType =
          children.front()->hasAttr("virtual_interface_clocking_block_event")
              ? FailureOr<Type>(sim::LogicType::get(module.getContext(), 1))
              : getNormalizedSemanticType(children.front());
      // Direct string variables already publish a signal occurrence only
      // when their contents change.  Subscribe to that descriptor instead of
      // outlining a value observer: string handles are not value identity,
      // and retaining a previous heap string in every waiter would add both
      // scheduler work and lifetime traffic to this common exact case.
      if (!event.getHasIff() &&
          event.getEdgeKind() == semantic::EdgeKind::Change &&
          succeeded(primaryType) && isa<sim::StringType>(*primaryType) &&
          isAddressableTimingExpression(children.front()) &&
          !isManagedMemberExpression(children.front()))
        return;
      if (succeeded(primaryType) && isa<sim::EventType>(*primaryType))
        primaryResult = ObserverResult::Event;
      else if (succeeded(primaryType) &&
               isa<sim::ClassHandleType>(*primaryType))
        // Event expressions compare class-handle identity, not object
        // contents. Observers operate on packed values, so carry the stable
        // non-address object ID across their comparison boundary.
        children.front()->setAttr(
            observerCoercedTypeAttrName,
            TypeAttr::get(IntegerType::get(module.getContext(), 64)));
      observerCandidates.push_back({children.front(), primaryResult, "primary",
                                    unit.id, unit.hierarchy});
      if (event.getHasIff() && children.size() == 2)
        observerCandidates.push_back({children[1], ObserverResult::Truth, "iff",
                                      unit.id, unit.hierarchy});
    });
  }

  // A specify condition is not part of an executable driver actor in the
  // semantic tree, but it is evaluated synchronously when that path's source
  // changes. Outline it with the same compact truth-evaluator ABI used by
  // event iff expressions. ifnone paths intentionally have no evaluator.
  semanticRoot->walk([&](semantic::SVTimingPathSymbolOp path) {
    if (!path->hasAttr("obelisk.simple_timing_path") ||
        !path->hasAttr("timing_condition"))
      return;
    SmallVector<Operation *> children = getChildren(path);
    size_t expectedChildren = path->hasAttr("timing_edge_sensitive") ? 2 : 1;
    if (children.size() != expectedChildren) {
      emitError(getSemanticLocation(path))
          << "conditional specify path has no unique frozen condition";
      invalid = true;
      return;
    }
    uint64_t nodeID = path.getNodeId();
    std::string hierarchy =
        (getHierarchyName(path) + ".$timing_path." + Twine(nodeID)).str();
    observerCandidates.push_back({children.front(), ObserverResult::Truth,
                                  "specify_condition", nodeID,
                                  std::move(hierarchy)});
  });

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
    if (candidate.label == "override_rhs")
      candidate.expression->setAttr("obelisk_sim.override_evaluator",
                                    builder.getUnitAttr());
    if (candidate.label == "abort" || candidate.sampled)
      candidate.expression->setAttr(sampledObserverAttrName,
                                    builder.getUnitAttr());
    result.units.push_back({candidate.expression,
                            id,
                            sim::EntryKind::Observer,
                            std::move(symbol),
                            std::move(hierarchy),
                            {},
                            candidate.result});
  }
  if (invalid)
    return failure();

  result.rootID = stableCodeUnitID("__obelisk_root");
  if (auto collision = codeUnitIDs.find(result.rootID);
      collision != codeUnitIDs.end()) {
    emitError(getSemanticLocation(collision->second))
        << "stable code-unit ID collides with the root initializer";
    return failure();
  }
  codeUnitIDs[result.rootID] = semanticRoot;

  std::function<void(Operation *, StringRef)> assignForkCodeUnits;
  assignForkCodeUnits = [&](Operation *operation, StringRef parentHierarchy) {
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(operation);
        assignment && assignment.getHasTimingControl() &&
        assignment.getAssignmentKind() ==
            semantic::SVAssignmentKind::Nonblocking) {
      SmallVector<Operation *> children = getChildren(assignment);
      if (children.size() == 3 &&
          !isa<semantic::SVDelayControlOp>(children.front())) {
        uint64_t nodeID = assignment.getNodeId();
        std::string hierarchy =
            (Twine(parentHierarchy) + ".$nba_event." + Twine(nodeID)).str();
        uint64_t id = stableCodeUnitID(hierarchy);
        auto [collision, inserted] = codeUnitIDs.try_emplace(id, assignment);
        if (!inserted) {
          emitError(getSemanticLocation(assignment))
              << "stable deferred-NBA code-unit ID collision for '" << hierarchy
              << "'";
          emitRemark(getSemanticLocation(collision->second))
              << "colliding code unit is here";
          invalid = true;
        } else {
          assignment->setAttr(
              "obelisk_sim.nba_event_code_unit_id",
              IntegerAttr::get(IntegerType::get(context, 64), id));
          assignment->setAttr("obelisk_sim.nba_event_hierarchy",
                              builder.getStringAttr(hierarchy));
        }
      }
    }
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
  for (PreparedUnit &unit : result.units)
    assignForkCodeUnits(unit.source, unit.hierarchy);
  if (invalid)
    return failure();

  sim::SimCodeUnitDeclOp::create(
      builder, module.getLoc(), result.rootID, uint64_t{0},
      sim::EntryKind::RootInitializer, builder.getStringAttr("__obelisk_root"),
      builder.getStringAttr("root initializer"), UnitAttr{});
  for (PreparedUnit &unit : result.units) {
    auto declaration = sim::SimCodeUnitDeclOp::create(
        builder, getSemanticLocation(unit.source), unit.id,
        scopes.lookup(unit.source), unit.entryKind,
        builder.getStringAttr(unit.hierarchy),
        builder.getStringAttr(getDebugName(unit.source)),
        isa<semantic::SVPortConnectionOp>(unit.source) ? builder.getUnitAttr()
                                                       : UnitAttr{});
    result.declarations[unit.source] = declaration;
  }
  return result;
}

} // namespace obelisk::simlowering
