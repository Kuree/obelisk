//===- PrepareTopology.cpp - Static design topology analysis -------------===//
//
// Resolves semantic port views and aliases before descriptor and net topology
// materialization.
//
//===----------------------------------------------------------------------===//

#include "PrepareTopology.h"

#include "Detail.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
#include "obelisk/Reflection/VPIObjectModel.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"

#include <algorithm>
#include <functional>
#include <limits>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

static sim::Strength lowerChargeStrength(semantic::SVChargeStrength strength) {
  switch (strength) {
  case semantic::SVChargeStrength::Small:
    return sim::Strength::Small;
  case semantic::SVChargeStrength::Medium:
    return sim::Strength::Medium;
  case semantic::SVChargeStrength::Large:
    return sim::Strength::Large;
  }
  llvm_unreachable("unknown SystemVerilog charge strength");
}

FailureOr<StaticStorageView> getStaticStorageView(Operation *expression) {
  StringRef path;
  if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression))
    path = named.getReferencedPath();
  else if (auto hierarchical =
               dyn_cast<semantic::SVHierarchicalValueExpressionOp>(expression))
    path = hierarchical.getReferencedPath();
  if (!path.empty()) {
    FailureOr<Type> type = getNormalizedSemanticType(expression);
    auto semanticType = expression->getAttrOfType<TypeAttr>("semantic_type");
    if (failed(type) || !semanticType)
      return failure();
    return StaticStorageView{
        path.str(),
        *type,
        *type,
        semanticType.getValue(),
        expression->getAttrOfType<ArrayAttr>(vpiTypedefLayersAttrName),
        0,
        0,
        {},
        *type};
  }

  SmallVector<Operation *> children = getChildren(expression);
  if (children.empty())
    return failure();
  FailureOr<StaticStorageView> base = getStaticStorageView(children.front());
  FailureOr<Type> resultType = getNormalizedSemanticType(expression);
  if (failed(base) || failed(resultType))
    return failure();
  auto resultSemanticType =
      expression->getAttrOfType<TypeAttr>("semantic_type");
  if (!resultSemanticType)
    return failure();
  base->identity = false;

  if (auto member =
          dyn_cast<semantic::SVMemberAccessExpressionOp>(expression)) {
    // A declaration-order subelement cannot be represented after a packed
    // bit view.
    if (base->packedOffset != 0)
      return failure();
    auto ordinal = member->getAttrOfType<IntegerAttr>("field_ordinal");
    if (!ordinal || ordinal.getValue().isNegative())
      return failure();
    auto subelement = sim::getAggregateProvenanceSubelement(
        base->viewType, ordinal.getValue().getZExtValue());
    if (!subelement || subelement->first > UINT64_MAX - base->offset)
      return failure();
    base->offset += subelement->first;
    base->indices.push_back(ordinal.getValue().getZExtValue());
    base->viewType = *resultType;
    base->semanticType = resultSemanticType.getValue();
    base->typedefLayers =
        expression->getAttrOfType<ArrayAttr>(vpiTypedefLayersAttrName);
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
        ordinal.ugt(llvm::APInt(65, std::numeric_limits<unsigned>::max())))
      return failure();
    auto subelement = sim::getAggregateProvenanceSubelement(
        base->viewType, static_cast<unsigned>(ordinal.getZExtValue()));
    if (!subelement || subelement->first > UINT64_MAX - base->offset)
      return failure();
    base->offset += subelement->first;
    base->indices.push_back(static_cast<unsigned>(ordinal.getZExtValue()));
    base->viewType = *resultType;
    base->semanticType = resultSemanticType.getValue();
    base->typedefLayers =
        expression->getAttrOfType<ArrayAttr>(vpiTypedefLayersAttrName);
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
    llvm::APInt offset = descending ? selected - boundary : boundary - selected;
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
    auto secondLiteral = dyn_cast<semantic::SVIntegerLiteralOp>(children[2]);
    if (!secondLiteral)
      return failure();
    FailureOr<ParsedConstant> second = parseSVInteger(
        secondLiteral.getConstantValue(), 64, getSemanticLocation(children[2]));
    if (failed(second) || !second->unknown.isZero())
      return failure();
    std::optional<uint64_t> other = physical(second->value.getSExtValue());
    if (!other)
      return failure();
    low = std::min(*low, *other);
  }
  if (*low > UINT64_MAX - base->offset ||
      *low > UINT64_MAX - base->packedOffset)
    return failure();
  base->offset += *low;
  base->packedOffset += *low;
  base->viewType = *resultType;
  base->semanticType = resultSemanticType.getValue();
  base->typedefLayers =
      expression->getAttrOfType<ArrayAttr>(vpiTypedefLayersAttrName);
  return *base;
}

} // namespace

static bool isStaticReturnVariable(Operation *op);

Operation *getSingleRegionRoot(Region &region) {
  if (region.empty() || region.front().empty())
    return nullptr;
  return &region.front().front();
}

Operation *getPortActualLValue(semantic::SVPortConnectionOp connection) {
  Operation *actual = getSingleRegionRoot(connection.getActual());
  auto assignment =
      dyn_cast_or_null<semantic::SVAssignmentExpressionOp>(actual);
  if (!assignment)
    return actual;
  SmallVector<Operation *> children = getChildren(assignment);
  if (children.size() != 2)
    return actual;
  Operation *placeholder = children[1];
  bool strippedConversion = false;
  while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
    strippedConversion = true;
    SmallVector<Operation *> converted = getChildren(placeholder);
    if (converted.size() != 1)
      return actual;
    placeholder = converted.front();
  }
  if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder) &&
      (!strippedConversion ||
       connection.getDirection() == semantic::SVArgumentDirection::InOut))
    return children.front();
  return actual;
}

FailureOr<PreparedPortAliases>
analyzePortAliases(semantic::SVRootSymbolOp semanticRoot) {
  PreparedPortAliases result;
  bool invalid = false;

  // IEEE 1800-2017 6.17 and 15.5.5 make event variables assignable handles.
  // Inventory the events that require cells during the existing topology
  // walk. Stable direct event input ports can then share their actual's
  // descriptor without a time-zero propagation process. Mutable actuals or
  // formals retain cell-backed input propagation.
  struct EventInputCandidate {
    semantic::SVPortConnectionOp connection;
    std::string internal;
    std::optional<StaticStorageView> actual;
  };
  SmallVector<EventInputCandidate> eventInputs;
  auto collectEventCellReferences = [&](Operation *root) {
    root->walk<WalkOrder::PreOrder>([&](Operation *nested) {
      auto type = nested->getAttrOfType<TypeAttr>("semantic_type");
      if (!type || !isa<semantic::EventType>(type.getValue()))
        return;
      StringRef path;
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested))
        path = named.getReferencedPath();
      else if (auto hierarchical =
                   dyn_cast<semantic::SVHierarchicalValueExpressionOp>(nested))
        path = hierarchical.getReferencedPath();
      else if (auto member =
                   dyn_cast<semantic::SVMemberAccessExpressionOp>(nested))
        path = member.getReferencedPath();
      if (!path.empty())
        result.eventCellPaths.insert(path);
    });
  };

  // IEEE 1800-2023 10.11 net aliases are static topology, not executable
  // connectivity.  Collapse direct whole-net aliases onto one descriptor so
  // drivers, readers, and observers all use the same resolved net without
  // adding any runtime propagation work.
  llvm::StringMap<std::string> netAliasParents;
  auto findNetAlias = [&](StringRef path) -> std::string {
    SmallVector<std::string> chain;
    std::string current = path.str();
    while (true) {
      auto [it, inserted] = netAliasParents.try_emplace(current, current);
      if (inserted || it->second == current)
        break;
      chain.push_back(current);
      current = it->second;
    }
    for (StringRef member : chain)
      netAliasParents[member] = current;
    return current;
  };
  auto uniteNetAliases = [&](StringRef lhs, StringRef rhs) {
    std::string lhsRoot = findNetAlias(lhs);
    std::string rhsRoot = findNetAlias(rhs);
    if (lhsRoot == rhsRoot)
      return false;
    netAliasParents[rhsRoot] = lhsRoot;
    return true;
  };
  auto recordNetAlias = [&](semantic::SVNetAliasSymbolOp alias) {
    SmallVector<Operation *> expressions = getChildren(alias);
    if (expressions.size() < 2)
      return;
    SmallVector<std::string> paths;
    for (Operation *expression : expressions) {
      FailureOr<StaticStorageView> view = getStaticStorageView(expression);
      if (failed(view) || !view->identity || view->offset != 0 ||
          view->packedOffset != 0 || !view->indices.empty() ||
          view->rootType != view->viewType)
        return;
      paths.push_back(view->path);
    }
    for (StringRef path : ArrayRef<std::string>(paths).drop_front())
      uniteNetAliases(paths.front(), path);
  };

  // Fold alias, port, and event-cell collection into one semantic-tree walk.
  semanticRoot->walk([&](Operation *op) {
    if (isStaticFormal(op)) {
      auto type = op->getAttrOfType<TypeAttr>("semantic_type");
      if (type && isa<semantic::EventType>(type.getValue()))
        result.eventCellPaths.insert(getHierarchyName(op));
      return;
    }
    if (auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op)) {
      auto type = variable->getAttrOfType<TypeAttr>("semantic_type");
      if (type && isa<semantic::EventType>(type.getValue()) &&
          (!getChildren(op).empty() || isStaticReturnVariable(op)))
        result.eventCellPaths.insert(getHierarchyName(op));
      return;
    }
    if (auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(op)) {
      auto type = property->getAttrOfType<TypeAttr>("semantic_type");
      if (type && isa<semantic::EventType>(type.getValue()) &&
          !getChildren(op).empty())
        result.eventCellPaths.insert(getHierarchyName(op));
      return;
    }
    if (auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(op)) {
      SmallVector<Operation *> children = getChildren(assignment);
      size_t destination = assignment.getHasTimingControl() ? 1 : 0;
      if (destination < children.size())
        collectEventCellReferences(children[destination]);
      return;
    }
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(op);
        call && call.getHasOutputArguments()) {
      for (Operation *child : getChildren(call))
        collectEventCellReferences(child);
      return;
    }
    if (auto alias = dyn_cast<semantic::SVNetAliasSymbolOp>(op)) {
      recordNetAlias(alias);
      return;
    }
    auto connection = dyn_cast<semantic::SVPortConnectionOp>(op);
    if (!connection)
      return;
    if (isCompileTimeOnlyInstanceMember(connection))
      return;
    result.connections.push_back(connection);
    if (connection.getDirection() == semantic::SVArgumentDirection::In &&
        isa<semantic::EventType>(connection.getFormalType())) {
      StringRef internal = connection.getInternalPath().value_or(StringRef{});
      Operation *actual = getSingleRegionRoot(connection.getActual());
      FailureOr<StaticStorageView> view =
          actual ? getStaticStorageView(actual)
                 : FailureOr<StaticStorageView>(failure());
      if (internal.empty()) {
        emitError(getSemanticLocation(connection))
            << "event input port has no internal event object";
        invalid = true;
      } else {
        eventInputs.push_back(
            {connection, internal.str(),
             succeeded(view) ? std::optional(*view) : std::nullopt});
      }
      return;
    }
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
    result.aliases[internal] = view->path;
    result.refViews[internal] = *view;
  });
  auto isWholeEvent = [](const EventInputCandidate &candidate) {
    return candidate.actual && candidate.actual->identity &&
           candidate.actual->offset == 0 &&
           candidate.actual->packedOffset == 0 &&
           candidate.actual->indices.empty() &&
           isa<sim::EventType>(candidate.actual->rootType);
  };
  for (const EventInputCandidate &candidate : eventInputs) {
    if (isWholeEvent(candidate))
      continue;
    // A computed actual cannot share one descriptor or cell. Retain a formal
    // cell and executable propagation so handle replacement remains live.
    result.eventCellPaths.insert(candidate.internal);
  }
  // Classify the full event-input graph before choosing aliases or executable
  // connections. A live cell may flow through any number of read-only input
  // formals before reaching a child-written formal that needs its own cell.
  // The fixpoint keeps that last edge live instead of freezing a time-zero
  // handle merely because its immediate source formal is itself aliased.
  if (!eventInputs.empty()) {
    llvm::StringSet<> liveEventPaths = result.eventCellPaths;
    bool changed;
    do {
      changed = false;
      // A whole-event ref port is the same cell under two elaborated paths.
      // Propagate liveness in both directions so traversal order and which
      // alias spelling an input uses cannot turn a live handle into
      // PortInitialize.
      for (const auto &[path, view] : result.refViews) {
        bool wholeEvent = view.identity && view.offset == 0 &&
                          view.packedOffset == 0 && view.indices.empty() &&
                          isa<sim::EventType>(view.rootType) &&
                          isa<sim::EventType>(view.viewType);
        if (!wholeEvent)
          continue;
        bool live =
            liveEventPaths.contains(path) || liveEventPaths.contains(view.path);
        if (live) {
          changed |= liveEventPaths.insert(path).second;
          changed |= liveEventPaths.insert(view.path).second;
        }
      }
      for (const EventInputCandidate &candidate : eventInputs) {
        if (!isWholeEvent(candidate))
          continue;
        bool liveActual = liveEventPaths.contains(candidate.actual->path);
        if (liveActual)
          changed |= liveEventPaths.insert(candidate.internal).second;
      }
    } while (changed);
    std::function<bool(Operation *)> isDependencyFreeEventExpression =
        [&](Operation *expression) {
          if (expression->hasAttr("folded_constant"))
            return true;
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
            if (succeeded(type) && isa<sim::EventType>(*type))
              return !liveEventPaths.contains(path);
            return false;
          }
          if (isa<semantic::SVIntegerLiteralOp,
                  semantic::SVUnbasedUnsizedIntegerLiteralOp>(expression))
            return true;
          if (!isa<semantic::SVConditionalExpressionOp,
                   semantic::SVConversionExpressionOp>(expression))
            return false;
          SmallVector<Operation *> children = getChildren(expression);
          return !children.empty() &&
                 llvm::all_of(children, isDependencyFreeEventExpression);
        };
    std::function<bool(Operation *)> isPureComputedEventExpression =
        [&](Operation *expression) {
          StringRef name = expression->getName().getStringRef();
          if (!name.starts_with("obelisk.sv.expression.") ||
              isa<semantic::SVCallExpressionOp,
                  semantic::SVAssignmentExpressionOp>(expression) ||
              name.starts_with("obelisk.sv.expression.new_"))
            return false;
          if (auto unary =
                  dyn_cast<semantic::SVUnaryExpressionOp>(expression)) {
            using Unary = semantic::SVUnaryOperator;
            Unary kind = unary.getOperatorKind();
            if (kind == Unary::Preincrement || kind == Unary::Predecrement ||
                kind == Unary::Postincrement || kind == Unary::Postdecrement)
              return false;
          }
          SmallVector<Operation *> children = getChildren(expression);
          return llvm::all_of(children, isPureComputedEventExpression);
        };
    for (EventInputCandidate &candidate : eventInputs) {
      if (isWholeEvent(candidate) || candidate.connection.getActualIsConstant())
        continue;
      Operation *actual = getSingleRegionRoot(candidate.connection.getActual());
      bool hasCall = false;
      if (actual)
        actual->walk([&](semantic::SVCallExpressionOp) { hasCall = true; });
      if (hasCall) {
        emitError(getSemanticLocation(candidate.connection))
            << "computed event input startup dependency has unresolved call "
               "effects";
        invalid = true;
      } else if (actual && !isPureComputedEventExpression(actual)) {
        emitError(getSemanticLocation(candidate.connection))
            << "computed event input actual is not a side-effect-free event "
               "expression";
        invalid = true;
      } else if (actual && isDependencyFreeEventExpression(actual))
        candidate.connection->setAttr(
            "actual_is_constant",
            BoolAttr::get(candidate.connection.getContext(), true));
    }
    for (EventInputCandidate &candidate : eventInputs) {
      if (!isWholeEvent(candidate))
        continue;
      bool liveActual = liveEventPaths.contains(candidate.actual->path);
      bool writtenFormal = result.eventCellPaths.contains(candidate.internal);
      if (!writtenFormal) {
        // A read-only input may share either a direct scheduler descriptor or
        // an event cell. The latter remains live without an executable
        // process.
        result.aliases[candidate.internal] = candidate.actual->path;
        continue;
      }
      result.eventCellPaths.insert(candidate.internal);
      if (!liveActual)
        candidate.connection->setAttr(
            "actual_is_constant",
            BoolAttr::get(candidate.connection.getContext(), true));
    }
  }
  if (!eventInputs.empty())
    llvm::erase_if(
        result.connections, [&](semantic::SVPortConnectionOp connection) {
          if (connection.getDirection() != semantic::SVArgumentDirection::In ||
              !isa<semantic::EventType>(connection.getFormalType()))
            return false;
          StringRef internal =
              connection.getInternalPath().value_or(StringRef{});
          return !internal.empty() && result.aliases.count(internal);
        });
  auto isWholeRefView = [](const StaticStorageView &view) {
    return view.identity && view.offset == 0 && view.packedOffset == 0 &&
           view.indices.empty() && view.rootType == view.viewType;
  };

  // A whole ref port may itself participate in a net-alias class in prepared
  // MLIR.  Pull every transitive whole-view target into that class.  Selected
  // or aggregate ref views remain directional aliases and are not collapsed.
  llvm::StringSet<> flattenedTargets;
  llvm::StringSet<> queuedAliases;
  SmallVector<std::string> aliasWorklist;
  for (const auto &entry : netAliasParents) {
    queuedAliases.insert(entry.getKey());
    aliasWorklist.push_back(entry.getKey().str());
  }
  while (!aliasWorklist.empty()) {
    std::string formal = std::move(aliasWorklist.pop_back_val());
    auto view = result.refViews.find(formal);
    if (view == result.refViews.end() || !isWholeRefView(view->second))
      continue;
    flattenedTargets.insert(view->second.path);
    uniteNetAliases(formal, view->second.path);
    if (queuedAliases.insert(view->second.path).second)
      aliasWorklist.push_back(view->second.path);
  }

  // Prefer a real flattened target over a formal alias as the descriptor
  // owner.  Lexical selection makes two-port and repeated-alias groups stable
  // regardless of StringMap iteration order.  A cyclic port-only graph has no
  // terminal target and is left for the existing cyclic-port diagnostic.
  llvm::StringMap<std::string> externalCanonicalByRoot;
  llvm::StringSet<> rootsWithFlattenedTargets;
  for (StringRef target : flattenedTargets.keys()) {
    std::string root = findNetAlias(target);
    rootsWithFlattenedTargets.insert(root);
    if (result.aliases.count(target))
      continue;
    auto [it, inserted] =
        externalCanonicalByRoot.try_emplace(root, target.str());
    if (!inserted && target < it->second)
      it->second = target.str();
  }
  for (const auto &entry : netAliasParents) {
    StringRef path = entry.getKey();
    std::string root = findNetAlias(path);
    auto external = externalCanonicalByRoot.find(root);
    if (external == externalCanonicalByRoot.end()) {
      if (rootsWithFlattenedTargets.count(root) || result.aliases.count(path))
        continue;
      if (path != root)
        result.aliases[path] = root;
      continue;
    }
    if (auto view = result.refViews.find(path);
        view != result.refViews.end() && !isWholeRefView(view->second))
      continue;
    StringRef canonical = external->second;
    if (path != canonical)
      result.aliases[path] = canonical.str();
  }
  semanticRoot->walk([&](semantic::SVModportPortSymbolOp port) {
    if (isCompileTimeOnlyInstanceMember(port))
      return;
    Operation *modport = port->getParentOp();
    Operation *interfaceBody = modport ? modport->getParentOp() : nullptr;
    StringRef path = getHierarchyName(port);
    StringRef base = getHierarchyName(interfaceBody);
    StringRef name = getDebugName(port);
    if (path.empty())
      return;
    if (Operation *expression = getSingleRegionRoot(port.getBody())) {
      if (FailureOr<StaticStorageView> view = getStaticStorageView(expression);
          succeeded(view)) {
        result.interfaceAliases[path] = view->path;
        if (!view->identity || view->offset != 0 || view->packedOffset != 0 ||
            !view->indices.empty() || view->rootType != view->viewType)
          result.interfaceViews[path] = *view;
        return;
      }
      emitError(getSemanticLocation(port))
          << "interface modport expression requires executable rather than "
             "static-view lowering: "
          << path;
      invalid = true;
      return;
    }
    // Older serialized semantic IR did not carry the resolved connection.
    // Its implicit-port spelling aliases the identically named interface item.
    if (port->hasAttr("modport_explicit_connection")) {
      emitError(getSemanticLocation(port))
          << "empty interface modport expression is not executable: " << path;
      invalid = true;
      return;
    }
    if (!base.empty() && !name.empty())
      result.interfaceAliases[path] = (base + Twine(".") + name).str();
  });
  if (invalid)
    return failure();
  return result;
}

