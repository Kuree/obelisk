//===- SimulationVPIExpressions.cpp - Exact source expression inventory
//----===//

#include "SimulationVerifiers.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Dialect/Simulation/SimulationVPI.h"
#include "talos/Dialect/Simulation/SimulationVPIConstantValues.h"
#include "talos/Reflection/VPIObjectModel.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include <limits>
#include <map>
#include <optional>
#include <tuple>

using namespace mlir;

namespace talos::sim {
namespace {
using Kind = reflection::VPIObjectKind;
using Relation = reflection::VPIRelationKind;
constexpr uint32_t concatOp = 33, multiConcatOp = 34;

bool assignmentMember(SimVPIDefinitionMemberDeclOp member) {
  return member.getVpiKind() == uint32_t(Kind::ParamAssign);
}
uint32_t sourceMemberKind(SimVPIDefinitionMemberDeclOp member) {
  return assignmentMember(member)
             ? uint32_t(member.getParamAssignType() ? Kind::TypeParameter
                                                    : Kind::Parameter)
             : member.getVpiKind();
}
bool originalSourceAvailable(SimVPIDefinitionMemberDeclOp member) {
  return assignmentMember(member) ? bool(member.getHasOriginalRhs())
                                  : bool(member.getHasOriginalDefault());
}
bool originalSourceUnavailable(SimVPIDefinitionMemberDeclOp member) {
  return assignmentMember(member) ? bool(member.getUnavailableRhs())
                                  : bool(member.getUnavailableDefault());
}
uint32_t originalSourceSelector(SimVPIDefinitionMemberDeclOp member) {
  return uint32_t(assignmentMember(member) ? Relation::RhsRel
                                           : Relation::ExprRel);
}

bool isIntegral(VPITypeSemanticsAttr semantic) {
  if (!semantic)
    return false;
  while (semantic.getKind() == VPITypeKind::PackedArray) {
    if (semantic.getChildren().size() != 1)
      return false;
    semantic = cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]);
  }
  switch (semantic.getKind()) {
  case VPITypeKind::GenericIntegral:
  case VPITypeKind::Bit:
  case VPITypeKind::Logic:
  case VPITypeKind::Reg:
  case VPITypeKind::Byte:
  case VPITypeKind::ShortInt:
  case VPITypeKind::Int:
  case VPITypeKind::LongInt:
  case VPITypeKind::Integer:
  case VPITypeKind::Time:
    return true;
  default:
    return false;
  }
}

std::optional<uint64_t> integralWidth(VPITypeSemanticsAttr semantic) {
  if (!isIntegral(semantic))
    return std::nullopt;
  if (auto width = semantic.getBitWidth(); width && !width.getValue().isZero())
    return width.getValue().getZExtValue();
  uint64_t multiplier = 1;
  while (semantic.getKind() == VPITypeKind::PackedArray) {
    auto range = semantic.getRange().asArrayRef();
    if (range.size() != 2)
      return std::nullopt;
    uint64_t distance = range[0] >= range[1]
                            ? uint64_t(range[0]) - uint64_t(range[1])
                            : uint64_t(range[1]) - uint64_t(range[0]);
    if (distance == UINT64_MAX || multiplier > UINT64_MAX / (distance + 1))
      return std::nullopt;
    multiplier *= distance + 1;
    semantic = cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]);
  }
  auto range = semantic.getRange().asArrayRef();
  uint64_t width = 0;
  if (range.size() == 2) {
    uint64_t distance = range[0] >= range[1]
                            ? uint64_t(range[0]) - uint64_t(range[1])
                            : uint64_t(range[1]) - uint64_t(range[0]);
    if (distance == UINT64_MAX)
      return std::nullopt;
    width = distance + 1;
  } else if (semantic.getKind() == VPITypeKind::Bit ||
             semantic.getKind() == VPITypeKind::Logic ||
             semantic.getKind() == VPITypeKind::Reg)
    width = 1;
  else {
    switch (semantic.getKind()) {
    case VPITypeKind::Byte:
      width = 8;
      break;
    case VPITypeKind::ShortInt:
      width = 16;
      break;
    case VPITypeKind::Int:
    case VPITypeKind::Integer:
      width = 32;
      break;
    case VPITypeKind::LongInt:
    case VPITypeKind::Time:
      width = 64;
      break;
    default:
      return std::nullopt;
    }
  }
  if (multiplier > UINT64_MAX / width)
    return std::nullopt;
  return multiplier * width;
}

