//===- PrepareTopology.cpp - Static design topology analysis -------------===//
//
// Resolves semantic port views and aliases before descriptor and net topology
// materialization.
//
//===----------------------------------------------------------------------===//

#include "PrepareTopology.h"

#include "Detail.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringSet.h"

#include <functional>
#include <limits>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

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
  if (children.size() == 2 &&
      isa<semantic::SVEmptyArgumentExpressionOp>(children[1]))
    return children.front();
  return actual;
}

FailureOr<PreparedPortAliases>
analyzePortAliases(semantic::SVRootSymbolOp semanticRoot) {
  PreparedPortAliases result;
  bool invalid = false;
  semanticRoot->walk([&](semantic::SVPortConnectionOp connection) {
    if (isCompileTimeOnlyInstanceMember(connection))
      return;
    result.connections.push_back(connection);
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
    ModuleOp module, semantic::SVRootSymbolOp semanticRoot,
    const PreparedPortAliases &portAliases,
    const PreparedScopeDeclarations &scopes, OpBuilder &builder) {
  llvm::StringMap<DescriptorInfo> descriptors;
  uint64_t nextStorageId = 0;
  uint64_t nextNetId = 0;
  uint64_t nextEventId = 0;
  bool invalid = false;
  SmallVector<Operation *> designObjects;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (isCompileTimeOnlyInstanceMember(op))
      return;
    auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op);
    auto classProperty = dyn_cast<semantic::SVClassPropertySymbolOp>(op);
    bool staticVariable = variable && variable.getLifetime() ==
                                          semantic::SVVariableLifetime::Static;
    bool staticClassProperty =
        classProperty &&
        classProperty.getLifetime() == semantic::SVVariableLifetime::Static;
    if (isNestedInCodeUnit(op) && !staticVariable && !isStaticFormal(op))
      return;
    bool storage = (isa<semantic::SVVariableSymbolOp>(op) &&
                    !isAutomaticLocalSymbol(op)) ||
                   isStaticFormal(op) || staticClassProperty;
    if (storage || isa<semantic::SVNetSymbolOp>(op) ||
        op->hasAttr(sequenceEndpointEventAttrName))
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
    if (op->hasAttr(sequenceEndpointEventAttrName)) {
      Type type = sim::EventType::get(builder.getContext());
      uint64_t id = nextEventId++;
      descriptors[path] = {DescriptorInfo::Kind::Event, id, scopes.lookup(op),
                           type, sim::NetResolutionKind::Wire};
      descriptors[path].rootType = type;
      return;
    }
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type)) {
      invalid = true;
      return;
    }
    uint64_t scopeId = scopes.lookup(op);
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
      auto declaration = sim::SimStorageDeclOp::create(
          builder, getSemanticLocation(op), id, scopeId, *type, lifetime,
          hierarchy, debug, sim::ComputeObservabilityKindAttr{});
      if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(op->getParentOp());
          body && body->hasAttr("virtual_interface_identity") &&
          !isCompileTimeOnlyInstanceMember(body))
        declaration->setAttr("obelisk_sim.virtual_interface_member", debug);
      return;
    }

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
      emitError(getSemanticLocation(op)) << "net strengths are not supported: "
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
    auto declaration = sim::SimNetDeclOp::create(
        builder, getSemanticLocation(op), id, scopeId, *type,
        sim::Lifetime::Design, hierarchy, debug,
        sim::ComputeObservabilityKindAttr{}, resolution, UnitAttr{});
    if (auto body = dyn_cast<semantic::SVInstanceBodySymbolOp>(op->getParentOp());
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
  if (invalid)
    return failure();
  return descriptors;
}

} // namespace obelisk::simlowering
