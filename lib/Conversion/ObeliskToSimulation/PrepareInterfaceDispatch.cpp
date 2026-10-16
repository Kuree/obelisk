//===- PrepareInterfaceDispatch.cpp - Source-owned VIF dispatch proof
//-------===//

#include "PrepareStatements.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"

using namespace mlir;
namespace obelisk::simlowering {
namespace {
struct SourceABI {
  FunctionType signature;
  DenseI32ArrayAttr directions;
};
FailureOr<SourceABI> originalABI(semantic::SVSubroutineSymbolOp source,
                                 OpBuilder &builder) {
  auto sourceType = source.getSemanticType();
  auto type = sourceType ? dyn_cast<semantic::SubroutineType>(*sourceType)
                         : semantic::SubroutineType{};
  auto signature =
      type ? dyn_cast<FunctionType>(type.getSignature()) : FunctionType{};
  if (!signature)
    return source.emitOpError("interface callable has no original signature");
  SmallVector<Type> inputs, results;
  for (Type input : signature.getInputs()) {
    auto normalized =
        normalizeSemanticType(input, getSemanticLocation(source), source);
    if (failed(normalized))
      return failure();
    inputs.push_back(*normalized);
  }
  for (Type result : signature.getResults()) {
    // Source void denotes absence of a result, never a simulation value type.
    if (isa<semantic::VoidType>(result))
      continue;
    auto normalized =
        normalizeSemanticType(result, getSemanticLocation(source), source);
    if (failed(normalized))
      return failure();
    results.push_back(*normalized);
  }
  SmallVector<int32_t> directions;
  for (Operation *child : getChildren(source))
    if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
      directions.push_back(static_cast<int32_t>(formal.getDirection()));
  if (directions.size() != inputs.size())
    return source.emitOpError(
        "interface callable signature and formal inventory disagree");
  return SourceABI{builder.getFunctionType(inputs, results),
                   builder.getDenseI32ArrayAttr(directions)};
}
semantic::SVInstanceBodySymbolOp
interfaceOwner(semantic::SVSubroutineSymbolOp source) {
  Operation *parent = source->getParentOp();
  if (isa<semantic::SVMethodPrototypeSymbolOp>(parent))
    parent = parent->getParentOp();
  // Nested class methods are not callable interface members.
  return dyn_cast_if_present<semantic::SVInstanceBodySymbolOp>(parent);
}
} // namespace

LogicalResult materializeVPIInterfaceDispatch(
    sim::SimDesignOp design, semantic::SVRootSymbolOp semanticRoot,
    const PreparedUnits &units, const PreparedScopeDeclarations &scopes,
    OpBuilder &builder) {
  SymbolTable symbols(design);
  llvm::DenseMap<std::pair<Attribute, Attribute>,
                 sim::SimVPIInterfaceMemberDeclOp>
      members;
  llvm::DenseMap<Operation *, sim::SimVPIInterfaceMemberDeclOp> sourceMembers;
  llvm::DenseMap<Operation *, sim::SimVPICallableDeclOp> callables;
  llvm::DenseSet<uint64_t> instanceScopes;
  uint64_t nextMember = 1;
  auto nominalType = [&](SymbolRefAttr identity) -> FlatSymbolRefAttr {
    auto reference =
        FlatSymbolRefAttr::get(getSimulationVirtualInterfaceTypespecSymbol(
            identity, builder.getStringAttr("")));
    return symbols.lookup<sim::SimVPITypespecDeclOp>(reference.getValue())
               ? reference
               : FlatSymbolRefAttr{};
  };
  auto walkResult = semanticRoot.walk([&](semantic::SVSubroutineSymbolOp source)
                                          -> WalkResult {
    auto body = interfaceOwner(source);
    auto identity =
        body ? body->getAttrOfType<SymbolRefAttr>("virtual_interface_identity")
             : SymbolRefAttr{};
    if (!identity)
      return WalkResult::advance();
    auto nominal = nominalType(identity);
    if (!nominal)
      return WalkResult::advance();
    auto abi = originalABI(source, builder);
    if (failed(abi))
      return WalkResult::interrupt();
    auto name = builder.getStringAttr(source.getName().value_or(""));
    bool isExtern = source.getIsInterfaceExtern().value_or(false);
    auto kind = source.getSubroutineKind() == semantic::SVSubroutineKind::Task
                    ? sim::EntryKind::Task
                    : sim::EntryKind::Function;
    auto key = std::make_pair(Attribute(nominal), Attribute(name));
    auto &member = members[key];
    if (!member) {
      member = sim::SimVPIInterfaceMemberDeclOp::create(
          builder, getSemanticLocation(source),
          builder.getStringAttr(
              ("__obelisk_vpi_interface_member_" + Twine(nextMember++)).str()),
          nominal, name, sim::EntryKindAttr::get(builder.getContext(), kind),
          TypeAttr::get(abi->signature), abi->directions,
          isExtern ? builder.getUnitAttr() : UnitAttr{});
      symbols.insert(member);
    } else if (member.getSignature() != abi->signature ||
               member.getDirectionsAttr() != abi->directions ||
               member.getMemberKind() != kind ||
               member.getIsExtern() != isExtern) {
      source.emitOpError("nominal interface member declarations disagree on "
                         "their original ABI");
      return WalkResult::interrupt();
    }
    sourceMembers[source] = member;
    return WalkResult::advance();
  });
  if (walkResult.wasInterrupted())
    return failure();
  // Instance membership is frozen from the interface body, independently of
  // the declarations and calls that will consume it.
  semanticRoot.walk([&](semantic::SVInstanceBodySymbolOp body) {
    if (body->hasAttr("is_virtual_interface_type_instance"))
      return;
    auto identity =
        body->getAttrOfType<SymbolRefAttr>("virtual_interface_identity");
    auto nominal = identity ? nominalType(identity) : FlatSymbolRefAttr{};
    uint64_t scope = scopes.lookup(body);
    if (nominal && scope && instanceScopes.insert(scope).second)
      sim::SimVPIInterfaceInstanceBindingOp::create(
          builder, getSemanticLocation(body), builder.getI64IntegerAttr(scope),
          nominal);
  });
  auto ensureCallable = [&](Operation *source) -> LogicalResult {
    if (callables.contains(source))
      return success();
    auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(source);
    auto declaration = units.declarations.lookup(source);
    if (!subroutine || !declaration)
      return source->emitOpError(
          "interface implementation has no exact code-unit declaration");
    auto abi = originalABI(subroutine, builder);
    if (failed(abi))
      return failure();
    callables[source] = sim::SimVPICallableDeclOp::create(
        builder, getSemanticLocation(source), declaration.getIdAttr(),
        TypeAttr::get(abi->signature), abi->directions, FlatSymbolRefAttr{},
        DenseI64ArrayAttr{}, UnitAttr{});
    return success();
  };
  // Use original prototype export resolution, never the call candidate list.
  for (const auto &unit : units.units) {
    auto member = sourceMembers.lookup(unit.source);
    if (!member)
      continue;
    if (failed(ensureCallable(unit.source)))
      return failure();
    auto callable = callables.lookup(unit.source);
    callable.setInterfaceMemberAttr(
        FlatSymbolRefAttr::get(member.getSymNameAttr()));
    if (!member.getIsExtern())
      continue;
    SmallVector<int64_t> exports;
    if (auto providers = units.externForkJoinTargets.find(unit.source);
        providers != units.externForkJoinTargets.end()) {
      callable.setIsForkJoin(true);
      for (Operation *provider : providers->second) {
        if (failed(ensureCallable(provider)))
          return failure();
        exports.push_back(units.declarations.lookup(provider).getId());
      }
    } else if (auto provider = units.externCalleeTargets.lookup(unit.source)) {
      if (failed(ensureCallable(provider)))
        return failure();
      exports.push_back(units.declarations.lookup(provider).getId());
    } else {
      return unit.source->emitOpError(
          "executable interface extern has no frozen provider inventory");
    }
    callable.setInterfaceExportsAttr(builder.getDenseI64ArrayAttr(exports));
  }
  llvm::DenseSet<Attribute> calls;
  walkResult = semanticRoot.walk([&](semantic::SVCallExpressionOp source)
                                     -> WalkResult {
    auto callRef = sim::get<sim::metadata::VpiCallSite>(source);
    if (!callRef || !calls.insert(callRef).second || !source.getHasThisClass())
      return WalkResult::advance();
    auto children = getChildren(source);
    auto receiverType =
        children.empty()
            ? TypeAttr{}
            : children.front()->getAttrOfType<TypeAttr>("semantic_type");
    auto interface =
        receiverType
            ? dyn_cast<semantic::VirtualInterfaceType>(receiverType.getValue())
            : semantic::VirtualInterfaceType{};
    if (!interface)
      return WalkResult::advance();
    auto identity = interface.getInterfaceName();
    if (auto receiverMembers =
            sim::get<sim::metadata::VirtualInterfaceReceiverMembers>(source);
        receiverMembers && !receiverMembers.empty())
      identity = cast<sim::VirtualInterfaceMemberAttr>(
                     receiverMembers[receiverMembers.size() - 1])
                     .getInterface();
    auto nominal = nominalType(identity);
    auto member = members.lookup(
        {nominal, builder.getStringAttr(source.getCalleeName())});
    if (!member) {
      source.emitOpError(
          "virtual interface call has no exact nominal member declaration");
      return WalkResult::interrupt();
    }
    auto call = symbols.lookup<sim::SimVPICallDeclOp>(callRef.getValue());
    call.setIsIndirect(true);
    int32_t access = 0;
    if (!interface.getModport().getValue().empty()) {
      if (source->hasAttr("virtual_interface_call_import"))
        access = 1;
      else if (source->hasAttr("virtual_interface_call_export"))
        access = 2;
      else {
        source.emitOpError("modport call has no resolved import/export access");
        return WalkResult::interrupt();
      }
    }
    sim::SimVPIInterfaceCallDispatchOp::create(
        builder, getSemanticLocation(source), callRef,
        FlatSymbolRefAttr::get(member.getSymNameAttr()),
        builder.getI32IntegerAttr(access));
    return WalkResult::advance();
  });
  return failure(walkResult.wasInterrupted());
}
} // namespace obelisk::simlowering