struct Endpoint {
  Operation *operation = nullptr;
  uint32_t kind = 0;
  uint64_t scope = 0;
  Type type;
  VPITypeSemanticsAttr semantic;
  FrozenConstantAttr frozen;
  uint64_t width = 0;
  Endpoint() = default;
  Endpoint(Operation *operation, uint32_t kind, uint64_t scope, Type type = {},
           VPITypeSemanticsAttr semantic = {}, FrozenConstantAttr frozen = {})
      : operation(operation), kind(kind), scope(scope), type(type),
        semantic(semantic), frozen(frozen),
        width(type ? getPackedWidth(type).value_or(0)
                   : integralWidth(semantic).value_or(0)) {}
};
std::optional<VPIConstantValue> constantLeaf(const Endpoint &endpoint) {
  if (!endpoint.frozen || (endpoint.kind != uint32_t(Kind::Constant) &&
                           endpoint.kind != uint32_t(Kind::Parameter) &&
                           endpoint.kind != uint32_t(Kind::SpecParam) &&
                           endpoint.kind != uint32_t(Kind::EnumConst)))
    return std::nullopt;
  auto planes = dyn_cast<ArrayAttr>(endpoint.frozen.getValue());
  if (!planes || planes.size() != 2)
    return std::nullopt;
  auto bits = dyn_cast<IntegerAttr>(planes[0]);
  auto unknown = dyn_cast<IntegerAttr>(planes[1]);
  if (!bits || !unknown || bits.getValue().getBitWidth() != endpoint.width ||
      unknown.getValue().getBitWidth() != endpoint.width)
    return std::nullopt;
  return VPIConstantValue{bits.getValue(), unknown.getValue()};
}
// Definition defaults are source occurrences, not an effective specialization.
// Keep their proof separate from value-typed instance expressions: the only
// untyped leaves are exact preceding Parameter declarations in this definition.
LogicalResult verifyDefinitionDefaults(SimDesignOp design) {
  SymbolTableCollection symbols;
  auto lookup = [&](VPIObjectRefAttr reference) -> Operation * {
    return reference.getKind() == VPIObjectRefKind::Symbol
               ? symbols.lookupSymbolIn(design, reference.getSymbol())
               : nullptr;
  };
  DenseMap<Operation *, SmallVector<SimVPIRelationDeclOp>> incoming, outgoing;
  DenseMap<Operation *, SimVPIDefinitionMemberDeclOp> roots;
  DenseSet<Operation *> concreteDefaults;
  for (auto source : design.getOps<SimVPIDefinitionTypeDefaultOp>()) {
    auto member = symbols.lookupSymbolIn<SimVPIDefinitionMemberDeclOp>(
        design, source.getMemberAttr());
    if (!member || !concreteDefaults.insert(member).second)
      return source.emitOpError("duplicates a concrete type default root");
  }
  SmallVector<SimVPIExpressionDeclOp> expressions;
  for (auto expression : design.getOps<SimVPIExpressionDeclOp>())
    if (expression.getDefinitionAttr())
      expressions.push_back(expression);
  for (auto relation : design.getOps<SimVPIRelationDeclOp>()) {
    Operation *source = lookup(relation.getSource());
    Operation *target = lookup(relation.getTarget());
    auto member = dyn_cast_or_null<SimVPIDefinitionMemberDeclOp>(source);
    auto computed = dyn_cast_or_null<SimVPIExpressionDeclOp>(source);
    auto targetComputed = dyn_cast_or_null<SimVPIExpressionDeclOp>(target);
    bool touchesDefinition =
        member || isa_and_nonnull<SimVPIDefinitionMemberDeclOp>(target) ||
        (computed && computed.getDefinitionAttr()) ||
        (targetComputed && targetComputed.getDefinitionAttr());
    if (!touchesDefinition)
      continue;
    if ((!member && (!computed || !computed.getDefinitionAttr())) ||
        (!isa_and_nonnull<SimVPIDefinitionMemberDeclOp>(target) &&
         (!targetComputed || !targetComputed.getDefinitionAttr())))
      return relation.emitOpError(
          "shared declaration symbols require an exact definition default");
    if (auto identifier = get<metadata::VpiExpressionIdentifier>(relation)) {
      auto leaf = dyn_cast<SimVPIDefinitionMemberDeclOp>(target);
      if (!computed || !leaf || identifier != leaf.getMemberNameAttr())
        return relation.emitOpError(
            "default identifier must match its exact declaration name");
    }
    incoming[target].push_back(relation);
    outgoing[source].push_back(relation);
    if (!member)
      continue;
    if ((sourceMemberKind(member) != uint32_t(Kind::Parameter) &&
         sourceMemberKind(member) != uint32_t(Kind::TypeParameter)) ||
        originalSourceUnavailable(member) || !originalSourceAvailable(member) ||
        relation.getSelector() != originalSourceSelector(member) ||
        relation.getMode() != VPIRelationMode::Handle || relation.getOrdinal())
      return relation.emitOpError(
          "definition default requires one available parameter Expr slot");
    if (outgoing[source].size() != 1)
      return relation.emitOpError("duplicates a definition default root");
    if (targetComputed) {
      if (sourceMemberKind(member) != uint32_t(Kind::Parameter))
        return relation.emitOpError(
            "type parameter default cannot be a value expression");
      roots[target] = member;
    } else {
      auto leaf = cast<SimVPIDefinitionMemberDeclOp>(target);
      if (leaf.getVpiKind() != sourceMemberKind(member) ||
          leaf.getDefinitionAttr() != member.getDefinitionAttr() ||
          leaf.getOrdinal() >= member.getOrdinal())
        return relation.emitOpError("default leaf must be an exact preceding "
                                    "parameter of the same kind");
    }
  }
  for (auto member : design.getOps<SimVPIDefinitionMemberDeclOp>())
    if (outgoing[member].size() + concreteDefaults.count(member) > 1 ||
        originalSourceAvailable(member) !=
            (outgoing[member].size() + concreteDefaults.count(member) == 1))
      return member.emitOpError(
          "original default fact requires exactly one source root");
  // Resolve the unique declaration root iteratively. Ownership remains a tree;
  // repeated references to declaration leaves do not participate in this walk.
  for (auto expression : expressions) {
    SmallVector<Operation *> path;
    DenseSet<Operation *> visiting;
    Operation *cursor = expression;
    while (!roots.count(cursor)) {
      if (!visiting.insert(cursor).second)
        return expression.emitOpError("definition default ownership is cyclic");
      path.push_back(cursor);
      auto current = dyn_cast<SimVPIExpressionDeclOp>(cursor);
      if (!current || !current.getDefinitionAttr() ||
          incoming[cursor].size() != 1)
        return expression.emitOpError(
            "definition default occurrence requires one exact owner");
      cursor = lookup(incoming[cursor].front().getSource());
    }
    auto owner = roots.lookup(cursor);
    for (Operation *node : path)
      roots[node] = owner;
  }
  for (auto expression : expressions) {
    auto declaration = roots.lookup(expression);
    if (!declaration || incoming[expression].size() != 1 ||
        expression.getDefinitionAttr() != declaration.getDefinitionAttr())
      return expression.emitOpError(
          "definition occurrence must match its declaring parameter");
    auto relation = incoming[expression].front();
    auto self = VPIObjectRefAttr::get(design.getContext(),
                                      FlatSymbolRefAttr::get(expression));
    if (relation.getTarget() != self ||
        relation.getSource() != expression.getOwner() ||
        relation.getSelector() != expression.getSelector() ||
        relation.getMode() != expression.getMode() ||
        relation.getOrdinal() != expression.getOrdinal())
      return expression.emitOpError(
          "definition occurrence requires its matching exact owner slot");
    bool literal = expression.getVpiKind() == uint32_t(Kind::Constant);
    if (!literal &&
        (!expression.getSourceOnly() ||
         (!reflection::findVPISourceOperator(*expression.getOpType()) &&
          *expression.getOpType() != 33 && *expression.getOpType() != 34)))
      return expression.emitOpError(
          "definition default requires an unbound value expression operator");
    auto children = outgoing[expression];
    llvm::sort(children, [](auto left, auto right) {
      return left.getOrdinal() < right.getOrdinal();
    });
    if ((literal && !children.empty()) ||
        (!literal && !reflection::validVPISourceOnlyArity(
                         *expression.getOpType(), children.size())))
      return expression.emitOpError(
          "definition default operator requires its exact ordered operands");
    for (auto [ordinal, child] : llvm::enumerate(children)) {
      if (child.getSelector() != uint32_t(Relation::OperandRel) ||
          child.getMode() != VPIRelationMode::Iterate ||
          child.getOrdinal() != ordinal)
        return child.emitOpError(
            "definition operands require dense source order");
      Operation *target = lookup(child.getTarget());
      if (auto leaf = dyn_cast<SimVPIDefinitionMemberDeclOp>(target)) {
        if (leaf.getVpiKind() != uint32_t(Kind::Parameter) ||
            leaf.getDefinitionAttr() != declaration.getDefinitionAttr() ||
            leaf.getOrdinal() >= declaration.getOrdinal())
          return child.emitOpError(
              "default leaf must be an exact preceding value parameter");
      } else if (auto node = dyn_cast<SimVPIExpressionDeclOp>(target)) {
        if (node.getDefinitionAttr() != declaration.getDefinitionAttr() ||
            roots.lookup(node) != declaration)
          return child.emitOpError(
              "default child must belong to the exact declaration occurrence");
      } else
        return child.emitOpError("default operand has no source identity");
    }
  }
  return success();
}

} // namespace

