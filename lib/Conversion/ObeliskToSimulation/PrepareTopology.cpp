//===- PrepareTopology.cpp - Static design topology analysis -------------===//
//
// Resolves semantic port views and aliases before descriptor and net topology
// materialization.
//
//===----------------------------------------------------------------------===//

#include "PrepareTopology.h"
#include "PrepareExpressions.h"
#include "PrepareRandomFunctions.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"

#include "Detail.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
#include "obelisk/Reflection/VPIObjectModel.h"

#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <tuple>

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

  // This is a speculative alias query, not expression validation. In
  // particular, concatenations and streams may contain legal void-typed
  // zero-width elements which have no independently normalizable storage.
  if (!isa<semantic::SVMemberAccessExpressionOp,
           semantic::SVElementSelectExpressionOp,
           semantic::SVRangeSelectExpressionOp>(expression))
    return failure();
  SmallVector<Operation *> children = getChildren(expression);
  if (children.empty())
    return failure();
  FailureOr<StaticStorageView> base = getStaticStorageView(children.front());
  if (failed(base))
    return failure();
  FailureOr<Type> resultType = getNormalizedSemanticType(expression);
  if (failed(resultType))
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

LogicalResult expandModportExpressions(ModuleOp module) {
  // IEEE 1800-2023 25.5.4: a modport expression names an expression of the
  // interface's items, and a use through an interface port or a virtual
  // interface denotes that expression in the selected instance. A single
  // storage view becomes an alias; any other expression, such as a
  // concatenation, is expanded at each use so reads and writes reach every
  // item it spans.
  llvm::DenseMap<StringAttr, semantic::SVModportPortSymbolOp> expressionPorts;
  llvm::DenseMap<StringAttr, Operation *> symbols;
  module.walk([&](Operation *operation) {
    if (auto name = operation->getAttrOfType<StringAttr>(
            SymbolTable::getSymbolAttrName()))
      symbols[name] = operation;
    auto port = dyn_cast<semantic::SVModportPortSymbolOp>(operation);
    Operation *expression =
        port ? getSingleRegionRoot(port.getBody()) : nullptr;
    if (expression && failed(getStaticStorageView(expression)))
      expressionPorts[port.getSymNameAttr()] = port;
  });
  if (expressionPorts.empty())
    return success();
  auto referencedPort = [&](Operation *operation) {
    auto reference =
        operation->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    return reference ? expressionPorts.lookup(reference.getLeafReference())
                     : semantic::SVModportPortSymbolOp{};
  };
  SmallVector<Operation *> uses;
  module.walk([&](Operation *operation) {
    if (isa<semantic::SVNamedValueExpressionOp,
            semantic::SVHierarchicalValueExpressionOp,
            semantic::SVMemberAccessExpressionOp>(operation) &&
        !operation->getParentOfType<semantic::SVModportPortSymbolOp>() &&
        referencedPort(operation))
      uses.push_back(operation);
  });

  SemanticOpFactory factory(module);
  for (Operation *use : uses) {
    semantic::SVModportPortSymbolOp port = referencedPort(use);
    Operation *interfaceBody = port->getParentOp()->getParentOp();
    OpBuilder builder(use);
    Operation *expanded = builder.clone(*getSingleRegionRoot(port.getBody()));
    factory.renumber(expanded);
    // Through a virtual interface the expression names the items of the
    // interface definition; each is selected from the same receiver.
    auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(use);
    SmallVector<Operation *> receiverChildren =
        member ? getChildren(member) : SmallVector<Operation *>{};
    if (member && receiverChildren.size() != 1)
      return use->emitError("virtual interface modport expression access has "
                            "no single receiver");
    SmallVector<Operation *> items;
    if (member)
      expanded->walk([&](Operation *nested) {
        if (isa<semantic::SVNamedValueExpressionOp,
                semantic::SVHierarchicalValueExpressionOp>(nested))
          items.push_back(nested);
      });
    for (Operation *item : items) {
      auto reference = item->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      Operation *symbol =
          reference ? symbols.lookup(reference.getLeafReference()) : nullptr;
      if (!symbol || !interfaceBody->isProperAncestor(symbol))
        continue;
      auto name = symbol->getAttrOfType<StringAttr>("name");
      if (symbol->getParentOp() != interfaceBody || !name)
        return use->emitError("virtual interface modport expression may only "
                              "name items declared directly in the interface");
      OpBuilder itemBuilder(item);
      SmallVector<NamedAttribute> attributes{
          itemBuilder.getNamedAttr("member_name", name)};
      for (StringRef attribute : {"is_signed", "referenced_path",
                                  "referenced_symbol", "semantic_type"})
        if (Attribute value = item->getAttr(attribute))
          attributes.push_back(itemBuilder.getNamedAttr(attribute, value));
      Operation *access = factory.create(
          itemBuilder, item->getLoc(),
          semantic::SVMemberAccessExpressionOp::getOperationName(), attributes);
      OpBuilder receiverBuilder =
          OpBuilder::atBlockEnd(&access->getRegion(0).front());
      factory.renumber(receiverBuilder.clone(*receiverChildren.front()));
      item->erase();
    }
    use->erase();
  }
  return success();
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
  llvm::StringMap<semantic::SVNetSymbolOp> nets;
  bool hasAnnotatedTimingOrSwitches = false;
  // Index constructor ref positions once per declaration. Repeated creates
  // must not rescan a covergroup's body or all of its formal descriptors.
  llvm::DenseMap<Type, SmallVector<unsigned>> covergroupEventRefs;
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp group) {
    unsigned ordinal = 0;
    for (Operation *child : getChildren(group)) {
      auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
      if (!formal || formal.getIsCoverageSampleFormal().value_or(false))
        continue;
      auto type = formal->getAttrOfType<TypeAttr>("semantic_type");
      if (formal.getDirection() == semantic::SVArgumentDirection::Ref && type &&
          isa<semantic::EventType>(type.getValue()))
        covergroupEventRefs[group.getSemanticType()].push_back(ordinal);
      ++ordinal;
    }
  });
  llvm::DenseSet<Operation *> collectedEventCellReferences;
  auto collectEventCellReferences = [&](Operation *root) {
    root->walk<WalkOrder::PreOrder>([&](Operation *nested) -> WalkResult {
      // Nested lvalues and calls can ask for the same subtree. Visit each
      // node once across the complete inventory to keep this walk linear.
      if (!collectedEventCellReferences.insert(nested).second)
        return WalkResult::skip();
      auto type = nested->getAttrOfType<TypeAttr>("semantic_type");
      if (!type || !isa<semantic::EventType>(type.getValue()))
        return WalkResult::advance();
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
      return WalkResult::advance();
    });
  };

  // IEEE 1800-2023 10.11 net aliases are static topology, not executable
  // connectivity.  Collapse direct whole-net aliases onto one descriptor so
  // drivers, readers, and observers all use the same resolved net without
  // adding any runtime propagation work. Any other alias shares bits, which
  // net topology joins like a collapsed port.
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
          view->rootType != view->viewType) {
        result.partialNetAliases.push_back(alias);
        return;
      }
      paths.push_back(view->path);
    }
    for (StringRef path : ArrayRef<std::string>(paths).drop_front())
      uniteNetAliases(paths.front(), path);
  };

  // Fold alias, port, and event-cell collection into one semantic-tree walk.
  semanticRoot->walk([&](Operation *op) {
    if (auto net = dyn_cast<semantic::SVNetSymbolOp>(op)) {
      if (!isCompileTimeOnlyInstanceMember(net))
        nets[getHierarchyName(net)] = net;
    }
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(op))
      hasAnnotatedTimingOrSwitches |= call.getCalleeName() == "$sdf_annotate";
    if (auto primitive = dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(op))
      if (auto name = primitive->getAttrOfType<StringAttr>("primitive_name"))
        hasAnnotatedTimingOrSwitches |= name.getValue() == "tran" ||
                                        name.getValue() == "rtran" ||
                                        name.getValue().starts_with("tranif") ||
                                        name.getValue().starts_with("rtranif");
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
    if (auto create = dyn_cast<semantic::SVNewCovergroupExpressionOp>(op)) {
      auto type = create->getAttrOfType<TypeAttr>("semantic_type");
      auto found = type ? covergroupEventRefs.find(type.getValue())
                        : covergroupEventRefs.end();
      if (found != covergroupEventRefs.end()) {
        auto actuals = getChildren(create);
        for (unsigned position : found->second)
          if (position < actuals.size())
            collectEventCellReferences(actuals[position]);
      }
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
    // LRM 15.5: aliases share event identity, including future triggers.
    // Propagate the finite live-cell set over an indexed dependency graph.
    llvm::StringMap<SmallVector<StringRef>> liveDependents;
    for (const auto &[path, view] : result.refViews) {
      bool wholeEvent = view.identity && view.offset == 0 &&
                        view.packedOffset == 0 && view.indices.empty() &&
                        isa<sim::EventType>(view.rootType) &&
                        isa<sim::EventType>(view.viewType);
      if (wholeEvent) {
        liveDependents[path].push_back(view.path);
        liveDependents[view.path].push_back(path);
      }
    }
    for (const EventInputCandidate &candidate : eventInputs)
      if (isWholeEvent(candidate))
        liveDependents[candidate.actual->path].push_back(candidate.internal);
    SmallVector<StringRef> pending;
    for (const auto &path : liveEventPaths)
      pending.push_back(path.getKey());
    while (!pending.empty()) {
      StringRef path = pending.pop_back_val();
      auto found = liveDependents.find(path);
      if (found == liveDependents.end())
        continue;
      for (StringRef dependent : found->second) {
        auto [entry, inserted] = liveEventPaths.insert(dependent);
        if (inserted)
          pending.push_back(entry->getKey());
      }
    }
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

  // IEEE 1800-2023 23.3.3.3, 23.3.3.6, 23.3.3.7: whole, identically typed
  // wire/tri/uwire port nets can use one simulated net. Variable ports and
  // conversions retain their continuous assignments under 23.3.3.2.
  if (!hasAnnotatedTimingOrSwitches)
    for (semantic::SVPortConnectionOp connection : result.connections) {
      if (connection.getDirection() != semantic::SVArgumentDirection::In &&
          connection.getDirection() != semantic::SVArgumentDirection::Out)
        continue;
      StringRef internal = connection.getInternalPath().value_or(StringRef{});
      auto formal = nets.find(internal);
      Operation *actual = getPortActualLValue(connection);
      if (formal == nets.end() || !actual)
        continue;
      FailureOr<StaticStorageView> view = getStaticStorageView(actual);
      if (failed(view) || !isWholeRefView(*view))
        continue;
      auto external = nets.find(view->path);
      if (external == nets.end())
        continue;
      auto eligible = [](semantic::SVNetSymbolOp net) {
        auto kind = net.getNetKind();
        return (kind == semantic::SVNetKind::Wire ||
                kind == semantic::SVNetKind::Tri ||
                kind == semantic::SVNetKind::UWire) &&
               !net.getDelayFs() && !net.getUnsupportedDelay();
      };
      semantic::SVNetSymbolOp lhs = formal->second, rhs = external->second;
      if (!eligible(lhs) || !eligible(rhs) ||
          lhs.getNetKind() != rhs.getNetKind() ||
          lhs->getAttr("semantic_type") != rhs->getAttr("semantic_type") ||
          connection.getFormalType() != view->semanticType)
        continue;
      if (Operation *expression =
              getSingleRegionRoot(connection.getInternal())) {
        FailureOr<StaticStorageView> low = getStaticStorageView(expression);
        if (failed(low) || !isWholeRefView(*low) || low->path != internal)
          continue;
      }
      uniteNetAliases(view->path, internal);
    }

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
      // expandModportExpressions replaced each use of this expression.
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

FailureOr<llvm::StringMap<DescriptorInfo>> materializeDesignDescriptors(
    ModuleOp module, sim::SimDesignOp design,
    semantic::SVRootSymbolOp semanticRoot,
    const PreparedPortAliases &portAliases,
    const PreparedScopeDeclarations &scopes,
    const PreparedClassDeclarations &classes, uint64_t designPrecisionFs,
    bool preserveVPIMethodBindings, bool preserveVPIBoxBindings,
    PreparedProceduralTypes &proceduralTypes, OpBuilder &builder) {
  OpBuilder::InsertionGuard insertionGuard(builder);
  builder.setInsertionPointToEnd(&design.getBody().front());
  llvm::StringMap<DescriptorInfo> descriptors;
  uint64_t nextStorageId = 0;
  uint64_t nextNetId = 0;
  uint64_t nextEventId = 0;
  bool invalid = false;
  SmallVector<Operation *> designObjects;
  bool retainCoverageSourceTypes = false;
  if (auto metrics = cov::get<cov::Metrics>(module))
    retainCoverageSourceTypes = llvm::any_of(metrics, [](Attribute metric) {
      auto name = dyn_cast<StringAttr>(metric);
      return name && name.getValue() == "toggle";
    });

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
  auto primitiveTerminalDirection =
      [](semantic::SVPrimitiveInstanceSymbolOp primitive, size_t index,
         Operation *terminal) {
        auto name = primitive->getAttrOfType<StringAttr>("primitive_name");
        StringRef spelling = name ? name.getValue() : StringRef{};
        bool bidirectional = spelling == "tran" || spelling == "rtran" ||
                             spelling == "tranif0" || spelling == "tranif1" ||
                             spelling == "rtranif0" || spelling == "rtranif1";
        if (bidirectional && index < 2)
          return uint32_t{3}; // vpiInout
        return isa<semantic::SVAssignmentExpressionOp>(terminal)
                   ? uint32_t{2}
                   : uint32_t{1}; // vpiOutput / vpiInput
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
    // Forwarded interface-array ports can reference a detached, elementless
    // Slang symbol stub. It describes the connection's type/shape, not a
    // second elaborated array with a runtime VPI identity. Real arrays live
    // in the elaborated hierarchy and must still satisfy the extent checks.
    if (isa<ModuleOp>(array->getParentOp()) &&
        array.getBody().front().empty())
      return;
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
  // IEEE 1800-2023 39.3.1: a named concurrent assertion is a VPI object of
  // its scope. Its name is the label of the statement block that wraps it.
  llvm::StringMap<VPIKind> assertionLabelKinds;
  module.walk([&](semantic::SVBlockStatementOp block) {
    SmallVector<Operation *> children = getChildren(block);
    auto path = block->getAttrOfType<StringAttr>("block_path");
    auto assertion = children.size() == 1
                         ? dyn_cast<semantic::SVConcurrentAssertionStatementOp>(
                               children.front())
                         : semantic::SVConcurrentAssertionStatementOp{};
    if (!path || !assertion)
      return;
    switch (assertion.getAssertionKind()) {
    case semantic::SVAssertionKind::Assert:
      assertionLabelKinds[path.getValue()] = VPIKind::Assert;
      break;
    case semantic::SVAssertionKind::Assume:
      assertionLabelKinds[path.getValue()] = VPIKind::Assume;
      break;
    case semantic::SVAssertionKind::CoverProperty:
    case semantic::SVAssertionKind::CoverSequence:
      assertionLabelKinds[path.getValue()] = VPIKind::Cover;
      break;
    case semantic::SVAssertionKind::Restrict:
      assertionLabelKinds[path.getValue()] = VPIKind::Restrict;
      break;
    case semantic::SVAssertionKind::Expect:
      break;
    }
  });
  auto sourceAnchorKind = [&](Operation *operation) -> std::optional<VPIKind> {
    if (!isa<semantic::SVCompilationUnitSymbolOp, semantic::SVPackageSymbolOp,
             semantic::SVClassTypeOp, semantic::SVGenericClassDefSymbolOp,
             semantic::SVSourceClassDefinitionSymbolOp,
             semantic::SVSubroutineSymbolOp,
             semantic::SVSourceClassMethodSymbolOp,
             semantic::SVSourceClassConstraintSymbolOp,
             semantic::SVConstraintBlockSymbolOp, semantic::SVPropertySymbolOp,
             semantic::SVSequenceSymbolOp, semantic::SVClockingBlockSymbolOp,
             semantic::SVModportSymbolOp, semantic::SVVariableSymbolOp,
             semantic::SVParameterSymbolOp, semantic::SVTypeParameterSymbolOp,
             semantic::SVSourceTypeParameterSymbolOp,
             semantic::SVSourceValueParameterSymbolOp,
             semantic::SVInstanceArraySymbolOp,
             semantic::SVGenerateBlockArraySymbolOp,
             semantic::SVPrimitiveInstanceSymbolOp,
             semantic::SVGenerateBlockSymbolOp,
             semantic::SVInstanceBodySymbolOp,
             semantic::SVStatementBlockSymbolOp>(operation))
      return std::nullopt;
    if (isa<semantic::SVStatementBlockSymbolOp>(operation)) {
      auto found = assertionLabelKinds.find(getHierarchyName(operation));
      return found == assertionLabelKinds.end() ? std::nullopt
                                                : std::optional(found->second);
    }
    // Slang materializes interface bodies solely to describe parameterized
    // virtual-interface types.  They have no run-time instance identity and
    // must not become traversable vpiInterface objects.  Their typespecs are
    // retained below and owned by the nearest persistent lexical anchor.
    if (isCompileTimeOnlyInstanceMember(operation))
      return std::nullopt;
    if (isa<semantic::SVCompilationUnitSymbolOp, semantic::SVPackageSymbolOp>(
            operation))
      return VPIKind::Package;
    if (auto method =
            dyn_cast<semantic::SVSourceClassMethodSymbolOp>(operation))
      return method.getSubroutineKind() == semantic::SVSubroutineKind::Task
                 ? VPIKind::Task
                 : VPIKind::Function;
    if (isa<semantic::SVSourceClassConstraintSymbolOp>(operation))
      return VPIKind::Constraint;
    if (isa<semantic::SVSourceTypeParameterSymbolOp,
            semantic::SVSourceValueParameterSymbolOp>(operation)) {
      Operation *owner = operation->getParentOp();
      if (!isa_and_nonnull<semantic::SVGenericClassDefSymbolOp,
                           semantic::SVSourceClassDefinitionSymbolOp>(owner))
        return std::nullopt;
      if (auto package =
              dyn_cast<semantic::SVPackageSymbolOp>(owner->getParentOp());
          package && getDebugName(package) == "std" &&
          (getDebugName(owner) == "mailbox" ||
           getDebugName(owner) == "semaphore" ||
           getDebugName(owner) == "process"))
        return std::nullopt;
      return isa<semantic::SVSourceTypeParameterSymbolOp>(operation)
                 ? VPIKind::TypeParameter
                 : VPIKind::Parameter;
    }
    if (isa<semantic::SVParameterSymbolOp, semantic::SVTypeParameterSymbolOp>(
            operation)) {
      // Package and class parameters are immutable declarations. Class
      // specializations retain their own resolved type/value; a generic
      // definition must not borrow one specialization's parameter payload.
      Operation *owner = operation->getParentOp();
      while (
          owner &&
          !isa<semantic::SVCompilationUnitSymbolOp, semantic::SVPackageSymbolOp,
               semantic::SVClassTypeOp, semantic::SVInstanceBodySymbolOp>(
              owner))
        owner = owner->getParentOp();
      if (!isa_and_nonnull<semantic::SVCompilationUnitSymbolOp,
                           semantic::SVPackageSymbolOp,
                           semantic::SVClassTypeOp>(owner))
        return std::nullopt;
      if (auto classType = dyn_cast<semantic::SVClassTypeOp>(owner);
          classType && (classType.getIsUninstantiated() ||
                        !classes.symbols.count(classType)))
        return std::nullopt;
      if (isa<semantic::SVTypeParameterSymbolOp>(operation))
        return VPIKind::TypeParameter;
      FailureOr<Type> normalized = getNormalizedSemanticType(operation);
      if (failed(normalized) || !sim::getPackedScalarType(*normalized))
        return std::nullopt;
      return VPIKind::Parameter;
    }
    // Builtin classes have typespecs but no lexical class definitions
    // (37.32 detail 13). Match the standard package and its declarations,
    // using the same source identity as builtin type normalization.
    if (isa<semantic::SVGenericClassDefSymbolOp, semantic::SVClassTypeOp>(
            operation))
      if (auto package =
              dyn_cast<semantic::SVPackageSymbolOp>(operation->getParentOp());
          package && getDebugName(package) == "std") {
        StringRef name = getDebugName(operation);
        if (name == "semaphore" || name == "mailbox" || name == "process")
          return std::nullopt;
      }
    if (auto generic = dyn_cast<semantic::SVGenericClassDefSymbolOp>(operation))
      return generic.getLexicalDefinitionAttr()
                 ? std::nullopt
                 : std::optional(VPIKind::ClassDefn);
    if (isa<semantic::SVSourceClassDefinitionSymbolOp>(operation))
      return VPIKind::ClassDefn;
    if (auto classType = dyn_cast<semantic::SVClassTypeOp>(operation)) {
      // A generic declaration has one lexical class definition. Its
      // elaborated class types are specializations of that definition, not
      // additional source declarations (IEEE 1800-2023 37.32).
      if (isa<semantic::SVGenericClassDefSymbolOp>(classType->getParentOp()) ||
          classType.getLexicalDefinitionAttr() ||
          classType.getIsUninstantiated())
        return std::nullopt;
      return VPIKind::ClassDefn;
    }
    if (isa<semantic::SVConstraintBlockSymbolOp>(operation)) {
      // Only declarations directly owned by this class belong to its
      // constraint inventory. Inherited constraints are reached through
      // extends, and inline randomize constraints have no declaration here.
      auto owner = dyn_cast<semantic::SVClassTypeOp>(operation->getParentOp());
      if (!owner || owner.getIsUninstantiated() || !classes.symbols.count(owner))
        return std::nullopt;
      return VPIKind::Constraint;
    }
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(operation)) {
      if (subroutine.getIsBuiltin().value_or(false))
        return std::nullopt;
      return subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Task
                 ? VPIKind::Task
                 : VPIKind::Function;
    }
    if (isa<semantic::SVPropertySymbolOp>(operation))
      return VPIKind::PropertyDecl;
    if (isa<semantic::SVSequenceSymbolOp>(operation))
      return VPIKind::SequenceDecl;
    if (isa<semantic::SVClockingBlockSymbolOp>(operation))
      return VPIKind::ClockingBlock;
    if (isa<semantic::SVModportSymbolOp>(operation))
      return VPIKind::Modport;
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
  llvm::StringMap<FlatSymbolRefAttr> builtinPrimitiveDefinitions;
  llvm::DenseMap<Operation *, uint64_t> anchorInventoryIds;
  llvm::DenseMap<Operation *, sim::SimVPIObjectAnchorOp> anchorDeclarations;
  llvm::DenseMap<Operation *, SmallVector<sim::SimVPIObjectAnchorOp>>
      parameterViews;
  SmallVector<Operation *> parameterViewSources;
  auto addParameterView = [&](Operation *source,
                              sim::SimVPIObjectAnchorOp view) {
    auto [entry, inserted] = parameterViews.try_emplace(source);
    if (inserted)
      parameterViewSources.push_back(source);
    entry->second.push_back(view);
    for (Operation *child : getChildren(source))
      if (auto unavailable =
              dyn_cast<semantic::SVSourceUnsupportedDefaultOp>(child))
        view.setUnavailableDefaultAttr(unavailable.getSyntaxAttr());
  };
  // Parameter payloads read only the frozen semantic inventory and construct
  // immutable attributes. Keep this work off the serial declaration path:
  // workers own separate result slots and never mutate IR or symbol tables.
  // The source-ordered loop below still assigns every VPI ordinal and symbol.
  struct ParameterPayload {
    sim::VPITypeSemanticsAttr type;
    sim::FrozenConstantAttr value;
  };
  SmallVector<ParameterPayload> parameterPayloads(anchorSources.size());
  SmallVector<size_t> parameterIndices;
  for (auto [index, source] : llvm::enumerate(anchorSources))
    if (isa<semantic::SVParameterSymbolOp,
            semantic::SVTypeParameterSymbolOp>(source))
      parameterIndices.push_back(index);
  if (failed(failableParallelForEach(
          module.getContext(), parameterIndices, [&](size_t index) {
            Operation *source = anchorSources[index];
            auto semanticType = source->getAttrOfType<TypeAttr>("semantic_type");
            if (!semanticType) {
              emitError(getSemanticLocation(source))
                  << "VPI parameter is missing semantic type metadata";
              return failure();
            }
            FailureOr<sim::VPITypeSemanticsAttr> converted =
                makeVPITypeSemantics(semanticType.getValue(),
                                     getSemanticLocation(source), {}, source);
            if (failed(converted))
              return failure();
            sim::FrozenConstantAttr value;
            if (isa<semantic::SVParameterSymbolOp>(source)) {
              FailureOr<sim::FrozenConstantAttr> frozen =
                  freezeSemanticConstant(source);
              if (failed(frozen))
                return failure();
              value = *frozen;
            }
            parameterPayloads[index] = {*converted, value};
            return success();
          })))
    return failure();

  for (auto [inventoryId, source] : llvm::enumerate(anchorSources)) {
    std::string symbolName =
        "__obelisk_vpi_anchor_" + std::to_string(inventoryId);
    anchorSymbols[source] =
        FlatSymbolRefAttr::get(builder.getContext(), symbolName);
    anchorInventoryIds[source] = inventoryId;
  }
  llvm::DenseMap<Operation *, FlatSymbolRefAttr> classTypespecSymbols;
  llvm::DenseMap<Operation *, FlatSymbolRefAttr> classDefinitionSymbols;
  SymbolTableCollection lexicalClassSymbols;
  auto hasSeparateLexicalClass = [](semantic::SVClassTypeOp classType) {
    return classType.getLexicalDefinitionAttr() ||
           isa<semantic::SVGenericClassDefSymbolOp>(classType->getParentOp());
  };
  for (semantic::SVClassTypeOp classType : classes.sources) {
    StringAttr runtimeSymbol = classes.symbols.lookup(classType);
    classTypespecSymbols[classType] = FlatSymbolRefAttr::get(
        builder.getContext(),
        (runtimeSymbol.getValue() + "_vpi_typespec").str());
    Operation *definition = classType;
    if (auto reference = classType.getLexicalDefinitionAttr())
      definition = lexicalClassSymbols.lookupSymbolIn(module, reference);
    else if (isa<semantic::SVGenericClassDefSymbolOp>(classType->getParentOp()))
      definition = classType->getParentOp();
    classDefinitionSymbols[classType] = anchorSymbols.lookup(definition);
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
    if (BoolAttr frozen = sim::get<sim::metadata::VpiTop>(source)) {
      top = frozen.getValue();
    } else if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(source)) {
      Operation *instance = body->getParentOp();
      top = isa_and_nonnull<semantic::SVInstanceSymbolOp>(instance) &&
            isa_and_nonnull<semantic::SVRootSymbolOp>(instance->getParentOp());
    }
    if (isa<semantic::SVPackageSymbolOp, semantic::SVCompilationUnitSymbolOp>(
            source))
      top = true;
    BoolAttr cell = sim::get<sim::metadata::VpiCellInstance>(source);
    BoolAttr automatic = sim::get<sim::metadata::VpiAutomatic>(source);
    bool isAutomatic = automatic && automatic.getValue();
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(source))
      isAutomatic = subroutine.getDefaultLifetime() ==
                    semantic::SVVariableLifetime::Automatic;
    if (auto method = dyn_cast<semantic::SVSourceClassMethodSymbolOp>(source))
      isAutomatic = method.getDefaultLifetime() ==
                    semantic::SVVariableLifetime::Automatic;
    // Named event anchors preserve the declaration lifetime even when no
    // activation frame currently exists.
    if (auto variable = dyn_cast<semantic::SVVariableSymbolOp>(source))
      isAutomatic = variable.getLifetime() ==
                    semantic::SVVariableLifetime::Automatic;
    auto addMethodProperties = [&](semantic::SVVisibility visibility,
                                   bool isMethod) {
      int32_t value = 1; // vpiPublicVis
      switch (visibility) {
      case semantic::SVVisibility::Public:
        break;
      case semantic::SVVisibility::Protected:
        value = 2;
        break;
      case semantic::SVVisibility::Local:
        value = 3;
        break;
      }
      properties.push_back(sim::VPIPropertyAttr::get(
          builder.getContext(), builder.getI32IntegerAttr(620),
          builder.getI32IntegerAttr(value)));
      addBoolean(645, isMethod);
    };
    if (auto definition =
            dyn_cast<semantic::SVSourceClassDefinitionSymbolOp>(source))
      properties.push_back(sim::VPIPropertyAttr::get(
          builder.getContext(), builder.getI32IntegerAttr(635),
          builder.getBoolAttr(definition.getIsAbstract())));
    if (auto method = dyn_cast<semantic::SVSourceClassMethodSymbolOp>(source)) {
      addMethodProperties(method.getMemberVisibility(), true);
      BoolAttr virtuality = method.getSourceVirtualityAttr();
      if (!virtuality &&
          (method.getIsDeclaredVirtualAttr() || method.getIsPureAttr()))
        virtuality = builder.getBoolAttr(true);
      if (!virtuality &&
          (method.getIsStaticAttr() || method.getIsConstructorAttr()))
        virtuality = builder.getBoolAttr(false);
      if (virtuality)
        properties.push_back(sim::VPIPropertyAttr::get(
            builder.getContext(), builder.getI32IntegerAttr(635), virtuality));
    }
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(source)) {
      addMethodProperties(subroutine.getMemberVisibility(),
                          isa<semantic::SVClassTypeOp>(source->getParentOp()));
      properties.push_back(sim::VPIPropertyAttr::get(
          builder.getContext(), builder.getI32IntegerAttr(635),
          builder.getBoolAttr(
              static_cast<bool>(subroutine.getIsVirtualAttr()))));
      // IEEE 37.41 detail 6 defines these exact DPI access modes. Ordinary
      // extern class prototypes are not interface tf declarations.
      int32_t access = subroutine.getDpiExportCIdentifierAttr() ? 3
                       : subroutine.getIsDpiImportAttr()        ? 4
                                                                : 0;
      if (access)
        properties.push_back(sim::VPIPropertyAttr::get(
            builder.getContext(), builder.getI32IntegerAttr(604),
            builder.getI32IntegerAttr(access)));
    }
    if (auto parameter =
            dyn_cast<semantic::SVSourceValueParameterSymbolOp>(source))
      addBoolean(70, static_cast<bool>(parameter.getIsLocalParamAttr()));
    if (auto constraint = dyn_cast<semantic::SVConstraintBlockSymbolOp>(source))
      isAutomatic = !constraint.getIsStatic().value_or(false);
    if (auto constraint =
            dyn_cast<semantic::SVSourceClassConstraintSymbolOp>(source))
      isAutomatic = !constraint.getIsStaticAttr();
    if (sourceKind == VPIKind::Module) {
      addBoolean(7, top);                     // vpiTopModule
      addBoolean(8, cell && cell.getValue()); // vpiCellInstance
    }
    StringAttr definitionName =
        sim::get<sim::metadata::VpiDefinitionName>(source);
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
    addString(9, definitionName); // vpiDefName
    addBoolean(50, isAutomatic);  // vpiAutomatic
    if (auto parameter = dyn_cast<semantic::SVParameterSymbolOp>(source))
      addBoolean(70, static_cast<bool>(parameter.getIsLocalParamAttr()));
    if (auto parameter = dyn_cast<semantic::SVTypeParameterSymbolOp>(source))
      addBoolean(70, static_cast<bool>(parameter.getIsLocalParamAttr()));
    if (auto parameter =
            dyn_cast<semantic::SVSourceTypeParameterSymbolOp>(source))
      addBoolean(70, static_cast<bool>(parameter.getIsLocalParamAttr()));
    if (auto constraint = dyn_cast<semantic::SVConstraintBlockSymbolOp>(source))
      properties.push_back(sim::VPIPropertyAttr::get(
          builder.getContext(), builder.getI32IntegerAttr(604), // vpiAccessType
          builder.getI32IntegerAttr(constraint.getOutOfBlockIndex() != 0
                                       ? 2  // vpiExternAcc
                                       : 0)));
    if (auto constraint =
            dyn_cast<semantic::SVSourceClassConstraintSymbolOp>(source))
      properties.push_back(sim::VPIPropertyAttr::get(
          builder.getContext(), builder.getI32IntegerAttr(604),
          builder.getI32IntegerAttr(
              constraint.getHasExternalDefinition() ? 2 : 0)));
    if (sourceKind == VPIKind::Constraint)
      addString(2, builder.getStringAttr(getDebugName(source)));
    addBoolean(600, top); // vpiTop
    addBoolean(602, isa<semantic::SVCompilationUnitSymbolOp>(source));

    if (properties.empty())
      return sim::VPIPropertySetAttr{};
    llvm::sort(properties, [](Attribute lhs, Attribute rhs) {
      return cast<sim::VPIPropertyAttr>(lhs).getSelector().getInt() <
             cast<sim::VPIPropertyAttr>(rhs).getSelector().getInt();
    });
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
    while (parent && !anchorSymbols.count(parent) &&
           !classTypespecSymbols.count(parent))
      parent = parent->getParentOp();
    FlatSymbolRefAttr parentSymbol = anchorSymbols.lookup(parent);
    if (!parentSymbol)
      parentSymbol = classTypespecSymbols.lookup(parent);
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
    sim::VPITypeSemanticsAttr anchorVPIType;
    sim::FrozenConstantAttr immutableValue;
    UnitAttr hasExplicitParameterRange;
    if (isa<semantic::SVPrimitiveInstanceSymbolOp>(source)) {
      SmallVector<Operation *> terminals = getChildren(source);
      auto primitive = cast<semantic::SVPrimitiveInstanceSymbolOp>(source);
      uint64_t count = 0;
      for (auto [index, terminal] : llvm::enumerate(terminals))
        count += primitiveTerminalDirection(primitive, index, terminal) == 1;
      primitiveInputCount = builder.getI64IntegerAttr(count);
    }
    if (isa<semantic::SVParameterSymbolOp,
            semantic::SVTypeParameterSymbolOp>(source)) {
      anchorVPIType = parameterPayloads[inventoryId].type;
      immutableValue = parameterPayloads[inventoryId].value;
      if (auto parameter = dyn_cast<semantic::SVParameterSymbolOp>(source))
        if (parameter.getHasExplicitRangeAttr())
          hasExplicitParameterRange = builder.getUnitAttr();
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
        primitiveInputCount, anchorVPIType, immutableValue,
        hasExplicitParameterRange,
        preserveVPIMethodBindings ? classes.methodSymbols.lookup(source)
                                  : FlatSymbolRefAttr{},
        UnitAttr{}, StringAttr{}, UnitAttr{});
    if (auto primitive =
            dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(source);
        primitive && !primitive->hasAttr("udp_metadata")) {
      auto name = primitive->getAttrOfType<StringAttr>("primitive_name");
      const auto *subtype = name ? reflection::findVPIBuiltinPrimitive(
                                       std::string_view(name.getValue()))
                                 : nullptr;
      if (!subtype || subtype->objectKind != anchor.getVpiKind()) {
        primitive.emitOpError("has no exact built-in VPI primitive definition");
        invalid = true;
        continue;
      }
      auto &definition = builtinPrimitiveDefinitions[name.getValue()];
      if (!definition) {
        std::string symbol = (Twine("__obelisk_vpi_primitive_definition_") +
                              Twine(subtype->value))
                                 .str();
        auto declared = sim::SimVPIDefinitionDeclOp::create(
            builder, getSemanticLocation(source), symbol, subtype->objectKind,
            name.getValue(), LocationAttr{},
            builder.getI32IntegerAttr(subtype->value));
        definition = FlatSymbolRefAttr::get(declared);
      }
      sim::SimVPIDefinitionBindingOp::create(
          builder, getSemanticLocation(source), FlatSymbolRefAttr::get(anchor),
          definition);
    }
    if (isa<semantic::SVSourceTypeParameterSymbolOp>(source))
      anchor.setUnresolvedTypeParameterAttr(builder.getUnitAttr());
    if (isa<semantic::SVSourceValueParameterSymbolOp>(source))
      anchor.setUnresolvedValueParameterAttr(builder.getUnitAttr());
    if (isa<semantic::SVSourceTypeParameterSymbolOp,
            semantic::SVSourceValueParameterSymbolOp,
            semantic::SVTypeParameterSymbolOp, semantic::SVParameterSymbolOp>(
            source))
      addParameterView(source, anchor);
    anchorDeclarations[source] = anchor;
    if (anchorVPIType)
      if (auto identity = source->getAttrOfType<IntegerAttr>(
              vpiSourceTypeIdentityAttrName))
        sim::set<sim::metadata::VpiSourceTypeIdentity>(anchor, identity);
    if (isa<semantic::SVPackageSymbolOp, semantic::SVCompilationUnitSymbolOp>(
            source))
      if (auto definitionLoc =
              getSemanticLocation(source)->findInstanceOf<FileLineColLoc>())
        anchor->setAttr("definition_loc", definitionLoc);
    if (sim::VPIPropertySetAttr properties =
            identityProperties(source, sourceKind))
      anchor->setAttr("vpi_properties", properties);
    sim::set<sim::metadata::VpiAnchor>(source, anchorSymbols.lookup(source));
  }

  // IO declarations are source-definition members, not the per-instance
  // vpiPort connection objects emitted below. Preserve their invariant
  // identity once, then share effective type sets across all instances with
  // the same elaborated specialization.
  struct DefinitionMemberSignature {
    std::string name;
    Location location;
    sim::VPIIODirection direction = sim::VPIIODirection::Undefined;
  };
  struct DefinitionMemberPlan {
    bool initialized = false;
    SmallVector<DefinitionMemberSignature> signatures;
    SmallVector<FlatSymbolRefAttr> memberSymbols;
    llvm::DenseMap<Attribute, FlatSymbolRefAttr> specializations;
  };
  struct DefinitionInstancePlan {
    semantic::SVInstanceBodySymbolOp body;
    sim::SimScopeDeclOp scope;
    FlatSymbolRefAttr definition;
  };
  llvm::StringMap<DefinitionMemberPlan> definitionMemberPlans;
  SmallVector<DefinitionInstancePlan> definitionInstancePlans;
  uint64_t nextDefinitionMemberId = 0;
  uint64_t nextDefinitionSpecializationId = 0;
  semanticRoot.walk<WalkOrder::PreOrder>(
      [&](semantic::SVInstanceBodySymbolOp body) {
        if (isCompileTimeOnlyInstanceMember(body))
          return;
        uint64_t scopeId = scopes.lookup(body);
        if (scopeId >= scopes.declarations.size())
          return;
        sim::SimScopeDeclOp scope = scopes.declarations[scopeId];
        FlatSymbolRefAttr definition = scope.getVpiDefinitionAttr();
        if (!definition || sim::vpiKindForScope(scope) !=
                               static_cast<uint32_t>(VPIKind::Module))
          return;

        SmallVector<DefinitionMemberSignature> signatures;
        for (Operation &child : body.getBody().front()) {
          sim::VPIIODirection direction = sim::VPIIODirection::Undefined;
          if (auto port = dyn_cast<semantic::SVPortSymbolOp>(child)) {
            switch (port.getDirection()) {
            case semantic::SVArgumentDirection::In:
              direction = sim::VPIIODirection::Input;
              break;
            case semantic::SVArgumentDirection::Out:
              direction = sim::VPIIODirection::Output;
              break;
            case semantic::SVArgumentDirection::InOut:
              direction = sim::VPIIODirection::InOut;
              break;
            case semantic::SVArgumentDirection::Ref:
              direction = sim::VPIIODirection::Ref;
              break;
            }
            // IEEE 1800-2023 37.13 defines virtual-interface IO declarations
            // as directionless, irrespective of the source argument mode.
            if (auto type = child.getAttrOfType<TypeAttr>("semantic_type");
                type && isa<semantic::VirtualInterfaceType>(type.getValue()))
              direction = sim::VPIIODirection::Undefined;
          } else if (!isa<semantic::SVInterfacePortSymbolOp>(child)) {
            // A multi-port is a vpiPort association. Its leaf declarations
            // appear as ordinary SVPortSymbolOps and are the IODecl objects.
            continue;
          }
          StringRef name = getDebugName(&child);
          // Empty ordered-port slots are connectivity placeholders, not IO
          // declarations in the source definition's VPI object graph.
          if (name.empty())
            continue;
          signatures.push_back(
              {name.str(), getSemanticLocation(&child), direction});
        }

        DefinitionMemberPlan &plan =
            definitionMemberPlans[definition.getValue()];
        if (!plan.initialized) {
          plan.initialized = true;
          plan.signatures = signatures;
          for (auto [ordinal, signature] : llvm::enumerate(signatures)) {
            std::string symbol = (Twine("__obelisk_vpi_definition_member_") +
                                  Twine(nextDefinitionMemberId++))
                                     .str();
            auto declaration = sim::SimVPIDefinitionMemberDeclOp::create(
                builder, signature.location, builder.getStringAttr(symbol),
                definition,
                builder.getI32IntegerAttr(
                    static_cast<uint32_t>(VPIKind::IODecl)),
                builder.getI32IntegerAttr(static_cast<uint32_t>(ordinal)),
                builder.getStringAttr(signature.name),
                sim::VPIIODirectionAttr::get(builder.getContext(),
                                             signature.direction));
            plan.memberSymbols.push_back(FlatSymbolRefAttr::get(declaration));
          }
        } else if (plan.signatures.size() != signatures.size()) {
          emitError(getSemanticLocation(body))
              << "instances of one definition disagree on IO declaration "
                 "count";
          invalid = true;
          return;
        } else {
          for (auto [expected, actual] :
               llvm::zip_equal(plan.signatures, signatures)) {
            if (expected.name != actual.name ||
                expected.direction != actual.direction) {
              emitError(getSemanticLocation(body))
                  << "instances of one definition disagree on IO declaration "
                     "identity";
              invalid = true;
              return;
            }
          }
        }
        if (!signatures.empty())
          definitionInstancePlans.push_back({body, scope, definition});
      });
  if (invalid)
    return failure();

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
      sim::SimVPIObjectAnchorOp member = sim::SimVPIObjectAnchorOp::create(
          builder, getSemanticLocation(source), symbolName,
          nextSyntheticInventoryId++,
          static_cast<uint32_t>(VPIKind::NamedEvent), scopes.lookup(source),
          anchorSymbols.lookup(source), ordinal,
          builder.getStringAttr(hierarchy),
          builder.getStringAttr(getDebugName(source)), UnitAttr{},
          sim::VPIObjectBackingAttr{}, DenseI64ArrayAttr{}, DenseI64ArrayAttr{},
          DenseI64ArrayAttr{}, builder.getDenseI64ArrayAttr(indices),
          IntegerAttr{}, sim::VPITypeSemanticsAttr{}, sim::FrozenConstantAttr{},
          UnitAttr{}, FlatSymbolRefAttr{}, UnitAttr{}, StringAttr{},
          UnitAttr{});
      if (sim::VPIPropertySetAttr properties =
              identityProperties(source, VPIKind::NamedEvent))
        member->setAttr("vpi_properties", properties);
    }
  }
  auto ownerAnchorFor = [&](Operation *member) -> FlatSymbolRefAttr {
    for (Operation *cursor = member; cursor; cursor = cursor->getParentOp()) {
      if (FlatSymbolRefAttr anchor = anchorSymbols.lookup(cursor))
        return anchor;
      if (FlatSymbolRefAttr typespec = classTypespecSymbols.lookup(cursor))
        return typespec;
      if (isa<semantic::SVStatementBlockSymbolOp>(cursor))
        return {};
    }
    return {};
  };
  auto proceduralOwnerFor = [&](Operation *member) -> Operation * {
    if (ownerAnchorFor(member))
      return nullptr;
    for (Operation *cursor = member; cursor; cursor = cursor->getParentOp())
      if (isa<semantic::SVStatementBlockSymbolOp>(cursor))
        return cursor;
    return nullptr;
  };
  llvm::DenseSet<Attribute> pendingTypespecSymbols;
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
        sim::has<sim::metadata::SequenceEndpointEvent>(op) ||
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

  // Parameter payloads are prepared before typedef symbols are inventoried.
  // Complete their declared type now so vpiTypespec preserves alias identity.
  for (size_t index : parameterIndices) {
    Operation *source = anchorSources[index];
    if (!source->hasAttr(vpiTypedefLayersAttrName))
      continue;
    auto semanticType = source->getAttrOfType<TypeAttr>("semantic_type");
    auto converted = makeVPITypeSemantics(
        semanticType.getValue(), getSemanticLocation(source),
        typedefLayersFor(source), source);
    if (failed(converted)) {
      invalid = true;
      continue;
    }
    anchorDeclarations.lookup(source).setVpiTypeAttr(*converted);
  }

  llvm::DenseMap<Attribute, semantic::SVPortConnectionOp>
      connectionsByFormalSymbol;
  for (semantic::SVPortConnectionOp connection : portAliases.connections)
    connectionsByFormalSymbol.try_emplace(connection.getFormalSymbolAttr(),
                                          connection);

  // Type identities can only be frozen after source typedef references have
  // been remapped to persistent simulation symbols. Keep template discovery
  // above independent of that ordering, then hash-cons the complete effective
  // type vector here into one specialization shared by equivalent instances.
  for (DefinitionInstancePlan &instance : definitionInstancePlans) {
    SmallVector<sim::VPITypeSemanticsAttr> effectiveTypes;
    SmallVector<Operation *> memberSources;
    effectiveTypes.reserve(definitionMemberPlans[instance.definition.getValue()]
                               .signatures.size());
    memberSources.reserve(definitionMemberPlans[instance.definition.getValue()]
                              .signatures.size());
    for (Operation &child : instance.body.getBody().front()) {
      if (!isa<semantic::SVPortSymbolOp, semantic::SVInterfacePortSymbolOp>(
              child) ||
          getDebugName(&child).empty())
        continue;
      Operation *member = &child;
      sim::VPITypeSemanticsAttr effectiveType;
      if (auto semanticType =
              member->getAttrOfType<TypeAttr>("semantic_type")) {
        FailureOr<sim::VPITypeSemanticsAttr> converted = makeVPITypeSemantics(
            semanticType.getValue(), getSemanticLocation(member),
            typedefLayersFor(member), member);
        if (failed(converted)) {
          invalid = true;
          break;
        }
        effectiveType = *converted;
      } else if (isa<semantic::SVInterfacePortSymbolOp>(member)) {
        auto connection =
            connectionsByFormalSymbol.find(getSemanticSymbolReference(member));
        if (connection == connectionsByFormalSymbol.end())
          connection = connectionsByFormalSymbol.find(
              getSemanticSymbolReference(member, true));
        if (connection != connectionsByFormalSymbol.end()) {
          FailureOr<sim::VPITypeSemanticsAttr> converted = makeVPITypeSemantics(
              connection->second.getFormalType(), getSemanticLocation(member),
              typedefLayersFor(connection->second), connection->second);
          if (failed(converted)) {
            invalid = true;
            break;
          }
          effectiveType = *converted;
        }
      }
      if (!effectiveType) {
        emitError(getSemanticLocation(member))
            << "VPI IO declaration is missing semantic type metadata";
        invalid = true;
        break;
      }
      effectiveTypes.push_back(effectiveType);
      memberSources.push_back(member);
    }
    if (invalid)
      continue;

    DefinitionMemberPlan &plan =
        definitionMemberPlans[instance.definition.getValue()];
    SmallVector<Attribute> keyElements;
    keyElements.reserve(effectiveTypes.size());
    for (sim::VPITypeSemanticsAttr type : effectiveTypes)
      keyElements.push_back(type);
    ArrayAttr key = builder.getArrayAttr(keyElements);
    FlatSymbolRefAttr specialization = plan.specializations.lookup(key);
    if (!specialization) {
      std::string symbol = (Twine("__obelisk_vpi_definition_specialization_") +
                            Twine(nextDefinitionSpecializationId++))
                               .str();
      auto declaration = sim::SimVPIDefinitionSpecializationDeclOp::create(
          builder, getSemanticLocation(instance.body),
          builder.getStringAttr(symbol), instance.definition);
      specialization = FlatSymbolRefAttr::get(declaration);
      plan.specializations.try_emplace(key, specialization);
      for (auto [member, type, source] :
           llvm::zip_equal(plan.memberSymbols, effectiveTypes, memberSources)) {
        auto binding = sim::SimVPIDefinitionMemberSpecializationOp::create(
            builder, getSemanticLocation(instance.body), specialization, member,
            type);
        if (auto identity = source->getAttrOfType<IntegerAttr>(
                vpiSourceTypeIdentityAttrName))
          sim::set<sim::metadata::VpiSourceTypeIdentity>(binding, identity);
      }
    }
    instance.scope.setVpiSpecializationAttr(specialization);
  }
  if (invalid)
    return failure();

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
      if (Operation *block = proceduralOwnerFor(operation))
        owner = getSemanticSymbolReference(block, true);
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
    Operation *block = owner ? nullptr : proceduralOwnerFor(alias);
    if (!owner && !block)
      continue;
    auto sourceTypeIdentity =
        alias->getAttrOfType<IntegerAttr>(vpiSourceTypeIdentityAttrName);
    FlatSymbolRefAttr symbol = aliasSymbols.lookup(alias);
    if (block) {
      proceduralTypes.typespecs.push_back(
          {alias, block, symbol, nextTypespecId++, scopes.lookup(alias),
           hierarchy.str(), debug.str(), *target,
           sim::VPITypespecOrigin::Typedef, sourceTypeIdentity});
      pendingTypespecSymbols.insert(symbol);
    } else {
      sim::SimVPITypespecDeclOp::create(
          builder, getSemanticLocation(alias), symbol.getValue(),
          nextTypespecId++, scopes.lookup(alias), owner, hierarchy, debug,
          *target, sim::VPITypespecOrigin::Typedef, sourceTypeIdentity,
          TypeAttr{}, IntegerAttr{});
    }
    if (target->getKind() == sim::VPITypeKind::Enum)
      enumTypespecsByIdentity.try_emplace(getEnumIdentity(alias, *target),
                                          symbol);
  }

  auto classPropertyFacts = [&](Operation *source, uint32_t kind) {
    SmallVector<Attribute> properties;
    auto add = [&](uint32_t selector, Attribute value) {
      if (reflection::findVPIProperty(kind, selector))
        properties.push_back(sim::VPIPropertyAttr::get(
            builder.getContext(), builder.getI32IntegerAttr(selector), value));
    };
    auto lifetime =
        source->getAttrOfType<semantic::SVVariableLifetimeAttr>("lifetime");
    auto visibility =
        source->getAttrOfType<semantic::SVVisibilityAttr>("member_visibility");
    auto random = source->getAttrOfType<semantic::SVRandModeAttr>("rand_mode");
    add(2, builder.getStringAttr(getDebugName(source)));
    if (lifetime)
      add(50, builder.getBoolAttr(lifetime.getValue() ==
                                  semantic::SVVariableLifetime::Automatic));
    if (random) {
      int32_t value = 1;
      switch (random.getValue()) {
      case semantic::SVRandMode::None:
        break;
      case semantic::SVRandMode::Rand:
        value = 2;
        break;
      case semantic::SVRandMode::RandC:
        value = 3;
        break;
      }
      add(610, builder.getI32IntegerAttr(value));
    }
    auto constant = source->getAttr("is_const");
    add(612, builder.getBoolAttr(isa_and_nonnull<UnitAttr>(constant) ||
                                 (isa_and_nonnull<BoolAttr>(constant) &&
                                  cast<BoolAttr>(constant).getValue())));
    if (visibility) {
      int32_t value = 1;
      switch (visibility.getValue()) {
      case semantic::SVVisibility::Public:
        break;
      case semantic::SVVisibility::Protected:
        value = 2;
        break;
      case semantic::SVVisibility::Local:
        value = 3;
        break;
      }
      add(620, builder.getI32IntegerAttr(value));
    }
    return sim::VPIPropertySetAttr::get(builder.getContext(),
                                        builder.getArrayAttr(properties));
  };

  // Original generic declarations have their own type graph. Only exact
  // concrete source types are materialized here; an unresolved T property has
  // no IEEE variable kind until specialization and is never guessed from an
  // arbitrary instance of the generic class.
  SymbolTableCollection sourceTypeSymbols;
  DenseMap<Operation *, sim::VPITypeSemanticsAttr> lexicalDeclaredTypes;
  DenseMap<Operation *, FlatSymbolRefAttr> lexicalUnresolvedAliases;
  module.walk([&](Operation *definition) {
    if (!isa<semantic::SVGenericClassDefSymbolOp,
             semantic::SVSourceClassDefinitionSymbolOp>(definition))
      return;
    FlatSymbolRefAttr owner = anchorSymbols.lookup(definition);
    if (!owner)
      return;
    for (Operation *source : getChildren(definition)) {
      if (!isa<semantic::SVSourceClassPropertySymbolOp,
               semantic::SVSourceClassTypeAliasSymbolOp>(source))
        continue;
      Operation *declared = getChildren(source).front();
      sim::VPITypeSemanticsAttr target;
      Operation *referencedDeclaration = nullptr;
      if (auto concrete =
              dyn_cast<semantic::SVSourceDeclaredTypeOp>(declared)) {
        auto converted = makeVPITypeSemantics(concrete.getSemanticType(),
                                              getSemanticLocation(concrete),
                                              ArrayAttr{}, concrete);
        if (failed(converted)) {
          invalid = true;
          continue;
        }
        target = *converted;
      } else if (auto reference =
                     dyn_cast<semantic::SVSourceDeclaredTypeReferenceOp>(
                         declared)) {
        Operation *declaration = sourceTypeSymbols.lookupNearestSymbolFrom(
            reference, reference.getDeclarationAttr());
        referencedDeclaration = declaration;
        target = lexicalDeclaredTypes.lookup(declaration);
      }
      if (!target) {
        if (!isa<semantic::SVSourceClassTypeAliasSymbolOp>(source))
          continue;
        FlatSymbolRefAttr unresolved =
            lexicalUnresolvedAliases.lookup(referencedDeclaration);
        if (!unresolved &&
            isa_and_nonnull<semantic::SVSourceTypeParameterSymbolOp>(
                referencedDeclaration))
          unresolved = anchorSymbols.lookup(referencedDeclaration);
        if (!unresolved)
          continue;
        std::string name =
            "__obelisk_vpi_source_typedef_" + std::to_string(nextTypespecId);
        auto alias = sim::SimVPIUnresolvedTypeAliasDeclOp::create(
            builder, getSemanticLocation(source), name, nextTypespecId++,
            scopes.lookup(source), owner.getValue(), unresolved.getValue(),
            getHierarchyName(source), getDebugName(source));
        lexicalUnresolvedAliases[source] =
            FlatSymbolRefAttr::get(alias.getSymNameAttr());
        continue;
      }
      if (isa<semantic::SVSourceClassTypeAliasSymbolOp>(source)) {
        std::string name =
            "__obelisk_vpi_source_typedef_" + std::to_string(nextTypespecId);
        FlatSymbolRefAttr symbol =
            FlatSymbolRefAttr::get(builder.getContext(), name);
        SmallVector<Attribute> aliases{symbol};
        if (ArrayAttr previous = target.getTypedefAliases())
          llvm::append_range(aliases, previous);
        target = sim::VPITypeSemanticsAttr::get(
            builder.getContext(), target.getKind(), target.getIsSigned(),
            target.getIsFourState(), target.getName(), target.getSymbol(),
            target.getModport(), target.getRange(), target.getChildren(),
            target.getChildNames(), target.getIsTagged(), target.getIsSoft(),
            target.getBitWidth(), target.getSelectableWidth(),
            target.getBitstreamWidth(), target.getTagBits(),
            target.getQueueBound(), target.getWildcardIndex(),
            target.getChildOrdinals(), target.getChildPackedOffsets(),
            target.getChildRandTypes(), builder.getArrayAttr(aliases));
        auto declaration = sim::SimVPITypespecDeclOp::create(
            builder, getSemanticLocation(source), name, nextTypespecId++,
            scopes.lookup(source), owner, getHierarchyName(source),
            getDebugName(source), target, sim::VPITypespecOrigin::Typedef,
            IntegerAttr{}, TypeAttr{}, IntegerAttr{});
        (void)declaration;
        lexicalDeclaredTypes[source] = target;
      } else {
        std::string name = "__obelisk_vpi_class_member_" +
                           std::to_string(nextSyntheticInventoryId);
        auto property = sim::SimVPIClassMemberDeclOp::create(
            builder, getSemanticLocation(source), builder.getStringAttr(name),
            builder.getI64IntegerAttr(nextSyntheticInventoryId++),
            builder.getI64IntegerAttr(scopes.lookup(source)), owner,
            FlatSymbolRefAttr{},
            builder.getStringAttr(getHierarchyName(source)),
            builder.getStringAttr(getDebugName(source)), target, IntegerAttr{},
            IntegerAttr{});
        property->setAttr(
            "vpi_properties",
            classPropertyFacts(source, sim::vpiKindForClassMember(target)));
      }
    }
  });

  // Class declarations and their fully resolved types have independent VPI
  // identities. The latter are shared by every variable of that class type,
  // including variables whose current value is null.
  auto isBuiltinVPIClass = [](sim::VPITypeKind kind) {
    return kind == sim::VPITypeKind::Process ||
           kind == sim::VPITypeKind::Semaphore ||
           kind == sim::VPITypeKind::Mailbox;
  };
  // Keep source identities separate from declared typedef spellings. The
  // original standard package owns builtin typespecs, including specializations
  // observed only through a variable, field, parameter, or aggregate child.
  llvm::DenseMap<Attribute, Operation *> builtinClassSources;
  bool hasStandardPackage = false;
  module.walk([&](semantic::SVPackageSymbolOp package) {
    hasStandardPackage |= getDebugName(package) == "std";
  });
  module.walk([&](semantic::SVClassTypeOp source) {
    if (source.getIsUninstantiated())
      return;
    Operation *parent = source->getParentOp();
    if (isa<semantic::SVGenericClassDefSymbolOp>(parent))
      parent = parent->getParentOp();
    auto package = dyn_cast<semantic::SVPackageSymbolOp>(parent);
    StringRef name = getDebugName(source);
    if (!package || getDebugName(package) != "std" ||
        (name != "process" && name != "semaphore" && name != "mailbox"))
      return;
    auto semantic = makeVPITypeSemantics(
        source.getSemanticType(), getSemanticLocation(source), {}, source);
    if (failed(semantic) || !isBuiltinVPIClass(semantic->getKind())) {
      emitError(getSemanticLocation(source))
          << "standard builtin class has no exact VPI semantic type";
      invalid = true;
      return;
    }
    builtinClassSources.try_emplace(sim::stripVPITypedefAliases(*semantic),
                                    source);
  });
  llvm::DenseMap<Attribute, FlatSymbolRefAttr> builtinClassTypespecs;
  auto internBuiltinTypespec = [&](sim::VPITypeSemanticsAttr semantic,
                                   Operation *use,
                                   bool requireSource) -> FlatSymbolRefAttr {
    auto canonical = sim::stripVPITypedefAliases(semantic);
    if (auto found = builtinClassTypespecs.find(canonical);
        found != builtinClassTypespecs.end())
      return found->second;
    Operation *source = builtinClassSources.lookup(canonical);
    FlatSymbolRefAttr owner =
        source ? ownerAnchorFor(source->getParentOp()) : FlatSymbolRefAttr{};
    if (!source || !owner) {
      // Legacy authored normalized IR may contain builtin types without the
      // source std package. Retain its existing semantic-only representation;
      // normal frontend inventory must provide the exact standard declaration.
      if (requireSource || hasStandardPackage) {
        emitError(getSemanticLocation(use))
            << "VPI builtin class has no preserved source type or package";
        invalid = true;
      }
      return {};
    }
    std::string symbolName =
        "__obelisk_vpi_builtin_class_" + std::to_string(nextTypespecId);
    auto declaration = sim::SimVPITypespecDeclOp::create(
        builder, getSemanticLocation(source), symbolName, nextTypespecId++,
        scopes.lookup(source), owner, getHierarchyName(source),
        getDebugName(source), canonical, sim::VPITypespecOrigin::BuiltinClass,
        IntegerAttr{}, TypeAttr{}, IntegerAttr{});
    if (canonical.getKind() == sim::VPITypeKind::Mailbox) {
      // Reuse the exact layout already normalized for the retained source
      // specialization. The semantic root deliberately strips typedef aliases;
      // storage offsets and tracing instead come from the executable type.
      auto normalized = getNormalizedSemanticType(source);
      auto mailbox = succeeded(normalized)
                         ? dyn_cast<sim::MailboxType>(*normalized)
                         : sim::MailboxType{};
      if (!mailbox) {
        emitError(getSemanticLocation(source))
            << "canonical mailbox has no normalized source message layout";
        invalid = true;
        return {};
      }
      declaration.setMessageTypeAttr(TypeAttr::get(mailbox.getElementType()));
    }
    auto reference = FlatSymbolRefAttr::get(declaration.getSymNameAttr());
    builtinClassTypespecs[canonical] = reference;
    // Builtin specializations retain their own effective formal parameters.
    // In particular mailbox::T denotes the actual source type assignment,
    // including typedef spelling (37.28 detail 2); the canonical class identity
    // above deliberately has a separate alias-free semantic root.
    uint64_t ordinal = 0;
    for (Operation *child : getChildren(source)) {
      auto parameter = dyn_cast<semantic::SVTypeParameterSymbolOp>(child);
      if (!parameter)
        continue;
      auto type = parameter.getSemanticType();
      auto payload =
          type ? makeVPITypeSemantics(*type, getSemanticLocation(parameter),
                                      typedefLayersFor(parameter), parameter)
               : FailureOr<sim::VPITypeSemanticsAttr>(failure());
      if (failed(payload)) {
        parameter.emitOpError(
            "builtin type parameter has no preserved effective source type");
        invalid = true;
        continue;
      }
      std::string parameterName =
          "__obelisk_vpi_anchor_" + std::to_string(nextSyntheticInventoryId);
      auto anchor = sim::SimVPIObjectAnchorOp::create(
          builder, getSemanticLocation(parameter), parameterName,
          nextSyntheticInventoryId++,
          static_cast<uint32_t>(VPIKind::TypeParameter), scopes.lookup(source),
          reference, ordinal++,
          builder.getStringAttr(getHierarchyName(parameter)),
          builder.getStringAttr(getDebugName(parameter)), UnitAttr{},
          sim::VPIObjectBackingAttr{}, DenseI64ArrayAttr{}, DenseI64ArrayAttr{},
          DenseI64ArrayAttr{}, DenseI64ArrayAttr{}, IntegerAttr{}, *payload,
          sim::FrozenConstantAttr{}, UnitAttr{}, FlatSymbolRefAttr{},
          UnitAttr{}, StringAttr{}, UnitAttr{});
      if (auto identity = parameter->getAttrOfType<IntegerAttr>(
              vpiSourceTypeIdentityAttrName))
        sim::set<sim::metadata::VpiSourceTypeIdentity>(anchor, identity);
      if (sim::VPIPropertySetAttr properties =
              identityProperties(parameter, VPIKind::TypeParameter))
        anchor->setAttr("vpi_properties", properties);
    }
    return reference;
  };
  llvm::DenseMap<Operation *, FlatSymbolRefAttr> builtinBaseTypespecs;
  for (semantic::SVClassTypeOp classType : classes.sources) {
    auto builtin = classType->getAttrOfType<TypeAttr>(builtinBaseTypeAttrName);
    if (!builtin)
      continue;
    auto target = makeVPITypeSemantics(
        builtin.getValue(), getSemanticLocation(classType), {}, classType);
    if (failed(target)) {
      invalid = true;
      continue;
    }
    builtinBaseTypespecs[classType] =
        internBuiltinTypespec(*target, classType, /*requireSource=*/true);
  }
  for (semantic::SVClassTypeOp classType : classes.sources) {
    FlatSymbolRefAttr owner = classDefinitionSymbols.lookup(classType);
    if (!owner) {
      emitError(getSemanticLocation(classType))
          << "VPI class specialization is missing its lexical definition";
      invalid = true;
      continue;
    }
    FailureOr<sim::VPITypeSemanticsAttr> target = makeVPITypeSemantics(
        classType.getSemanticType(), getSemanticLocation(classType), {},
        classType);
    if (failed(target)) {
      invalid = true;
      continue;
    }
    sim::SimVPITypespecDeclOp::create(
        builder, getSemanticLocation(classType),
        classTypespecSymbols.lookup(classType).getValue(), nextTypespecId++,
        scopes.lookup(classType), owner, getHierarchyName(classType),
        getDebugName(classType), *target,
        sim::VPITypespecOrigin::ClassSpecialization, IntegerAttr{}, TypeAttr{},
        IntegerAttr{});
  }
  // Nonparameterized declarations still have separate lexical and resolved
  // member views. The executable method belongs to the canonical view so its
  // IO declarations and body remain reachable from live class objects.
  for (semantic::SVClassTypeOp classType : classes.sources) {
    if (hasSeparateLexicalClass(classType))
      continue;
    uint64_t ordinal = 0;
    for (Operation *child : getChildren(classType)) {
      semantic::SVSubroutineSymbolOp method = getClassMethod(child);
      Operation *source = method ? method.getOperation() : child;
      bool parameter = isa<semantic::SVParameterSymbolOp,
                           semantic::SVTypeParameterSymbolOp>(source);
      bool constraint = isa<semantic::SVConstraintBlockSymbolOp>(source);
      sim::SimVPIObjectAnchorOp lexical = anchorDeclarations.lookup(source);
      if (!lexical ||
          (!parameter && !constraint && !classes.methodSymbols.count(method)))
        continue;
      auto specialized =
          cast<sim::SimVPIObjectAnchorOp>(builder.clone(*lexical));
      std::string symbolName =
          "__obelisk_vpi_anchor_" + std::to_string(nextSyntheticInventoryId);
      specialized.setSymNameAttr(builder.getStringAttr(symbolName));
      specialized.setInventoryIdAttr(
          builder.getI64IntegerAttr(nextSyntheticInventoryId++));
      specialized.setParentAttr(classTypespecSymbols.lookup(classType));
      specialized.setOwnerOrdinalAttr(builder.getI64IntegerAttr(ordinal++));
      if (parameter)
        addParameterView(source, specialized);
      if (method)
        sim::set<sim::metadata::VpiAnchor>(
            method, FlatSymbolRefAttr::get(specialized.getSymNameAttr()));
    }
  }
  // Each declaration view owns its original default independently of its
  // elaborated value. Exact sibling views remap symbolic parameter references;
  // no specialization supplies the unresolved lexical definition's payload.
  auto parameterTarget = [&](Operation *reference, FlatSymbolRefAttr symbol,
                             FlatSymbolRefAttr viewOwner) -> FlatSymbolRefAttr {
    Operation *source = SymbolTable::lookupNearestSymbolFrom(reference, symbol);
    auto found = parameterViews.find(source);
    if (found == parameterViews.end())
      return {};
    for (auto view : found->second)
      if (view.getParentAttr() == viewOwner)
        return FlatSymbolRefAttr::get(view.getSymNameAttr());
    return {};
  };
  auto sourceRelation = [&](Operation *source, FlatSymbolRefAttr from,
                            uint32_t selector, sim::VPIRelationMode mode,
                            uint64_t ordinal, FlatSymbolRefAttr to) {
    sim::SimVPIRelationDeclOp::create(
        builder, getSemanticLocation(source),
        sim::VPIObjectRefAttr::get(builder.getContext(), from), selector, mode,
        ordinal, sim::VPIObjectRefAttr::get(builder.getContext(), to));
  };
  auto expression =
      [&](auto &&self, Operation *node, FlatSymbolRefAttr parent,
          uint64_t ordinal, uint64_t scopeID,
          FlatSymbolRefAttr viewOwner) -> FailureOr<FlatSymbolRefAttr> {
    if (auto reference =
            dyn_cast<semantic::SVSourceParameterReferenceOp>(node)) {
      auto target =
          parameterTarget(reference, reference.getParameterAttr(), viewOwner);
      if (!target)
        return reference.emitOpError(
                   "original value reference has no exact parameter view"),
               failure();
      return target;
    }
    VPIKind kind;
    const reflection::VPISourceOperatorDescriptor *operation = nullptr;
    sim::VPITypeSemanticsAttr type;
    sim::FrozenConstantAttr value;
    if (auto unary = dyn_cast<semantic::SVSourceUnaryExpressionOp>(node)) {
      kind = VPIKind::Operation;
      auto name = semantic::stringifySVUnaryOperator(unary.getOperatorKind());
      operation = reflection::findVPISourceOperator(
          std::string_view(name.data(), name.size()), 1);
    } else if (auto binary =
                   dyn_cast<semantic::SVSourceBinaryExpressionOp>(node)) {
      kind = VPIKind::Operation;
      auto name = semantic::stringifySVBinaryOperator(binary.getOperatorKind());
      operation = reflection::findVPISourceOperator(
          std::string_view(name.data(), name.size()), 2);
    } else if (isa<semantic::SVSourceConditionalExpressionOp>(node)) {
      kind = VPIKind::Operation;
      operation = reflection::findVPISourceOperator("conditional", 3);
    } else if (isa<semantic::SVIntegerLiteralOp>(node)) {
      kind = VPIKind::Constant;
      auto semantic = makeVPITypeSemantics(
          node->getAttrOfType<TypeAttr>("semantic_type").getValue(),
          getSemanticLocation(node), {}, node);
      auto frozen = freezeSemanticConstant(node);
      if (failed(semantic) || failed(frozen))
        return failure();
      type = *semantic;
      value = *frozen;
    } else {
      return node->emitOpError("unsupported typed source default expression"),
             failure();
    }
    if (kind == VPIKind::Operation && !operation)
      return node->emitOpError(
                 "source default has no exact VPI operator encoding"),
             failure();
    std::string symbolName =
        "__obelisk_vpi_expression_" + std::to_string(nextSyntheticInventoryId);
    auto anchor = sim::SimVPIObjectAnchorOp::create(
        builder, getSemanticLocation(node), symbolName,
        nextSyntheticInventoryId++, static_cast<uint32_t>(kind), scopeID,
        parent, ordinal, builder.getStringAttr(""), builder.getStringAttr(""),
        UnitAttr{}, sim::VPIObjectBackingAttr{}, DenseI64ArrayAttr{},
        DenseI64ArrayAttr{}, DenseI64ArrayAttr{}, DenseI64ArrayAttr{},
        IntegerAttr{}, type, value, UnitAttr{}, FlatSymbolRefAttr{}, UnitAttr{},
        StringAttr{}, UnitAttr{});
    auto reference = FlatSymbolRefAttr::get(anchor.getSymNameAttr());
    if (kind == VPIKind::Operation || kind == VPIKind::Constant) {
      auto property = sim::VPIPropertyAttr::get(
          builder.getContext(),
          builder.getI32IntegerAttr(kind == VPIKind::Operation ? 39 : 40),
          builder.getI32IntegerAttr(operation ? operation->value : 7));
      anchor->setAttr("vpi_properties", sim::VPIPropertySetAttr::get(
                                            builder.getContext(),
                                            builder.getArrayAttr({property})));
    }
    if (operation) {
      uint64_t ownedOrdinal = 0;
      for (auto [index, child] : llvm::enumerate(getChildren(node))) {
        // Shared Parameter leaves have a source operand position but do
        // not consume an ordinal in this operation's owned inventory.
        auto operand =
            self(self, child, reference, ownedOrdinal, scopeID, viewOwner);
        if (!isa<semantic::SVSourceParameterReferenceOp>(child))
          ++ownedOrdinal;
        if (failed(operand))
          return failure();
        sourceRelation(
            node, reference,
            static_cast<uint32_t>(reflection::VPIRelationKind::OperandRel),
            sim::VPIRelationMode::Iterate, index, *operand);
      }
    }
    return reference;
  };
  for (Operation *source : parameterViewSources) {
    for (auto owner : parameterViews.lookup(source)) {
      FlatSymbolRefAttr ownerRef =
          FlatSymbolRefAttr::get(owner.getSymNameAttr());
      for (Operation *child : getChildren(source)) {
        FlatSymbolRefAttr target;
        if (auto reference =
                dyn_cast<semantic::SVSourceTypeReferenceDefaultOp>(child)) {
          target = parameterTarget(reference, reference.getParameterAttr(),
                                   owner.getParentAttr());
          if (!target) {
            reference.emitOpError(
                "original type reference has no exact parameter view");
            invalid = true;
            continue;
          }
        } else if (auto concrete =
                       dyn_cast<semantic::SVSourceTypeDefaultOp>(child)) {
          auto semantic = makeVPITypeSemantics(
              concrete.getSemanticType(), getSemanticLocation(concrete),
              typedefLayersFor(concrete), concrete);
          if (failed(semantic)) {
            invalid = true;
            continue;
          }
          std::string symbolName =
              "__obelisk_vpi_source_default_" + std::to_string(nextTypespecId);
          auto typespec = sim::SimVPITypespecDeclOp::create(
              builder, getSemanticLocation(concrete), symbolName,
              nextTypespecId++, owner.getEnclosingScopeId(), ownerRef, "", "",
              *semantic, sim::VPITypespecOrigin::SourceDefault, IntegerAttr{},
              TypeAttr{}, IntegerAttr{});
          target = FlatSymbolRefAttr::get(typespec.getSymNameAttr());
        } else if (auto value =
                       dyn_cast<semantic::SVSourceValueDefaultOp>(child)) {
          auto result =
              expression(expression, &value.getBody().front().front(), ownerRef,
                         0, owner.getEnclosingScopeId(), owner.getParentAttr());
          if (failed(result)) {
            invalid = true;
            continue;
          }
          target = *result;
        }
        if (target)
          sourceRelation(
              child, ownerRef,
              static_cast<uint32_t>(reflection::VPIRelationKind::ExprRel),
              sim::VPIRelationMode::Handle, 0, target);
      }
    }
  }

  auto emitClassRelation = [&](Location location, FlatSymbolRefAttr source,
                               uint32_t selector, sim::VPIRelationMode mode,
                               uint64_t ordinal, FlatSymbolRefAttr target) {
    sim::SimVPIRelationDeclOp::create(
        builder, location,
        sim::VPIObjectRefAttr::get(builder.getContext(), source), selector,
        mode, ordinal,
        sim::VPIObjectRefAttr::get(builder.getContext(), target));
  };
  llvm::DenseSet<std::pair<Attribute, Attribute>> derivedDefinitions;
  llvm::DenseMap<Attribute, uint64_t> nextDerivedOrdinal;
  llvm::DenseSet<Attribute> sourceExtendsDefinitions;
  llvm::DenseMap<Attribute, Attribute> lexicalBaseDefinitions;
  SmallVector<semantic::SVSourceClassExtendsOp> sourceExtends;
  module.walk([&](semantic::SVSourceClassExtendsOp clause) {
    sourceExtends.push_back(clause);
  });
  for (auto clause : sourceExtends) {
    Operation *source = clause->getParentOp();
    if (auto type = dyn_cast<semantic::SVClassTypeOp>(source);
        type && hasSeparateLexicalClass(type))
      continue;
    FlatSymbolRefAttr owner = anchorSymbols.lookup(source);
    if (!owner) {
      emitError(getSemanticLocation(clause))
          << "source extends has no exact lexical class owner";
      invalid = true;
      continue;
    }
    Operation *base = clause.getBaseDefinitionAttr()
                          ? lexicalClassSymbols.lookupSymbolIn(
                                module, clause.getBaseDefinitionAttr())
                          : nullptr;
    FlatSymbolRefAttr definition =
        base ? anchorSymbols.lookup(base) : FlatSymbolRefAttr{};
    // Preserve the existing resolved builtin path when it has an exact
    // canonical standard-class typespec. Source-only dependent builtin forms
    // remain explicitly unavailable instead of inventing a class definition.
    if (!definition && isa<semantic::SVClassTypeOp>(source) &&
        builtinBaseTypespecs.count(source))
      continue;
    sourceExtendsDefinitions.insert(owner);
    FlatSymbolRefAttr lexicalType;
    if (definition) {
      lexicalBaseDefinitions[owner] = definition;
      std::string name =
          "__obelisk_vpi_lexical_class_" + std::to_string(nextTypespecId);
      auto declaration = sim::SimVPILexicalClassTypespecDeclOp::create(
          builder, getSemanticLocation(clause), builder.getStringAttr(name),
          builder.getI64IntegerAttr(nextTypespecId++),
          builder.getI64IntegerAttr(scopes.lookup(source)), owner, definition,
          builder.getStringAttr(getDebugName(base)),
          clause.getParameterArgumentsAttr());
      lexicalType = FlatSymbolRefAttr::get(declaration.getSymNameAttr());
      if (!clause.getParameterArgumentsAttr()) {
        llvm::DenseMap<Operation *, semantic::SVSourceClassParameterActualOp>
            actuals;
        for (Operation *child : getChildren(clause)) {
          auto actual = cast<semantic::SVSourceClassParameterActualOp>(child);
          Operation *formal = lexicalClassSymbols.lookupSymbolIn(
              module, actual.getParameterAttr());
          actuals[formal] = actual;
        }
        uint64_t ordinal = 0;
        for (Operation *parameter : getChildren(base)) {
          if (!isa<semantic::SVSourceValueParameterSymbolOp,
                   semantic::SVSourceTypeParameterSymbolOp,
                   semantic::SVParameterSymbolOp,
                   semantic::SVTypeParameterSymbolOp>(parameter))
            continue;
          Attribute local = parameter->getAttr("is_local_param");
          if (isa_and_nonnull<UnitAttr>(local) ||
              (isa_and_nonnull<BoolAttr>(local) &&
               cast<BoolAttr>(local).getValue()))
            continue;
          auto formal = anchorSymbols.lookup(parameter);
          if (!formal) {
            emitError(getSemanticLocation(parameter))
                << "source class parameter has no exact lexical declaration";
            invalid = true;
            continue;
          }
          std::string assignmentName = "__obelisk_vpi_class_actual_" +
                                       std::to_string(nextSyntheticInventoryId);
          auto actual = actuals.lookup(parameter);
          auto assignment = sim::SimVPIClassParameterAssignDeclOp::create(
              builder,
              getSemanticLocation(actual ? actual.getOperation()
                                         : clause.getOperation()),
              builder.getStringAttr(assignmentName),
              builder.getI64IntegerAttr(nextSyntheticInventoryId++),
              builder.getI64IntegerAttr(scopes.lookup(source)), lexicalType,
              formal, builder.getI64IntegerAttr(ordinal++), FlatSymbolRefAttr{},
              actual ? actual.getUnavailableRhsAttr() : StringAttr{},
              actual ? actual.getIsNamedAttr() : UnitAttr{});
          if (!actual || actual.getUseDefaultAttr() ||
              actual.getUnavailableRhsAttr())
            continue;
          Operation *value = &actual.getBody().front().front();
          FlatSymbolRefAttr target;
          auto assignmentRef =
              FlatSymbolRefAttr::get(assignment.getSymNameAttr());
          if (auto reference =
                  dyn_cast<semantic::SVSourceTypeReferenceDefaultOp>(value)) {
            target =
                parameterTarget(reference, reference.getParameterAttr(), owner);
            if (!target) {
              reference.emitOpError(
                  "class type actual has no exact derived parameter view");
              invalid = true;
            }
          } else if (auto concrete =
                         dyn_cast<semantic::SVSourceTypeDefaultOp>(value)) {
            auto semantic = makeVPITypeSemantics(
                concrete.getSemanticType(), getSemanticLocation(concrete),
                typedefLayersFor(concrete), concrete);
            if (failed(semantic)) {
              invalid = true;
              continue;
            }
            std::string typeName =
                "__obelisk_vpi_source_actual_" + std::to_string(nextTypespecId);
            auto type = sim::SimVPITypespecDeclOp::create(
                builder, getSemanticLocation(concrete), typeName,
                nextTypespecId++, scopes.lookup(source), assignmentRef, "", "",
                *semantic, sim::VPITypespecOrigin::SourceDefault, IntegerAttr{},
                TypeAttr{}, IntegerAttr{});
            target = FlatSymbolRefAttr::get(type.getSymNameAttr());
          } else {
            auto result = expression(expression, value, assignmentRef, 0,
                                     scopes.lookup(source), owner);
            if (failed(result))
              invalid = true;
            else
              target = *result;
          }
          if (target)
            assignment.setRhsAttr(target);
        }
      }
    }
    std::string name =
        "__obelisk_vpi_extends_" + std::to_string(nextSyntheticInventoryId);
    StringAttr unavailable =
        definition ? StringAttr{} : clause.getUnavailableBaseAttr();
    if (!definition && !unavailable)
      unavailable = builder.getStringAttr(getDebugName(base));
    sim::SimVPIClassExtendsDeclOp::create(
        builder, getSemanticLocation(clause), builder.getStringAttr(name),
        builder.getI64IntegerAttr(nextSyntheticInventoryId++),
        builder.getI64IntegerAttr(scopes.lookup(source)), owner, lexicalType,
        unavailable, clause.getConstructorArgumentsAttr());
  }
  // Older authored source IR may encode a nonparameterized base solely by
  // its exact class handle. Include those declaration edges when walking an
  // unused generic descendant, without choosing a generic specialization.
  for (semantic::SVClassTypeOp type : classes.sources) {
    if (hasSeparateLexicalClass(type))
      continue;
    auto baseType = dyn_cast_or_null<semantic::ClassHandleType>(
        type.getBaseClass().value_or(Type{}));
    auto base = baseType ? classes.semanticClasses.lookup(
                               baseType.getClassName().getLeafReference())
                         : semantic::SVClassTypeOp{};
    if (!base || hasSeparateLexicalClass(base))
      continue;
    auto owner = classDefinitionSymbols.lookup(type);
    auto definition = classDefinitionSymbols.lookup(base);
    if (owner && definition)
      lexicalBaseDefinitions.try_emplace(owner, definition);
  }
  // The source graph exists even when no specialization was materialized.
  // Preserve transitive lexical descendants in deterministic declaration order.
  for (auto clause : sourceExtends) {
    FlatSymbolRefAttr definition = anchorSymbols.lookup(clause->getParentOp());
    Attribute ancestor = lexicalBaseDefinitions.lookup(definition);
    llvm::DenseSet<Attribute> visited;
    while (ancestor) {
      if (!visited.insert(ancestor).second) {
        emitError(getSemanticLocation(clause))
            << "source class inheritance contains a cycle";
        invalid = true;
        break;
      }
      if (derivedDefinitions.insert({ancestor, definition}).second)
        emitClassRelation(getSemanticLocation(clause),
                          cast<FlatSymbolRefAttr>(ancestor), 727,
                          sim::VPIRelationMode::Iterate,
                          nextDerivedOrdinal[ancestor]++, definition);
      ancestor = lexicalBaseDefinitions.lookup(ancestor);
    }
  }
  for (semantic::SVClassTypeOp classType : classes.sources) {
    auto builtin = classType->getAttrOfType<TypeAttr>(builtinBaseTypeAttrName);
    auto baseType = dyn_cast_or_null<semantic::ClassHandleType>(
        builtin ? builtin.getValue()
                : classType.getBaseClass().value_or(Type{}));
    if (!baseType)
      continue;
    semantic::SVClassTypeOp baseClass =
        builtin ? semantic::SVClassTypeOp{}
                : classes.semanticClasses.lookup(
                      baseType.getClassName().getLeafReference());
    FlatSymbolRefAttr baseTypespec =
        builtin ? builtinBaseTypespecs.lookup(classType)
                : classTypespecSymbols.lookup(baseClass);
    FlatSymbolRefAttr definition = classDefinitionSymbols.lookup(classType);
    FlatSymbolRefAttr baseDefinition = classDefinitionSymbols.lookup(baseClass);
    if (!baseTypespec || !definition || (!builtin && !baseDefinition)) {
      emitError(getSemanticLocation(classType))
          << "VPI class inheritance cannot resolve its base class identity";
      invalid = true;
      continue;
    }
    emitClassRelation(getSemanticLocation(classType),
                      classTypespecSymbols.lookup(classType), 677,
                      sim::VPIRelationMode::Handle, 0, baseTypespec);
    // Unlike the specialization inventory, 37.31 does not restrict derived
    // definitions to immediate inheritance. Walk the ancestry of each
    // concrete type and deduplicate definitions reached by multiple generic
    // specializations.
    llvm::SmallPtrSet<Operation *, 8> ancestors;
    for (semantic::SVClassTypeOp ancestor = baseClass; ancestor;) {
      if (!ancestors.insert(ancestor).second) {
        emitError(getSemanticLocation(classType))
            << "VPI class inheritance contains a cycle";
        invalid = true;
        break;
      }
      FlatSymbolRefAttr ancestorDefinition =
          classDefinitionSymbols.lookup(ancestor);
      if (!ancestorDefinition) {
        emitError(getSemanticLocation(classType))
            << "VPI class inheritance cannot resolve an ancestor definition";
        invalid = true;
        break;
      }
      if (derivedDefinitions.insert({ancestorDefinition, definition}).second)
        emitClassRelation(getSemanticLocation(classType), ancestorDefinition,
                          727, sim::VPIRelationMode::Iterate,
                          nextDerivedOrdinal[ancestorDefinition]++, definition);
      auto ancestorBase = dyn_cast_or_null<semantic::ClassHandleType>(
          ancestor.getBaseClass().value_or(Type{}));
      ancestor = ancestorBase
                     ? classes.semanticClasses.lookup(
                           ancestorBase.getClassName().getLeafReference())
                     : semantic::SVClassTypeOp{};
    }

    // A nonparameterized class typespec may also represent the lexical
    // construct. Parameterized lexical extends expressions need their own
    // unresolved source typespec, rather than one chosen specialization.
    if (sourceExtendsDefinitions.contains(definition) ||
        hasSeparateLexicalClass(classType) ||
        (baseClass && hasSeparateLexicalClass(baseClass)))
      continue;
    std::string symbolName =
        "__obelisk_vpi_anchor_" + std::to_string(nextSyntheticInventoryId);
    auto extends = sim::SimVPIObjectAnchorOp::create(
        builder, getSemanticLocation(classType), symbolName,
        nextSyntheticInventoryId++, static_cast<uint32_t>(VPIKind::Extends),
        scopes.lookup(classType), definition, nextAnchorOrdinal[classType]++,
        builder.getStringAttr(
            (getHierarchyName(classType) + "::extends").str()),
        builder.getStringAttr("extends"), UnitAttr{},
        sim::VPIObjectBackingAttr{}, DenseI64ArrayAttr{}, DenseI64ArrayAttr{},
        DenseI64ArrayAttr{}, DenseI64ArrayAttr{}, IntegerAttr{},
        sim::VPITypeSemanticsAttr{}, sim::FrozenConstantAttr{}, UnitAttr{},
        FlatSymbolRefAttr{}, UnitAttr{}, StringAttr{}, UnitAttr{});
    FlatSymbolRefAttr extendsRef =
        FlatSymbolRefAttr::get(extends.getSymNameAttr());
    emitClassRelation(getSemanticLocation(classType), definition, 677,
                      sim::VPIRelationMode::Handle, 0, extendsRef);
    emitClassRelation(getSemanticLocation(classType), extendsRef, 652,
                      sim::VPIRelationMode::Handle, 0, definition);
    emitClassRelation(getSemanticLocation(classType), extendsRef, 630,
                      sim::VPIRelationMode::Handle, 0, baseTypespec);
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
        scopes.lookup(entry.representative), owner, identity, debug, *target,
        sim::VPITypespecOrigin::Interface, IntegerAttr{}, TypeAttr{},
        IntegerAttr{});
  }

  // Materialize a standalone enum typespec when no typedef declaration names
  // the exact Slang type identity.  Identity, rather than a hierarchy display
  // string, is the relation key so same-named `$unit` enums remain distinct.
  struct EnumValueInventory {
    semantic::SVEnumValueSymbolOp operation;
    sim::VPITypeSemanticsAttr type;
    Attribute identity;
    FlatSymbolRefAttr owner;
    Operation *block;
    StringAttr constant;
  };
  SmallVector<EnumValueInventory> enumValues;
  module.walk([&](semantic::SVEnumValueSymbolOp enumValue) {
    FlatSymbolRefAttr owner = ownerAnchorFor(enumValue);
    Operation *block = owner ? nullptr : proceduralOwnerFor(enumValue);
    if (!owner && !block)
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
    enumValues.push_back(
        {enumValue, *target, identity, owner, block, constant});
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
    if (inventory.block) {
      proceduralTypes.typespecs.push_back(
          {enumValue, inventory.block, symbol, nextTypespecId++,
           scopes.lookup(enumValue), hierarchy.str(), name.str(), target,
           sim::VPITypespecOrigin::AnonymousEnum, sourceTypeIdentity});
      pendingTypespecSymbols.insert(symbol);
    } else {
      sim::SimVPITypespecDeclOp::create(
          builder, getSemanticLocation(enumValue), symbolName, nextTypespecId++,
          scopes.lookup(enumValue), inventory.owner, hierarchy, name, target,
          sim::VPITypespecOrigin::AnonymousEnum, sourceTypeIdentity, TypeAttr{},
          IntegerAttr{});
    }
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
    auto frozen = freezeSemanticConstant(enumValue);
    if (failed(frozen)) {
      invalid = true;
      continue;
    }
    if (pendingTypespecSymbols.contains(typespec)) {
      proceduralTypes.constants.push_back(
          {enumValue, nextEnumConstId++, nextEnumOrdinal[inventory.identity]++,
           typespec, name.str(), inventory.constant.getValue().str(), *frozen});
    } else {
      sim::SimVPIEnumConstDeclOp::create(
          builder, getSemanticLocation(enumValue), nextEnumConstId++,
          nextEnumOrdinal[inventory.identity]++, typespec.getValue(), name,
          inventory.constant.getValue(), *frozen);
    }
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
          sim::set<sim::metadata::VpiSourceTypeIdentity>(declaration, identity);
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
        if (auto existing =
                sim::get<sim::metadata::VirtualInterfaceClockEvents>(scope))
          llvm::append_range(members, existing);
        members.push_back(sim::VirtualInterfaceEventAttr::get(
            builder.getContext(), builder.getStringAttr(getDebugName(op)), id));
        sim::set<sim::metadata::VirtualInterfaceClockEvents>(
            scope, builder.getArrayAttr(members));
        break;
      }
      return;
    }
    if (sim::has<sim::metadata::SequenceEndpointEvent>(op)) {
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
              sim::get<sim::metadata::InterconnectLeaves>(op)) {
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
              DenseI64ArrayAttr{}, IntegerAttr{}, sim::VPITypeSemanticsAttr{},
              sim::FrozenConstantAttr{}, UnitAttr{}, FlatSymbolRefAttr{},
              UnitAttr{}, StringAttr{}, UnitAttr{});
          anchor->setAttr("vpi_properties",
                          netProperties(cast<semantic::SVNetSymbolOp>(op)));
          return anchor;
        };
        sim::SimVPIObjectAnchorOp root = makeArrayAnchor();
        for (auto definition :
             leafDefinitions.getAsRange<sim::InterconnectLeafAttr>()) {
          StringAttr leafPath = definition.getPath();
          sim::VPITypeSemanticsAttr semanticVPIType = definition.getVpiType();
          SymbolRefAttr sourceNettypeReference = definition.getNettype();
          IntegerAttr sourceTypeIdentity = definition.getTypeIdentity();
          ArrayRef<int64_t> indices = definition.getIndices().asArrayRef();
          if (indices.size() * 2 != bounds.size()) {
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
          FailureOr<Type> type = normalizeSemanticType(
              definition.getType(), getSemanticLocation(op), op);
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
              schedule::ComputeObservabilityKindAttr{},
              sim::NetResolutionKind::Wire, DenseI64ArrayAttr{},
              sim::StrengthAttr{}, UnitAttr{}, retainedVPIType, sourceNettype);
          cov::mark<cov::SourceAuthored>(declaration);
          if (retainCoverageSourceTypes)
            cov::set<cov::SourceType>(declaration,
                                      TypeAttr::get(definition.getType()));
          declaration->setAttr(
              "vpi_properties",
              netProperties(cast<semantic::SVNetSymbolOp>(op)));
          if (sourceTypeIdentity)
            sim::set<sim::metadata::VpiSourceTypeIdentity>(declaration,
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
              builder.getDenseI64ArrayAttr(indices), IntegerAttr{},
              sim::VPITypeSemanticsAttr{}, sim::FrozenConstantAttr{},
              UnitAttr{}, FlatSymbolRefAttr{}, UnitAttr{}, StringAttr{},
              UnitAttr{});
        }
        return;
      }
    }
    FailureOr<Type> type = getNormalizedSemanticType(op);
    auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
    sim::VPITypeSemanticsAttr inferredInterconnectVPIType =
        sim::get<sim::metadata::InterconnectVpiType>(op);
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
          hierarchy, debug, schedule::ComputeObservabilityKindAttr{},
          retainedVPIType);
      cov::mark<cov::SourceAuthored>(declaration);
      if (retainCoverageSourceTypes)
        cov::set<cov::SourceType>(declaration,
                                  op->getAttrOfType<TypeAttr>("semantic_type"));
      retainVPISourceTypeIdentity(op, declaration, retainedVPIType);
      if (namedEventArrayRanges.count(op))
        sim::set<sim::metadata::VpiIdentityDelegated>(declaration,
                                                      anchorSymbols.lookup(op));
      if (isa<sim::EventType>(*type) &&
          isa<semantic::SVVariableSymbolOp, semantic::SVClassPropertySymbolOp>(
              op) &&
          !getChildren(op).empty())
        sim::mark<sim::metadata::EventExplicitInitializer>(declaration);
      // Storage a subroutine owns is written by its callers, which the driver
      // rules of IEEE 1800-2017 6.5 do not count as competing drivers.
      if (op->getParentOfType<semantic::SVSubroutineSymbolOp>())
        sim::mark<sim::metadata::SubroutineStorage>(declaration);
      if (auto body =
              dyn_cast<semantic::SVInstanceBodySymbolOp>(op->getParentOp());
          body && body->hasAttr("virtual_interface_identity") &&
          !isCompileTimeOnlyInstanceMember(body))
        sim::set<sim::metadata::VirtualInterfaceMember>(declaration, debug);
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
      nettypeReference = sim::get<sim::metadata::InterconnectNettype>(op);
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
        schedule::ComputeObservabilityKindAttr{}, resolution, propagationDelays,
        resolution == sim::NetResolutionKind::TriReg
            ? sim::StrengthAttr::get(
                  builder.getContext(),
                  net.getChargeStrength()
                      ? lowerChargeStrength(*net.getChargeStrength())
                      : sim::Strength::Medium)
            : sim::StrengthAttr{},
        UnitAttr{}, retainedVPIType, sourceNettype);
    cov::mark<cov::SourceAuthored>(declaration);
    if (retainCoverageSourceTypes)
      cov::set<cov::SourceType>(declaration,
                                op->getAttrOfType<TypeAttr>("semantic_type"));
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
          DenseI64ArrayAttr{}, DenseI64ArrayAttr{}, IntegerAttr{},
          sim::VPITypeSemanticsAttr{}, sim::FrozenConstantAttr{}, UnitAttr{},
          FlatSymbolRefAttr{}, UnitAttr{}, StringAttr{}, UnitAttr{});
      anchor->setAttr("vpi_properties", netProperties(net));
    }
    if (net.getNetKind() == semantic::SVNetKind::UserDefined) {
      sim::mark<sim::metadata::UserDefinedNet>(declaration);
      if (auto path = net.getResolutionFunctionPath())
        sim::set<sim::metadata::ResolutionFunctionPath>(
            declaration, builder.getStringAttr(*path));
      if (auto symbol = net.getResolutionFunctionSymbol())
        sim::set<sim::metadata::ResolutionFunctionSymbol>(declaration, *symbol);
    }
    if (auto body =
            dyn_cast<semantic::SVInstanceBodySymbolOp>(op->getParentOp());
        body && body->hasAttr("virtual_interface_identity") &&
        !isCompileTimeOnlyInstanceMember(body))
      sim::set<sim::metadata::VirtualInterfaceMember>(declaration, debug);
  };

  // Materialize canonical objects first so alias resolution is independent of
  // semantic-tree traversal order.
  for (Operation *op : designObjects)
    if (!portAliases.aliases.count(getHierarchyName(op)))
      emitDescriptor(op);

  // Standalone named events have an executable event identity without an
  // allocated storage cell. Preserve that identity separately from the source
  // anchor's inventory ID; those namespaces are unrelated.
  {
    for (Operation *source : designObjects) {
      FlatSymbolRefAttr anchor = anchorSymbols.lookup(source);
      auto descriptor = descriptors.find(getHierarchyName(source));
      if (!anchor || !scalarNamedEvents.contains(source) ||
          descriptor == descriptors.end())
        continue;
      if (descriptor->second.kind == DescriptorInfo::Kind::Storage) {
        anchorDeclarations.lookup(source).setBackingAttr(
            sim::VPIObjectBackingAttr::get(
                builder.getContext(), sim::VPIObjectBackingKind::Storage,
                builder.getI64IntegerAttr(descriptor->second.id),
                FlatSymbolRefAttr{}));
      } else if (descriptor->second.kind == DescriptorInfo::Kind::Event) {
        sim::SimVPIEventBindingOp::create(
            builder, getSemanticLocation(source), anchor,
            builder.getI64IntegerAttr(descriptor->second.id));
      }
    }
  }

  // Source properties are immutable declarations, even though the same field
  // is backed by distinct storage in every live receiver. Keep that binding
  // symbolic until native class layout is known to the bytecode serializer.
  SymbolTable classSymbols(design);
  for (semantic::SVClassTypeOp classType : classes.sources) {
    for (Operation *child : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child);
      if (!property || property.getIsCompilerGenerated().value_or(false))
        continue;
      FlatSymbolRefAttr fieldRef = classes.fieldSymbols.lookup(property);
      if (!fieldRef)
        continue;
      auto field = dyn_cast_or_null<sim::SimClassFieldDeclOp>(
          classSymbols.lookup(fieldRef.getValue()));
      // Slang may materialize inherited embedded-covergroup declarations in
      // a derived scope. They retain their base field identity and are reached
      // through vpiExtends, not redeclared as properties of the derived class.
      if (!field || field.getOwnerAttr().getValue() !=
                        classes.symbols.lookup(classType).getValue())
        continue;
      auto semanticType = property.getSemanticType();
      if (!semanticType) {
        property.emitError("VPI class property is missing its semantic type");
        invalid = true;
        continue;
      }
      FailureOr<sim::VPITypeSemanticsAttr> vpiType = makeVPITypeSemantics(
          *semanticType, getSemanticLocation(property),
          typedefLayersFor(property), property);
      if (failed(vpiType)) {
        invalid = true;
        continue;
      }
      IntegerAttr storageID, eventID;
      if (field.getIsStatic()) {
        auto descriptor = descriptors.find(getHierarchyName(property));
        if (descriptor == descriptors.end() ||
            (descriptor->second.kind != DescriptorInfo::Kind::Storage &&
             descriptor->second.kind != DescriptorInfo::Kind::Event)) {
          property.emitError("VPI static class property has no design backing");
          invalid = true;
          continue;
        }
        IntegerAttr id = builder.getI64IntegerAttr(descriptor->second.id);
        if (descriptor->second.kind == DescriptorInfo::Kind::Storage)
          storageID = id;
        else
          eventID = id;
      }
      auto emitMember = [&](FlatSymbolRefAttr owner) {
        std::string symbolName =
            "__obelisk_vpi_class_member_" +
            std::to_string(nextSyntheticInventoryId);
        auto declaration = sim::SimVPIClassMemberDeclOp::create(
            builder, getSemanticLocation(property), symbolName,
            nextSyntheticInventoryId++, scopes.lookup(property),
            owner.getValue(), fieldRef, getHierarchyName(property),
            getDebugName(property), *vpiType, storageID, eventID);
        declaration->setAttr(
            "vpi_properties",
            classPropertyFacts(property, sim::vpiKindForClassMember(*vpiType)));
        retainVPISourceTypeIdentity(property, declaration, *vpiType);
      };
      emitMember(classTypespecSymbols.lookup(classType));
      if (!hasSeparateLexicalClass(classType))
        emitMember(classDefinitionSymbols.lookup(classType));
    }
  }

  if (preserveVPIBoxBindings) {
    SmallVector<Operation *> boxedArguments;
    semanticRoot.walk([&](semantic::SVCallExpressionOp call) {
      if (call.getCalleeName() != "put" && call.getCalleeName() != "try_put")
        return;
      auto children = getChildren(call);
      if (children.size() != 2)
        return;
      auto receiver = getNormalizedSemanticType(children.front());
      auto mailbox = succeeded(receiver) ? dyn_cast<sim::MailboxType>(*receiver)
                                         : sim::MailboxType{};
      if (mailbox && isa<sim::BoxType>(mailbox.getElementType()))
        boxedArguments.push_back(children[1]);
    });
    DenseMap<std::pair<Type, Attribute>, FlatSymbolRefAttr> boxTypes;
    uint64_t nextBoxTypeID = 1;
    for (Operation *source : boxedArguments) {
      auto sourceType = source->getAttrOfType<TypeAttr>("semantic_type");
      auto normalized = getNormalizedSemanticType(source);
      if (!sourceType || failed(normalized)) {
        source->emitError("boxed VPI value has no exact source storage type");
        invalid = true;
        continue;
      }
      auto semantic = makeVPITypeSemantics(sourceType.getValue(),
                                           getSemanticLocation(source),
                                           typedefLayersFor(source), source);
      if (failed(semantic)) {
        invalid = true;
        continue;
      }
      auto key = std::make_pair(*normalized, Attribute(*semantic));
      auto [found, inserted] = boxTypes.try_emplace(key, FlatSymbolRefAttr{});
      if (inserted) {
        const uint64_t id = nextBoxTypeID++;
        auto descriptor = sim::SimVPIBoxTypeDeclOp::create(
            builder, getSemanticLocation(source),
            "__obelisk_vpi_box_type_" + std::to_string(id), id, *normalized,
            *semantic);
        retainVPISourceTypeIdentity(source, descriptor, *semantic);
        found->second = FlatSymbolRefAttr::get(descriptor);
      }
      sim::set<sim::metadata::VpiBoxType>(source, found->second);
    }
  }

  // A static constraint block has one mode bit shared by every instance of
  // its declaring class (IEEE 1800-2017 18.5.11). Keep that bit in flattened
  // design storage rather than in any class object. Zero is the required
  // initial enabled state; the stored value is the disabled bit used by the
  // executable constraint mask.
  semanticRoot.walk([&](semantic::SVConstraintBlockSymbolOp constraint) {
    if (!constraint.getIsStatic().value_or(false))
      return;
    uint64_t id = nextStorageId++;
    sim::set<sim::metadata::StaticConstraintStorage>(
        constraint, builder.getI64IntegerAttr(id));
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
        schedule::ComputeObservabilityKindAttr{}, sim::VPITypeSemanticsAttr{});
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
    sim::set<sim::metadata::StaticRandomModeStorage>(
        property, builder.getI64IntegerAttr(id));
    Type type = builder.getI64Type();
    uint64_t scopeId = scopes.lookup(property);
    descriptors[hierarchy] = {DescriptorInfo::Kind::Storage, id, scopeId, type,
                              sim::NetResolutionKind::Wire};
    descriptors[hierarchy].rootType = type;
    sim::SimStorageDeclOp::create(
        builder, getSemanticLocation(property), id, scopeId, type,
        sim::Lifetime::Design, builder.getStringAttr(hierarchy),
        builder.getStringAttr("__obelisk_rand_mode"),
        schedule::ComputeObservabilityKindAttr{}, sim::VPITypeSemanticsAttr{});
    FailureOr<Type> propertyType = getNormalizedSemanticType(property);
    if (succeeded(propertyType) &&
        property.getRandMode() != semantic::SVRandMode::None)
      if (auto layout = getFixedRandomArrayLayout(*propertyType)) {
        std::string elements =
            (llvm::Twine(path) + ".$rand_element_modes").str();
        if (descriptors.count(elements)) {
          emitError(getSemanticLocation(property))
              << "static array rand_mode conflicts with an existing design "
                 "object";
          invalid = true;
          return;
        }
        uint64_t elementId = nextStorageId++;
        Type elementType = builder.getIntegerType(layout->elementCount);
        sim::set<sim::metadata::StaticRandomArrayModeStorage>(
            property, builder.getI64IntegerAttr(elementId));
        descriptors[elements] = {DescriptorInfo::Kind::Storage, elementId,
                                 scopeId, elementType,
                                 sim::NetResolutionKind::Wire};
        descriptors[elements].rootType = elementType;
        sim::SimStorageDeclOp::create(
            builder, getSemanticLocation(property), elementId, scopeId,
            elementType, sim::Lifetime::Design, builder.getStringAttr(elements),
            builder.getStringAttr("__obelisk_rand_element_modes"),
            schedule::ComputeObservabilityKindAttr{},
            sim::VPITypeSemanticsAttr{});
      }
    if (auto members = sim::get<sim::metadata::RandomStructMembers>(property)) {
      SmallVector<Attribute> updated;
      for (auto [index, member] :
           llvm::enumerate(members.getAsRange<sim::RandomStructMemberAttr>())) {
        std::optional<uint64_t> elementModeStorage =
            member.getElementModeStorage();
        if (auto layout = getFixedRandomArrayLayout(member.getType())) {
          std::string elements = (llvm::Twine(path) + ".$rand_member_" +
                                  llvm::Twine(index) + "_element_modes")
                                     .str();
          if (descriptors.count(elements) || nextStorageId == UINT64_MAX) {
            emitError(getSemanticLocation(property))
                << "static structure member rand_mode storage is unavailable";
            invalid = true;
            return;
          }
          uint64_t elementId = nextStorageId++;
          Type elementType = builder.getIntegerType(layout->elementCount);
          elementModeStorage = elementId;
          descriptors[elements] = {DescriptorInfo::Kind::Storage, elementId,
                                   scopeId, elementType,
                                   sim::NetResolutionKind::Wire};
          descriptors[elements].rootType = elementType;
          sim::SimStorageDeclOp::create(
              builder, getSemanticLocation(property), elementId, scopeId,
              elementType, sim::Lifetime::Design,
              builder.getStringAttr(elements),
              builder.getStringAttr("__obelisk_rand_member_element_modes"),
              schedule::ComputeObservabilityKindAttr{},
              sim::VPITypeSemanticsAttr{});
        }
        updated.push_back(sim::RandomStructMemberAttr::get(
            property.getContext(), member.getPath(), member.getSemanticType(),
            member.getType(), member.getIsRandC(), member.getModeIndex(),
            member.getCycle(), member.getElementModeField(),
            elementModeStorage));
      }
      sim::set<sim::metadata::RandomStructMembers>(
          property, builder.getArrayAttr(updated));
    }
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
  struct RefMemberSource {
    uint64_t scopeId = 0;
    FlatSymbolRefAttr member;
  };
  llvm::DenseMap<uint64_t, sim::SimScopeDeclOp> scopeDeclarationsById;
  for (sim::SimScopeDeclOp scope : scopes.declarations)
    scopeDeclarationsById[scope.getId()] = scope;
  using ScopedSymbol = std::pair<uint64_t, Attribute>;
  llvm::DenseMap<ScopedSymbol, RefMemberSource> refMembersByFormalSymbol;
  llvm::DenseMap<ScopedSymbol, RefMemberSource> refMembersByInternalSymbol;
  for (DefinitionInstancePlan &instance : definitionInstancePlans) {
    DefinitionMemberPlan &plan =
        definitionMemberPlans[instance.definition.getValue()];
    uint32_t ordinal = 0;
    for (Operation &child : instance.body.getBody().front()) {
      if (!isa<semantic::SVPortSymbolOp, semantic::SVInterfacePortSymbolOp>(
              child) ||
          getDebugName(&child).empty())
        continue;
      if (ordinal >= plan.memberSymbols.size())
        break;
      if (plan.signatures[ordinal].direction != sim::VPIIODirection::Ref) {
        ++ordinal;
        continue;
      }
      auto connection =
          connectionsByFormalSymbol.find(getSemanticSymbolReference(&child));
      if (connection == connectionsByFormalSymbol.end())
        connection = connectionsByFormalSymbol.find(
            getSemanticSymbolReference(&child, true));
      if (connection != connectionsByFormalSymbol.end()) {
        RefMemberSource source{instance.scope.getId(),
                               plan.memberSymbols[ordinal]};
        if (!refMembersByFormalSymbol
                 .try_emplace(
                     ScopedSymbol{source.scopeId,
                                  connection->second.getFormalSymbolAttr()},
                     source)
                 .second) {
          connection->second.emitOpError(
              "duplicate ref formal symbol in one instance");
          invalid = true;
        }
        if (SymbolRefAttr internal = connection->second.getInternalSymbolAttr())
          if (!refMembersByInternalSymbol
                   .try_emplace(ScopedSymbol{source.scopeId, internal}, source)
                   .second) {
            connection->second.emitOpError(
                "duplicate ref internal symbol in one instance");
            invalid = true;
          }
      }
      ++ordinal;
    }
  }
  llvm::DenseMap<std::tuple<uint64_t, Attribute, uint32_t>, uint64_t>
      nextRefMemberRelationOrdinal;
  auto emitRefMemberPortRelation = [&](const RefMemberSource &source,
                                       uint32_t selector, uint64_t portId,
                                       Location location) {
    auto key =
        std::make_tuple(source.scopeId, Attribute(source.member), selector);
    uint64_t ordinal = nextRefMemberRelationOrdinal[key]++;
    sim::VPIObjectRefAttr target = sim::VPIObjectRefAttr::get(
        builder.getContext(), sim::VPIObjectRefKind::Port,
        builder.getI64IntegerAttr(portId));
    sim::SimVPIDefinitionMemberInstanceRelationOp::create(
        builder, location, source.scopeId, source.member.getValue(), selector,
        sim::VPIRelationMode::Iterate, ordinal, target);
  };

  uint64_t nextPortId = 0;
  SmallVector<std::pair<sim::SimPortDeclOp, semantic::SVPortConnectionOp>>
      declaredNetPorts, declaredPorts;
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
    uint64_t portId = nextPortId++;
    auto declaration = sim::SimPortDeclOp::create(
        builder, getSemanticLocation(connection), portId, *portScopeId,
        source->second.id, source->second.kind == DescriptorInfo::Kind::Net,
        source->second.viewOffset, source->second.type, direction,
        connection.getFormalOrdinal(), builder.getStringAttr(portHierarchy),
        connection.getFormalName()
            ? builder.getStringAttr(*connection.getFormalName())
            : StringAttr{},
        *formalVPIType);
    declaredPorts.emplace_back(declaration, connection);
    if (source->second.kind == DescriptorInfo::Kind::Net)
      declaredNetPorts.emplace_back(declaration, connection);
    cov::mark<cov::SourceAuthored>(declaration);
    if (retainCoverageSourceTypes)
      cov::set<cov::SourceType>(declaration,
                                TypeAttr::get(connection.getFormalType()));
    retainVPISourceTypeIdentity(connection, declaration, *formalVPIType);

    if (auto own = refMembersByFormalSymbol.find(
            ScopedSymbol{*portScopeId, connection.getFormalSymbolAttr()});
        own != refMembersByFormalSymbol.end())
      emitRefMemberPortRelation(own->second,
                                static_cast<uint32_t>(VPIKind::Port), portId,
                                getSemanticLocation(connection));

    Operation *actual = getPortActualLValue(connection);
    SymbolRefAttr referenced =
        actual ? actual->getAttrOfType<SymbolRefAttr>("referenced_symbol")
               : SymbolRefAttr{};
    if (referenced) {
      auto scope = scopeDeclarationsById.find(*portScopeId);
      std::optional<uint64_t> ancestor = scope == scopeDeclarationsById.end()
                                             ? std::nullopt
                                             : scope->second.getParent();
      while (ancestor) {
        auto high = refMembersByInternalSymbol.find(
            ScopedSymbol{*ancestor, referenced});
        if (high != refMembersByInternalSymbol.end()) {
          emitRefMemberPortRelation(
              high->second,
              static_cast<uint32_t>(reflection::VPIRelationKind::PortInstRel),
              portId, getSemanticLocation(connection));
          break;
        }
        auto parent = scopeDeclarationsById.find(*ancestor);
        ancestor = parent == scopeDeclarationsById.end()
                       ? std::nullopt
                       : parent->second.getParent();
      }
    }
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

  // IEEE 1800-2023 37.14: port connections name the declared objects on
  // each side of the hierarchy, even when 37.16's simulated net is shared.
  for (auto [port, connection] : declaredNetPorts) {
    auto low =
        declaredNets.find(connection.getInternalPath().value_or(StringRef{}));
    if (low == declaredNets.end())
      continue;
    bool declaredLow =
        low->second.reference.getKind() == sim::VPIObjectRefKind::NetIdentity;
    auto source = sim::VPIObjectRefAttr::get(
        builder.getContext(), sim::VPIObjectRefKind::Port, port.getIdAttr());
    if (declaredLow)
      sim::SimVPIRelationDeclOp::create(
          builder, getSemanticLocation(connection), source,
          static_cast<uint32_t>(reflection::VPIRelationKind::LowConnRel),
          sim::VPIRelationMode::Handle, 0, low->second.reference);
  }

  struct ReverseRelation {
    sim::VPIObjectRefAttr source;
    uint32_t selector;
    sim::VPIObjectRefAttr target;
    Location location;
  };
  SmallVector<ReverseRelation> reverseRelations;

  // Both variable and net ports retain the exact higher-side expression.
  // Electrical collapse does not decide the identity of the declaration that
  // appeared in source; named variables and independently resolved nets are
  // equally valid whole-object connections (37.14).
  VPIExpressionResolver portExpressions(design, descriptors);
  const auto *highConnection = reflection::findVPITraversal(
      static_cast<uint32_t>(VPIKind::Port),
      static_cast<uint32_t>(reflection::VPIRelationKind::HighConnRel),
      reflection::VPITraversalMode::Handle);
  for (auto [port, connection] : declaredPorts) {
    Operation *actual = getPortActualLValue(connection);
    if (!actual)
      continue;
    auto high = portExpressions.resolve(actual);
    if (!high || !highConnection ||
        !reflection::vpiObjectSetContains(highConnection->targets, high->kind))
      continue;
    auto source = sim::VPIObjectRefAttr::get(
        builder.getContext(), sim::VPIObjectRefKind::Port, port.getIdAttr());
    sim::SimVPIRelationDeclOp::create(
        builder, getSemanticLocation(connection), source,
        static_cast<uint32_t>(reflection::VPIRelationKind::HighConnRel),
        sim::VPIRelationMode::Handle, 0, high->reference);
    for (auto selector : {reflection::VPIRelationKind::PortInstRel,
                          reflection::VPIRelationKind::UseRel}) {
      auto *edge = reflection::findVPITraversal(
          high->kind, static_cast<uint32_t>(selector),
          reflection::VPITraversalMode::Iterate);
      if (!edge || !reflection::vpiObjectSetContains(
                       edge->targets, static_cast<uint32_t>(VPIKind::Port)))
        continue;
      reverseRelations.push_back({high->reference,
                                  static_cast<uint32_t>(selector), source,
                                  getSemanticLocation(connection)});
    }
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
  auto directEndpointForPath =
      [&](StringRef path) -> std::optional<DirectEndpoint> {
    auto found = descriptors.find(path);
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
      auto declared = declaredNets.find(path);
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
  auto directEndpoint =
      [&](Operation *expression) -> std::optional<DirectEndpoint> {
    FailureOr<StaticStorageView> view = getStaticStorageView(expression);
    if (failed(view) || !view->identity || view->offset != 0 ||
        view->packedOffset != 0 || !view->indices.empty() ||
        view->rootType != view->viewType)
      return std::nullopt;
    return directEndpointForPath(view->path);
  };

  // Emit only exact whole formal objects here. Ref/interface selections use
  // the dedicated compact view/ref-object layer added in the following
  // chunk; never collapse them onto an unrelated backing root.
  const auto *memberExprEdge = reflection::findVPITraversal(
      static_cast<uint32_t>(VPIKind::IODecl),
      static_cast<uint32_t>(reflection::VPIRelationKind::ExprRel),
      reflection::VPITraversalMode::Handle);
  const auto *refActualEdge = reflection::findVPITraversal(
      static_cast<uint32_t>(VPIKind::RefObj),
      static_cast<uint32_t>(reflection::VPIRelationKind::ActualRel),
      reflection::VPITraversalMode::Handle);
  for (DefinitionInstancePlan &instance : definitionInstancePlans) {
    DefinitionMemberPlan &plan =
        definitionMemberPlans[instance.definition.getValue()];
    uint32_t ordinal = 0;
    for (Operation &child : instance.body.getBody().front()) {
      if (!isa<semantic::SVPortSymbolOp, semantic::SVInterfacePortSymbolOp>(
              child) ||
          getDebugName(&child).empty())
        continue;
      if (ordinal >= plan.memberSymbols.size()) {
        emitError(getSemanticLocation(&child))
            << "VPI definition member endpoint ordinal is out of range";
        invalid = true;
        break;
      }
      auto connection =
          connectionsByFormalSymbol.find(getSemanticSymbolReference(&child));
      if (connection == connectionsByFormalSymbol.end())
        connection = connectionsByFormalSymbol.find(
            getSemanticSymbolReference(&child, true));
      if (connection == connectionsByFormalSymbol.end()) {
        ++ordinal;
        continue;
      }
      sim::VPIIODirection direction = plan.signatures[ordinal].direction;
      // A ref declaration denotes the caller's ultimate actual object, not
      // the scope-local formal alias.  Keeping that one instance-specific
      // word beside the shared definition member lets the runtime synthesize
      // the RefObj identity without materializing another object record.
      Operation *internalExpression =
          getSingleRegionRoot(direction == sim::VPIIODirection::Ref
                                  ? connection->second.getActual()
                                  : connection->second.getInternal());
      std::optional<DirectEndpoint> endpoint;
      if (internalExpression) {
        endpoint = directEndpoint(internalExpression);
      } else if (direction != sim::VPIIODirection::Ref) {
        StringRef internal = connection->second.getInternalPath().value_or(
            connection->second.getFormalPath());
        endpoint = directEndpointForPath(internal);
      }
      bool virtualInterface =
          endpoint && endpoint->vpiKind ==
                          static_cast<uint32_t>(VPIKind::VirtualInterfaceVar);
      bool compatibleDirection =
          virtualInterface ? direction == sim::VPIIODirection::Undefined ||
                                 direction == sim::VPIIODirection::Ref
                           : direction == sim::VPIIODirection::Input ||
                                 direction == sim::VPIIODirection::Output ||
                                 direction == sim::VPIIODirection::InOut ||
                                 direction == sim::VPIIODirection::Ref;
      bool compatibleScope =
          direction == sim::VPIIODirection::Ref ||
          (endpoint && endpoint->scopeId == instance.scope.getId());
      bool legalEndpoint =
          endpoint && memberExprEdge &&
          (direction == sim::VPIIODirection::Ref
               ? endpoint->descriptor->kind == DescriptorInfo::Kind::Storage &&
                     refActualEdge &&
                     reflection::vpiObjectSetContains(
                         memberExprEdge->targets,
                         static_cast<uint32_t>(VPIKind::RefObj)) &&
                     reflection::vpiObjectSetContains(refActualEdge->targets,
                                                      endpoint->vpiKind)
               : reflection::vpiObjectSetContains(memberExprEdge->targets,
                                                  endpoint->vpiKind));
      if (legalEndpoint && compatibleScope && compatibleDirection)
        sim::SimVPIDefinitionMemberInstanceBindingOp::create(
            builder, getSemanticLocation(&child), instance.scope.getIdAttr(),
            plan.memberSymbols[ordinal], endpoint->reference);
      ++ordinal;
    }
  }
  if (invalid)
    return failure();
  auto statementReference = [&](uint64_t id) {
    return sim::VPIObjectRefAttr::get(builder.getContext(),
                                      sim::VPIObjectRefKind::Statement,
                                      builder.getI64IntegerAttr(id));
  };
  auto selectorValue = [](reflection::VPIRelationKind relation) {
    return static_cast<uint32_t>(relation);
  };
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
  // A terminal is an occurrence, even when two terminals name the same net.
  // Preserve the source order and the assignment wrapper's lvalue identity;
  // selected and compound expressions require their own occurrence graph.
  for (Operation *source : anchorSources) {
    auto primitive = dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(source);
    if (!primitive)
      continue;
    auto parent = anchorDeclarations.lookup(source);
    if (!parent)
      continue;
    auto primitiveModule = moduleInstanceScope(parent.getEnclosingScopeId());
    auto terminalSources = getChildren(source);
    for (auto [index, terminalSource] : llvm::enumerate(terminalSources)) {
      uint32_t direction =
          primitiveTerminalDirection(primitive, index, terminalSource);
      Operation *expression = terminalSource;
      if (isa<semantic::SVAssignmentExpressionOp>(terminalSource)) {
        auto children = getChildren(terminalSource);
        if (children.empty())
          continue;
        expression = children.front();
      }
      std::string symbol =
          "__obelisk_vpi_terminal_" + std::to_string(nextSyntheticInventoryId);
      auto terminal = sim::SimVPIObjectAnchorOp::create(
          builder, getSemanticLocation(terminalSource),
          builder.getStringAttr(symbol), nextSyntheticInventoryId++,
          static_cast<uint32_t>(VPIKind::PrimTerm),
          parent.getEnclosingScopeId(),
          FlatSymbolRefAttr::get(parent.getSymNameAttr()), index,
          builder.getStringAttr(""), builder.getStringAttr(""), UnitAttr{},
          sim::VPIObjectBackingAttr{}, DenseI64ArrayAttr{}, DenseI64ArrayAttr{},
          DenseI64ArrayAttr{}, DenseI64ArrayAttr{}, IntegerAttr{},
          sim::VPITypeSemanticsAttr{}, sim::FrozenConstantAttr{}, UnitAttr{},
          FlatSymbolRefAttr{}, UnitAttr{}, StringAttr{}, UnitAttr{});
      SmallVector<Attribute> properties;
      for (auto [selector, value] :
           {std::pair<uint32_t, uint32_t>{20, direction}})
        properties.push_back(sim::VPIPropertyAttr::get(
            builder.getContext(), builder.getI32IntegerAttr(selector),
            builder.getI32IntegerAttr(value)));
      terminal->setAttr(
          "vpi_properties",
          sim::VPIPropertySetAttr::get(builder.getContext(),
                                       builder.getArrayAttr(properties)));
      auto terminalRef = sim::VPIObjectRefAttr::get(
          builder.getContext(), sim::VPIObjectRefKind::Symbol, {},
          FlatSymbolRefAttr::get(terminal.getSymNameAttr()));
      auto endpoint = directEndpoint(expression);
      if (!endpoint)
        continue;
      sim::SimVPIRelationDeclOp::create(
          builder, getSemanticLocation(terminalSource), terminalRef,
          selectorValue(reflection::VPIRelationKind::ExprRel),
          sim::VPIRelationMode::Handle, 0, endpoint->reference);
      auto reverse = [&](const DirectEndpoint &spelling, uint32_t selector) {
        const auto *edge = reflection::findVPITraversal(
            spelling.vpiKind, selector, reflection::VPITraversalMode::Iterate);
        if (edge &&
            reflection::vpiObjectSetContains(
                edge->targets, static_cast<uint32_t>(VPIKind::PrimTerm)))
          reverseRelations.push_back({spelling.reference, selector, terminalRef,
                                      getSemanticLocation(terminalSource)});
      };
      forPhysicalNetSpelling(*endpoint, [&](const DirectEndpoint &spelling) {
        auto width = sim::getPackedWidth(spelling.type);
        if (spelling.descriptor->kind == DescriptorInfo::Kind::Net &&
            (!width || *width != 1))
          return; // 37.16: terminal connectivity is scalar/bit-select only.
        reverse(spelling, static_cast<uint32_t>(VPIKind::PrimTerm));
        bool local = primitiveModule &&
                     moduleInstanceScope(spelling.scopeId) == primitiveModule;
        if (direction != 1) {
          reverse(spelling,
                  selectorValue(reflection::VPIRelationKind::DriverRel));
          if (local && spelling.descriptor->kind == DescriptorInfo::Kind::Net)
            reverse(spelling,
                    selectorValue(reflection::VPIRelationKind::LocalDriverRel));
        }
        if (direction != 2) {
          reverse(spelling,
                  selectorValue(reflection::VPIRelationKind::LoadRel));
          if (local && spelling.descriptor->kind == DescriptorInfo::Kind::Net)
            reverse(spelling,
                    selectorValue(reflection::VPIRelationKind::LocalLoadRel));
        }
      });
      reverse(*endpoint, selectorValue(reflection::VPIRelationKind::UseRel));
    }
  }
  llvm::sort(reverseRelations, [](const ReverseRelation &left,
                                  const ReverseRelation &right) {
    auto targetKey = [](sim::VPIObjectRefAttr reference) {
      return std::tuple(
          reference.getKind(),
          reference.getId() ? reference.getId().getValue().getZExtValue() : 0,
          reference.getSymbol() ? reference.getSymbol().getValue()
                                : StringRef{});
    };
    return std::make_tuple(targetKey(left.source), left.selector,
                           targetKey(left.target)) <
           std::make_tuple(targetKey(right.source), right.selector,
                           targetKey(right.target));
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
  // Freeze procedural declarations with the same typedef remapping as
  // static inventory. Their exact callable/statement scopes are assigned
  // after statement inventory; no display-name reconstruction is needed.
  if (preserveVPIMethodBindings) {
    llvm::DenseSet<Attribute> materializedAliases = pendingTypespecSymbols;
    for (auto declaration : design.getOps<sim::SimVPITypespecDeclOp>())
      materializedAliases.insert(
          FlatSymbolRefAttr::get(declaration.getSymNameAttr()));
    // A process's block symbols are siblings of its executable declaration.
    // Resolve actual declaration statements instead of requiring the variable
    // symbol itself to be physically nested in the code-unit operation.
    llvm::DenseSet<Operation *> proceduralDeclarations;
    SymbolTableCollection declarationSymbols;
    semanticRoot->walk([&](semantic::SVVariableDeclStatementOp declaration) {
      if (!isNestedInCodeUnit(declaration))
        return;
      if (auto variable = dyn_cast_or_null<semantic::SVVariableSymbolOp>(
              declarationSymbols.lookupSymbolIn(
                  module, declaration.getReferencedSymbolAttr())))
        proceduralDeclarations.insert(variable);
    });
    semanticRoot->walk([&](Operation *source) {
      if (!isa<semantic::SVVariableSymbolOp,
               semantic::SVFormalArgumentSymbolOp>(source) ||
          (!isNestedInCodeUnit(source) &&
           !proceduralDeclarations.contains(source)))
        return;
      auto type = source->getAttrOfType<TypeAttr>("semantic_type");
      if (!type)
        return;
      auto converted =
          makeVPITypeSemantics(type.getValue(), getSemanticLocation(source),
                               typedefLayersFor(source), source);
      if (failed(converted)) {
        invalid = true;
        return;
      }
      bool complete = true;
      AttrTypeWalker aliases;
      aliases.addWalk([&](sim::VPITypeSemanticsAttr current) {
        if (auto names = current.getTypedefAliases())
          for (Attribute name : names)
            complete &= materializedAliases.contains(name);
        if (current.getKind() == sim::VPITypeKind::Enum &&
            !current.getTypedefAliases())
          complete &= enumTypespecsByIdentity.count(
                          getEnumIdentity(source, current)) != 0;
      });
      aliases.walk(*converted);
      // Block-owned types are frozen here and assigned their exact statement
      // owner after statement inventory. No alias layer is erased.
      if (complete)
        sim::set<sim::metadata::VpiDeclaredType>(source, *converted);
    });
  }
  // Gather first, then emit: creating declarations inside the IR walk would
  // invalidate its operation iterator. AttrTypeWalker memoizes the attribute
  // DAG, so shared nested semantic types are visited once in deterministic
  // declaration/attribute/child order.
  SmallVector<std::pair<sim::VPITypeSemanticsAttr, Operation *>>
      observedBuiltins;
  Operation *observedOwner = nullptr;
  AttrTypeWalker semanticWalker;
  semanticWalker.addWalk([&](sim::VPITypeSemanticsAttr type) {
    if (isBuiltinVPIClass(type.getKind()))
      observedBuiltins.emplace_back(type, observedOwner);
  });
  design.walk([&](Operation *operation) {
    observedOwner = operation;
    for (NamedAttribute attribute : operation->getAttrs())
      semanticWalker.walk(attribute.getValue());
  });
  semanticRoot->walk([&](Operation *source) {
    if (auto declared = sim::get<sim::metadata::VpiDeclaredType>(source)) {
      observedOwner = source;
      semanticWalker.walk(declared);
    }
  });
  for (auto [semantic, owner] : observedBuiltins)
    internBuiltinTypespec(semantic, owner, /*requireSource=*/false);
  if (invalid)
    return failure();
  return descriptors;
}

} // namespace obelisk::simlowering
