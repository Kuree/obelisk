//===- SimulationVPIInterfaceDispatch.cpp - Exact VIF call identities
//-------===//

#include "SimulationVerifiers.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

using namespace mlir;
using namespace obelisk::sim;

namespace {
LogicalResult verifyOwner(Operation *op) {
  if (!isa<SimDesignOp>(op->getParentOp()))
    return op->emitOpError("requires a simulation.design owner");
  return success();
}
LogicalResult verifySignature(Operation *op, FunctionType signature,
                              DenseI32ArrayAttr directions) {
  if (signature.getNumInputs() != directions.size())
    return op->emitOpError(
        "signature and formal directions must have equal arity");
  if (signature.getNumResults() > 1)
    return op->emitOpError("source callable must have at most one result");
  for (Type type :
       llvm::concat<const Type>(signature.getInputs(), signature.getResults()))
    if (!isNormalizedValueType(type))
      return op->emitOpError(
          "source signature requires normalized simulation value types");
  for (int32_t direction : directions.asArrayRef())
    if (direction < 0 || direction > 3)
      return op->emitOpError(
          "formal direction must be input, output, inout, or ref");
  return success();
}
LogicalResult verifyNominal(Operation *op, FlatSymbolRefAttr reference,
                            SymbolTableCollection &symbols) {
  auto type =
      symbols.lookupNearestSymbolFrom<SimVPITypespecDeclOp>(op, reference);
  if (!type || type.getOrigin() != VPITypespecOrigin::Interface ||
      type.getTargetType().getKind() != VPITypeKind::VirtualInterface ||
      (type.getTargetType().getModport() &&
       !type.getTargetType().getModport().getValue().empty()))
    return op->emitOpError(
        "interface type must name an unmodported nominal interface typespec");
  return success();
}
} // namespace

LogicalResult SimVPICallableDeclOp::verify() {
  if (failed(verifyOwner(*this)) ||
      failed(verifySignature(*this, getSignature(), getDirectionsAttr())))
    return failure();
  if (getCodeUnitIdAttr().getValue().isNegative() || !getCodeUnitId())
    return emitOpError("code-unit ID must be positive");
  if ((getInterfaceExportsAttr() || getIsForkJoin()) &&
      !getInterfaceMemberAttr())
    return emitOpError(
        "interface exports require declaration-owned membership");
  if (getIsForkJoin() && !getInterfaceExportsAttr())
    return emitOpError(
        "fork/join declaration requires its explicit export inventory");
  llvm::DenseSet<int64_t> providers;
  if (auto exports = getInterfaceExports())
    for (int64_t id : *exports)
      if (id <= 0 || static_cast<uint64_t>(id) == getCodeUnitId() ||
          !providers.insert(id).second)
        return emitOpError("exports must name unique positive provider IDs "
                           "distinct from the declaration");
  return success();
}
LogicalResult
SimVPICallableDeclOp::verifySymbolUses(SymbolTableCollection &symbols) {
  if (auto member = getInterfaceMemberAttr())
    if (!symbols.lookupNearestSymbolFrom<SimVPIInterfaceMemberDeclOp>(*this,
                                                                      member))
      return emitOpError(
          "interface member must name an exact callable declaration");
  return success();
}
LogicalResult SimVPIInterfaceMemberDeclOp::verify() {
  if (failed(verifyOwner(*this)) ||
      failed(verifySignature(*this, getSignature(), getDirectionsAttr())))
    return failure();
  if (getName().empty() || getName().contains('\0'))
    return emitOpError("requires a nonempty member name without NUL bytes");
  if (getMemberKind() != EntryKind::Task &&
      getMemberKind() != EntryKind::Function)
    return emitOpError("member must be a task or function");
  if (getMemberKind() == EntryKind::Task && getSignature().getNumResults())
    return emitOpError("task source signature must have no results");
  return success();
}
LogicalResult
SimVPIInterfaceMemberDeclOp::verifySymbolUses(SymbolTableCollection &symbols) {
  return verifyNominal(*this, getInterfaceTypeAttr(), symbols);
}
LogicalResult SimVPIInterfaceInstanceBindingOp::verify() {
  if (failed(verifyOwner(*this)))
    return failure();
  if (getScopeIdAttr().getValue().isNegative() || !getScopeId())
    return emitOpError("interface scope ID must be positive");
  return success();
}
LogicalResult SimVPIInterfaceInstanceBindingOp::verifySymbolUses(
    SymbolTableCollection &symbols) {
  return verifyNominal(*this, getInterfaceTypeAttr(), symbols);
}
LogicalResult SimVPIInterfaceCallDispatchOp::verify() {
  if (failed(verifyOwner(*this)))
    return failure();
  if (getAccess() > 2)
    return emitOpError("interface access must be plain, import, or export");
  return success();
}
LogicalResult SimVPIInterfaceCallDispatchOp::verifySymbolUses(
    SymbolTableCollection &symbols) {
  if (!symbols.lookupNearestSymbolFrom<SimVPICallDeclOp>(*this, getCallAttr()))
    return emitOpError("call must name a source call declaration");
  if (!symbols.lookupNearestSymbolFrom<SimVPIInterfaceMemberDeclOp>(
          *this, getMemberAttr()))
    return emitOpError(
        "member must name an exact interface callable declaration");
  return success();
}