bool isAutomaticLocalSymbol(Operation *op) {
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
  // task locals need activation-owned storage because the task may suspend.
  if (auto subroutine = op->getParentOfType<semantic::SVSubroutineSymbolOp>();
      subroutine &&
      subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Function)
    return statementBlock != nullptr;
  return variable.getLifetime() == semantic::SVVariableLifetime::Automatic ||
         statementBlock != nullptr;
}

static bool isStaticReturnVariable(Operation *op) {
  auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op);
  if (!variable || !variable.getIsCompilerGenerated())
    return false;
  // IEEE 1800-2017 13.4.2 makes every declaration of a static subroutine
  // static, and 13.4.1 gives the return value a variable of its own. Slang
  // always models that compiler-generated variable as automatic, so the
  // subroutine's own lifetime decides here.
  auto subroutine = op->getParentOfType<semantic::SVSubroutineSymbolOp>();
  if (!subroutine ||
      subroutine.getDefaultLifetime() != semantic::SVVariableLifetime::Static)
    return false;
  std::optional<StringRef> returnPath = subroutine.getReturnVariablePath();
  return returnPath && *returnPath == getHierarchyName(op);
}

bool isStaticFormal(Operation *op) {
  auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(op);
  if (!formal || formal.getDirection() == semantic::SVArgumentDirection::Ref)
    return false;
  // A randsequence creates an automatic scope irrespective of the lifetime
  // of the function or task that contains it (IEEE 1800-2017 18.17). Its
  // production formals are activation-owned locals, never static subroutine
  // formals.
  if (op->getParentOfType<semantic::SVRandSeqProductionSymbolOp>())
    return false;
  auto subroutine = op->getParentOfType<semantic::SVSubroutineSymbolOp>();
  return subroutine && subroutine.getDefaultLifetime() ==
                           semantic::SVVariableLifetime::Static;
}

bool isNestedInCodeUnit(Operation *op) {
  for (Operation *parent = op->getParentOp(); parent;
       parent = parent->getParentOp())
    if (isCodeUnit(parent))
      return true;
  return false;
}

