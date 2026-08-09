//===- MaterializeClockedSamples.cpp - Static alternate-clock samplers --===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMMATERIALIZECLOCKEDSAMPLESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimMaterializeClockedSamplesPass
    : public impl::ObeliskSimMaterializeClockedSamplesPassBase<
          ObeliskSimMaterializeClockedSamplesPass> {
public:
  void runOnOperation() override {
    sim::SimDesignOp design = getOperation();
    sim::SimFuncOp root;
    llvm::StringMap<SmallVector<sim::SimFuncOp, 2>> planGroups;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (function.getEntryKind() == sim::EntryKind::RootInitializer)
        root = function;
      auto plan = function->getAttrOfType<DictionaryAttr>(
          "obelisk_sim.clocked_sample_plan");
      auto key = plan ? plan.getAs<StringAttr>("key") : StringAttr{};
      if (key)
        planGroups[key.getValue()].push_back(function);
    }
    if (planGroups.empty())
      return;
    if (!root || root.getBody().empty() ||
        !root.getBody().front().getTerminator()) {
      design.emitError("alternate-clock samplers require one root initializer");
      signalPassFailure();
      return;
    }

    SmallVector<StringRef> keys;
    keys.reserve(planGroups.size());
    for (const auto &entry : planGroups)
      keys.push_back(entry.getKey());
    llvm::sort(keys);

    OpBuilder rootBuilder(root.getBody().front().getTerminator());
    Value context = root.getBody().front().getArgument(0);
    OpBuilder declarationBuilder(&design.getBody().front(),
                                 design.getBody().front().begin());
    bool invalid = false;
    llvm::DenseMap<uint64_t, Operation *> codeUnitIDs;
    for (sim::SimCodeUnitDeclOp declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
      codeUnitIDs.try_emplace(declaration.getId(), declaration);
    for (StringRef key : keys) {
      SmallVector<sim::SimFuncOp, 2> &group = planGroups[key];
      llvm::sort(group, [](sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
        return lhs.getSymName() < rhs.getSymName();
      });
      sim::SimFuncOp sampler = group.front();
      DictionaryAttr plan = sampler->getAttrOfType<DictionaryAttr>(
          "obelisk_sim.clocked_sample_plan");
      auto id = plan.getAs<IntegerAttr>("id");
      auto hierarchy = plan.getAs<StringAttr>("hierarchy");
      if (!id || !id.getValue().isStrictlyPositive() || !hierarchy) {
        sampler.emitError("malformed alternate-clock sample plan");
        invalid = true;
        continue;
      }
      uint64_t codeUnitID = id.getValue().getZExtValue();
      bool compatible = true;
      ArrayRef<sim::SimFuncOp> duplicates(group);
      for (sim::SimFuncOp duplicate : duplicates.drop_front()) {
        DictionaryAttr duplicatePlan = duplicate->getAttrOfType<DictionaryAttr>(
            "obelisk_sim.clocked_sample_plan");
        compatible &= duplicate.getFunctionType() == sampler.getFunctionType();
        compatible &=
            duplicatePlan && duplicatePlan.getAs<IntegerAttr>("id") == id;
        if (duplicate.getNumArguments() == sampler.getNumArguments())
          for (unsigned index = 0; index != sampler.getNumArguments(); ++index)
            compatible &= duplicate.getArgAttrDict(index) ==
                          sampler.getArgAttrDict(index);
      }
      if (!compatible) {
        sampler.emitError()
            << "incompatible duplicate alternate-clock sampler plan '" << key
            << "'";
        invalid = true;
        continue;
      }
      auto [collision, inserted] =
          codeUnitIDs.try_emplace(codeUnitID, sampler.getOperation());
      if (!inserted) {
        sampler.emitError()
            << "alternate-clock sampler code-unit ID collision for plan '"
            << key << "'";
        collision->second->emitRemark("colliding code unit is here");
        invalid = true;
        continue;
      }
      for (sim::SimFuncOp duplicate : duplicates.drop_front())
        duplicate.erase();
      sampler->setAttr("code_unit_id", id);
      sampler->removeAttr("obelisk_sim.clocked_sample_plan");
      sim::SimCodeUnitDeclOp::create(
          declarationBuilder, sampler.getLoc(), codeUnitID, uint64_t{0},
          sim::EntryKind::Always, hierarchy,
          declarationBuilder.getStringAttr("alternate-clock sampler"),
          declarationBuilder.getUnitAttr());

      SmallVector<Value> operands{context};
      for (unsigned index = 1; index < sampler.getNumArguments(); ++index) {
        DictionaryAttr attrs = sampler.getArgAttrDict(index);
        auto kind = attrs ? dyn_cast_or_null<sim::CaptureKindAttr>(
                                attrs.get(simlowering::captureKindAttrName))
                          : sim::CaptureKindAttr{};
        auto descriptor =
            attrs ? attrs.getAs<IntegerAttr>(simlowering::descriptorIdAttrName)
                  : IntegerAttr{};
        Type type = sampler.getArgumentTypes()[index];
        Value value;
        if (kind && descriptor && kind.getValue() == sim::CaptureKind::Storage)
          value = sim::SimContextStorageOp::create(rootBuilder, sampler.getLoc(),
                                                   type, context, descriptor);
        else if (kind && descriptor &&
                 kind.getValue() == sim::CaptureKind::Net)
          value = sim::SimContextNetOp::create(rootBuilder, sampler.getLoc(),
                                               type, context, descriptor);
        if (!value) {
          sampler.emitError()
              << "alternate-clock sampler argument #" << index
              << " is not a direct storage or net descriptor capture";
          invalid = true;
          break;
        }
        operands.push_back(value);
      }
      if (operands.size() != sampler.getNumArguments())
        continue;
      sim::SimSpawnOp::create(rootBuilder, sampler.getLoc(),
                              sampler.getSymNameAttr(), operands, ArrayAttr{},
                              ArrayAttr{});
    }
    if (invalid)
      signalPassFailure();
  }
};

} // namespace
} // namespace obelisk