LogicalResult SimVPIDefinitionTypeDefaultOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (!isVPIOriginalBuiltinTypeDefault(getOriginalType()))
    return emitOpError("requires an original anonymous builtin leaf typespec");
  if (auto location = getLoc()->findInstanceOf<FileLineColLoc>())
    if (location.getFilename().getValue().empty() ||
        location.getFilename().getValue().contains('\0') ||
        !location.getLine() || location.getLine() > INT32_MAX)
      return emitOpError("requires a valid original type source location");
  return success();
}

LogicalResult SimVPIDefinitionTypeDefaultOp::verifySymbolUses(
    SymbolTableCollection &symbols) {
  auto member = symbols.lookupNearestSymbolFrom<SimVPIDefinitionMemberDeclOp>(
      *this, getMemberAttr());
  if (!member || sourceMemberKind(member) != uint32_t(Kind::TypeParameter) ||
      !originalSourceAvailable(member) || originalSourceUnavailable(member))
    return emitOpError(
        "requires an available TypeParameter original default member");
  return success();
}

LogicalResult SimVPIExpressionDeclOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  for (auto [attribute, label] :
       {std::pair(getIdAttr(), "expression ID"),
        std::pair(getSelectorAttr(), "owner selector"),
        std::pair(getOrdinalAttr(), "owner ordinal")})
    if (attribute.getValue().isNegative())
      return emitOpError() << label << " must be nonnegative";
  if (getSelector() > UINT16_MAX || getOrdinal() > UINT32_MAX ||
      (getMode() == VPIRelationMode::Handle && getOrdinal() != 0))
    return emitOpError("owner relation slot is not encodable");
  if (bool(getScopeIdAttr()) == bool(getDefinitionAttr()) ||
      (getScopeIdAttr() && getScopeIdAttr().getValue().isNegative()))
    return emitOpError(
        "requires exactly one nonnegative scope or definition context");
  bool sourceOnly = getSourceOnly();
  if (sourceOnly) {
    if (getVpiKind() != uint32_t(Kind::Operation) || getTypeAttr() ||
        getVpiTypeAttr())
      return emitOpError(
          "source_only requires an Operation without result types");
  } else {
    if (!getTypeAttr() || !getVpiTypeAttr() || !isIntegral(getVpiTypeAttr()) ||
        !getPackedScalarType(getTypeAttr().getValue()) ||
        !getPackedWidth(getTypeAttr().getValue()).value_or(0))
      return emitOpError("requires an exact fixed integral result type");
    if (failed(verifyVPITypeSemantics([&] { return emitOpError(); },
                                      getTypeAttr().getValue(),
                                      getVpiTypeAttr())))
      return failure();
  }
  auto kind = static_cast<Kind>(getVpiKind());
  if (kind != Kind::Constant && kind != Kind::Operation &&
      kind != Kind::PartSelect && kind != Kind::IndexedPartSelect &&
      !reflection::isVPIBitSelection(getVpiKind()))
    return emitOpError("unsupported exact expression kind");
  if ((kind == Kind::Constant) != bool(getImmutableValueAttr()) ||
      (kind == Kind::Operation) != bool(getOpTypeAttr()) ||
      (kind == Kind::IndexedPartSelect) != bool(getIndexedDirectionAttr()))
    return emitOpError(
        "literal, operator and selection metadata must match kind");
  if (auto value = getImmutableValueAttr();
      value && (value.getType() != getTypeAttr().getValue() ||
                value.getIsSigned() != getVpiTypeAttr().getIsSigned()))
    return emitOpError("literal planes must match the exact result type");
  if (auto operation = getOpType();
      operation && !sourceOnly && *operation != concatOp &&
      *operation != multiConcatOp &&
      reflection::integralOperatorRule(*operation) ==
          reflection::VPIIntegralOperatorRule::Unsupported)
    return emitOpError("unsupported exact expression operator");
  if (auto direction = getIndexedDirection();
      direction && *direction != 1 && *direction != 2)
    return emitOpError(
        "indexed selection direction must be positive or negative");
  return success();
}

