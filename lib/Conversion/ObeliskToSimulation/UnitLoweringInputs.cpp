//===- UnitLoweringInputs.cpp - Parallel lowering inputs
//-------------------===//

#include "UnitLoweringInputs.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"

#include "mlir/IR/BuiltinOps.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPREPAREUNITLOWERINGPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace simlowering {

UnitLoweringInputs::UnitLoweringInputs(Operation *module)
    : lockedSymbols(symbols) {
  symbols.getSymbolTable(module);
  module->walk<WalkOrder::PreOrder>([&](Operation *operation) {
    if (auto design = dyn_cast<sim::SimDesignOp>(operation))
      symbols.getSymbolTable(design);
    if (auto scope = dyn_cast<sim::SimScopeDeclOp>(operation)) {
      auto design = dyn_cast<sim::SimDesignOp>(scope->getParentOp());
      auto hierarchy = scope.getHierarchicalName();
      auto coverageID = scope.getCoverageId();
      if (design && hierarchy && coverageID &&
          (scope.getId() == 0 || scope.getDefinitionName()))
        coverageInstances[design].try_emplace(*hierarchy, *coverageID);
    }
    if (auto function = dyn_cast<sim::SimFuncOp>(operation)) {
      callables.try_emplace(
          function, Callable{function.getFunctionType(),
                             function->hasAttr("simulation.void_function")});
      return WalkResult::skip();
    }
    if (auto group = dyn_cast<ir::SVCovergroupTypeOp>(operation)) {
      auto entry = covergroups.try_emplace(group.getSemanticType(), operation);
      if (!entry.second)
        entry.first->second = nullptr; // Ambiguous identities never alias.
      auto nameEntry =
          covergroupsByName.try_emplace(group.getSymName(), operation);
      if (!nameEntry.second)
        nameEntry.first->second = nullptr;
      if (auto base =
              group->getAttrOfType<TypeAttr>("obelisk.coverage.base_group"))
        derivedCovergroups[base.getValue()].push_back(operation);
    }
    return WalkResult::advance();
  });
}

const UnitLoweringInputs::Callable *
UnitLoweringInputs::getCallable(sim::SimFuncOp function) const {
  auto found = callables.find(function);
  return found == callables.end() ? nullptr : &found->second;
}

Operation *UnitLoweringInputs::getCovergroup(Type handle) const {
  auto found = covergroups.find(handle);
  return found == covergroups.end() ? nullptr : found->second;
}

Operation *UnitLoweringInputs::getCovergroup(StringRef symbolName) const {
  return covergroupsByName.lookup(symbolName);
}

ArrayRef<Operation *>
UnitLoweringInputs::getDerivedCovergroups(Type baseHandle) const {
  auto found = derivedCovergroups.find(baseHandle);
  if (found == derivedCovergroups.end())
    return {};
  return found->second;
}

std::optional<uint64_t>
UnitLoweringInputs::getCoverageInstanceID(sim::SimDesignOp design,
                                          StringRef hierarchy) const {
  auto found = coverageInstances.find(design);
  if (found == coverageInstances.end())
    return std::nullopt;
  auto instance = found->second.find(hierarchy);
  if (instance == found->second.end())
    return std::nullopt;
  return instance->second;
}

} // namespace simlowering

namespace {

class ObeliskSimPrepareUnitLoweringPass
    : public impl::ObeliskSimPrepareUnitLoweringPassBase<
          ObeliskSimPrepareUnitLoweringPass> {
public:
  void runOnOperation() override {
    getAnalysis<simlowering::UnitLoweringInputs>();
    markAllAnalysesPreserved();
  }
};

} // namespace
} // namespace obelisk
