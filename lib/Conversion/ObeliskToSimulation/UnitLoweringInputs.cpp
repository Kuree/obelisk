//===- UnitLoweringInputs.cpp - Parallel lowering inputs
//-------------------===//

#include "UnitLoweringInputs.h"
#include "Detail.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "llvm/Support/MathExtras.h"

#include "mlir/IR/BuiltinOps.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPREPAREUNITLOWERINGPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace simlowering {

UnitLoweringInputs::UnitLoweringInputs(Operation *module)
    : semanticTypes(module), lockedSymbols(symbols) {
  ScopedSemanticTypeIndex semanticTypeScope(semanticTypes);
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
      return WalkResult::advance();
    }
    if (auto member = dyn_cast<ir::SVMemberAccessExpressionOp>(operation)) {
      auto children = getChildren(member);
      if (children.size() != 1)
        return WalkResult::advance();
      // A covergroup item has no value type and no aggregate members;
      // normalizing it would diagnose the receiver of its option access.
      auto baseType =
          children.front()->getAttrOfType<TypeAttr>("semantic_type");
      if (!baseType || isa<ir::VoidType>(baseType.getValue()))
        return WalkResult::advance();
      auto normalized = getNormalizedSemanticType(children.front());
      if (failed(normalized) || !sim::isAggregateType(*normalized))
        return WalkResult::advance();
      Type type = *normalized;
      auto [entry, inserted] = aggregateMembers.try_emplace(type);
      if (!inserted)
        return WalkResult::advance();
      auto owner = describeContainerElement(type, member.getLoc());
      if (failed(owner))
        return WalkResult::advance();
      entry->second.owner = *owner;
      uint64_t next = 0;
      for (unsigned index = 0; index < sim::getAggregateNumElements(type);
           ++index) {
        Type child = sim::getAggregateElementType(type, index);
        auto [element, fresh] = memberElements.try_emplace(child);
        if (fresh) {
          auto descriptor = describeContainerElement(child, member.getLoc());
          if (failed(descriptor))
            break;
          element->second =
              std::make_unique<ContainerElementDescriptor>(*descriptor);
        }
        if (!element->second)
          break;
        uint64_t offset = 0;
        auto record = dyn_cast<sim::UnpackedStructType>(type);
        auto unionType = dyn_cast<sim::UnpackedUnionType>(type);
        if (record || (unionType && unionType.getIsTagged())) {
          Attribute layoutAttr = record ? record.getLayout() : Attribute{};
          if (auto layout =
                  dyn_cast_or_null<sim::StorageLayoutAttr>(layoutAttr)) {
            offset = layout.getOffsets()[index] * 8;
          } else {
            auto alignment = sim::getProvenanceAlignment(child);
            if (!alignment || !*alignment ||
                next > UINT64_MAX - (*alignment - 1))
              break;
            offset = llvm::alignTo(next, *alignment);
            uint64_t span = element->second->bitWidth
                                ? element->second->bitWidth
                                : element->second->valueSize * 8;
            if (auto tagged = dyn_cast<sim::UnpackedUnionType>(child);
                tagged && tagged.getIsTagged()) {
              uint64_t tagBits = llvm::Log2_64_Ceil(
                  uint64_t{sim::getAggregateNumElements(child)} + 1);
              if (span > UINT64_MAX - tagBits)
                break;
              span += tagBits;
            }
            if (span > UINT64_MAX - offset)
              break;
            next = offset + span;
          }
        } else {
          // Packed fields carry their offset; unpacked unions overlap.
          auto slice = sim::getAggregateProvenanceSubelement(type, index);
          if (!slice)
            break;
          offset = slice->first;
        }
        entry->second.members.push_back({offset, element->second.get()});
      }
    }
    if (auto group = dyn_cast<ir::SVCovergroupTypeOp>(operation)) {
      auto entry = covergroups.try_emplace(group.getSemanticType(), operation);
      if (!entry.second)
        entry.first->second = nullptr; // Ambiguous identities never alias.
      auto nameEntry =
          covergroupsByName.try_emplace(group.getSymName(), operation);
      if (!nameEntry.second)
        nameEntry.first->second = nullptr;
      if (auto base = cov::get<cov::BaseGroup>(group))
        derivedCovergroups[base.getValue()].push_back(operation);
    }
    return WalkResult::advance();
  });
}

const UnitLoweringInputs::AggregateMembers *
UnitLoweringInputs::getAggregateMembers(Type type) const {
  auto found = aggregateMembers.find(type);
  return found == aggregateMembers.end() ? nullptr : &found->second;
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