LogicalResult obelisk::sim::verifyVPIInterfaceDispatch(SimDesignOp design) {
  llvm::DenseMap<uint64_t, SimScopeDeclOp> scopes;
  llvm::DenseMap<uint64_t, SimCodeUnitDeclOp> units;
  llvm::DenseMap<uint64_t, SimVPICallableDeclOp> callables;
  llvm::DenseMap<uint64_t, FlatSymbolRefAttr> instances;
  llvm::DenseSet<std::pair<Attribute, Attribute>> memberNames;
  SymbolTableCollection symbols;
  for (Operation &op : design.getBody().front()) {
    if (auto scope = dyn_cast<SimScopeDeclOp>(op))
      scopes.try_emplace(scope.getId(), scope);
    else if (auto unit = dyn_cast<SimCodeUnitDeclOp>(op))
      units.try_emplace(unit.getId(), unit);
    else if (auto callable = dyn_cast<SimVPICallableDeclOp>(op)) {
      if (!callables.try_emplace(callable.getCodeUnitId(), callable).second)
        return callable.emitOpError(
            "duplicates a code-unit callable declaration");
    } else if (auto instance = dyn_cast<SimVPIInterfaceInstanceBindingOp>(op)) {
      if (!instances
               .try_emplace(instance.getScopeId(),
                            instance.getInterfaceTypeAttr())
               .second)
        return instance.emitOpError("duplicates an interface instance binding");
    } else if (auto member = dyn_cast<SimVPIInterfaceMemberDeclOp>(op)) {
      if (!memberNames
               .insert({member.getInterfaceTypeAttr(), member.getNameAttr()})
               .second)
        return member.emitOpError(
            "duplicates a member of the same nominal interface");
    }
  }
  using Kind = obelisk::reflection::VPIObjectKind;
  for (auto binding :
       design.getBody().front().getOps<SimVPIInterfaceInstanceBindingOp>()) {
    auto scope = scopes.lookup(binding.getScopeId());
    if (!scope ||
        vpiKindForScope(scope) != static_cast<uint32_t>(Kind::Interface))
      return binding.emitOpError(
          "instance must name a physical interface scope");
  }
  llvm::DenseSet<std::pair<uint64_t, Attribute>> declarations;
  for (auto [id, callable] : callables) {
    auto unit = units.lookup(id);
    auto sourceKind = vpiCallableSourceKind(unit);
    if (!sourceKind)
      return callable.emitOpError(
          "code unit must name a task or function declaration");
    if (sourceKind == EntryKind::Task &&
        callable.getSignature().getNumResults())
      return callable.emitOpError("task source signature must have no results");
    auto reference = callable.getInterfaceMemberAttr();
    if (!reference)
      continue;
    auto member = symbols.lookupNearestSymbolFrom<SimVPIInterfaceMemberDeclOp>(
        callable, reference);
    if (!member)
      return callable.emitOpError("references an unknown interface member");
    if (instances.lookup(unit.getScopeId()) != member.getInterfaceTypeAttr())
      return callable.emitOpError("declaration scope must have the member's "
                                  "exact nominal interface type");
    if (!unit.getDebugName() || *unit.getDebugName() != member.getName())
      return callable.emitOpError(
          "declaration must preserve the exact source member name");
    if (sourceKind != member.getMemberKind() ||
        callable.getSignature() != member.getSignature() ||
        callable.getDirectionsAttr() != member.getDirectionsAttr())
      return callable.emitOpError(
          "declaration must preserve the member's exact original ABI");
    if (!declarations.insert({unit.getScopeId(), reference}).second)
      return callable.emitOpError(
          "duplicates a member declaration in the same interface instance");
    auto exports = callable.getInterfaceExportsAttr();
    if (member.getIsExtern() != bool(exports))
      return callable.emitOpError(
          "only extern members must retain an explicit export inventory");
    if (!exports)
      continue;
    if (callable.getIsForkJoin()) {
      if (sourceKind != EntryKind::Task)
        return callable.emitOpError(
            "only an extern task may use fork/join aggregation");
    } else if (exports.size() != 1)
      return callable.emitOpError(
          "ordinary extern requires exactly one provider");
    for (int64_t providerID : exports.asArrayRef()) {
      auto provider = callables.lookup(providerID);
      auto providerUnit = units.lookup(providerID);
      if (!provider || !providerUnit || provider.getInterfaceExportsAttr() ||
          vpiCallableSourceKind(providerUnit) != sourceKind ||
          provider.getSignature() != callable.getSignature() ||
          provider.getDirectionsAttr() != callable.getDirectionsAttr())
        return callable.emitOpError(
            "export provider must name a distinct implementation with the "
            "exact original ABI");
    }
  }
  llvm::DenseSet<Attribute> seenCalls;
  for (auto contract :
       design.getBody().front().getOps<SimVPIClassCallDispatchOp>())
    seenCalls.insert(contract.getCallAttr());
  for (auto contract :
       design.getBody().front().getOps<SimVPIInterfaceCallDispatchOp>()) {
    if (!seenCalls.insert(contract.getCallAttr()).second)
      return contract.emitOpError("duplicates an indirect call contract");
    auto call = symbols.lookupNearestSymbolFrom<SimVPICallDeclOp>(
        contract, contract.getCallAttr());
    auto member = symbols.lookupNearestSymbolFrom<SimVPIInterfaceMemberDeclOp>(
        contract, contract.getMemberAttr());
    if (!call || !member)
      return contract.emitOpError(
          "requires an existing call and member declaration");
    bool task =
        call.getVpiKind() == static_cast<uint32_t>(Kind::TaskCall) ||
        call.getVpiKind() == static_cast<uint32_t>(Kind::MethodTaskCall);
    bool function =
        call.getVpiKind() == static_cast<uint32_t>(Kind::FuncCall) ||
        call.getVpiKind() == static_cast<uint32_t>(Kind::MethodFuncCall);
    if ((!task && !function) || !call.getIsIndirect() ||
        call.getName() != member.getName() ||
        member.getMemberKind() !=
            (task ? EntryKind::Task : EntryKind::Function))
      return contract.emitOpError(
          "call must name this exact indirect member and kind");
    if (contract.getAccess() == 2 && !member.getIsExtern())
      return contract.emitOpError(
          "modport export call requires an extern member");
    for (auto [scope, nominal] : instances)
      if (nominal == member.getInterfaceTypeAttr() &&
          !declarations.contains({scope, contract.getMemberAttr()}))
        return contract.emitOpError("member inventory must cover every "
                                    "instance of its nominal interface");
  }
  return success();
}
