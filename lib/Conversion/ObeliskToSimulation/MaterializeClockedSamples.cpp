//===- MaterializeClockedSamples.cpp - Deferred clock samplers ------------===//

#include "Detail.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

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
    if (failed(simlowering::materializeCovergroupClockingSamplers(design))) {
      signalPassFailure();
      return;
    }
    // Functional expressions can call ordinary code units before their
    // constructor/sample helpers exist. Prepare keeps those callees alive
    // through the early SymbolDCE with references on the root. Every semantic
    // call has now become an executable symbol use, so release that temporary
    // inventory before the ordinary post-lowering DCE and inliner.
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      function->removeAttr(sim::metadata::coverageRetainedCodeUnits);
    bool invalid = false;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      auto propagateObserverRequest = [&](schedule::Field requestName,
                                          schedule::Field targetName,
                                          StringRef kind) {
        auto requests = schedule::get<ArrayAttr>(function, requestName);
        if (!requests)
          return;
        bool requestInvalid = false;
        for (Attribute request : requests) {
          auto evaluatorRef = dyn_cast<FlatSymbolRefAttr>(request);
          if (!evaluatorRef) {
            function.emitError()
                << "concurrent " << kind
                << " observer request is not a flat symbol reference";
            requestInvalid = invalid = true;
            continue;
          }
          sim::SimFuncOp evaluator =
              design.lookupSymbol<sim::SimFuncOp>(evaluatorRef);
          if (!evaluator) {
            function.emitError()
                << "concurrent " << kind << " observer evaluator "
                << evaluatorRef << " is missing";
            requestInvalid = invalid = true;
            continue;
          }
          schedule::set(evaluator, targetName,
                        UnitAttr::get(design.getContext()));
          ::obelisk::schedule::set<
              ::obelisk::schedule::Field::DetachedControls>(
              evaluator, UnitAttr::get(design.getContext()));
        }
        if (!requestInvalid)
          schedule::remove(function, requestName);
      };
      propagateObserverRequest(
          simlowering::concurrentCancelObserverRequestAttrName,
          ::obelisk::schedule::Field::ConcurrentCancelObserver, "cancel");
      propagateObserverRequest(
          simlowering::concurrentAbortObserverRequestAttrName,
          ::obelisk::schedule::Field::ConcurrentAbortObserver, "abort");
    }
    if (invalid) {
      signalPassFailure();
      return;
    }

    sim::SimFuncOp root;
    struct ClockingOutputTracker {
      Type type;
      uint64_t descriptor;
      uint64_t width;
      sim::EdgeKind edge;
      Location location;
    };
    SmallVector<ClockingOutputTracker> clockingOutputTrackers;
    llvm::DenseSet<Attribute> clockingOutputTrackerKeys;
    llvm::StringMap<SmallVector<sim::SimFuncOp, 2>> planGroups;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (function.getEntryKind() == sim::EntryKind::RootInitializer)
        root = function;
      function.walk([&](sim::SimClockingOutputCurrentOp current) {
        Type type = current.getClock().getType();
        if (!isa<sim::RefType, sim::NetType>(type)) {
          current.emitError(
              "static clocking-output tracker requires storage or net clock");
          invalid = true;
          return;
        }
        Attribute key = ArrayAttr::get(
            design.getContext(),
            {TypeAttr::get(type),
             IntegerAttr::get(IntegerType::get(design.getContext(), 64),
                              current.getDescriptor()),
             IntegerAttr::get(IntegerType::get(design.getContext(), 64),
                              current.getWidth()),
             IntegerAttr::get(IntegerType::get(design.getContext(), 32),
                              static_cast<uint32_t>(current.getEdge()))});
        if (!clockingOutputTrackerKeys.insert(key).second)
          return;
        clockingOutputTrackers.push_back({type, current.getDescriptor(),
                                          current.getWidth(), current.getEdge(),
                                          current.getLoc()});
      });
      auto plan = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::ClockedSamplePlan>(function);
      auto key = plan ? plan.getKey() : StringAttr{};
      if (key)
        planGroups[key.getValue()].push_back(function);
    }
    if (invalid) {
      signalPassFailure();
      return;
    }
    if (planGroups.empty() && clockingOutputTrackers.empty())
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
    for (const ClockingOutputTracker &tracker : clockingOutputTrackers) {
      Value clock;
      if (isa<sim::RefType>(tracker.type))
        clock = sim::SimContextStorageOp::create(rootBuilder, tracker.location,
                                                 tracker.type, context,
                                                 tracker.descriptor);
      else
        clock = sim::SimContextNetOp::create(rootBuilder, tracker.location,
                                             tracker.type, context,
                                             tracker.descriptor);
      sim::SimClockingOutputTrackOp::create(rootBuilder, tracker.location,
                                            clock, tracker.edge, tracker.width);
    }
    if (planGroups.empty())
      return;
    OpBuilder declarationBuilder(&design.getBody().front(),
                                 design.getBody().front().begin());
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
      auto plan = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::ClockedSamplePlan>(sampler);
      auto id = plan.getId();
      auto hierarchy = plan.getHierarchy();
      if (!id || !id.getValue().isStrictlyPositive() || !hierarchy) {
        sampler.emitError("malformed alternate-clock sample plan");
        invalid = true;
        continue;
      }
      uint64_t codeUnitID = id.getValue().getZExtValue();
      bool compatible = true;
      ArrayRef<sim::SimFuncOp> duplicates(group);
      for (sim::SimFuncOp duplicate : duplicates.drop_front()) {
        auto duplicatePlan = ::obelisk::schedule::get<
            ::obelisk::schedule::Field::ClockedSamplePlan>(duplicate);
        compatible &= duplicate.getFunctionType() == sampler.getFunctionType();
        compatible &= duplicatePlan && duplicatePlan.getId() == id;
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
      ::obelisk::schedule::remove<
          ::obelisk::schedule::Field::ClockedSamplePlan>(sampler);
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
          value = sim::SimContextStorageOp::create(
              rootBuilder, sampler.getLoc(), type, context, descriptor);
        else if (kind && descriptor && kind.getValue() == sim::CaptureKind::Net)
          value = sim::SimContextNetOp::create(rootBuilder, sampler.getLoc(),
                                               type, context, descriptor);
        else if (kind && descriptor &&
                 kind.getValue() == sim::CaptureKind::Event)
          value = sim::SimContextEventOp::create(rootBuilder, sampler.getLoc(),
                                                 type, context, descriptor);
        if (!value) {
          sampler.emitError()
              << "alternate-clock sampler argument #" << index
              << " is not a direct storage, net, or event descriptor capture";
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