FailureOr<llvm::StringMap<DescriptorInfo>>
materializeDesignDescriptors(ModuleOp module,
                             semantic::SVRootSymbolOp semanticRoot,
                             const PreparedPortAliases &portAliases,
                             const PreparedScopeDeclarations &scopes,
                             const PreparedClassDeclarations &classes,
                             uint64_t designPrecisionFs, OpBuilder &builder) {
  llvm::StringMap<DescriptorInfo> descriptors;
  uint64_t nextStorageId = 0;
  uint64_t nextNetId = 0;
  uint64_t nextEventId = 0;
  bool invalid = false;
  SmallVector<Operation *> designObjects;

  const llvm::StringSet<> &eventCellPaths = portAliases.eventCellPaths;

  // Freeze every source object that can own the static reflection records
  // materialized below.  The anchor symbol, rather than an erased semantic
  // node ID or a display path, is the canonical identity of the object.
  using VPIKind = reflection::VPIObjectKind;
  llvm::StringMap<semantic::SVDefinitionKind> definitionKinds;
  llvm::StringMap<StringAttr> definitionNames;
  module.walk([&](semantic::SVDefinitionSymbolOp definition) {
    definitionKinds.try_emplace(definition.getSymName(),
                                definition.getDefinitionKind());
    if (StringAttr name = definition->getAttrOfType<StringAttr>("name"))
      definitionNames.try_emplace(definition.getSymName(), name);
  });
  auto primitiveKind = [&](semantic::SVPrimitiveInstanceSymbolOp primitive) {
    if (primitive->hasAttr("udp_metadata"))
      return VPIKind::Udp;
    StringAttr primitiveName =
        primitive->getAttrOfType<StringAttr>("primitive_name");
    StringRef name = primitiveName ? primitiveName.getValue() : StringRef{};
    bool isSwitch = name == "nmos" || name == "pmos" || name == "cmos" ||
                    name == "rnmos" || name == "rpmos" || name == "rcmos" ||
                    name == "tran" || name == "rtran" || name == "tranif0" ||
                    name == "tranif1" || name == "rtranif0" ||
                    name == "rtranif1";
    return isSwitch ? VPIKind::Switch : VPIKind::Gate;
  };
  auto instanceLeafKind = [&](Operation *leaf) -> std::optional<VPIKind> {
    if (auto primitive =
            dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(leaf)) {
      switch (primitiveKind(primitive)) {
      case VPIKind::Gate:
        return VPIKind::GateArray;
      case VPIKind::Switch:
        return VPIKind::SwitchArray;
      case VPIKind::Udp:
        return VPIKind::UdpArray;
      default:
        llvm_unreachable("unexpected primitive VPI kind");
      }
    }
    auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(leaf);
    if (!instance)
      return std::nullopt;
    if (auto reference = instance.getReferencedSymbolAttr()) {
      auto found = definitionKinds.find(reference.getLeafReference());
      if (found != definitionKinds.end()) {
        switch (found->second) {
        case semantic::SVDefinitionKind::Interface:
          return VPIKind::InterfaceArray;
        case semantic::SVDefinitionKind::Program:
          return VPIKind::ProgramArray;
        case semantic::SVDefinitionKind::Module:
          return VPIKind::ModuleArray;
        }
      }
    }
    for (Operation &child : instance.getBody().front()) {
      auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(&child);
      if (!body)
        continue;
      if (auto kind = body->getAttrOfType<IntegerAttr>("vpi_scope_kind")) {
        switch (static_cast<VPIKind>(kind.getValue().getZExtValue())) {
        case VPIKind::Interface:
          return VPIKind::InterfaceArray;
        case VPIKind::Program:
          return VPIKind::ProgramArray;
        default:
          return VPIKind::ModuleArray;
        }
      }
    }
    return VPIKind::ModuleArray;
  };
  llvm::DenseMap<Operation *, SmallVector<int64_t>> fixedArrayRanges;
  llvm::DenseMap<Operation *, VPIKind> fixedArrayKinds;
  llvm::DenseMap<Operation *, SmallVector<int64_t>> sparseArrayIndices;
  llvm::DenseMap<Operation *, Operation *> relationArrayMemberRoots;
  llvm::DenseMap<Operation *, SmallVector<int64_t>> relationArrayMemberIndices;
  llvm::DenseMap<Operation *, SmallVector<int64_t>> namedEventArrayRanges;
  llvm::DenseSet<Operation *> scalarNamedEvents;
  module.walk([&](semantic::SVInstanceArraySymbolOp array) {
    if (isa_and_nonnull<semantic::SVInstanceArraySymbolOp>(
            array->getParentOp()))
      return;
    // Legacy hand-authored semantic IR predates exact source ranges. It can
    // still lower executable array values, but it cannot safely contribute a
    // relation-indexed VPI identity.
    if (!array.getArrayRangeAttr())
      return;
    SmallVector<int64_t> ranges;
    std::optional<VPIKind> leafKind;
    std::optional<unsigned> leafDepth;
    SmallVector<int64_t> path;
    SmallVector<std::pair<Operation *, SmallVector<int64_t>>> memberPlans;
    std::function<bool(semantic::SVInstanceArraySymbolOp, unsigned)> visit =
        [&](semantic::SVInstanceArraySymbolOp current,
            unsigned dimension) -> bool {
      DenseI64ArrayAttr rangeAttr = current.getArrayRangeAttr();
      if (!rangeAttr || rangeAttr.size() != 2) {
        emitError(getSemanticLocation(current))
            << "instance-array dimension requires an exact source range";
        return false;
      }
      ArrayRef<int64_t> range = rangeAttr.asArrayRef();
      if (ranges.size() == dimension * 2)
        llvm::append_range(ranges, range);
      else if (ranges[dimension * 2] != range[0] ||
               ranges[dimension * 2 + 1] != range[1]) {
        emitError(getSemanticLocation(current))
            << "instance-array branches have mismatched dimension ranges";
        return false;
      }
      uint64_t distance = range[0] >= range[1]
                              ? static_cast<uint64_t>(range[0]) -
                                    static_cast<uint64_t>(range[1])
                              : static_cast<uint64_t>(range[1]) -
                                    static_cast<uint64_t>(range[0]);
      if (distance == UINT64_MAX || distance + 1 > UINT32_MAX) {
        emitError(getSemanticLocation(current))
            << "instance-array dimension exceeds VPI relation encoding";
        return false;
      }
      uint64_t extent = distance + 1;
      SmallVector<Operation *> elements;
      for (Operation &child : current.getBody().front())
        if (isa<semantic::SVInstanceArraySymbolOp, semantic::SVInstanceSymbolOp,
                semantic::SVPrimitiveInstanceSymbolOp>(&child))
          elements.push_back(&child);
      if (elements.size() != extent) {
        emitError(getSemanticLocation(current))
            << "instance-array dimension has " << elements.size()
            << " elements but its source range requires " << extent;
        return false;
      }
      bool nested = isa<semantic::SVInstanceArraySymbolOp>(elements.front());
      for (Operation *element : elements)
        if (isa<semantic::SVInstanceArraySymbolOp>(element) != nested) {
          emitError(getSemanticLocation(current))
              << "instance-array dimension mixes nested arrays and leaves";
          return false;
        }
      int64_t lower = std::min(range[0], range[1]);
      for (auto [ordinal, element] : llvm::enumerate(elements)) {
        __int128 index = static_cast<__int128>(lower) + ordinal;
        if (index < INT64_MIN || index > INT64_MAX)
          return false;
        path.push_back(static_cast<int64_t>(index));
        if (nested) {
          if (!visit(cast<semantic::SVInstanceArraySymbolOp>(element),
                     dimension + 1))
            return false;
        } else {
          unsigned depth = dimension + 1;
          if (leafDepth && *leafDepth != depth) {
            emitError(getSemanticLocation(element))
                << "instance-array branches have mismatched terminal ranks";
            return false;
          }
          leafDepth = depth;
          std::optional<VPIKind> kind = instanceLeafKind(element);
          if (!kind) {
            emitError(getSemanticLocation(element))
                << "instance-array leaf has no supported VPI identity";
            return false;
          }
          if (leafKind && *leafKind != *kind) {
            emitError(getSemanticLocation(element))
                << "instance-array leaves have mixed VPI object kinds";
            return false;
          }
          leafKind = *kind;
          memberPlans.emplace_back(element, path);
        }
        path.pop_back();
      }
      return true;
    };
    if (!visit(array, 0) || !leafKind) {
      invalid = true;
      return;
    }
    if ((*leafKind == VPIKind::GateArray || *leafKind == VPIKind::SwitchArray ||
         *leafKind == VPIKind::UdpArray) &&
        ranges.size() != 2) {
      emitError(getSemanticLocation(array))
          << "primitive instance arrays must be one-dimensional";
      invalid = true;
      return;
    }
    fixedArrayKinds.try_emplace(array, *leafKind);
    fixedArrayRanges.try_emplace(array, std::move(ranges));
    for (auto &[element, indices] : memberPlans) {
      relationArrayMemberRoots[element] = array;
      relationArrayMemberIndices[element] = std::move(indices);
    }
  });
  module.walk([&](semantic::SVGenerateBlockArraySymbolOp array) {
    if (DenseI64ArrayAttr indices = array.getArrayIndicesAttr()) {
      SmallVector<Operation *> elements;
      for (Operation &child : array.getBody().front()) {
        if (isa<semantic::SVGenerateBlockSymbolOp>(&child)) {
          if (auto uninstantiated =
                  child.getAttrOfType<BoolAttr>("is_uninstantiated");
              uninstantiated && uninstantiated.getValue()) {
            emitError(getSemanticLocation(&child))
                << "generate-array contains an uninstantiated indexed "
                   "element";
            invalid = true;
            return;
          }
          elements.push_back(&child);
        }
      }
      if (elements.size() != static_cast<size_t>(indices.size())) {
        emitError(getSemanticLocation(array))
            << "generate-array source indices do not match its elements";
        invalid = true;
        return;
      }
      llvm::DenseSet<int64_t> uniqueIndices;
      for (int64_t index : indices.asArrayRef())
        if (!uniqueIndices.insert(index).second) {
          emitError(getSemanticLocation(array))
              << "generate-array source indices must be unique";
          invalid = true;
          return;
        }
      sparseArrayIndices.try_emplace(
          array, SmallVector<int64_t>(indices.asArrayRef()));
      for (auto [ordinal, element] : llvm::enumerate(elements)) {
        relationArrayMemberRoots[element] = array;
        relationArrayMemberIndices[element] = {indices.asArrayRef()[ordinal]};
      }
    }
  });
  module.walk([&](semantic::SVVariableSymbolOp variable) {
    TypeAttr semanticType = variable->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType)
      return;
    Type current = semanticType.getValue();
    SmallVector<int64_t> ranges;
    for (;;) {
      if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(current)) {
        ranges.push_back(array.getLeft());
        ranges.push_back(array.getRight());
        current = array.getElementType();
        continue;
      }
      if (auto array = dyn_cast<semantic::UnpackedArrayType>(current)) {
        ranges.push_back(static_cast<int64_t>(array.getSize()) - 1);
        ranges.push_back(0);
        current = array.getElementType();
        continue;
      }
      break;
    }
    if (!isa<semantic::EventType>(current))
      return;
    if (ranges.empty())
      scalarNamedEvents.insert(variable);
    else
      namedEventArrayRanges.try_emplace(variable, std::move(ranges));
  });
  auto sourceAnchorKind = [&](Operation *operation) -> std::optional<VPIKind> {
    if (!isa<semantic::SVCompilationUnitSymbolOp, semantic::SVPackageSymbolOp,
             semantic::SVClassTypeOp, semantic::SVSubroutineSymbolOp,
             semantic::SVPropertySymbolOp, semantic::SVSequenceSymbolOp,
             semantic::SVClockingBlockSymbolOp, semantic::SVVariableSymbolOp,
             semantic::SVInstanceArraySymbolOp,
             semantic::SVGenerateBlockArraySymbolOp,
             semantic::SVPrimitiveInstanceSymbolOp,
             semantic::SVGenerateBlockSymbolOp,
             semantic::SVInstanceBodySymbolOp>(operation))
      return std::nullopt;
    // Slang materializes interface bodies solely to describe parameterized
    // virtual-interface types.  They have no run-time instance identity and
    // must not become traversable vpiInterface objects.  Their typespecs are
    // retained below and owned by the nearest persistent lexical anchor.
    if (isCompileTimeOnlyInstanceMember(operation))
      return std::nullopt;
    if (isa<semantic::SVCompilationUnitSymbolOp, semantic::SVPackageSymbolOp>(
            operation))
      return VPIKind::Package;
    if (isa<semantic::SVClassTypeOp>(operation))
      return VPIKind::ClassDefn;
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(operation))
      return subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Task
                 ? VPIKind::Task
                 : VPIKind::Function;
    if (isa<semantic::SVPropertySymbolOp>(operation))
      return VPIKind::PropertyDecl;
    if (isa<semantic::SVSequenceSymbolOp>(operation))
      return VPIKind::SequenceDecl;
    if (isa<semantic::SVClockingBlockSymbolOp>(operation))
      return VPIKind::ClockingBlock;
    if (isa<semantic::SVVariableSymbolOp>(operation)) {
      if (namedEventArrayRanges.count(operation))
        return VPIKind::NamedEventArray;
      return scalarNamedEvents.contains(operation)
                 ? std::optional(VPIKind::NamedEvent)
                 : std::nullopt;
    }
    if (auto array = dyn_cast<semantic::SVInstanceArraySymbolOp>(operation)) {
      if (!fixedArrayRanges.count(array))
        return std::nullopt;
      return fixedArrayKinds.lookup(array);
    }
    if (auto array =
            dyn_cast<semantic::SVGenerateBlockArraySymbolOp>(operation))
      return sparseArrayIndices.count(array)
                 ? std::optional(VPIKind::GenScopeArray)
                 : std::nullopt;
    if (auto primitive =
            dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(operation))
      return primitiveKind(primitive);
    if (auto generate =
            dyn_cast<semantic::SVGenerateBlockSymbolOp>(operation)) {
      if (auto uninstantiated =
              generate->getAttrOfType<BoolAttr>("is_uninstantiated");
          uninstantiated && uninstantiated.getValue())
        return std::nullopt;
      return VPIKind::GenScope;
    }
    if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(operation)) {
      if (auto kind = body->getAttrOfType<IntegerAttr>("vpi_scope_kind"))
        return static_cast<VPIKind>(kind.getValue().getZExtValue());
      if (body->hasAttr("virtual_interface_identity"))
        return VPIKind::Interface;
      if (auto instance = dyn_cast_or_null<semantic::SVInstanceSymbolOp>(
              body->getParentOp()))
        if (auto reference = instance.getReferencedSymbolAttr()) {
          auto found = definitionKinds.find(reference.getLeafReference());
          if (found != definitionKinds.end()) {
            switch (found->second) {
            case semantic::SVDefinitionKind::Module:
              return VPIKind::Module;
            case semantic::SVDefinitionKind::Interface:
              return VPIKind::Interface;
            case semantic::SVDefinitionKind::Program:
              return VPIKind::Program;
            }
          }
        }
      return VPIKind::Module;
    }
    return std::nullopt;
  };

  SmallVector<Operation *> anchorSources;
  llvm::DenseMap<Operation *, VPIKind> anchorKinds;
  module.walk<WalkOrder::PreOrder>([&](Operation *operation) {
    std::optional<VPIKind> kind = sourceAnchorKind(operation);
    if (!kind)
      return;
    anchorSources.push_back(operation);
    anchorKinds[operation] = *kind;
  });
  llvm::DenseMap<Operation *, FlatSymbolRefAttr> anchorSymbols;
  llvm::DenseMap<Operation *, uint64_t> anchorInventoryIds;
  llvm::DenseMap<Operation *, sim::SimVPIObjectAnchorOp> anchorDeclarations;
  for (auto [inventoryId, source] : llvm::enumerate(anchorSources)) {
    std::string symbolName =
        "__obelisk_vpi_anchor_" + std::to_string(inventoryId);
    anchorSymbols[source] =
        FlatSymbolRefAttr::get(builder.getContext(), symbolName);
    anchorInventoryIds[source] = inventoryId;
  }
  llvm::DenseMap<Operation *, uint64_t> nextAnchorOrdinal;
  auto identityProperties = [&](Operation *source, VPIKind sourceKind) {
    SmallVector<Attribute> properties;
    auto addBoolean = [&](uint32_t selector, bool value) {
      if (!value)
        return;
      properties.push_back(sim::VPIPropertyAttr::get(
          builder.getContext(), builder.getI32IntegerAttr(selector),
          builder.getBoolAttr(true)));
    };
    auto addString = [&](uint32_t selector, StringAttr value) {
      if (!value)
        return;
      properties.push_back(sim::VPIPropertyAttr::get(
          builder.getContext(), builder.getI32IntegerAttr(selector), value));
    };

    bool top = false;
    if (BoolAttr frozen =
            source->getAttrOfType<BoolAttr>("obelisk_sim.vpi_top")) {
      top = frozen.getValue();
    } else if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(source)) {
      Operation *instance = body->getParentOp();
      top = isa_and_nonnull<semantic::SVInstanceSymbolOp>(instance) &&
            isa_and_nonnull<semantic::SVRootSymbolOp>(instance->getParentOp());
    }
    if (isa<semantic::SVPackageSymbolOp, semantic::SVCompilationUnitSymbolOp>(
            source))
      top = true;
    BoolAttr cell =
        source->getAttrOfType<BoolAttr>("obelisk_sim.vpi_cell_instance");
    BoolAttr automatic =
        source->getAttrOfType<BoolAttr>("obelisk_sim.vpi_automatic");
    if (sourceKind == VPIKind::Module) {
      addBoolean(7, top);                     // vpiTopModule
      addBoolean(8, cell && cell.getValue()); // vpiCellInstance
    }
    StringAttr definitionName =
        source->getAttrOfType<StringAttr>("obelisk_sim.vpi_definition_name");
    if (!definitionName)
      if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(source))
        if (auto instance = dyn_cast_or_null<semantic::SVInstanceSymbolOp>(
                body->getParentOp()))
          if (auto reference = instance.getReferencedSymbolAttr()) {
            auto found = definitionNames.find(reference.getLeafReference());
            if (found != definitionNames.end())
              definitionName = found->second;
          }
    if (!definitionName && isa<semantic::SVPackageSymbolOp>(source))
      definitionName = builder.getStringAttr(getDebugName(source));
    // Compilation units have no declared definition name. Use the same
    // implementation-defined `$unit` spelling as their special-access name.
    if (!definitionName && isa<semantic::SVCompilationUnitSymbolOp>(source))
      definitionName = builder.getStringAttr("$unit");
    // Production frontend IR freezes the definition identity on every
    // instance. Keep hand-authored and partially lowered MLIR valid when that
    // optional provenance is absent: sparse fixed properties represent the
    // lack of a value by omitting the record, and the runtime reports the
    // property as unavailable instead of inventing a definition name.
    addString(9, definitionName);                      // vpiDefName
    addBoolean(50, automatic && automatic.getValue()); // vpiAutomatic
    addBoolean(600, top);                              // vpiTop
    addBoolean(602, isa<semantic::SVCompilationUnitSymbolOp>(source));

    if (properties.empty())
      return sim::VPIPropertySetAttr{};
    return sim::VPIPropertySetAttr::get(builder.getContext(),
                                        builder.getArrayAttr(properties));
  };
  auto netProperties = [&](semantic::SVNetSymbolOp net) {
    SmallVector<Attribute> properties;
    auto addBoolean = [&](uint32_t selector, bool value) {
      if (value)
        properties.push_back(sim::VPIPropertyAttr::get(
            builder.getContext(), builder.getI32IntegerAttr(selector),
            builder.getBoolAttr(true)));
    };
    auto addInteger = [&](uint32_t selector, int32_t value) {
      properties.push_back(sim::VPIPropertyAttr::get(
          builder.getContext(), builder.getI32IntegerAttr(selector),
          builder.getI32IntegerAttr(value)));
    };

    // IEEE 1800-2023 37.16 adds exact declaration subtypes for user-defined
    // nettypes and interconnects. A selected part is reported as
    // vpiNettypeNetSelect by the query layer because it is handle-specific.
    std::optional<int32_t> netType;
    switch (net.getNetKind()) {
    case semantic::SVNetKind::Wire:
      netType = 1; // vpiWire
      break;
    case semantic::SVNetKind::WAnd:
      netType = 2; // vpiWand
      break;
    case semantic::SVNetKind::WOr:
      netType = 3; // vpiWor
      break;
    case semantic::SVNetKind::Tri:
      netType = 4; // vpiTri
      break;
    case semantic::SVNetKind::Tri0:
      netType = 5; // vpiTri0
      break;
    case semantic::SVNetKind::Tri1:
      netType = 6; // vpiTri1
      break;
    case semantic::SVNetKind::TriReg:
      netType = 7; // vpiTriReg
      break;
    case semantic::SVNetKind::TriAnd:
      netType = 8; // vpiTriAnd
      break;
    case semantic::SVNetKind::TriOr:
      netType = 9; // vpiTriOr
      break;
    case semantic::SVNetKind::Supply1:
      netType = 10; // vpiSupply1
      break;
    case semantic::SVNetKind::Supply0:
      netType = 11; // vpiSupply0
      break;
    case semantic::SVNetKind::UWire:
      netType = 13; // vpiUwire
      break;
    case semantic::SVNetKind::Interconnect:
      netType = 16; // vpiInterconnect
      break;
    case semantic::SVNetKind::UserDefined:
      netType = 14; // vpiNettypeNet
      break;
    case semantic::SVNetKind::Unknown:
      break;
    }
    if (netType)
      addInteger(22, *netType); // vpiNetType
    bool scalared =
        net.getExpansionHint() == semantic::SVNetExpansionHint::Scalared;
    bool vectored =
        net.getExpansionHint() == semantic::SVNetExpansionHint::Vectored;
    addBoolean(23, scalared);            // vpiExplicitScalared
    addBoolean(24, vectored);            // vpiExplicitVectored
    addBoolean(25, scalared);            // vpiExpanded
    addBoolean(26, net.getIsImplicit()); // vpiImplicitDecl

    int32_t chargeStrength = 0;
    if (net.getNetKind() == semantic::SVNetKind::TriReg) {
      switch (net.getChargeStrength().value_or(
          semantic::SVChargeStrength::Medium)) {
      case semantic::SVChargeStrength::Small:
        chargeStrength = 0x02;
        break;
      case semantic::SVChargeStrength::Medium:
        chargeStrength = 0x04;
        break;
      case semantic::SVChargeStrength::Large:
        chargeStrength = 0x10;
        break;
      }
    }
    addInteger(27, chargeStrength); // vpiChargeStrength
    bool declarationAssignment = !getNetInitializerExpressions(net).empty();
    if (declarationAssignment) {
      auto driveStrength = [](semantic::SVDriveStrength strength) -> int32_t {
        switch (strength) {
        case semantic::SVDriveStrength::Supply:
          return 0x80; // vpiSupplyDrive
        case semantic::SVDriveStrength::Strong:
          return 0x40; // vpiStrongDrive
        case semantic::SVDriveStrength::Pull:
          return 0x20; // vpiPullDrive
        case semantic::SVDriveStrength::Weak:
          return 0x08; // vpiWeakDrive
        case semantic::SVDriveStrength::HighZ:
          return 0x01; // vpiHiZ
        }
        llvm_unreachable("unknown SystemVerilog drive strength");
      };
      addInteger(31, driveStrength(net.getDriveStrength0().value_or(
                         semantic::SVDriveStrength::Strong)));
      addInteger(32, driveStrength(net.getDriveStrength1().value_or(
                         semantic::SVDriveStrength::Strong)));
    }
    addBoolean(43, declarationAssignment);

    return sim::VPIPropertySetAttr::get(builder.getContext(),
                                        builder.getArrayAttr(properties));
  };
  for (auto [inventoryId, source] : llvm::enumerate(anchorSources)) {
    Operation *parent = source->getParentOp();
    while (parent && !anchorSymbols.count(parent))
      parent = parent->getParentOp();
    FlatSymbolRefAttr parentSymbol = anchorSymbols.lookup(parent);
    uint64_t ordinal = nextAnchorOrdinal[parent]++;
    uint64_t scopeId = scopes.lookup(source);

    sim::VPIObjectBackingAttr backing;
    if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(source);
        body && !isCompileTimeOnlyInstanceMember(body)) {
      backing = sim::VPIObjectBackingAttr::get(
          builder.getContext(), sim::VPIObjectBackingKind::Scope,
          builder.getI64IntegerAttr(scopeId), FlatSymbolRefAttr{});
      // Authored and legacy semantic IR may omit the frontend's explicit
      // vpi_scope_kind attribute. Once the definition has been resolved for
      // the source anchor, freeze that exact kind on the physical scope too so
      // backing verification does not have to infer identity from defaults.
      if (scopeId < scopes.declarations.size()) {
        sim::SimScopeDeclOp scope = scopes.declarations[scopeId];
        if (!scope.getVpiKindAttr())
          scope->setAttr("vpi_kind",
                         builder.getI32IntegerAttr(static_cast<uint32_t>(
                             anchorKinds.lookup(source))));
      }
    } else if (auto classType = dyn_cast<semantic::SVClassTypeOp>(source)) {
      auto classSymbol = classes.symbols.find(classType);
      if (classSymbol != classes.symbols.end())
        backing = sim::VPIObjectBackingAttr::get(
            builder.getContext(), sim::VPIObjectBackingKind::Class,
            IntegerAttr{}, FlatSymbolRefAttr::get(classSymbol->second));
    }

    StringRef hierarchy = getHierarchyName(source);
    VPIKind sourceKind = anchorKinds.lookup(source);
    bool aggregateArray = sourceKind == VPIKind::ModuleArray ||
                          sourceKind == VPIKind::InterfaceArray ||
                          sourceKind == VPIKind::ProgramArray ||
                          sourceKind == VPIKind::GateArray ||
                          sourceKind == VPIKind::SwitchArray ||
                          sourceKind == VPIKind::UdpArray ||
                          sourceKind == VPIKind::NamedEventArray ||
                          sourceKind == VPIKind::GenScopeArray;
    Operation *arrayRoot = nullptr;
    SmallVector<int64_t> memberIndices;
    if (!aggregateArray) {
      for (Operation *cursor = source; cursor; cursor = cursor->getParentOp()) {
        if (cursor != source && anchorSymbols.count(cursor))
          break;
        auto root = relationArrayMemberRoots.find(cursor);
        if (root == relationArrayMemberRoots.end())
          continue;
        arrayRoot = root->second;
        memberIndices = relationArrayMemberIndices.lookup(cursor);
        break;
      }
    }
    if (!memberIndices.empty())
      if (arrayRoot) {
        std::string indexedHierarchy = getHierarchyName(arrayRoot).str();
        for (int64_t index : memberIndices)
          indexedHierarchy += "[" + std::to_string(index) + "]";
        hierarchy = builder.getStringAttr(indexedHierarchy).getValue();
      }
    if (hierarchy.empty() && parent)
      hierarchy = getHierarchyName(parent);
    if (hierarchy.empty()) {
      emitError(getSemanticLocation(source))
          << "VPI source object is missing a hierarchy name";
      invalid = true;
      continue;
    }
    DenseI64ArrayAttr indexRanges;
    DenseI64ArrayAttr sparseIndices;
    if (auto found = fixedArrayRanges.find(source);
        found != fixedArrayRanges.end())
      indexRanges = builder.getDenseI64ArrayAttr(found->second);
    if (auto found = namedEventArrayRanges.find(source);
        found != namedEventArrayRanges.end())
      indexRanges = builder.getDenseI64ArrayAttr(found->second);
    if (auto found = sparseArrayIndices.find(source);
        found != sparseArrayIndices.end())
      sparseIndices = builder.getDenseI64ArrayAttr(found->second);
    IntegerAttr primitiveInputCount;
    if (isa<semantic::SVPrimitiveInstanceSymbolOp>(source)) {
      SmallVector<Operation *> terminals = getChildren(source);
      uint64_t count = llvm::count_if(terminals, [](Operation *terminal) {
        return !isa<semantic::SVAssignmentExpressionOp>(terminal);
      });
      primitiveInputCount = builder.getI64IntegerAttr(count);
    }
    sim::SimVPIObjectAnchorOp anchor = sim::SimVPIObjectAnchorOp::create(
        builder, getSemanticLocation(source),
        anchorSymbols.lookup(source).getValue(), inventoryId,
        static_cast<uint32_t>(anchorKinds.lookup(source)), scopeId,
        parentSymbol, ordinal, builder.getStringAttr(hierarchy),
        builder.getStringAttr(getDebugName(source)),
        isa<semantic::SVCompilationUnitSymbolOp>(source) ? builder.getUnitAttr()
                                                         : UnitAttr{},
        backing, indexRanges, DenseI64ArrayAttr{}, sparseIndices,
        memberIndices.empty() ? DenseI64ArrayAttr{}
                              : builder.getDenseI64ArrayAttr(memberIndices),
        primitiveInputCount);
    anchorDeclarations[source] = anchor;
    if (sim::VPIPropertySetAttr properties =
            identityProperties(source, sourceKind))
      anchor->setAttr("vpi_properties", properties);
    source->setAttr("obelisk_sim.vpi_anchor", anchorSymbols.lookup(source));
  }

  // Continuous assignments and net aliases are scope-owned VPI statement
  // objects even though continuous assignments also have an internal
  // executable code unit. Preserve their source identity independently of
  // that executable representation. The generated traversal model decides
  // whether the exact lexical scope may expose each statement kind.
  uint64_t nextStatementId = 1;
  struct ScopeOwnedStatementPlan {
    Operation *source;
    uint64_t statementId;
    uint64_t scopeId;
    VPIKind kind;
    Operation *lhs = nullptr;
    Operation *rhs = nullptr;
  };
  SmallVector<ScopeOwnedStatementPlan> scopeOwnedStatementPlans;
  llvm::DenseMap<Operation *, llvm::DenseMap<uint32_t, uint64_t>>
      nextStatementOrdinal;
  auto isInUninstantiatedGenerate = [](Operation *operation) {
    for (Operation *cursor = operation; cursor;
         cursor = cursor->getParentOp()) {
      auto generate = dyn_cast<semantic::SVGenerateBlockSymbolOp>(cursor);
      if (!generate)
        continue;
      auto uninstantiated =
          generate->getAttrOfType<BoolAttr>("is_uninstantiated");
      if (uninstantiated && uninstantiated.getValue())
        return true;
    }
    return false;
  };
  semanticRoot->walk<WalkOrder::PreOrder>(
      [&](Operation *source) {
        VPIKind statementKind;
        if (isa<semantic::SVContinuousAssignSymbolOp>(source))
          statementKind = VPIKind::ContAssign;
        else if (isa<semantic::SVNetAliasSymbolOp>(source))
          statementKind = VPIKind::AliasStmt;
        else
          return;
        if (isCompileTimeOnlyInstanceMember(source) ||
            isInUninstantiatedGenerate(source))
          return;

        Location location = getSemanticLocation(source);
        Operation *owner = source->getParentOp();
        while (owner && !anchorKinds.count(owner))
          owner = owner->getParentOp();
        if (!owner) {
          emitError(location)
              << "VPI scope-owned statement has no persistent lexical anchor";
          invalid = true;
          return;
        }

        uint32_t ownerKind = static_cast<uint32_t>(anchorKinds.lookup(owner));
        uint32_t selector = static_cast<uint32_t>(statementKind);
        const auto *edge = reflection::findVPITraversal(
            ownerKind, selector, reflection::VPITraversalMode::Iterate);
        if (!edge || !edge->statementContainment ||
            !reflection::vpiObjectSetContains(edge->targets, selector)) {
          emitError(location)
              << "VPI " << reflection::findVPIObjectKind(selector)->apiName
              << " is not legal in lexical owner kind " << ownerKind;
          invalid = true;
          return;
        }

        sim::SimVPIObjectAnchorOp ownerAnchor =
            anchorDeclarations.lookup(owner);
        if (!ownerAnchor) {
          emitError(location)
              << "VPI statement lexical anchor was not materialized";
          invalid = true;
          return;
        }
        sim::VPIObjectBackingAttr backing = ownerAnchor.getBackingAttr();
        bool physicalScope =
            backing && backing.getKind() == sim::VPIObjectBackingKind::Scope;
        // IEEE 1800-2023 37.76 defines an N-net alias declaration as N-1
        // alias objects, each relating one of the first N-1 nets to the final
        // net. A continuous-assignment declaration always contributes one.
        size_t statementCount = 1;
        SmallVector<Operation *> aliasOperands;
        if (isa<semantic::SVNetAliasSymbolOp>(source)) {
          aliasOperands = getChildren(source);
          size_t operandCount = aliasOperands.size();
          statementCount = operandCount > 1 ? operandCount - 1 : 0;
        }
        for (size_t statement = 0; statement != statementCount; ++statement) {
          uint64_t statementId = nextStatementId++;
          uint64_t ordinal = nextStatementOrdinal[owner][selector]++;
          sim::SimStatementDeclOp::create(builder, location, statementId,
                                          IntegerAttr{}, scopes.lookup(source),
                                          selector, IntegerAttr{}, StringAttr{},
                                          UnitAttr{}, UnitAttr{});
          if (statementKind == VPIKind::ContAssign)
            scopeOwnedStatementPlans.push_back(
                {source, statementId, scopes.lookup(source), statementKind});
          else
            scopeOwnedStatementPlans.push_back(
                {source, statementId, scopes.lookup(source), statementKind,
                 aliasOperands[statement], aliasOperands.back()});
          sim::SimVPIStatementRelationDeclOp::create(
              builder, location,
              physicalScope ? sim::VPIStatementSourceKind::Scope
                            : sim::VPIStatementSourceKind::Anchor,
              physicalScope ? backing.getId().getValue().getZExtValue()
                            : anchorInventoryIds.lookup(owner),
              ownerKind, selector, ordinal,
              uint32_t{1} << static_cast<uint32_t>(
                  reflection::VPITraversalMode::Iterate),
              statementId);
        }
      });
  uint64_t nextSyntheticInventoryId = anchorSources.size();
  for (Operation *source : anchorSources) {
    auto eventArray = namedEventArrayRanges.find(source);
    if (eventArray == namedEventArrayRanges.end())
      continue;
    ArrayRef<int64_t> ranges = eventArray->second;
    SmallVector<uint64_t> extents;
    uint64_t elementCount = 1;
    for (size_t dimension = 0; dimension != ranges.size(); dimension += 2) {
      uint64_t distance = ranges[dimension] >= ranges[dimension + 1]
                              ? static_cast<uint64_t>(ranges[dimension]) -
                                    static_cast<uint64_t>(ranges[dimension + 1])
                              : static_cast<uint64_t>(ranges[dimension + 1]) -
                                    static_cast<uint64_t>(ranges[dimension]);
      if (distance == UINT64_MAX || distance + 1 > UINT32_MAX ||
          elementCount > UINT32_MAX / (distance + 1)) {
        emitError(getSemanticLocation(source))
            << "named-event array shape exceeds VPI relation encoding";
        invalid = true;
        elementCount = 0;
        break;
      }
      extents.push_back(distance + 1);
      elementCount *= distance + 1;
    }
    for (uint64_t ordinal = 0; ordinal != elementCount; ++ordinal) {
      uint64_t remainder = ordinal;
      SmallVector<int64_t> indices(extents.size());
      for (size_t dimension = extents.size(); dimension != 0;) {
        --dimension;
        uint64_t coordinate = remainder % extents[dimension];
        remainder /= extents[dimension];
        int64_t left = ranges[dimension * 2];
        indices[dimension] = left >= ranges[dimension * 2 + 1]
                                 ? left - static_cast<int64_t>(coordinate)
                                 : left + static_cast<int64_t>(coordinate);
      }
      std::string hierarchy = getHierarchyName(source).str();
      for (int64_t index : indices)
        hierarchy += "[" + std::to_string(index) + "]";
      std::string symbolName =
          "__obelisk_vpi_anchor_" + std::to_string(nextSyntheticInventoryId);
      sim::SimVPIObjectAnchorOp::create(
          builder, getSemanticLocation(source), symbolName,
          nextSyntheticInventoryId++,
          static_cast<uint32_t>(VPIKind::NamedEvent), scopes.lookup(source),
          anchorSymbols.lookup(source), ordinal,
          builder.getStringAttr(hierarchy),
          builder.getStringAttr(getDebugName(source)), UnitAttr{},
          sim::VPIObjectBackingAttr{}, DenseI64ArrayAttr{}, DenseI64ArrayAttr{},
          DenseI64ArrayAttr{}, builder.getDenseI64ArrayAttr(indices),
          IntegerAttr{});
    }
  }
  auto ownerAnchorFor = [&](Operation *member) -> FlatSymbolRefAttr {
    for (Operation *cursor = member; cursor; cursor = cursor->getParentOp()) {
      if (FlatSymbolRefAttr anchor = anchorSymbols.lookup(cursor))
        return anchor;
      if (isa<semantic::SVStatementBlockSymbolOp>(cursor))
        return {};
    }
    return {};
  };
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (isCompileTimeOnlyInstanceMember(op))
      return;
    auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op);
    auto classProperty = dyn_cast<semantic::SVClassPropertySymbolOp>(op);
    bool staticVariable =
        variable &&
        (variable.getLifetime() == semantic::SVVariableLifetime::Static ||
         isStaticReturnVariable(variable));
    bool staticClassProperty =
        classProperty &&
        classProperty.getLifetime() == semantic::SVVariableLifetime::Static;
    if (isNestedInCodeUnit(op) && !staticVariable && !isStaticFormal(op))
      return;
    bool storage = (isa<semantic::SVVariableSymbolOp>(op) &&
                    !isAutomaticLocalSymbol(op)) ||
                   isStaticFormal(op) || staticClassProperty;
    if (storage || isa<semantic::SVNetSymbolOp>(op) ||
        op->hasAttr(sequenceEndpointEventAttrName) ||
        (isa<semantic::SVClockingBlockSymbolOp>(op) &&
         (op->hasAttr(clockingEventMonitorRequiredAttrName) ||
          op->hasAttr(clockingEventListAttrName))))
      designObjects.push_back(op);
  });

  // Source typedef symbols live in the semantic hierarchy, which is erased
  // at finalization. Every explicit declaration is independently traversable
  // through vpiTypedef, including declarations unused by executable storage.
  // Give all of them flat simulation symbols and remap every alias chain before
  // embedding it in immutable VPI type inventory.
  auto getSemanticSymbolReference = [&](Operation *symbol,
                                        bool collapseTransparentWrappers =
                                            false) {
    SmallVector<Operation *> path;
    for (Operation *current = symbol; current; current = current->getParentOp())
      if (isa<SymbolOpInterface>(current))
        path.push_back(current);
    std::reverse(path.begin(), path.end());
    SmallVector<StringAttr> names;
    for (auto [index, current] : llvm::enumerate(path)) {
      // Frontend semantic references omit wrappers around elaborated instance
      // bodies and generic-class specializations. Preserve the target symbol
      // itself and every component that participates in frontend identity.
      if (collapseTransparentWrappers && current != symbol &&
          index + 1 < path.size() &&
          path[index + 1]->getParentOp() == current &&
          ((isa<semantic::SVInstanceSymbolOp>(current) &&
            isa<semantic::SVInstanceBodySymbolOp>(path[index + 1])) ||
           (isa<semantic::SVGenericClassDefSymbolOp>(current) &&
            isa<semantic::SVClassTypeOp>(path[index + 1]))))
        continue;
      names.push_back(SymbolTable::getSymbolName(current));
    }
    SmallVector<FlatSymbolRefAttr> nested;
    for (StringAttr name : ArrayRef(names).drop_front())
      nested.push_back(FlatSymbolRefAttr::get(name));
    return SymbolRefAttr::get(names.front(), nested);
  };
  SmallVector<semantic::SVNetTypeOp> sourceNettypes;
  llvm::DenseMap<Attribute, semantic::SVNetTypeOp> nettypesByReference;
  llvm::DenseMap<Operation *, FlatSymbolRefAttr> nettypeSymbols;
  module.walk([&](semantic::SVNetTypeOp nettype) {
    if (nettype.getIsBuiltin())
      return;
    auto index = [&](SymbolRefAttr reference) {
      auto [found, inserted] =
          nettypesByReference.try_emplace(reference, nettype);
      if (!inserted && found->second != nettype) {
        emitError(getSemanticLocation(nettype))
            << "VPI nettype identity collides with another declaration "
            << reference;
        invalid = true;
      }
    };
    index(getSemanticSymbolReference(nettype, true));
    index(getSemanticSymbolReference(nettype));
    sourceNettypes.push_back(nettype);
  });
  for (auto [index, nettype] : llvm::enumerate(sourceNettypes)) {
    std::string name = "__obelisk_vpi_nettype_" + std::to_string(index);
    nettypeSymbols[nettype] =
        FlatSymbolRefAttr::get(builder.getContext(), name);
  }
  llvm::DenseMap<Attribute, Operation *> anchorsBySemanticReference;
  for (Operation *source : anchorSources) {
    anchorsBySemanticReference.try_emplace(
        getSemanticSymbolReference(source, true), source);
    anchorsBySemanticReference.try_emplace(getSemanticSymbolReference(source),
                                           source);
  }
  auto resolveSourceNettype = [&](SymbolRefAttr reference) {
    auto found = nettypesByReference.find(reference);
    return found == nettypesByReference.end() ? semantic::SVNetTypeOp{}
                                              : found->second;
  };
  llvm::DenseMap<Attribute, semantic::SVTypeAliasTypeOp>
      aliasesBySymbolReference;
  SmallVector<semantic::SVTypeAliasTypeOp> aliases;
  module.walk([&](semantic::SVTypeAliasTypeOp alias) {
    auto indexAlias = [&](SymbolRefAttr reference) {
      auto [found, inserted] =
          aliasesBySymbolReference.try_emplace(reference, alias);
      if (!inserted && found->second != alias) {
        emitError(getSemanticLocation(alias))
            << "VPI typedef identity collides with another semantic alias "
            << reference;
        invalid = true;
      }
    };
    // Native frontend IR uses the collapsed identity. Also retain the raw
    // exact path for authored MLIR and legacy producers that model wrappers as
    // identity-bearing symbols.
    indexAlias(getSemanticSymbolReference(alias, true));
    indexAlias(getSemanticSymbolReference(alias));
    aliases.push_back(alias);
  });
  auto resolveAlias = [&](Operation *, SymbolRefAttr reference) {
    auto found = aliasesBySymbolReference.find(reference);
    return found == aliasesBySymbolReference.end()
               ? semantic::SVTypeAliasTypeOp{}
               : found->second;
  };
  auto collectAliases = [&](Operation *owner, ArrayAttr layers) {
    if (!layers)
      return;
    for (Attribute rawLayer : layers) {
      auto layer = dyn_cast<DictionaryAttr>(rawLayer);
      auto sourceAliases =
          layer ? layer.getAs<ArrayAttr>("aliases") : ArrayAttr{};
      if (!sourceAliases) {
        emitError(getSemanticLocation(owner))
            << "malformed VPI typedef-layer inventory";
        invalid = true;
        continue;
      }
      for (Attribute rawAlias : sourceAliases) {
        auto reference = dyn_cast<SymbolRefAttr>(rawAlias);
        auto alias = reference ? resolveAlias(owner, reference)
                               : semantic::SVTypeAliasTypeOp{};
        if (!alias) {
          emitError(getSemanticLocation(owner))
              << "VPI typedef inventory references an unknown alias "
              << rawAlias;
          invalid = true;
          continue;
        }
      }
    }
  };
  for (Operation *op : designObjects)
    collectAliases(op, op->getAttrOfType<ArrayAttr>(vpiTypedefLayersAttrName));
  for (semantic::SVPortConnectionOp connection : portAliases.connections)
    collectAliases(connection, connection->getAttrOfType<ArrayAttr>(
                                   vpiTypedefLayersAttrName));
  for (const auto &entry : portAliases.refViews)
    collectAliases(semanticRoot, entry.second.typedefLayers);
  for (const auto &entry : portAliases.interfaceViews)
    collectAliases(semanticRoot, entry.second.typedefLayers);

  // `module.walk` is source order for the semantic inventory.  Do not rebuild
  // this list from the DenseSet: hand-authored IR may reuse source node IDs,
  // and declaration order is part of VPI traversal semantics.
  llvm::DenseMap<Operation *, FlatSymbolRefAttr> aliasSymbols;
  for (auto [index, alias] : llvm::enumerate(aliases)) {
    std::string name = "__obelisk_vpi_typespec_" + std::to_string(index);
    aliasSymbols[alias] = FlatSymbolRefAttr::get(builder.getContext(), name);
  }
  auto remapTypedefLayers = [&](Operation *owner,
                                ArrayAttr layers) -> FailureOr<ArrayAttr> {
    if (!layers)
      return ArrayAttr{};
    SmallVector<Attribute> remappedLayers;
    remappedLayers.reserve(layers.size());
    for (Attribute rawLayer : layers) {
      auto layer = dyn_cast<DictionaryAttr>(rawLayer);
      auto path =
          layer ? layer.getAs<DenseI64ArrayAttr>("path") : DenseI64ArrayAttr{};
      auto sourceAliases =
          layer ? layer.getAs<ArrayAttr>("aliases") : ArrayAttr{};
      if (!path || !sourceAliases) {
        emitError(getSemanticLocation(owner))
            << "malformed VPI typedef-layer inventory";
        return failure();
      }
      SmallVector<Attribute> remappedAliases;
      remappedAliases.reserve(sourceAliases.size());
      for (Attribute rawAlias : sourceAliases) {
        auto sourceReference = dyn_cast<SymbolRefAttr>(rawAlias);
        auto sourceAlias = sourceReference
                               ? resolveAlias(owner, sourceReference)
                               : semantic::SVTypeAliasTypeOp{};
        auto mapped =
            sourceAlias ? aliasSymbols.find(sourceAlias) : aliasSymbols.end();
        if (mapped == aliasSymbols.end()) {
          emitError(getSemanticLocation(owner))
              << "VPI typedef layer cannot resolve its simulation alias";
          return failure();
        }
        remappedAliases.push_back(mapped->second);
      }
      remappedLayers.push_back(builder.getDictionaryAttr(
          {builder.getNamedAttr("path", path),
           builder.getNamedAttr("aliases",
                                builder.getArrayAttr(remappedAliases))}));
    }
    return builder.getArrayAttr(remappedLayers);
  };
  auto typedefLayersFor = [&](Operation *owner) -> ArrayAttr {
    FailureOr<ArrayAttr> layers = remapTypedefLayers(
        owner, owner->getAttrOfType<ArrayAttr>(vpiTypedefLayersAttrName));
    if (failed(layers)) {
      invalid = true;
      return {};
    }
    return *layers;
  };
  std::function<FailureOr<sim::VPITypeSemanticsAttr>(Operation *,
                                                     sim::VPITypeSemanticsAttr)>
      remapVPITypeAliases;
  remapVPITypeAliases = [&](Operation *owner, sim::VPITypeSemanticsAttr current)
      -> FailureOr<sim::VPITypeSemanticsAttr> {
    SmallVector<Attribute> children;
    children.reserve(current.getChildren().size());
    for (Attribute childAttr : current.getChildren()) {
      FailureOr<sim::VPITypeSemanticsAttr> child = remapVPITypeAliases(
          owner, cast<sim::VPITypeSemanticsAttr>(childAttr));
      if (failed(child))
        return failure();
      children.push_back(*child);
    }
    ArrayAttr aliases;
    if (ArrayAttr sourceAliases = current.getTypedefAliases()) {
      SmallVector<Attribute> mappedAliases;
      mappedAliases.reserve(sourceAliases.size());
      for (Attribute rawAlias : sourceAliases) {
        auto sourceReference = dyn_cast<SymbolRefAttr>(rawAlias);
        auto sourceAlias = sourceReference
                               ? resolveAlias(owner, sourceReference)
                               : semantic::SVTypeAliasTypeOp{};
        auto mapped =
            sourceAlias ? aliasSymbols.find(sourceAlias) : aliasSymbols.end();
        if (mapped == aliasSymbols.end()) {
          emitError(getSemanticLocation(owner))
              << "inferred interconnect VPI type cannot resolve its typedef "
                 "alias";
          return failure();
        }
        mappedAliases.push_back(mapped->second);
      }
      aliases = builder.getArrayAttr(mappedAliases);
    }
    return sim::VPITypeSemanticsAttr::get(
        builder.getContext(), current.getKind(), current.getIsSigned(),
        current.getIsFourState(), current.getName(), current.getSymbol(),
        current.getModport(), current.getRange(),
        builder.getArrayAttr(children), current.getChildNames(),
        current.getIsTagged(), current.getIsSoft(), current.getBitWidth(),
        current.getSelectableWidth(), current.getBitstreamWidth(),
        current.getTagBits(), current.getQueueBound(),
        current.getWildcardIndex(), current.getChildOrdinals(),
        current.getChildPackedOffsets(), current.getChildRandTypes(), aliases);
  };
  uint64_t nextNettypeId = 0;
  for (semantic::SVNetTypeOp nettype : sourceNettypes) {
    FlatSymbolRefAttr owner = ownerAnchorFor(nettype);
    if (!owner)
      continue;
    ArrayAttr layers = typedefLayersFor(nettype);
    FailureOr<sim::VPITypeSemanticsAttr> target = makeVPITypeSemantics(
        nettype.getDataType(), getSemanticLocation(nettype), layers, nettype);
    StringRef hierarchy = getHierarchyName(nettype);
    StringRef debug = getDebugName(nettype);
    if (failed(target) || hierarchy.empty() || debug.empty()) {
      if (hierarchy.empty() || debug.empty())
        emitError(getSemanticLocation(nettype))
            << "VPI nettype is missing a hierarchy or debug name";
      invalid = true;
      continue;
    }
    FlatSymbolRefAttr directAlias;
    if (SymbolRefAttr reference = nettype.getAliasedNettypeSymbolAttr()) {
      semantic::SVNetTypeOp targetNettype = resolveSourceNettype(reference);
      directAlias = nettypeSymbols.lookup(targetNettype);
      if (!directAlias) {
        emitError(getSemanticLocation(nettype))
            << "VPI nettype alias target was not preserved";
        invalid = true;
        continue;
      }
    }
    FlatSymbolRefAttr resolutionFunction;
    if (SymbolRefAttr reference = nettype.getResolutionFunctionSymbolAttr()) {
      Operation *source = anchorsBySemanticReference.lookup(reference);
      resolutionFunction = anchorSymbols.lookup(source);
      if (!resolutionFunction) {
        emitError(getSemanticLocation(nettype))
            << "VPI nettype resolver anchor was not preserved";
        invalid = true;
        continue;
      }
    }
    sim::SimVPINettypeDeclOp::create(builder, getSemanticLocation(nettype),
                                     nettypeSymbols.lookup(nettype).getValue(),
                                     nextNettypeId++, scopes.lookup(nettype),
                                     owner.getValue(), hierarchy, debug,
                                     *target, directAlias, resolutionFunction);
  }
  uint64_t nextTypespecId = 0;
  llvm::DenseMap<Attribute, FlatSymbolRefAttr> enumTypespecsByIdentity;
  auto getEnumIdentity = [&](Operation *operation,
                             sim::VPITypeSemanticsAttr type) -> Attribute {
    if (auto identity = operation->getAttrOfType<IntegerAttr>(
            vpiSourceTypeIdentityAttrName))
      return builder.getArrayAttr(
          {builder.getStringAttr("source-type"), identity});
    Attribute owner = ownerAnchorFor(operation);
    if (!owner)
      owner = builder.getUnitAttr();
    return builder.getArrayAttr(
        {builder.getStringAttr("lexical-owner"), owner,
         type.getName() ? type.getName() : builder.getStringAttr("")});
  };
  for (semantic::SVTypeAliasTypeOp alias : aliases) {
    auto semanticType = alias->getAttrOfType<TypeAttr>("semantic_type");
    ArrayAttr layers = typedefLayersFor(alias);
    FailureOr<sim::VPITypeSemanticsAttr> target =
        semanticType
            ? makeVPITypeSemantics(semanticType.getValue(),
                                   getSemanticLocation(alias), layers, alias)
            : FailureOr<sim::VPITypeSemanticsAttr>(failure());
    StringRef hierarchy = getHierarchyName(alias);
    StringRef debug = getDebugName(alias);
    if (!semanticType || failed(target) || hierarchy.empty() || debug.empty()) {
      if (!semanticType)
        emitError(getSemanticLocation(alias))
            << "VPI typedef is missing semantic type metadata";
      else if (hierarchy.empty() || debug.empty())
        emitError(getSemanticLocation(alias))
            << "VPI typedef is missing a hierarchy or debug name";
      invalid = true;
      continue;
    }
    FlatSymbolRefAttr owner = ownerAnchorFor(alias);
    if (!owner)
      continue;
    auto sourceTypeIdentity =
        alias->getAttrOfType<IntegerAttr>(vpiSourceTypeIdentityAttrName);
    sim::SimVPITypespecDeclOp declaration = sim::SimVPITypespecDeclOp::create(
        builder, getSemanticLocation(alias),
        aliasSymbols.lookup(alias).getValue(), nextTypespecId++,
        scopes.lookup(alias), owner.getValue(), hierarchy, debug, *target,
        sim::VPITypespecOrigin::Typedef, sourceTypeIdentity);
    if (target->getKind() == sim::VPITypeKind::Enum)
      enumTypespecsByIdentity.try_emplace(
          getEnumIdentity(alias, *target),
          FlatSymbolRefAttr::get(declaration.getSymNameAttr()));
  }

  struct InterfaceTypespec {
    SymbolRefAttr identity;
    StringAttr modport;
    semantic::SVInstanceBodySymbolOp representative;
  };
  SmallVector<InterfaceTypespec> interfaceTypespecs;
  llvm::DenseSet<Attribute> interfaceTypespecKeys;
  llvm::DenseMap<Attribute, semantic::SVInstanceBodySymbolOp> interfaceBodies;
  llvm::DenseMap<Attribute, semantic::SVInstanceSymbolOp>
      interfaceInstancesByIdentity;
  SmallVector<SymbolRefAttr> interfaceIdentityOrder;
  semanticRoot.walk([&](semantic::SVInstanceSymbolOp instance) {
    auto indexInstance = [&](SymbolRefAttr reference) {
      auto [found, inserted] =
          interfaceInstancesByIdentity.try_emplace(reference, instance);
      if (!inserted && found->second != instance) {
        emitError(getSemanticLocation(instance))
            << "VPI interface identity collides with another semantic "
               "instance "
            << reference;
        invalid = true;
      }
    };
    indexInstance(getSemanticSymbolReference(instance, true));
    indexInstance(getSemanticSymbolReference(instance));
  });
  semanticRoot.walk([&](semantic::SVInstanceBodySymbolOp body) {
    if (auto identity =
            body->getAttrOfType<SymbolRefAttr>("virtual_interface_identity"))
      if (interfaceBodies.try_emplace(identity, body).second)
        interfaceIdentityOrder.push_back(identity);
  });
  auto findInterfaceBody = [&](SymbolRefAttr identity) {
    auto found = interfaceBodies.find(identity);
    if (found != interfaceBodies.end())
      return found->second;
    auto instance = interfaceInstancesByIdentity.find(identity);
    if (instance == interfaceInstancesByIdentity.end())
      return semantic::SVInstanceBodySymbolOp{};
    for (Operation *child : getChildren(instance->second))
      if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(child))
        return body;
    return semantic::SVInstanceBodySymbolOp{};
  };
  auto addInterfaceTypespec = [&](SymbolRefAttr identity, StringAttr modport,
                                  Operation *owner) {
    if (!identity || !modport)
      return;
    Attribute key = builder.getArrayAttr({identity, modport});
    if (!interfaceTypespecKeys.insert(key).second)
      return;
    semantic::SVInstanceBodySymbolOp representative =
        findInterfaceBody(identity);
    if (!representative) {
      emitError(getSemanticLocation(owner))
          << "VPI virtual-interface typespec cannot resolve elaborated "
             "interface identity "
          << identity;
      invalid = true;
      return;
    }
    interfaceTypespecs.push_back({identity, modport, representative});
  };
  for (SymbolRefAttr identity : interfaceIdentityOrder)
    addInterfaceTypespec(identity, builder.getStringAttr(""),
                         interfaceBodies.lookup(identity));
  llvm::DenseSet<Type> inspectedInterfaceTypes;
  semanticRoot.walk([&](Operation *op) {
    auto inspectType = [&](Type type) {
      if (!inspectedInterfaceTypes.insert(type).second)
        return;
      type.walk([&](semantic::VirtualInterfaceType interface) {
        addInterfaceTypespec(interface.getInterfaceName(),
                             interface.getModport(), op);
        addInterfaceTypespec(interface.getInterfaceName(),
                             builder.getStringAttr(""), op);
      });
    };
    for (NamedAttribute named : op->getAttrs())
      named.getValue().walk(inspectType);
    for (Type type : op->getOperandTypes())
      inspectType(type);
    for (Type type : op->getResultTypes())
      inspectType(type);
    for (Region &region : op->getRegions())
      for (Block &block : region)
        for (BlockArgument argument : block.getArguments())
          inspectType(argument.getType());
  });
  llvm::sort(interfaceTypespecs,
             [](const InterfaceTypespec &left, const InterfaceTypespec &right) {
               if (left.identity != right.identity) {
                 std::string leftText, rightText;
                 llvm::raw_string_ostream(leftText) << left.identity;
                 llvm::raw_string_ostream(rightText) << right.identity;
                 return leftText < rightText;
               }
               return left.modport.getValue() < right.modport.getValue();
             });
  for (const InterfaceTypespec &entry : interfaceTypespecs) {
    Type type = semantic::VirtualInterfaceType::get(
        builder.getContext(), entry.identity, entry.modport);
    FailureOr<sim::VPITypeSemanticsAttr> target =
        makeVPITypeSemantics(type, getSemanticLocation(entry.representative),
                             {}, entry.representative);
    if (failed(target)) {
      invalid = true;
      continue;
    }
    std::string identity;
    llvm::raw_string_ostream(identity) << entry.identity;
    StringRef debug = getDebugName(entry.representative);
    if (debug.empty()) {
      emitError(getSemanticLocation(entry.representative))
          << "VPI interface typespec is missing its definition name";
      invalid = true;
      continue;
    }
    FlatSymbolRefAttr owner = ownerAnchorFor(entry.representative);
    // Statement-backed ownership is emitted in the next chunk. Defer the
    // complete local typespec instead of collapsing its identity outward or
    // rejecting otherwise valid source IR.
    if (!owner)
      continue;
    StringAttr symbolName = getSimulationVirtualInterfaceTypespecSymbol(
        entry.identity, entry.modport);
    sim::SimVPITypespecDeclOp::create(
        builder, getSemanticLocation(entry.representative),
        symbolName.getValue(), nextTypespecId++,
        scopes.lookup(entry.representative), owner.getValue(), identity, debug,
        *target, sim::VPITypespecOrigin::Interface, IntegerAttr{});
  }

  // Materialize a standalone enum typespec when no typedef declaration names
  // the exact Slang type identity.  Identity, rather than a hierarchy display
  // string, is the relation key so same-named `$unit` enums remain distinct.
  struct EnumValueInventory {
    semantic::SVEnumValueSymbolOp operation;
    sim::VPITypeSemanticsAttr type;
    Attribute identity;
    FlatSymbolRefAttr owner;
    StringAttr constant;
  };
  SmallVector<EnumValueInventory> enumValues;
  module.walk([&](semantic::SVEnumValueSymbolOp enumValue) {
    // Statement-backed lexical ownership is materialized in the next chunk.
    // Until then, omit the whole local enum inventory rather than emitting a
    // dangling constant or collapsing its vpiTypedef ownership outward.
    FlatSymbolRefAttr owner = ownerAnchorFor(enumValue);
    if (!owner)
      return;
    auto semanticType = enumValue->getAttrOfType<TypeAttr>("semantic_type");
    auto constant = enumValue->getAttrOfType<StringAttr>("constant_value");
    StringRef name = getDebugName(enumValue);
    if (!semanticType || !isa<semantic::EnumType>(semanticType.getValue()) ||
        !constant || name.empty()) {
      emitError(getSemanticLocation(enumValue))
          << "VPI enum constant is missing semantic type, exact typespec, "
             "name, or value";
      invalid = true;
      return;
    }
    FailureOr<sim::VPITypeSemanticsAttr> target = makeVPITypeSemantics(
        semanticType.getValue(), getSemanticLocation(enumValue), {}, enumValue);
    if (failed(target)) {
      invalid = true;
      return;
    }
    if (target->getKind() != sim::VPITypeKind::Enum) {
      emitError(getSemanticLocation(enumValue))
          << "VPI enum constant does not describe an enum type";
      invalid = true;
      return;
    }
    Attribute identity = getEnumIdentity(enumValue, *target);
    // A value can join an existing typedef typespec through its lexical
    // identity. Creating a new anonymous typespec, however, requires the
    // frontend's exact source-type identity; otherwise identically named
    // anonymous enums in the same owner cannot be distinguished.
    if (!enumTypespecsByIdentity.count(identity) &&
        !enumValue->getAttrOfType<IntegerAttr>(vpiSourceTypeIdentityAttrName))
      return;
    enumValues.push_back({enumValue, *target, identity, owner, constant});
  });

  llvm::DenseMap<Attribute, size_t> anonymousEnumRepresentatives;
  SmallVector<Attribute> anonymousEnumIdentities;
  for (auto [index, enumValue] : llvm::enumerate(enumValues)) {
    Attribute identity = enumValue.identity;
    if (!enumTypespecsByIdentity.count(identity) &&
        anonymousEnumRepresentatives.try_emplace(identity, index).second)
      anonymousEnumIdentities.push_back(identity);
  }
  for (Attribute identity : anonymousEnumIdentities) {
    const EnumValueInventory &inventory =
        enumValues[anonymousEnumRepresentatives.lookup(identity)];
    semantic::SVEnumValueSymbolOp enumValue = inventory.operation;
    sim::VPITypeSemanticsAttr target = inventory.type;
    StringRef hierarchy = getHierarchyName(enumValue);
    StringRef name = target.getName() ? target.getName().getValue()
                                      : getDebugName(enumValue);
    size_t separator = hierarchy.rfind('.');
    size_t namespaceSeparator = hierarchy.rfind("::");
    if (namespaceSeparator != StringRef::npos &&
        (separator == StringRef::npos || namespaceSeparator > separator))
      separator = namespaceSeparator;
    if (separator != StringRef::npos)
      hierarchy = hierarchy.take_front(separator);
    if (hierarchy.empty() || name.empty()) {
      emitError(getSemanticLocation(enumValue))
          << "VPI enum typespec is missing source names";
      invalid = true;
      continue;
    }
    auto sourceTypeIdentity =
        enumValue->getAttrOfType<IntegerAttr>(vpiSourceTypeIdentityAttrName);
    std::string symbolName =
        "__obelisk_vpi_enum_typespec_" + std::to_string(nextTypespecId);
    FlatSymbolRefAttr symbol =
        FlatSymbolRefAttr::get(builder.getContext(), symbolName);
    sim::SimVPITypespecDeclOp::create(
        builder, getSemanticLocation(enumValue), symbolName, nextTypespecId++,
        scopes.lookup(enumValue), inventory.owner.getValue(), hierarchy, name,
        target, sim::VPITypespecOrigin::AnonymousEnum, sourceTypeIdentity);
    enumTypespecsByIdentity[identity] = symbol;
  }

  uint64_t nextEnumConstId = 0;
  llvm::DenseMap<Attribute, uint64_t> nextEnumOrdinal;
  for (const EnumValueInventory &inventory : enumValues) {
    semantic::SVEnumValueSymbolOp enumValue = inventory.operation;
    StringRef name = getDebugName(enumValue);
    FlatSymbolRefAttr typespec =
        enumTypespecsByIdentity.lookup(inventory.identity);
    if (!typespec) {
      emitError(getSemanticLocation(enumValue))
          << "VPI enum constant cannot resolve its exact enum typespec";
      invalid = true;
      continue;
    }
    sim::SimVPIEnumConstDeclOp::create(
        builder, getSemanticLocation(enumValue), nextEnumConstId++,
        nextEnumOrdinal[inventory.identity]++, typespec.getValue(), name,
        inventory.constant.getValue());
  }

  auto retainVPITypeForPersistentOwner =
      [&](Operation *owner, sim::VPITypeSemanticsAttr type,
          IntegerAttr propagatedTypeIdentity = {}) {
        // Statement-backed source ownership is emitted in the next chunk.
        // Runtime storage must still exist now, but it must not retain a
        // typedef or interface-typespec symbol whose local declaration was
        // deliberately deferred with that owner.
        if (!ownerAnchorFor(owner))
          return sim::VPITypeSemanticsAttr{};
        // Anonymous enum reflection is only sound when the frontend supplied
        // the exact declaration identity used to join the value to its
        // typespec. Legacy and hand-authored semantic IR may carry an
        // enum-shaped value without that identity; keep lowering its executable
        // storage, but do not retain an unresolvable VPI type on the persistent
        // declaration.
        if (type.getKind() == sim::VPITypeKind::Enum &&
            !type.getTypedefAliases() && !propagatedTypeIdentity &&
            !owner->getAttrOfType<IntegerAttr>(vpiSourceTypeIdentityAttrName))
          return sim::VPITypeSemanticsAttr{};
        return type;
      };
  auto retainVPISourceTypeIdentity =
      [&](Operation *source, Operation *declaration,
          sim::VPITypeSemanticsAttr retainedType) {
        if (!retainedType)
          return;
        if (auto identity = source->getAttrOfType<IntegerAttr>(
                vpiSourceTypeIdentityAttrName))
          declaration->setAttr(sim::metadata::vpiSourceTypeIdentity, identity);
      };

  auto emitDescriptor = [&](Operation *op) {
    bool storage =
        isa<semantic::SVVariableSymbolOp, semantic::SVFormalArgumentSymbolOp,
            semantic::SVClassPropertySymbolOp>(op);
    StringRef path = getHierarchyName(op);
    if (path.empty()) {
      emitError(getSemanticLocation(op))
          << "design object is missing a hierarchy name";
      invalid = true;
      return;
    }
    if (descriptors.count(path))
      return;
    if (isa<semantic::SVClockingBlockSymbolOp>(op) &&
        (op->hasAttr(clockingEventMonitorRequiredAttrName) ||
         op->hasAttr(clockingEventListAttrName))) {
      Type type = sim::EventType::get(builder.getContext());
      uint64_t id = nextEventId++;
      uint64_t scopeId = scopes.lookup(op);
      descriptors[path] = {DescriptorInfo::Kind::Event, id, scopeId, type,
                           sim::NetResolutionKind::Wire};
      descriptors[path].rootType = type;
      FailureOr<sim::VPITypeSemanticsAttr> vpiType =
          makeVPITypeSemantics(type, getSemanticLocation(op));
      if (failed(vpiType)) {
        invalid = true;
        return;
      }
      descriptors[path].vpiType = *vpiType;

      // Event descriptors have no standalone declaration operation. Record
      // interface-owned clocking events on their scope declaration so
      // virtual-interface lowering can select the right event object.
      for (sim::SimScopeDeclOp scope : scopes.declarations) {
        if (scope.getId() != scopeId || !scope.getInterfaceTypeAttr())
          continue;
        SmallVector<Attribute> members;
        if (auto existing = scope->getAttrOfType<ArrayAttr>(
                virtualInterfaceClockEventMembersAttrName))
          llvm::append_range(members, existing);
        members.push_back(builder.getDictionaryAttr(
            {builder.getNamedAttr("member",
                                  builder.getStringAttr(getDebugName(op))),
             builder.getNamedAttr("descriptor",
                                  builder.getI64IntegerAttr(id))}));
        scope->setAttr(virtualInterfaceClockEventMembersAttrName,
                       builder.getArrayAttr(members));
        break;
      }
      return;
    }
    if (op->hasAttr(sequenceEndpointEventAttrName)) {
      Type type = sim::EventType::get(builder.getContext());
      uint64_t id = nextEventId++;
      descriptors[path] = {DescriptorInfo::Kind::Event, id, scopes.lookup(op),
                           type, sim::NetResolutionKind::Wire};
      descriptors[path].rootType = type;
      FailureOr<sim::VPITypeSemanticsAttr> vpiType =
          makeVPITypeSemantics(type, getSemanticLocation(op));
      if (failed(vpiType)) {
        invalid = true;
        return;
      }
      descriptors[path].vpiType = *vpiType;
      return;
    }
    uint64_t scopeId = scopes.lookup(op);
    if (!storage) {
      if (auto leafDefinitions =
              op->getAttrOfType<ArrayAttr>(interconnectLeavesAttrName)) {
        SmallVector<int64_t> boundsStorage;
        SmallVector<int64_t> packedStorage;
        auto sourceType = op->getAttrOfType<TypeAttr>("semantic_type");
        Type shape = sourceType ? sourceType.getValue() : Type{};
        while (shape) {
          if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(shape)) {
            boundsStorage.push_back(array.getLeft());
            boundsStorage.push_back(array.getRight());
            packedStorage.push_back(false);
            shape = array.getElementType();
            continue;
          }
          if (auto array = dyn_cast<semantic::RangedPackedArrayType>(shape)) {
            boundsStorage.push_back(array.getLeft());
            boundsStorage.push_back(array.getRight());
            packedStorage.push_back(true);
            shape = array.getElementType();
            continue;
          }
          break;
        }
        ArrayRef<int64_t> bounds = boundsStorage;
        ArrayRef<int64_t> packed = packedStorage;
        if (bounds.empty() || !isa<semantic::UntypedType>(shape)) {
          emitError(getSemanticLocation(op))
              << "interconnect has malformed typeless array shape";
          invalid = true;
          return;
        }
        Operation *lexicalOwner = op->getParentOp();
        while (lexicalOwner && !anchorSymbols.count(lexicalOwner))
          lexicalOwner = lexicalOwner->getParentOp();
        FlatSymbolRefAttr lexicalOwnerSymbol =
            anchorSymbols.lookup(lexicalOwner);
        if (!lexicalOwnerSymbol) {
          emitError(getSemanticLocation(op))
              << "interconnect array has no persistent lexical owner";
          invalid = true;
          return;
        }
        auto makeSymbol = [&] {
          return "__obelisk_vpi_anchor_" +
                 std::to_string(nextSyntheticInventoryId);
        };
        auto makeArrayAnchor = [&]() {
          std::string symbolName = makeSymbol();
          sim::SimVPIObjectAnchorOp anchor = sim::SimVPIObjectAnchorOp::create(
              builder, getSemanticLocation(op), symbolName,
              nextSyntheticInventoryId++,
              static_cast<uint32_t>(VPIKind::InterconnectArray), scopeId,
              lexicalOwnerSymbol, nextAnchorOrdinal[lexicalOwner]++,
              builder.getStringAttr(path),
              builder.getStringAttr(getDebugName(op)), UnitAttr{},
              sim::VPIObjectBackingAttr{}, builder.getDenseI64ArrayAttr(bounds),
              builder.getDenseI64ArrayAttr(packed), DenseI64ArrayAttr{},
              DenseI64ArrayAttr{}, IntegerAttr{});
          anchor->setAttr("vpi_properties",
                          netProperties(cast<semantic::SVNetSymbolOp>(op)));
          return anchor;
        };
        sim::SimVPIObjectAnchorOp root = makeArrayAnchor();
        for (Attribute attribute : leafDefinitions) {
          auto definition = dyn_cast<DictionaryAttr>(attribute);
          auto leafPath =
              definition ? definition.getAs<StringAttr>("path") : StringAttr{};
          auto semanticType =
              definition ? definition.getAs<TypeAttr>("type") : TypeAttr{};
          auto semanticVPIType =
              definition
                  ? definition.getAs<sim::VPITypeSemanticsAttr>("vpi_type")
                  : sim::VPITypeSemanticsAttr{};
          auto sourceNettypeReference =
              definition ? definition.getAs<SymbolRefAttr>("nettype")
                         : SymbolRefAttr{};
          auto sourceTypeIdentity =
              definition ? definition.getAs<IntegerAttr>("type_identity")
                         : IntegerAttr{};
          auto indicesAttr =
              definition ? definition.getAs<DenseI64ArrayAttr>("indices")
                         : DenseI64ArrayAttr{};
          ArrayRef<int64_t> indices =
              indicesAttr ? indicesAttr.asArrayRef() : ArrayRef<int64_t>{};
          if (!leafPath || !semanticType || !semanticVPIType ||
              indices.size() * 2 != bounds.size()) {
            emitError(getSemanticLocation(op))
                << "interconnect has malformed typed-leaf metadata";
            invalid = true;
            continue;
          }
          FailureOr<sim::VPITypeSemanticsAttr> mappedVPIType =
              remapVPITypeAliases(op, semanticVPIType);
          if (failed(mappedVPIType)) {
            invalid = true;
            continue;
          }
          semanticVPIType = *mappedVPIType;
          FailureOr<Type> type = normalizeSemanticType(semanticType.getValue(),
                                                       getSemanticLocation(op));
          if (failed(type)) {
            invalid = true;
            continue;
          }
          uint64_t id = nextNetId++;
          descriptors[leafPath.getValue()] = {DescriptorInfo::Kind::Net, id,
                                              scopeId, *type,
                                              sim::NetResolutionKind::Wire};
          descriptors[leafPath.getValue()].rootType = *type;
          sim::VPITypeSemanticsAttr retainedVPIType =
              retainVPITypeForPersistentOwner(op, semanticVPIType,
                                              sourceTypeIdentity);
          descriptors[leafPath.getValue()].vpiType = retainedVPIType;
          FlatSymbolRefAttr sourceNettype;
          if (sourceNettypeReference) {
            sourceNettype = nettypeSymbols.lookup(
                resolveSourceNettype(sourceNettypeReference));
            if (!sourceNettype) {
              emitError(getSemanticLocation(op))
                  << "interconnect leaf's VPI nettype declaration was not "
                     "preserved";
              invalid = true;
              continue;
            }
          }
          auto declaration = sim::SimNetDeclOp::create(
              builder, getSemanticLocation(op), id, scopeId, *type,
              sim::Lifetime::Design, leafPath,
              builder.getStringAttr((Twine(getDebugName(op)) + ".leaf").str()),
              sim::ComputeObservabilityKindAttr{}, sim::NetResolutionKind::Wire,
              DenseI64ArrayAttr{}, sim::StrengthAttr{}, UnitAttr{},
              retainedVPIType, sourceNettype);
          declaration->setAttr(
              "vpi_properties",
              netProperties(cast<semantic::SVNetSymbolOp>(op)));
          if (sourceTypeIdentity)
            declaration->setAttr(sim::metadata::vpiSourceTypeIdentity,
                                 sourceTypeIdentity);
          else
            retainVPISourceTypeIdentity(op, declaration, retainedVPIType);
          std::string leafSymbolName = makeSymbol();
          sim::VPIObjectBackingAttr backing = sim::VPIObjectBackingAttr::get(
              builder.getContext(), sim::VPIObjectBackingKind::Net,
              builder.getI64IntegerAttr(id), FlatSymbolRefAttr{});
          sim::SimVPIObjectAnchorOp::create(
              builder, getSemanticLocation(op), leafSymbolName,
              nextSyntheticInventoryId++,
              static_cast<uint32_t>(VPIKind::InterconnectNet), scopeId,
              FlatSymbolRefAttr::get(root.getSymNameAttr()),
              nextAnchorOrdinal[root.getOperation()]++, leafPath,
              builder.getStringAttr(getDebugName(op)), UnitAttr{}, backing,
              DenseI64ArrayAttr{}, DenseI64ArrayAttr{}, DenseI64ArrayAttr{},
              builder.getDenseI64ArrayAttr(indices), IntegerAttr{});
        }
        return;
      }
    }
    FailureOr<Type> type = getNormalizedSemanticType(op);
    auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
    sim::VPITypeSemanticsAttr inferredInterconnectVPIType =
        op->getAttrOfType<sim::VPITypeSemanticsAttr>(
            interconnectVPITypeAttrName);
    FailureOr<sim::VPITypeSemanticsAttr> inferredMappedVPIType =
        inferredInterconnectVPIType
            ? remapVPITypeAliases(op, inferredInterconnectVPIType)
            : FailureOr<sim::VPITypeSemanticsAttr>(failure());
    FailureOr<sim::VPITypeSemanticsAttr> vpiType =
        inferredInterconnectVPIType ? inferredMappedVPIType
        : semanticType ? makeVPITypeSemantics(semanticType.getValue(),
                                              getSemanticLocation(op),
                                              typedefLayersFor(op), op)
                       : FailureOr<sim::VPITypeSemanticsAttr>(failure());
    if (failed(type) || failed(vpiType)) {
      if (!semanticType)
        emitError(getSemanticLocation(op))
            << "design object is missing semantic type metadata";
      invalid = true;
      return;
    }
    StringAttr hierarchy = builder.getStringAttr(path);
    StringAttr debug = builder.getStringAttr(getDebugName(op));
    sim::VPITypeSemanticsAttr retainedVPIType =
        retainVPITypeForPersistentOwner(op, *vpiType);
    if (storage && isa<sim::EventType>(*type) &&
        !eventCellPaths.contains(path)) {
      uint64_t id = nextEventId++;
      descriptors[path] = {DescriptorInfo::Kind::Event, id, scopeId, *type,
                           sim::NetResolutionKind::Wire};
      descriptors[path].rootType = *type;
      descriptors[path].vpiType = retainedVPIType;
      return;
    }
    if (storage) {
      uint64_t id = nextStorageId++;
      descriptors[path] = {DescriptorInfo::Kind::Storage, id, scopeId, *type,
                           sim::NetResolutionKind::Wire};
      descriptors[path].rootType = *type;
      descriptors[path].vpiType = retainedVPIType;
      sim::Lifetime lifetime =
          (op->getParentOfType<semantic::SVStatementBlockSymbolOp>() ||
           isStaticFormal(op))
              ? sim::Lifetime::Static
              : sim::Lifetime::Design;
      auto declaration = sim::SimStorageDeclOp::create(
          builder, getSemanticLocation(op), id, scopeId, *type, lifetime,
          hierarchy, debug, sim::ComputeObservabilityKindAttr{},
          retainedVPIType);
      retainVPISourceTypeIdentity(op, declaration, retainedVPIType);
      if (namedEventArrayRanges.count(op))
        declaration->setAttr(sim::metadata::vpiIdentityDelegated,
                             anchorSymbols.lookup(op));
      if (isa<sim::EventType>(*type) &&
          isa<semantic::SVVariableSymbolOp, semantic::SVClassPropertySymbolOp>(
              op) &&
          !getChildren(op).empty())
        declaration->setAttr(eventExplicitInitializerAttrName,
                             builder.getUnitAttr());
      // Storage a subroutine owns is written by its callers, which the driver
      // rules of IEEE 1800-2017 6.5 do not count as competing drivers.
      if (op->getParentOfType<semantic::SVSubroutineSymbolOp>())
        declaration->setAttr(sim::metadata::subroutineStorage,
                             builder.getUnitAttr());
      if (auto body =
              dyn_cast<semantic::SVInstanceBodySymbolOp>(op->getParentOp());
          body && body->hasAttr("virtual_interface_identity") &&
          !isCompileTimeOnlyInstanceMember(body))
        declaration->setAttr("obelisk_sim.virtual_interface_member", debug);
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
    case semantic::SVNetKind::WAnd:
    case semantic::SVNetKind::TriAnd:
      resolution = sim::NetResolutionKind::WAnd;
      break;
    case semantic::SVNetKind::WOr:
    case semantic::SVNetKind::TriOr:
      resolution = sim::NetResolutionKind::WOr;
      break;
    case semantic::SVNetKind::Tri0:
      resolution = sim::NetResolutionKind::Tri0;
      break;
    case semantic::SVNetKind::Tri1:
      resolution = sim::NetResolutionKind::Tri1;
      break;
    case semantic::SVNetKind::Supply0:
      resolution = sim::NetResolutionKind::Supply0;
      break;
    case semantic::SVNetKind::Supply1:
      resolution = sim::NetResolutionKind::Supply1;
      break;
    case semantic::SVNetKind::TriReg:
      resolution = sim::NetResolutionKind::TriReg;
      break;
    case semantic::SVNetKind::Interconnect:
      // A typed interconnect is structural and inherits the resolution of its
      // connected net ports. The ordinary wire value is a temporary default;
      // port dominance freezes the effective component resolution below.
      resolution = sim::NetResolutionKind::Wire;
      break;
    case semantic::SVNetKind::UserDefined:
      // User-defined nets are atomic. Their generated resolver process reads
      // raw driver contributions, and every such drive is explicitly marked
      // deferred, so the bitwise runtime resolver is never entered for these
      // declarations. Use wire as the storage-layout fallback: uwire would
      // reject the multiple drivers before the user resolution process runs.
      resolution = sim::NetResolutionKind::Wire;
      break;
    default:
      emitError(getSemanticLocation(op))
          << "unsupported net resolution kind "
          << semantic::stringifySVNetKind(net.getNetKind());
      invalid = true;
      return;
    }
    if (net.getUnsupportedDelay()) {
      emitError(getSemanticLocation(op))
          << "net delays are not supported: " << *net.getUnsupportedDelay();
      invalid = true;
      return;
    }
    DenseI64ArrayAttr propagationDelays;
    if (auto delays = net.getDelayFs();
        delays && getNetInitializerExpressions(op).empty()) {
      if (delays->empty() || delays->size() > 3) {
        emitError(getSemanticLocation(op))
            << "net delay must contain one to three values";
        invalid = true;
        return;
      }
      SmallVector<int64_t, 3> ticks;
      for (int64_t femtoseconds : *delays) {
        if (femtoseconds < 0 ||
            static_cast<uint64_t>(femtoseconds) % designPrecisionFs != 0) {
          emitError(getSemanticLocation(op))
              << "net delay is incompatible with design precision";
          invalid = true;
          return;
        }
        ticks.push_back(static_cast<int64_t>(
            static_cast<uint64_t>(femtoseconds) / designPrecisionFs));
      }
      int64_t rise = ticks[0];
      int64_t fall = ticks.size() == 1 ? rise : ticks[1];
      // IEEE 1800-2017 28.16.2: a trireg's third delay is charge
      // decay, not turn-off.  One- and two-value declarations therefore
      // retain charge indefinitely; -1 is the internal no-decay sentinel.
      int64_t turnoffOrDecay;
      if (resolution == sim::NetResolutionKind::TriReg)
        turnoffOrDecay = ticks.size() == 3 ? ticks[2] : -1;
      else if (ticks.size() == 1)
        turnoffOrDecay = rise;
      else if (ticks.size() == 2)
        turnoffOrDecay = std::min(rise, fall);
      else
        turnoffOrDecay = ticks[2];
      propagationDelays =
          builder.getDenseI64ArrayAttr({rise, fall, turnoffOrDecay});
    }
    uint64_t id = nextNetId++;
    descriptors[path] = {DescriptorInfo::Kind::Net, id, scopeId, *type,
                         resolution};
    descriptors[path].rootType = *type;
    descriptors[path].vpiType = retainedVPIType;
    descriptors[path].delayedNet = static_cast<bool>(propagationDelays);
    FlatSymbolRefAttr sourceNettype;
    SymbolRefAttr nettypeReference = net.getNettypeSymbolAttr();
    if (!nettypeReference)
      nettypeReference =
          op->getAttrOfType<SymbolRefAttr>(interconnectNettypeAttrName);
    if (nettypeReference) {
      sourceNettype =
          nettypeSymbols.lookup(resolveSourceNettype(nettypeReference));
      if (!sourceNettype) {
        emitError(getSemanticLocation(net))
            << "user-defined net's VPI nettype declaration was not preserved";
        invalid = true;
        return;
      }
    }
    auto declaration = sim::SimNetDeclOp::create(
        builder, getSemanticLocation(op), id, scopeId, *type,
        sim::Lifetime::Design, hierarchy, debug,
        sim::ComputeObservabilityKindAttr{}, resolution, propagationDelays,
        resolution == sim::NetResolutionKind::TriReg
            ? sim::StrengthAttr::get(
                  builder.getContext(),
                  net.getChargeStrength()
                      ? lowerChargeStrength(*net.getChargeStrength())
                      : sim::Strength::Medium)
            : sim::StrengthAttr{},
        UnitAttr{}, retainedVPIType, sourceNettype);
    declaration->setAttr("vpi_properties", netProperties(net));
    retainVPISourceTypeIdentity(op, declaration, retainedVPIType);
    if (net.getNetKind() == semantic::SVNetKind::Interconnect) {
      Operation *lexicalOwner = op->getParentOp();
      while (lexicalOwner && !anchorSymbols.count(lexicalOwner))
        lexicalOwner = lexicalOwner->getParentOp();
      FlatSymbolRefAttr lexicalOwnerSymbol = anchorSymbols.lookup(lexicalOwner);
      if (!lexicalOwnerSymbol) {
        emitError(getSemanticLocation(op))
            << "interconnect net has no persistent lexical owner";
        invalid = true;
        return;
      }
      std::string symbolName =
          "__obelisk_vpi_anchor_" + std::to_string(nextSyntheticInventoryId);
      sim::VPIObjectBackingAttr backing = sim::VPIObjectBackingAttr::get(
          builder.getContext(), sim::VPIObjectBackingKind::Net,
          builder.getI64IntegerAttr(id), FlatSymbolRefAttr{});
      sim::SimVPIObjectAnchorOp anchor = sim::SimVPIObjectAnchorOp::create(
          builder, getSemanticLocation(op), symbolName,
          nextSyntheticInventoryId++,
          static_cast<uint32_t>(VPIKind::InterconnectNet), scopeId,
          lexicalOwnerSymbol, nextAnchorOrdinal[lexicalOwner]++, hierarchy,
          debug, UnitAttr{}, backing, DenseI64ArrayAttr{}, DenseI64ArrayAttr{},
          DenseI64ArrayAttr{}, DenseI64ArrayAttr{}, IntegerAttr{});
      anchor->setAttr("vpi_properties", netProperties(net));
    }
    if (net.getNetKind() == semantic::SVNetKind::UserDefined ||
        net->hasAttr("obelisk_sim.inferred_user_net")) {
      declaration->setAttr("obelisk_sim.user_defined_net",
                           builder.getUnitAttr());
      if (auto path = net.getResolutionFunctionPath())
        declaration->setAttr("obelisk_sim.resolution_function_path",
                             builder.getStringAttr(*path));
      if (auto symbol = net.getResolutionFunctionSymbol())
        declaration->setAttr("obelisk_sim.resolution_function_symbol", *symbol);
    }
    if (auto body =
            dyn_cast<semantic::SVInstanceBodySymbolOp>(op->getParentOp());
        body && body->hasAttr("virtual_interface_identity") &&
        !isCompileTimeOnlyInstanceMember(body))
      declaration->setAttr("obelisk_sim.virtual_interface_member", debug);
  };

  // Materialize canonical objects first so alias resolution is independent of
  // semantic-tree traversal order.
  for (Operation *op : designObjects)
    if (!portAliases.aliases.count(getHierarchyName(op)))
      emitDescriptor(op);

  // A static constraint block has one mode bit shared by every instance of
  // its declaring class (IEEE 1800-2017 18.5.11). Keep that bit in flattened
  // design storage rather than in any class object. Zero is the required
  // initial enabled state; the stored value is the disabled bit used by the
  // executable constraint mask.
  semanticRoot.walk([&](semantic::SVConstraintBlockSymbolOp constraint) {
    if (!constraint.getIsStatic().value_or(false))
      return;
    uint64_t id = nextStorageId++;
    constraint->setAttr(staticConstraintStorageAttrName,
                        builder.getI64IntegerAttr(id));
    StringRef path = getHierarchyName(constraint);
    if (path.empty()) {
      emitError(getSemanticLocation(constraint))
          << "static constraint block is missing a hierarchy name";
      invalid = true;
      return;
    }
    std::string hierarchy =
        (llvm::Twine(path) + ".__obelisk_constraint_mode").str();
    Type type = builder.getI64Type();
    uint64_t scopeId = scopes.lookup(constraint);
    descriptors[hierarchy] = {DescriptorInfo::Kind::Storage, id, scopeId, type,
                              sim::NetResolutionKind::Wire};
    descriptors[hierarchy].rootType = type;
    sim::SimStorageDeclOp::create(
        builder, getSemanticLocation(constraint), id, scopeId, type,
        sim::Lifetime::Design, builder.getStringAttr(hierarchy),
        builder.getStringAttr("__obelisk_constraint_mode"),
        sim::ComputeObservabilityKindAttr{}, sim::VPITypeSemanticsAttr{});
  });

  // A static random property has one rand_mode bit shared by all instances of
  // its declaring class (IEEE 1800-2017 18.8). Keep the disabled bit beside
  // the class-wide value rather than in any object's ordinary mode mask.
  semanticRoot.walk([&](semantic::SVClassPropertySymbolOp property) {
    if (property.getLifetime() != semantic::SVVariableLifetime::Static ||
        property.getRandMode() == semantic::SVRandMode::None)
      return;
    StringRef path = getHierarchyName(property);
    if (path.empty()) {
      emitError(getSemanticLocation(property))
          << "static random property is missing a hierarchy name";
      invalid = true;
      return;
    }
    std::string hierarchy = (llvm::Twine(path) + ".$rand_mode").str();
    if (descriptors.count(hierarchy)) {
      emitError(getSemanticLocation(property))
          << "static rand_mode state conflicts with an existing design "
             "object";
      invalid = true;
      return;
    }
    uint64_t id = nextStorageId++;
    property->setAttr(staticRandomModeStorageAttrName,
                      builder.getI64IntegerAttr(id));
    Type type = builder.getI64Type();
    uint64_t scopeId = scopes.lookup(property);
    descriptors[hierarchy] = {DescriptorInfo::Kind::Storage, id, scopeId, type,
                              sim::NetResolutionKind::Wire};
    descriptors[hierarchy].rootType = type;
    sim::SimStorageDeclOp::create(
        builder, getSemanticLocation(property), id, scopeId, type,
        sim::Lifetime::Design, builder.getStringAttr(hierarchy),
        builder.getStringAttr("__obelisk_rand_mode"),
        sim::ComputeObservabilityKindAttr{}, sim::VPITypeSemanticsAttr{});
  });

  for (Operation *op : designObjects) {
    StringRef path = getHierarchyName(op);
    auto alias = portAliases.aliases.find(path);
    if (alias == portAliases.aliases.end())
      continue;
    llvm::StringSet<> seen;
    StringRef canonical = alias->second;
    uint64_t viewOffset = 0;
    uint64_t packedViewOffset = 0;
    SmallVector<const StaticStorageView *> viewChain;
    if (auto view = portAliases.refViews.find(path);
        view != portAliases.refViews.end()) {
      viewOffset = view->second.offset;
      packedViewOffset = view->second.packedOffset;
      viewChain.push_back(&view->second);
    }
    bool cyclic = false;
    auto next = portAliases.aliases.find(canonical);
    while (next != portAliases.aliases.end()) {
      if (!seen.insert(canonical).second) {
        emitError(getSemanticLocation(op)) << "cyclic port alias for " << path;
        invalid = true;
        cyclic = true;
        break;
      }
      if (auto view = portAliases.refViews.find(canonical);
          view != portAliases.refViews.end()) {
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
      next = portAliases.aliases.find(canonical);
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
    if (portAliases.refViews.count(path) &&
        target->second.kind != DescriptorInfo::Kind::Storage) {
      emitError(getSemanticLocation(op))
          << "ref port cannot alias a net or driver";
      invalid = true;
      continue;
    }
    descriptors[path] = target->second;
    if (auto view = portAliases.refViews.find(path);
        view != portAliases.refViews.end()) {
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
      auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
      FailureOr<sim::VPITypeSemanticsAttr> viewType = makeVPITypeSemantics(
          semanticType ? semanticType.getValue() : view->second.viewType,
          getSemanticLocation(op), typedefLayersFor(op), op);
      if (failed(viewType)) {
        invalid = true;
        continue;
      }
      descriptors[path].vpiType = *viewType;
    }
  }
  for (const auto &[path, targetPath] : portAliases.interfaceAliases) {
    auto target = descriptors.find(targetPath);
    if (target == descriptors.end()) {
      emitError(module.getLoc())
          << "interface modport member has no flattened target: " << path;
      invalid = true;
      continue;
    }
    descriptors[path] = target->second;
    auto view = portAliases.interfaceViews.find(path);
    if (view == portAliases.interfaceViews.end())
      continue;
    if (target->second.kind != DescriptorInfo::Kind::Storage &&
        target->second.kind != DescriptorInfo::Kind::Net) {
      emitError(module.getLoc())
          << "interface modport expression does not select storage or a net: "
          << path;
      invalid = true;
      continue;
    }
    if (target->second.type != view->second.rootType) {
      emitError(module.getLoc())
          << "interface modport expression view has a mismatched root type: "
          << path;
      invalid = true;
      continue;
    }
    if (view->second.offset > UINT64_MAX - target->second.viewOffset ||
        view->second.packedOffset >
            UINT64_MAX - target->second.packedViewOffset) {
      emitError(module.getLoc())
          << "interface modport expression view offset overflows: " << path;
      invalid = true;
      continue;
    }
    SmallVector<int64_t> viewIndices = target->second.viewIndices;
    llvm::append_range(viewIndices, view->second.indices);
    Type aggregateViewType = target->second.rootType;
    for (int64_t index : viewIndices) {
      if (index < 0 ||
          static_cast<uint64_t>(index) > std::numeric_limits<unsigned>::max()) {
        aggregateViewType = {};
        break;
      }
      aggregateViewType = sim::getAggregateElementType(
          aggregateViewType, static_cast<unsigned>(index));
      if (!aggregateViewType)
        break;
    }
    if (!aggregateViewType) {
      emitError(module.getLoc())
          << "interface modport expression has an invalid storage view: "
          << path;
      invalid = true;
      continue;
    }
    descriptors[path].type = view->second.viewType;
    descriptors[path].rootType = target->second.rootType;
    descriptors[path].viewOffset =
        target->second.viewOffset + view->second.offset;
    descriptors[path].packedViewOffset =
        target->second.packedViewOffset + view->second.packedOffset;
    descriptors[path].viewIndices = std::move(viewIndices);
    descriptors[path].aggregateViewType = aggregateViewType;
    FailureOr<ArrayAttr> viewTypedefLayers =
        remapTypedefLayers(semanticRoot, view->second.typedefLayers);
    if (failed(viewTypedefLayers)) {
      invalid = true;
      continue;
    }
    FailureOr<sim::VPITypeSemanticsAttr> viewType =
        makeVPITypeSemantics(view->second.semanticType, module.getLoc(),
                             *viewTypedefLayers, semanticRoot);
    if (failed(viewType)) {
      invalid = true;
      continue;
    }
    descriptors[path].vpiType = *viewType;
  }
  uint64_t nextPortId = 0;
  llvm::StringSet<> emittedPorts;
  auto hasInterconnectLeaves = [&](StringRef root) {
    std::string prefix = (root + Twine("[")).str();
    return llvm::any_of(descriptors, [&](const auto &entry) {
      return entry.getKey().starts_with(prefix) &&
             entry.second.kind == DescriptorInfo::Kind::Net;
    });
  };
  for (semantic::SVPortConnectionOp connection : portAliases.connections) {
    StringRef path = connection.getInternalPath().value_or(StringRef{});
    if (path.empty())
      continue;
    auto source = descriptors.find(path);
    if (source == descriptors.end()) {
      // Interface-instance and untyped ports intentionally have no packed
      // canonical state descriptor and therefore cannot appear in EVCD.
      if (connection.getInterfaceInstanceSymbol() ||
          isa<semantic::UntypedType>(connection.getFormalType()) ||
          hasInterconnectLeaves(path))
        continue;
      emitError(getSemanticLocation(connection))
          << "module port has no flattened source descriptor: " << path;
      invalid = true;
      continue;
    }
    if (source->second.kind != DescriptorInfo::Kind::Storage &&
        source->second.kind != DescriptorInfo::Kind::Net) {
      emitError(getSemanticLocation(connection))
          << "module port does not reference packed storage or a net: " << path;
      invalid = true;
      continue;
    }
    // The current EVCD record is one fixed packed vector. Keep unpacked and
    // otherwise aggregate-only ports out of that inventory until they can be
    // expanded into declaration-ordered element records; their executable
    // port connection remains fully supported.
    if (!sim::getPackedWidth(source->second.type))
      continue;
    sim::PortDirection direction = sim::PortDirection::InOut;
    switch (connection.getDirection()) {
    case semantic::SVArgumentDirection::In:
      direction = sim::PortDirection::Input;
      break;
    case semantic::SVArgumentDirection::Out:
      direction = sim::PortDirection::Output;
      break;
    case semantic::SVArgumentDirection::InOut:
    case semantic::SVArgumentDirection::Ref:
      direction = sim::PortDirection::InOut;
      break;
    }
    std::optional<uint64_t> portScopeId;
    std::string portScopeHierarchy;
    for (Operation *member : getChildren(connection->getParentOp()))
      if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(member)) {
        auto found = scopes.ids.find(body);
        if (found != scopes.ids.end()) {
          portScopeId = found->second;
          portScopeHierarchy = getHierarchyName(body).str();
        }
        break;
      }
    if (!portScopeId) {
      emitError(getSemanticLocation(connection))
          << "module port has no owning instance scope: " << path;
      invalid = true;
      continue;
    }
    std::string portKey =
        (Twine(*portScopeId) + ":" + Twine(connection.getFormalOrdinal()))
            .str();
    if (!emittedPorts.insert(portKey).second)
      continue;
    StringRef formalName =
        connection.getFormalName().value_or(path.rsplit('.').second);
    if (portScopeHierarchy.empty() || formalName.empty()) {
      emitError(getSemanticLocation(connection))
          << "module port has no reflected formal hierarchy: " << path;
      invalid = true;
      continue;
    }
    std::string portHierarchy =
        (Twine(portScopeHierarchy) + "." + formalName).str();
    FailureOr<sim::VPITypeSemanticsAttr> formalVPIType = makeVPITypeSemantics(
        connection.getFormalType(), getSemanticLocation(connection),
        typedefLayersFor(connection), connection);
    if (failed(formalVPIType)) {
      invalid = true;
      continue;
    }
    auto declaration = sim::SimPortDeclOp::create(
        builder, getSemanticLocation(connection), nextPortId++, *portScopeId,
        source->second.id, source->second.kind == DescriptorInfo::Kind::Net,
        source->second.viewOffset, source->second.type, direction,
        connection.getFormalOrdinal(), builder.getStringAttr(portHierarchy),
        connection.getFormalName()
            ? builder.getStringAttr(*connection.getFormalName())
            : StringAttr{},
        *formalVPIType);
    retainVPISourceTypeIdentity(connection, declaration, *formalVPIType);
  }

  // Execution collapses whole-net aliases onto one resolved-net descriptor.
  // Preserve every other declared spelling as a separate cold VPI object
  // whose value plane is the canonical descriptor's state offset. The
  // canonical spelling continues to use the physical net declaration itself.
  struct DeclaredNetObject {
    sim::VPIObjectRefAttr reference;
    uint32_t vpiKind;
    uint64_t scopeId;
    Type type;
    uint64_t backingNetId;
  };
  llvm::StringMap<DeclaredNetObject> declaredNets;
  llvm::DenseMap<uint64_t, SmallVector<DeclaredNetObject, 2>>
      declaredNetsByBacking;
  uint64_t nextVPINetIdentityId = 0;
  for (Operation *operation : designObjects) {
    auto net = dyn_cast<semantic::SVNetSymbolOp>(operation);
    if (!net)
      continue;
    StringRef path = getHierarchyName(operation);
    auto descriptor = descriptors.find(path);
    if (path.empty() || descriptor == descriptors.end() ||
        descriptor->second.kind != DescriptorInfo::Kind::Net ||
        descriptor->second.viewOffset != 0 ||
        descriptor->second.packedViewOffset != 0 ||
        !descriptor->second.viewIndices.empty() ||
        descriptor->second.type != descriptor->second.rootType)
      continue;
    uint64_t scopeId = scopes.lookup(operation);

    if (!portAliases.aliases.count(path)) {
      sim::VPIObjectRefAttr reference = sim::VPIObjectRefAttr::get(
          builder.getContext(), sim::VPIObjectRefKind::Net,
          builder.getI64IntegerAttr(descriptor->second.id));
      DeclaredNetObject declared{
          reference, sim::vpiKindForNet(descriptor->second.vpiType), scopeId,
          descriptor->second.type, descriptor->second.id};
      declaredNets[path] = declared;
      declaredNetsByBacking[descriptor->second.id].push_back(declared);
      continue;
    }

    FailureOr<Type> sourceType = getNormalizedSemanticType(operation);
    auto semanticType = operation->getAttrOfType<TypeAttr>("semantic_type");
    FailureOr<sim::VPITypeSemanticsAttr> vpiType =
        semanticType
            ? makeVPITypeSemantics(semanticType.getValue(),
                                   getSemanticLocation(operation),
                                   typedefLayersFor(operation), operation)
            : FailureOr<sim::VPITypeSemanticsAttr>(failure());
    if (failed(sourceType) || failed(vpiType)) {
      if (!semanticType)
        emitError(getSemanticLocation(operation))
            << "aliased net is missing semantic type metadata";
      invalid = true;
      continue;
    }
    sim::VPITypeSemanticsAttr retainedVPIType =
        retainVPITypeForPersistentOwner(operation, *vpiType);
    if (!retainedVPIType) {
      emitError(getSemanticLocation(operation))
          << "aliased net source type cannot be retained for VPI";
      invalid = true;
      continue;
    }
    FlatSymbolRefAttr sourceNettype;
    if (SymbolRefAttr reference = net.getNettypeSymbolAttr()) {
      sourceNettype = nettypeSymbols.lookup(resolveSourceNettype(reference));
      if (!sourceNettype) {
        emitError(getSemanticLocation(net))
            << "collapsed user-defined net's VPI nettype declaration was not "
               "preserved";
        invalid = true;
        continue;
      }
    }
    uint64_t identityId = nextVPINetIdentityId++;
    sim::SimVPINetIdentityDeclOp identity =
        sim::SimVPINetIdentityDeclOp::create(
            builder, getSemanticLocation(operation), identityId,
            descriptor->second.id, scopeId, *sourceType,
            builder.getStringAttr(path),
            builder.getStringAttr(getDebugName(operation)), retainedVPIType,
            sourceNettype);
    identity->setAttr("vpi_properties", netProperties(net));
    retainVPISourceTypeIdentity(operation, identity, retainedVPIType);
    sim::VPIObjectRefAttr reference = sim::VPIObjectRefAttr::get(
        builder.getContext(), sim::VPIObjectRefKind::NetIdentity,
        builder.getI64IntegerAttr(identityId));
    DeclaredNetObject declared{reference, sim::vpiKindForNet(retainedVPIType),
                               scopeId, *sourceType, descriptor->second.id};
    declaredNets[path] = declared;
    declaredNetsByBacking[descriptor->second.id].push_back(declared);
  }

  // IEEE 1800-2023 37.16 requires every collapsed declared net to identify
  // one unique simulated net. Only collapsed spellings need image edges;
  // the cold VPI query returns an uncollapsed net itself without storing one
  // redundant relation for every physical net in the design.
  for (Operation *operation : designObjects) {
    StringRef path = getHierarchyName(operation);
    auto source = declaredNets.find(path);
    auto descriptor = descriptors.find(path);
    if (source == declaredNets.end() || descriptor == descriptors.end() ||
        descriptor->second.kind != DescriptorInfo::Kind::Net ||
        source->second.reference.getKind() !=
            sim::VPIObjectRefKind::NetIdentity)
      continue;
    sim::VPIObjectRefAttr target = sim::VPIObjectRefAttr::get(
        builder.getContext(), sim::VPIObjectRefKind::Net,
        builder.getI64IntegerAttr(descriptor->second.id));
    sim::SimVPIRelationDeclOp::create(
        builder, getSemanticLocation(operation), source->second.reference,
        static_cast<uint32_t>(reflection::VPIRelationKind::SimNetRel),
        sim::VPIRelationMode::Handle, 0, target);
  }

  // Preserve direct whole-object operands of scope-owned statements as cold
  // immutable relation inventory. Selects and computed expressions require a
  // distinct occurrence identity and are intentionally left for the compact
  // expression table instead of being misrepresented as their root storage.
  struct DirectEndpoint {
    sim::VPIObjectRefAttr reference;
    const DescriptorInfo *descriptor;
    uint32_t vpiKind;
    uint64_t scopeId;
    Type type;
  };
  auto directEndpoint =
      [&](Operation *expression) -> std::optional<DirectEndpoint> {
    FailureOr<StaticStorageView> view = getStaticStorageView(expression);
    if (failed(view) || !view->identity || view->offset != 0 ||
        view->packedOffset != 0 || !view->indices.empty() ||
        view->rootType != view->viewType)
      return std::nullopt;
    auto found = descriptors.find(view->path);
    if (found == descriptors.end())
      return std::nullopt;
    const DescriptorInfo &descriptor = found->second;
    if ((descriptor.kind != DescriptorInfo::Kind::Storage &&
         descriptor.kind != DescriptorInfo::Kind::Net) ||
        descriptor.viewOffset != 0 || descriptor.packedViewOffset != 0 ||
        !descriptor.viewIndices.empty() ||
        descriptor.type != descriptor.rootType)
      return std::nullopt;
    sim::VPIObjectRefAttr reference;
    uint64_t scopeId = descriptor.scopeId;
    Type endpointType = descriptor.type;
    uint32_t vpiKind = 0;
    if (descriptor.kind == DescriptorInfo::Kind::Net) {
      auto declared = declaredNets.find(view->path);
      if (declared == declaredNets.end())
        return std::nullopt;
      reference = declared->second.reference;
      scopeId = declared->second.scopeId;
      endpointType = declared->second.type;
      vpiKind = declared->second.vpiKind;
    } else {
      reference = sim::VPIObjectRefAttr::get(
          builder.getContext(), sim::VPIObjectRefKind::Storage,
          builder.getI64IntegerAttr(descriptor.id));
      vpiKind = sim::vpiKindForStorage(descriptor.vpiType);
    }
    return DirectEndpoint{reference, &descriptor, vpiKind, scopeId,
                          endpointType};
  };
  auto statementReference = [&](uint64_t id) {
    return sim::VPIObjectRefAttr::get(builder.getContext(),
                                      sim::VPIObjectRefKind::Statement,
                                      builder.getI64IntegerAttr(id));
  };
  auto selectorValue = [](reflection::VPIRelationKind relation) {
    return static_cast<uint32_t>(relation);
  };
  struct ReverseRelation {
    sim::VPIObjectRefAttr source;
    uint32_t selector;
    sim::VPIObjectRefAttr target;
    Location location;
  };
  SmallVector<ReverseRelation> reverseRelations;
  auto addReverse = [&](const DirectEndpoint &source, uint32_t selector,
                        const ScopeOwnedStatementPlan &target) {
    const auto *edge = reflection::findVPITraversal(
        source.vpiKind, selector, reflection::VPITraversalMode::Iterate);
    if (!edge || edge->statementContainment ||
        edge->automaticRelation != reflection::VPIAutomaticRelation::None ||
        !reflection::vpiObjectSetContains(edge->targets,
                                          static_cast<uint32_t>(target.kind)))
      return;
    reverseRelations.push_back({source.reference, selector,
                                statementReference(target.statementId),
                                getSemanticLocation(target.source)});
  };
  auto forPhysicalNetSpelling = [&](const DirectEndpoint &source,
                                    auto &&callback) {
    if (source.descriptor->kind != DescriptorInfo::Kind::Net) {
      callback(source);
      return;
    }
    auto aliases = declaredNetsByBacking.find(source.descriptor->id);
    if (aliases == declaredNetsByBacking.end()) {
      callback(source);
      return;
    }
    for (const DeclaredNetObject &declared : aliases->second)
      callback(DirectEndpoint{declared.reference, source.descriptor,
                              declared.vpiKind, declared.scopeId,
                              declared.type});
  };
  llvm::DenseMap<uint64_t, sim::SimScopeDeclOp> scopeDeclarationsById;
  for (sim::SimScopeDeclOp scope : scopes.declarations)
    scopeDeclarationsById[scope.getId()] = scope;
  llvm::DenseMap<uint64_t, std::optional<uint64_t>> moduleScopeCache;
  std::function<std::optional<uint64_t>(uint64_t)> moduleInstanceScope =
      [&](uint64_t id) -> std::optional<uint64_t> {
    if (auto cached = moduleScopeCache.find(id);
        cached != moduleScopeCache.end())
      return cached->second;
    auto found = scopeDeclarationsById.find(id);
    if (found == scopeDeclarationsById.end())
      return moduleScopeCache[id] = std::nullopt;
    uint32_t kind = sim::vpiKindForScope(found->second);
    if (kind == static_cast<uint32_t>(VPIKind::Module) ||
        kind == static_cast<uint32_t>(VPIKind::Interface) ||
        kind == static_cast<uint32_t>(VPIKind::Program))
      return moduleScopeCache[id] = id;
    if (!found->second.getParent())
      return moduleScopeCache[id] = std::nullopt;
    return moduleScopeCache[id] =
               moduleInstanceScope(*found->second.getParent());
  };

  for (const ScopeOwnedStatementPlan &plan : scopeOwnedStatementPlans) {
    SmallVector<Operation *> lhsRhs;
    if (plan.kind == VPIKind::AliasStmt) {
      lhsRhs = {plan.lhs, plan.rhs};
    } else {
      SmallVector<Operation *> children = getChildren(plan.source);
      if (children.size() != 1)
        continue;
      auto assignment =
          dyn_cast<semantic::SVAssignmentExpressionOp>(children.front());
      if (!assignment)
        continue;
      lhsRhs = getChildren(assignment);
    }
    if (lhsRhs.size() != 2)
      continue;

    std::optional<DirectEndpoint> lhs = directEndpoint(lhsRhs[0]);
    std::optional<DirectEndpoint> rhs = directEndpoint(lhsRhs[1]);
    sim::VPIObjectRefAttr statement = statementReference(plan.statementId);
    auto emitForward = [&](std::optional<DirectEndpoint> target,
                           reflection::VPIRelationKind selector) {
      if (!target)
        return;
      uint32_t selectorNumber = selectorValue(selector);
      const auto *edge = reflection::findVPITraversal(
          static_cast<uint32_t>(plan.kind), selectorNumber,
          reflection::VPITraversalMode::Handle);
      if (!edge || edge->statementContainment ||
          edge->automaticRelation != reflection::VPIAutomaticRelation::None ||
          !reflection::vpiObjectSetContains(edge->targets, target->vpiKind))
        return;
      sim::SimVPIRelationDeclOp::create(
          builder, getSemanticLocation(plan.source), statement, selectorNumber,
          sim::VPIRelationMode::Handle, 0, target->reference);
    };
    emitForward(lhs, reflection::VPIRelationKind::LhsRel);
    emitForward(rhs, reflection::VPIRelationKind::RhsRel);

    if (plan.kind == VPIKind::AliasStmt)
      continue;

    // IEEE 1800-2023 37.46 and 37.58 define the continuous-assignment
    // connectivity relations below.
    std::optional<uint64_t> statementModule = moduleInstanceScope(plan.scopeId);
    if (lhs) {
      forPhysicalNetSpelling(*lhs, [&](const DirectEndpoint &spelling) {
        addReverse(spelling,
                   selectorValue(reflection::VPIRelationKind::DriverRel), plan);
        std::optional<uint64_t> objectModule =
            moduleInstanceScope(spelling.scopeId);
        if (spelling.descriptor->kind == DescriptorInfo::Kind::Net &&
            statementModule && objectModule &&
            *statementModule == *objectModule)
          addReverse(spelling,
                     selectorValue(reflection::VPIRelationKind::LocalDriverRel),
                     plan);
        std::optional<uint64_t> width = sim::getPackedWidth(spelling.type);
        if (spelling.descriptor->kind == DescriptorInfo::Kind::Net && width &&
            *width == 1)
          addReverse(spelling, static_cast<uint32_t>(VPIKind::ContAssign),
                     plan);
      });
      addReverse(*lhs, selectorValue(reflection::VPIRelationKind::UseRel),
                 plan);
      // IEEE 1800-2023 37.16 restricts vpiContAssign iteration from a net to
      // scalar nets and bit-selects. Variables have their own driver model in
      // 37.17 and 37.21 and expose whole-variable continuous assignments.
      if (lhs->descriptor->kind == DescriptorInfo::Kind::Storage)
        addReverse(*lhs, static_cast<uint32_t>(VPIKind::ContAssign), plan);
    }
    if (rhs) {
      forPhysicalNetSpelling(*rhs, [&](const DirectEndpoint &spelling) {
        addReverse(spelling,
                   selectorValue(reflection::VPIRelationKind::LoadRel), plan);
        std::optional<uint64_t> objectModule =
            moduleInstanceScope(spelling.scopeId);
        if (spelling.descriptor->kind == DescriptorInfo::Kind::Net &&
            statementModule && objectModule &&
            *statementModule == *objectModule)
          addReverse(spelling,
                     selectorValue(reflection::VPIRelationKind::LocalLoadRel),
                     plan);
      });
      addReverse(*rhs, selectorValue(reflection::VPIRelationKind::UseRel),
                 plan);
    }
  }
  llvm::sort(reverseRelations, [](const ReverseRelation &left,
                                  const ReverseRelation &right) {
    uint64_t leftSource = left.source.getId().getValue().getZExtValue();
    uint64_t rightSource = right.source.getId().getValue().getZExtValue();
    uint64_t leftTarget = left.target.getId().getValue().getZExtValue();
    uint64_t rightTarget = right.target.getId().getValue().getZExtValue();
    return std::make_tuple(left.source.getKind(), leftSource, left.selector,
                           leftTarget) <
           std::make_tuple(right.source.getKind(), rightSource, right.selector,
                           rightTarget);
  });
  reverseRelations.erase(std::unique(reverseRelations.begin(),
                                     reverseRelations.end(),
                                     [](const ReverseRelation &left,
                                        const ReverseRelation &right) {
                                       return left.source == right.source &&
                                              left.selector == right.selector &&
                                              left.target == right.target;
                                     }),
                         reverseRelations.end());
  sim::VPIObjectRefAttr previousSource;
  uint32_t previousSelector = 0;
  uint64_t ordinal = 0;
  for (const ReverseRelation &relation : reverseRelations) {
    if (relation.source != previousSource ||
        relation.selector != previousSelector) {
      previousSource = relation.source;
      previousSelector = relation.selector;
      ordinal = 0;
    }
    sim::SimVPIRelationDeclOp::create(
        builder, relation.location, relation.source, relation.selector,
        sim::VPIRelationMode::Iterate, ordinal++, relation.target);
  }
  if (invalid)
    return failure();
  return descriptors;
}

} // namespace obelisk::simlowering