LogicalResult
SimVPIExpressionDeclOp::verifySymbolUses(SymbolTableCollection &symbols) {
  if (getDefinitionAttr() &&
      !symbols.lookupNearestSymbolFrom<SimVPIDefinitionDeclOp>(
          *this, getDefinitionAttr()))
    return emitOpError(
        "definition context must name an exact source definition");
  if (getOwner().getKind() == VPIObjectRefKind::Symbol &&
      !symbols.lookupNearestSymbolFrom(*this, getOwner().getSymbol()))
    return emitOpError("references an unknown expression owner");
  return success();
}

LogicalResult verifyVPIExpressionInventory(SimDesignOp design) {
  if (failed(verifyDefinitionDefaults(design)))
    return failure();
  SymbolTableCollection referenceSymbols;
  for (auto relation : design.getOps<SimVPIRelationDeclOp>()) {
    if (relation.getTarget().getKind() != VPIObjectRefKind::DefinitionMember ||
        relation.getMode() == VPIRelationMode::Handle)
      continue;
    auto source = relation.getSource();
    auto computed =
        source.getKind() == VPIObjectRefKind::Symbol
            ? referenceSymbols.lookupNearestSymbolFrom<SimVPIExpressionDeclOp>(
                  relation, source.getSymbol())
            : SimVPIExpressionDeclOp{};
    bool caseLabel =
        source.getKind() == VPIObjectRefKind::Statement &&
        relation.getSelector() == uint32_t(Relation::ExprRel) &&
        llvm::any_of(design.getOps<SimStatementDeclOp>(), [&](auto statement) {
          return statement.getIdAttr() == source.getId() &&
                 statement.getVpiKind() == uint32_t(Kind::CaseItem);
        });
    auto call =
        source.getKind() == VPIObjectRefKind::Symbol
            ? referenceSymbols.lookupNearestSymbolFrom<SimVPICallDeclOp>(
                  relation, source.getSymbol())
            : SimVPICallDeclOp{};
    bool callArgument =
        call &&
        (call.getVpiKind() == uint32_t(Kind::FuncCall) ||
         call.getVpiKind() == uint32_t(Kind::TaskCall)) &&
        relation.getSelector() == uint32_t(Relation::ArgumentRel);
    if (!computed && !caseLabel && !callArgument)
      return relation.emitOpError("contextual declaration root requires a "
                                  "singular, case-label or call-argument slot");
  }
  SmallVector<SimVPIExpressionDeclOp> expressions;
  DenseSet<uint64_t> allExpressionIDs;
  for (auto expression : design.getOps<SimVPIExpressionDeclOp>()) {
    if (!allExpressionIDs.insert(expression.getId()).second)
      return expression.emitOpError("duplicates an expression occurrence ID");
    if (!expression.getDefinitionAttr())
      expressions.push_back(expression);
  }
  if (expressions.empty()) {
    for (auto relation : design.getOps<SimVPIRelationDeclOp>())
      if (get<metadata::VpiExpressionIdentifier>(relation)) {
        auto reference = relation.getSource();
        auto source =
            reference.getKind() == VPIObjectRefKind::Symbol
                ? referenceSymbols
                      .lookupNearestSymbolFrom<SimVPIExpressionDeclOp>(
                          relation, reference.getSymbol())
                : SimVPIExpressionDeclOp{};
        if (!source || !source.getDefinitionAttr())
          return relation.emitOpError(
              "expression identifier requires a computed source");
      }
    return success();
  }
  Builder builder(design.getContext());
  DenseMap<Attribute, Endpoint> endpoints;
  DenseMap<uint64_t, SimScopeDeclOp> scopes;
  DenseSet<uint64_t> ids;
  auto numeric = [&](VPIObjectRefKind kind, IntegerAttr id) {
    return VPIObjectRefAttr::get(design.getContext(), kind, id);
  };
  auto symbolic = [&](StringAttr name) {
    return VPIObjectRefAttr::get(design.getContext(),
                                 FlatSymbolRefAttr::get(name));
  };
  for (Operation &operation : design.getBody().front()) {
    if (auto scope = dyn_cast<SimScopeDeclOp>(operation))
      scopes[scope.getId()] = scope;
    else if (auto statement = dyn_cast<SimStatementDeclOp>(operation))
      endpoints[numeric(VPIObjectRefKind::Statement, statement.getIdAttr())] = {
          statement, statement.getVpiKind(), statement.getScopeId()};
    else if (auto call = dyn_cast<SimVPICallDeclOp>(operation))
      endpoints[symbolic(call.getSymNameAttr())] = {call, call.getVpiKind(),
                                                    call.getScopeId()};
    else if (auto port = dyn_cast<SimPortDeclOp>(operation))
      endpoints[numeric(VPIObjectRefKind::Port, port.getIdAttr())] = {
          port, uint32_t(Kind::Port), port.getScopeId(), port.getType(),
          port.getVpiTypeAttr()};
    else if (auto storage = dyn_cast<SimStorageDeclOp>(operation))
      endpoints[numeric(VPIObjectRefKind::Storage, storage.getIdAttr())] = {
          storage, vpiKindForStorage(storage.getVpiTypeAttr()),
          storage.getScopeId(), storage.getType(), storage.getVpiTypeAttr()};
    else if (auto net = dyn_cast<SimNetDeclOp>(operation))
      endpoints[numeric(VPIObjectRefKind::Net, net.getIdAttr())] = {
          net, vpiKindForNet(net.getVpiTypeAttr()), net.getScopeId(),
          net.getType(), net.getVpiTypeAttr()};
    else if (auto net = dyn_cast<SimVPINetIdentityDeclOp>(operation))
      endpoints[numeric(VPIObjectRefKind::NetIdentity, net.getIdAttr())] = {
          net, vpiKindForNet(net.getVpiType()), net.getScopeId(), net.getType(),
          net.getVpiType()};
    else if (auto anchor = dyn_cast<SimVPIObjectAnchorOp>(operation)) {
      auto frozen = anchor.getImmutableValueAttr();
      endpoints[symbolic(anchor.getSymNameAttr())] = {
          anchor,
          anchor.getVpiKind(),
          anchor.getEnclosingScopeId(),
          frozen ? frozen.getType() : Type{},
          anchor.getVpiTypeAttr(),
          frozen};
    } else if (auto local = dyn_cast<SimVPILocalDeclOp>(operation))
      endpoints[symbolic(local.getSymNameAttr())] = {
          local, vpiKindForClassMember(local.getVpiType()), local.getScopeId(),
          local.getValueType(), local.getVpiType()};
    else if (auto expression = dyn_cast<SimVPIExpressionDeclOp>(operation)) {
      if (expression.getDefinitionAttr())
        continue;
      if (!ids.insert(expression.getId()).second)
        return expression.emitOpError("duplicates an expression occurrence ID");
      endpoints[symbolic(expression.getSymNameAttr())] = {
          expression,
          expression.getVpiKind(),
          *expression.getScopeId(),
          expression.getType().value_or(Type{}),
          expression.getVpiTypeAttr(),
          expression.getImmutableValueAttr()};
    }
  }
  // Definition members retain their shared declaration and exact instance
  // specialization. Never substitute the source default for the effective
  // parameter value or allocate a duplicate per-instance declaration.
  SymbolTableCollection symbols;
  for (auto relation : design.getOps<SimVPIRelationDeclOp>()) {
    for (auto reference : {relation.getSource(), relation.getTarget()}) {
      if (reference.getKind() != VPIObjectRefKind::DefinitionMember ||
          endpoints.contains(reference))
        continue;
      if (failed(verifyVPIDefinitionMemberRef(relation, design, reference,
                                              symbols)))
        return failure();
      auto member = symbols.lookupSymbolIn<SimVPIDefinitionMemberDeclOp>(
          design, reference.getSymbol());
      auto scope = scopes.lookup(reference.getId().getValue().getZExtValue());
      for (auto specialization :
           design.getOps<SimVPIDefinitionMemberSpecializationOp>()) {
        if (specialization.getSpecializationAttr() !=
                scope.getVpiSpecializationAttr() ||
            specialization.getMemberAttr() != reference.getSymbol())
          continue;
        endpoints[reference] = {
            member,
            member.getVpiKind(),
            scope.getId(),
            Type{},
            specialization.getEffectiveType(),
            specialization->getAttrOfType<FrozenConstantAttr>(
                "immutable_value")};
        break;
      }
      if (!endpoints.contains(reference))
        return relation.emitOpError(
            "expression member requires its exact effective specialization");
    }
  }
  using Slot = std::tuple<const void *, uint32_t, uint32_t, uint64_t>;
  std::map<Slot, SmallVector<SimVPIRelationDeclOp>> relations;
  DenseMap<Attribute, SmallVector<SimVPIRelationDeclOp>> outgoing, incoming;
  for (auto relation : design.getOps<SimVPIRelationDeclOp>()) {
    if (auto name = get<metadata::VpiExpressionIdentifier>(relation)) {
      auto source = endpoints.find(relation.getSource());
      auto target = endpoints.find(relation.getTarget());
      auto definitionSource =
          source == endpoints.end() &&
                  relation.getSource().getKind() == VPIObjectRefKind::Symbol
              ? referenceSymbols
                    .lookupNearestSymbolFrom<SimVPIExpressionDeclOp>(
                        relation, relation.getSource().getSymbol())
              : SimVPIExpressionDeclOp{};
      if (definitionSource && definitionSource.getDefinitionAttr()) {
        auto member =
            referenceSymbols
                .lookupNearestSymbolFrom<SimVPIDefinitionMemberDeclOp>(
                    relation, relation.getTarget().getSymbol());
        if (!member || name != member.getMemberNameAttr())
          return relation.emitOpError(
              "default identifier must match its exact declaration name");
        continue;
      }
      if (name.empty() ||
          llvm::any_of(
              name.getValue(),
              [](unsigned char byte) { return byte <= 32 || byte == 127; }) ||
          source == endpoints.end() || target == endpoints.end() ||
          !isa<SimVPIExpressionDeclOp>(source->second.operation) ||
          isa<SimVPIExpressionDeclOp>(target->second.operation))
        return relation.emitOpError("expression identifier requires a raw "
                                    "nonempty declaration-leaf name");
    }
    relations[{relation.getSource().getAsOpaquePointer(),
               relation.getSelector(), uint32_t(relation.getMode()),
               relation.getOrdinal()}]
        .push_back(relation);
    outgoing[relation.getSource()].push_back(relation);
    incoming[relation.getTarget()].push_back(relation);
  }
  // Resolve only immutable arithmetic. This walk precedes the complete
  // ownership proof, so it checks cycles, exact operand ordering, arity and
  // effective type context before invoking the pure evaluator.
  auto knownInteger = [&](const Endpoint &requested) -> std::optional<int64_t> {
    DenseMap<Operation *, std::optional<VPIConstantValue>> values;
    DenseSet<Operation *> visiting;
    SmallVector<std::pair<const Endpoint *, bool>> pending{{&requested, false}};
    auto operandsFor = [&](const Endpoint &node)
        -> std::optional<SmallVector<const Endpoint *, 3>> {
      auto expression =
          dyn_cast_if_present<SimVPIExpressionDeclOp>(node.operation);
      if (!expression || expression.getSourceOnly() ||
          node.kind != uint32_t(Kind::Operation) || !expression.getOpType())
        return std::nullopt;
      auto descriptor =
          reflection::findVPISourceOperator(*expression.getOpType());
      bool concatenation = *expression.getOpType() == concatOp ||
                           *expression.getOpType() == multiConcatOp;
      if (!concatenation &&
          (!descriptor || reflection::integralOperatorRule(descriptor->value) ==
                              reflection::VPIIntegralOperatorRule::Unsupported))
        return std::nullopt;
      auto &relations = outgoing[symbolic(expression.getSymNameAttr())];
      size_t count =
          concatenation
              ? llvm::count_if(relations,
                               [](auto relation) {
                                 return relation.getSelector() ==
                                        uint32_t(Relation::OperandRel);
                               })
              : descriptor->arity;
      if (concatenation &&
          count < (*expression.getOpType() == multiConcatOp ? 2u : 1u))
        return std::nullopt;
      SmallVector<const Endpoint *, 3> result(count, nullptr);
      for (auto relation : relations) {
        if (!reflection::isVPIExpressionChildRelation(relation.getSelector()))
          continue;
        if (relation.getSelector() != uint32_t(Relation::OperandRel) ||
            relation.getMode() != VPIRelationMode::Iterate ||
            relation.getOrdinal() >= result.size() ||
            result[relation.getOrdinal()])
          return std::nullopt;
        auto found = endpoints.find(relation.getTarget());
        if (found == endpoints.end() || !found->second.semantic ||
            !isIntegral(found->second.semantic) || !found->second.width)
          return std::nullopt;
        result[relation.getOrdinal()] = &found->second;
      }
      if (llvm::is_contained(result, nullptr))
        return std::nullopt;
      return result;
    };
    auto valueFor =
        [&](const Endpoint &node) -> std::optional<VPIConstantValue> {
      if (auto leaf = constantLeaf(node))
        return leaf;
      return values.lookup(node.operation);
    };
    while (!pending.empty()) {
      auto [node, ready] = pending.pop_back_val();
      if (constantLeaf(*node) || values.contains(node->operation))
        continue;
      auto operands = operandsFor(*node);
      if (!operands || !node->semantic || !isIntegral(node->semantic))
        return std::nullopt;
      if (!ready) {
        if (!visiting.insert(node->operation).second)
          return std::nullopt;
        pending.push_back({node, true});
        for (auto *operand : *operands)
          pending.push_back({operand, false});
        continue;
      }
      SmallVector<VPIConstantValue, 3> inputs;
      SmallVector<reflection::VPIIntegralExpressionShape, 3> shapes;
      for (auto *operand : *operands) {
        auto value = valueFor(*operand);
        if (!value)
          return std::nullopt;
        inputs.push_back(std::move(*value));
        auto child =
            dyn_cast_if_present<SimVPIExpressionDeclOp>(operand->operation);
        shapes.push_back({operand->width, operand->semantic.getIsSigned(),
                          operand->semantic.getIsFourState(),
                          child ? uint32_t(child.getOpType().value_or(0)) : 0});
      }
      auto expression = cast<SimVPIExpressionDeclOp>(node->operation);
      values[node->operation] = evaluateVPIIntegralConstant(
          *expression.getOpType(),
          {node->width, node->semantic.getIsSigned(),
           node->semantic.getIsFourState()},
          inputs, shapes);
      visiting.erase(node->operation);
    }
    auto value = valueFor(requested);
    return value && requested.semantic
               ? knownVPIConstantInteger(*value,
                                         requested.semantic.getIsSigned())
               : std::nullopt;
  };
  DenseMap<Attribute, Attribute> parents;
  for (auto expression : expressions) {
    auto self = symbolic(expression.getSymNameAttr());
    auto owner = endpoints.find(expression.getOwner());
    if (owner == endpoints.end() || !scopes.count(*expression.getScopeId()))
      return expression.emitOpError("requires exact retained owner and scope");
    bool ownerExpression = isa<SimVPIExpressionDeclOp>(owner->second.operation);
    bool ownerStatement = isa<SimStatementDeclOp>(owner->second.operation);
    bool ownerPort = owner->second.kind == uint32_t(Kind::Port);
    bool ownerTerminal = owner->second.kind == uint32_t(Kind::PrimTerm);
    bool callArgument =
        isa<SimVPICallDeclOp>(owner->second.operation) &&
        (owner->second.kind == uint32_t(Kind::FuncCall) ||
         owner->second.kind == uint32_t(Kind::TaskCall)) &&
        expression.getSelector() == uint32_t(Relation::ArgumentRel);
    bool caseLabel = owner->second.kind == uint32_t(Kind::CaseItem) &&
                     expression.getSelector() == uint32_t(Relation::ExprRel);
    if (!ownerExpression && expression.getMode() != VPIRelationMode::Handle &&
        !caseLabel && !callArgument)
      return expression.emitOpError(
          "compact root requires a singular, case-label or call-argument slot");
    if (!ownerExpression && !ownerStatement && !ownerPort && !ownerTerminal &&
        !callArgument)
      return expression.emitOpError("owner must be an expression, statement, "
                                    "port, terminal or ordinary call");
    uint64_t scope = owner->second.scope;
    if (ownerPort &&
        expression.getSelector() == uint32_t(Relation::HighConnRel)) {
      auto instance = scopes.lookup(scope);
      if (!instance || !instance.getParent())
        return expression.emitOpError(
            "higher connection requires an instantiating scope");
      scope = *instance.getParent();
    }
    if (scope != *expression.getScopeId())
      return expression.emitOpError(
          "expression scope disagrees with its source occurrence");
    const auto *edge = reflection::findVPITraversal(
        owner->second.kind, expression.getSelector(),
        expression.getMode() == VPIRelationMode::Handle
            ? reflection::VPITraversalMode::Handle
            : reflection::VPITraversalMode::Iterate);
    if (!edge || edge->statementContainment ||
        edge->automaticRelation != reflection::VPIAutomaticRelation::None ||
        !reflection::vpiObjectSetContains(edge->targets,
                                          expression.getVpiKind()))
      return expression.emitOpError(
          "owner relation is not a legal expression slot");
    auto &slot = relations[{
        expression.getOwner().getAsOpaquePointer(), expression.getSelector(),
        uint32_t(expression.getMode()), expression.getOrdinal()}];
    if (slot.size() != 1 || slot[0].getTarget() != self)
      return expression.emitOpError(
          "requires one matching exact owner relation");
    for (auto relation : incoming[self])
      if (relation != slot[0] &&
          !(relation.getSelector() == uint32_t(Relation::UseRel) &&
            relation.getMode() == VPIRelationMode::Iterate))
        return expression.emitOpError(
            "computed occurrence cannot have multiple owners");
    parents[self] =
        ownerExpression ? Attribute(expression.getOwner()) : Attribute{};

    DenseMap<uint32_t, SmallVector<Endpoint>> children;
    auto ordered = outgoing[self];
    llvm::sort(ordered, [](auto left, auto right) {
      return std::tuple(left.getSelector(), left.getMode(), left.getOrdinal()) <
             std::tuple(right.getSelector(), right.getMode(),
                        right.getOrdinal());
    });
    for (auto relation : ordered) {
      if (!reflection::isVPIExpressionChildRelation(relation.getSelector()))
        continue;
      auto target = endpoints.find(relation.getTarget());
      bool sourceOnlyChild =
          target != endpoints.end() &&
          isa_and_nonnull<SimVPIExpressionDeclOp>(target->second.operation) &&
          cast<SimVPIExpressionDeclOp>(target->second.operation)
              .getSourceOnly();
      if (target == endpoints.end() ||
          ((!isIntegral(target->second.semantic) || !target->second.width) &&
           !(expression.getSourceOnly() && sourceOnlyChild)))
        return expression.emitOpError("expression operand requires an exact "
                                      "integral value or source-only syntax");
      bool operand = relation.getSelector() == uint32_t(Relation::OperandRel);
      if (relation.getMode() !=
          (operand ? VPIRelationMode::Iterate : VPIRelationMode::Handle))
        return expression.emitOpError(
            "expression child has the wrong access mode");
      auto &group = children[relation.getSelector()];
      if (relation.getOrdinal() != group.size())
        return expression.emitOpError(
            "expression children require dense source order");
      group.push_back(target->second);
    }
    auto one = [&](Relation selector) -> const Endpoint * {
      auto found = children.find(uint32_t(selector));
      return found != children.end() && found->second.size() == 1
                 ? &found->second.front()
                 : nullptr;
    };
    if (expression.getSourceOnly()) {
      auto operands = children.find(uint32_t(Relation::OperandRel));
      if (children.size() != 1 || operands == children.end() ||
          !reflection::validVPISourceOnlyArity(*expression.getOpType(),
                                               operands->second.size()))
        return expression.emitOpError(
            "source-only operator requires its exact ordered operands");
      continue;
    }
    uint64_t width = *getPackedWidth(expression.getTypeAttr().getValue());
    switch (static_cast<Kind>(expression.getVpiKind())) {
    case Kind::Constant:
      if (!children.empty())
        return expression.emitOpError("constant cannot have value operands");
      break;
    case Kind::Operation: {
      auto &operands = children[uint32_t(Relation::OperandRel)];
      uint32_t code = *expression.getOpType();
      if (code != concatOp && code != multiConcatOp) {
        SmallVector<reflection::VPIIntegralExpressionShape, 3> shapes;
        for (const auto &operand : operands) {
          auto child =
              dyn_cast_if_present<SimVPIExpressionDeclOp>(operand.operation);
          shapes.push_back(
              {operand.width, operand.semantic.getIsSigned(),
               operand.semantic.getIsFourState(),
               child ? uint32_t(child.getOpType().value_or(0)) : 0});
        }
        auto semantic = expression.getVpiTypeAttr();
        if (children.size() != 1 ||
            !reflection::validVPIIntegralOperator(
                code,
                {width, semantic.getIsSigned(), semantic.getIsFourState()},
                shapes.data(), shapes.size()))
          return expression.emitOpError(
              "operator operands disagree with the elaborated result context");
        break;
      }
      bool repeated = expression.getOpType() == multiConcatOp;
      if (children.size() != 1 || operands.size() < (repeated ? 2 : 1))
        return expression.emitOpError(
            "concatenation requires its ordered operands");
      uint64_t sum = 0, multiplier = 1;
      if (repeated) {
        auto value = knownInteger(operands.front());
        if (!value || *value <= 0)
          return expression.emitOpError(
              "replication requires a positive constant multiplier");
        multiplier = *value;
      }
      for (const auto &operand :
           ArrayRef<Endpoint>(operands).drop_front(repeated)) {
        uint64_t bits = operand.width;
        if (bits > UINT64_MAX - sum)
          return expression.emitOpError("concatenation result width overflows");
        sum += bits;
      }
      if (sum > UINT64_MAX / multiplier || sum * multiplier != width)
        return expression.emitOpError(
            "concatenation result width disagrees with operands");
      bool fourState =
          llvm::any_of(ArrayRef<Endpoint>(operands).drop_front(repeated),
                       [](const Endpoint &operand) {
                         return operand.semantic.getIsFourState();
                       });
      if (expression.getVpiTypeAttr().getIsSigned() ||
          expression.getVpiTypeAttr().getIsFourState() != fourState)
        return expression.emitOpError("concatenation requires unsigned result "
                                      "and exact operand state domain");
      break;
    }
    case Kind::NetBit:
    case Kind::RegBit:
    case Kind::BitSelect: {
      auto parent = one(Relation::ParentRel);
      auto index = one(Relation::IndexRel);
      if (children.size() != 2 || !parent || !index || width != 1 ||
          reflection::vpiScalarBitSelectionKind(parent->kind) !=
              expression.getVpiKind())
        return expression.emitOpError("bit selection requires its exact "
                                      "declaration kind and source index");
      bool unitStride =
          parent->semantic.getKind() != VPITypeKind::PackedArray ||
          (parent->semantic.getChildren().size() == 1 &&
           integralWidth(cast<VPITypeSemanticsAttr>(
               parent->semantic.getChildren()[0])) == 1);
      if (!unitStride || expression.getVpiTypeAttr().getIsSigned() ||
          expression.getVpiTypeAttr().getIsFourState() !=
              parent->semantic.getIsFourState())
        return expression.emitOpError("bit selection requires an unsigned "
                                      "scalar in the parent state domain");
      break;
    }
    case Kind::PartSelect:
    case Kind::IndexedPartSelect: {
      bool indexed =
          expression.getVpiKind() == uint32_t(Kind::IndexedPartSelect);
      auto parent = one(Relation::ParentRel);
      auto first =
          one(indexed ? Relation::BaseExprRel : Relation::LeftRangeRel);
      auto last =
          one(indexed ? Relation::WidthExprRel : Relation::RightRangeRel);
      if (children.size() != 3 || !parent || !first || !last)
        return expression.emitOpError(
            "selection requires exact parent and range expressions");
      auto right = knownInteger(*last);
      if (!right)
        return expression.emitOpError(
            "selection width or bound must be constant");
      uint64_t count = 0;
      if (indexed) {
        if (*right <= 0)
          return expression.emitOpError(
              "indexed selection width must be positive");
        count = *right;
      } else {
        auto left = knownInteger(*first);
        if (!left)
          return expression.emitOpError("part-select bounds must be constant");
        uint64_t distance = *left >= *right
                                ? uint64_t(*left) - uint64_t(*right)
                                : uint64_t(*right) - uint64_t(*left);
        if (distance == UINT64_MAX)
          return expression.emitOpError("part-select width overflows");
        count = distance + 1;
      }
      // This slice selects scalar bits or one-bit packed-array elements.
      // Wider packed elements require an exact element-stride projection.
      bool unitStride =
          parent->semantic.getKind() != VPITypeKind::PackedArray ||
          (parent->semantic.getChildren().size() == 1 &&
           integralWidth(cast<VPITypeSemanticsAttr>(
               parent->semantic.getChildren()[0])) == 1);
      if (!unitStride || count != width)
        return expression.emitOpError(
            "selection result requires the exact scalar integral width");
      if (expression.getVpiTypeAttr().getIsSigned() ||
          expression.getVpiTypeAttr().getIsFourState() !=
              parent->semantic.getIsFourState())
        return expression.emitOpError(
            "selection requires unsigned result and the selected state domain");
      if (!indexed) {
        auto range = parent->semantic.getRange().asArrayRef();
        auto left = *knownInteger(*first);
        bool descending = range.size() == 2 ? range[0] >= range[1] : true;
        if (left != *right &&
            (range.size() != 2 || range[0] != range[1] || parent->width > 1) &&
            (left > *right) != descending)
          return expression.emitOpError(
              "part-select direction disagrees with selected range");
      }
      break;
    }
    default:
      return expression.emitOpError("unsupported expression kind");
    }
  }
  DenseSet<Attribute> proven;
  for (auto expression : expressions) {
    Attribute cursor = symbolic(expression.getSymNameAttr());
    DenseSet<Attribute> visiting;
    SmallVector<Attribute> path;
    while (cursor && !proven.count(cursor)) {
      if (!visiting.insert(cursor).second)
        return expression.emitOpError("expression ownership must be acyclic");
      path.push_back(cursor);
      cursor = parents.lookup(cursor);
    }
    proven.insert(path.begin(), path.end());
  }
  return success();
}
} // namespace talos::sim
