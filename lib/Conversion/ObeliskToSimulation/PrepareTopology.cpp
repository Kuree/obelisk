//===- PrepareTopology.cpp - Static design topology analysis -------------===//
//
// Resolves semantic port views and aliases before descriptor and net topology
// materialization.
//
//===----------------------------------------------------------------------===//

#include "PrepareTopology.h"

#include "Detail.h"

#include "llvm/ADT/APInt.h"
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

  // IEEE 1800-2017 10.10 net aliases are static topology, not executable
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
    if (!path.empty() && !base.empty() && !name.empty())
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
                             uint64_t designPrecisionFs, OpBuilder &builder) {
  llvm::StringMap<DescriptorInfo> descriptors;
  uint64_t nextStorageId = 0;
  uint64_t nextNetId = 0;
  uint64_t nextEventId = 0;
  bool invalid = false;
  SmallVector<Operation *> designObjects;

  const llvm::StringSet<> &eventCellPaths = portAliases.eventCellPaths;

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
      return;
    }
    uint64_t scopeId = scopes.lookup(op);
    if (!storage) {
      if (auto leafDefinitions =
              op->getAttrOfType<ArrayAttr>(interconnectLeavesAttrName)) {
        for (Attribute attribute : leafDefinitions) {
          auto definition = dyn_cast<DictionaryAttr>(attribute);
          auto leafPath =
              definition ? definition.getAs<StringAttr>("path") : StringAttr{};
          auto semanticType =
              definition ? definition.getAs<TypeAttr>("type") : TypeAttr{};
          if (!leafPath || !semanticType) {
            emitError(getSemanticLocation(op))
                << "interconnect has malformed typed-leaf metadata";
            invalid = true;
            continue;
          }
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
          sim::SimNetDeclOp::create(
              builder, getSemanticLocation(op), id, scopeId, *type,
              sim::Lifetime::Design, leafPath,
              builder.getStringAttr((Twine(getDebugName(op)) + ".leaf").str()),
              sim::ComputeObservabilityKindAttr{}, sim::NetResolutionKind::Wire,
              DenseI64ArrayAttr{}, sim::StrengthAttr{}, UnitAttr{});
        }
        return;
      }
    }
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type)) {
      invalid = true;
      return;
    }
    StringAttr hierarchy = builder.getStringAttr(path);
    StringAttr debug = builder.getStringAttr(getDebugName(op));
    if (storage && isa<sim::EventType>(*type) &&
        !eventCellPaths.contains(path)) {
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
      auto declaration = sim::SimStorageDeclOp::create(
          builder, getSemanticLocation(op), id, scopeId, *type, lifetime,
          hierarchy, debug, sim::ComputeObservabilityKindAttr{});
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
    descriptors[path].delayedNet = static_cast<bool>(propagationDelays);
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
        UnitAttr{});
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
        sim::ComputeObservabilityKindAttr{});
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
    sim::SimStorageDeclOp::create(builder, getSemanticLocation(property), id,
                                  scopeId, type, sim::Lifetime::Design,
                                  builder.getStringAttr(hierarchy),
                                  builder.getStringAttr("__obelisk_rand_mode"),
                                  sim::ComputeObservabilityKindAttr{});
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
    sim::SimPortDeclOp::create(
        builder, getSemanticLocation(connection), nextPortId++, *portScopeId,
        source->second.id, source->second.kind == DescriptorInfo::Kind::Net,
        source->second.viewOffset, source->second.type, direction,
        connection.getFormalOrdinal(), builder.getStringAttr(portHierarchy),
        connection.getFormalName()
            ? builder.getStringAttr(*connection.getFormalName())
            : StringAttr{});
  }
  if (invalid)
    return failure();
  return descriptors;
}

} // namespace obelisk::simlowering
