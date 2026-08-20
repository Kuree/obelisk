//===- PrepareUnits.cpp - Simulation code-unit planning -------------------===//

#include "PrepareUnits.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
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
  if (op->hasAttr(sequenceEndpointEventAttrName))
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
  if (op->hasAttr(sequenceEndpointEventAttrName))
    return (getHierarchyName(op) + ".$sequence_endpoint").str();
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
  // Prefer the elaborator's symbol identity. Paths can differ in spelling for
  // defaulted constructors and out-of-block class methods.
  if (SymbolRefAttr reference = call.getReferencedSymbolAttr()) {
    auto symbol = symbols.find(reference.getLeafReference());
    if (symbol != symbols.end() && directCalleeNames.count(symbol->second))
      return symbol->second;
  }
  if (std::optional<StringRef> path = call.getReferencedPath()) {
    auto source = directCalleeSources.find(*path);
    if (source != directCalleeSources.end())
      return source->second;
  }
  return nullptr;
}

SmallVector<Operation *> PreparedUnits::resolveVirtualInterfaceCallees(
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
  SmallVector<Operation *> result;
  for (const PreparedVirtualInterfaceCallee &candidate :
       virtualInterfaceCallees) {
    if (candidate.method != call.getCalleeName() ||
        candidate.interfaceIdentity != identity || candidate.design != design)
      continue;
    if (selectedModport.empty() ||
        call->hasAttr("virtual_interface_call_import"))
      result.push_back(candidate.source);
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

  for (auto [index, source] : llvm::enumerate(sourceUnits)) {
    if (auto exported =
            source->getAttrOfType<StringAttr>("dpi_export_c_identifier")) {
      emitWarning(getSemanticLocation(source))
          << "DPI export '" << exported.getValue()
          << "' has no generated C entry point; lowering its SystemVerilog "
             "body for internal calls only";
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
          if (block.getBlockKind() ==
              semantic::SVStatementBlockKind::JoinNone)
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
      auto body =
          subroutine->getParentOfType<semantic::SVInstanceBodySymbolOp>();
      if (body && !body->hasAttr("is_virtual_interface_type_instance")) {
        auto identity =
            body->getAttrOfType<SymbolRefAttr>("virtual_interface_identity");
        semantic::SVInstanceSymbolOp top;
        for (Operation *parent = subroutine; parent;
             parent = parent->getParentOp())
          if (auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(parent))
            top = instance;
        if (identity && top)
          result.virtualInterfaceCallees.push_back(
              {source, identity, subroutine.getName().value_or("").str(),
               getHierarchyName(top).str()});
      }
    }
    if (!isa<semantic::SVPortConnectionOp, semantic::SVVariableSymbolOp,
             semantic::SVNetSymbolOp, semantic::SVClassPropertySymbolOp,
             semantic::SVSequenceSymbolOp>(source)) {
      result.directCalleeSources[hierarchy] = source;
      result.directCalleeNames[source] = symbol;
    }
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
      bool compatible = lhs.getSemanticType() == rhs.getSemanticType() &&
                        lhs.getSubroutineKind() == rhs.getSubroutineKind();
      SmallVector<semantic::SVFormalArgumentSymbolOp> lhsFormals;
      SmallVector<semantic::SVFormalArgumentSymbolOp> rhsFormals;
      for (Operation *child : getChildren(lhs))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          lhsFormals.push_back(formal);
      for (Operation *child : getChildren(rhs))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          rhsFormals.push_back(formal);
      compatible &= lhsFormals.size() == rhsFormals.size();
      if (compatible)
        for (auto [lhsFormal, rhsFormal] :
             llvm::zip_equal(lhsFormals, rhsFormals))
          compatible &=
              lhsFormal.getDirection() == rhsFormal.getDirection() &&
              lhsFormal.getSemanticType() == rhsFormal.getSemanticType();
      if (!compatible) {
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
      auto event = dyn_cast<semantic::SVSignalEventControlOp>(nested);
      if (!event)
        return;
      SmallVector<Operation *> children = getChildren(event);
      if (children.empty())
        return;
      if (auto instance = dyn_cast<semantic::SVAssertionInstanceExpressionOp>(
              children.front()))
        if (auto type =
                instance->getAttrOfType<TypeAttr>("semantic_type");
            type && isa<semantic::SequenceType>(type.getValue()))
          return;
      // A virtual clocking-block event is lowered directly to the dynamically
      // selected clock descriptor. Outlining its void-typed surface
      // expression as a value observer would lose that interface handle.
      if (children.front()->hasAttr("virtual_interface_clocking_block_event"))
        return;
      ObserverResult primaryResult = ObserverResult::Value;
      FailureOr<Type> primaryType =
          children.front()->hasAttr("virtual_interface_clocking_block_event")
              ? FailureOr<Type>(sim::LogicType::get(module.getContext(), 1))
              : getNormalizedSemanticType(children.front());
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
    if (candidate.label == "abort")
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
