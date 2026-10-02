#include "../../../Conversion/SimulationToSchedule/NativePipeline.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "llvm/ADT/MapVector.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_PLANNATIVETRANSFERSPASS
#define GEN_PASS_DEF_SHARENATIVETRANSFERSPASS
#define GEN_PASS_DEF_PREPARENATIVETRANSFERKERNELSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"
namespace {
using Pipeline = detail::NativePipelineAnalysis;

// Only these captures can be represented by the transfer ABI. The slots come
// from the canonical frame analysis, never from a specialized function body.
static std::optional<SmallVector<int64_t>>
captureOffsets(sim::SimFuncOp function,
               const SimulationProcessFrameAnalysis &frame) {
  auto types = function.getFunctionType().getInputs();
  auto layout = frame.getEntryCaptureLayout();
  if (types.size() < 3 || types.size() != layout.size() ||
      !isa<sim::ContextType>(types.front()))
    return std::nullopt;
  SmallVector<int64_t> offsets{-1};
  for (unsigned i = 1; i < types.size(); ++i) {
    auto ref = dyn_cast<sim::RefType>(types[i]);
    auto kind =
        function.getArgAttrOfType<IntegerAttr>(i, sim::metadata::captureKind);
    if (!ref || !sim::getPackedWidth(ref.getElementType()) || !kind ||
        kind.getInt() != int32_t(sim::CaptureKind::Storage) ||
        !layout[i].hasValueStorage() || layout[i].hasSecondaryStorage() ||
        layout[i].storageSize != 8)
      return std::nullopt;
    offsets.push_back(layout[i].valueOffset);
  }
  return offsets;
}

class PlanNativeTransfersPass final
    : public impl::PlanNativeTransfersPassBase<PlanNativeTransfersPass> {
  void runOnOperation() override {
    auto cached = getCachedAnalysis<Pipeline>();
    if (!cached || cached->get().stage != Pipeline::Stage::Frames) {
      getOperation().emitError("schedule-plan-native-transfers requires "
                               "native pipeline phase Frames");
      return signalPassFailure();
    }
    auto &state = cached->get();
    OpBuilder builder(getOperation().getBodyRegion());
    builder.setInsertionPointToEnd(getOperation().getBody());
    // IEEE 1800-2023 4.9.1/4.9.6 require time-zero evaluation and source
    // sensitivity for these implicit continuous assignments. Admission keeps
    // the original Active actor (4.4.2.2) and its source wait (9.4.2); it never
    // applies the net-collapsing rules of 23.3.3.7 to variable storage.
    // analyses preserves source order even when frame preparation is parallel.
    for (const auto &[op, frame] : state.analyses) {
      auto copy = state.copyActivations.find(op);
      if (copy == state.copyActivations.end() ||
          !state.tableProcesses.contains(op))
        continue;
      auto function = cast<sim::SimFuncOp>(op);
      auto offsets = captureOffsets(function, *frame);
      if (!offsets)
        continue;
      SymbolRefAttr actor = FlatSymbolRefAttr::get(function);
      if (auto design = function->getParentOfType<sim::SimDesignOp>())
        actor = SymbolRefAttr::get(design.getSymNameAttr(),
                                   {FlatSymbolRefAttr::get(function)});
      schedule::TransferActivationOp::create(
          builder, function.getLoc(), actor, function.getFunctionType(),
          copy->second.source, copy->second.destination, *offsets,
          FlatSymbolRefAttr{});
    }
    markAnalysesPreserved<Pipeline>();
  }
};

class ShareNativeTransfersPass final
    : public impl::ShareNativeTransfersPassBase<ShareNativeTransfersPass> {
  void runOnOperation() override {
    ModuleOp module = getOperation();
    OpBuilder builder(module.getContext());
    llvm::MapVector<DictionaryAttr, SmallVector<schedule::TransferActivationOp>>
        classes;
    for (auto activation : module.getOps<schedule::TransferActivationOp>()) {
      if (activation.getKernelAttr())
        continue;
      auto key = builder.getDictionaryAttr(
          {builder.getNamedAttr("signature", activation.getSignatureAttr()),
           builder.getNamedAttr("source", activation.getSourceAttr()),
           builder.getNamedAttr("destination", activation.getDestinationAttr()),
           builder.getNamedAttr("capture_offsets",
                                activation.getCaptureOffsetsAttr())});
      classes[key].push_back(activation);
    }
    SymbolTable symbols(module);
    unsigned ordinal = 0;
    // IEEE 1800-2023 4.9.1 and 10.3.2: share implementation code, not
    // evaluations or updates. Every row retains its own process and wait;
    // no equal-value inference permits dropping an activation/publication.
    for (auto &[key, activations] : classes) {
      if (activations.size() < 2)
        continue;
      auto prototype = activations.front();
      builder.setInsertionPointToEnd(module.getBody());
      auto kernel = schedule::TransferKernelOp::create(
          builder, prototype.getLoc(),
          "__obelisk_transfer_kernel_" + std::to_string(ordinal++),
          prototype.getSignature(), prototype.getSource(),
          prototype.getDestination(), prototype.getCaptureOffsets());
      symbols.insert(kernel);
      for (auto activation : activations)
        activation.setKernelAttr(FlatSymbolRefAttr::get(kernel));
    }
    markAnalysesPreserved<Pipeline>();
  }
};

class PrepareNativeTransferKernelsPass final
    : public impl::PrepareNativeTransferKernelsPassBase<
          PrepareNativeTransferKernelsPass> {
  void runOnOperation() override {
    auto cached = getCachedAnalysis<Pipeline>();
    if (!cached || cached->get().stage != Pipeline::Stage::EvalVariants) {
      getOperation().emitError("schedule-prepare-native-transfer-kernels "
                               "requires native pipeline phase EvalVariants");
      return signalPassFailure();
    }
    auto &state = cached->get();
    ModuleOp module = getOperation();
    OpBuilder builder(module.getContext());
    SymbolTableCollection symbols;
    DenseSet<Operation *> boundActors;
    for (auto activation : module.getOps<schedule::TransferActivationOp>()) {
      auto actor = symbols.lookupSymbolIn<sim::SimFuncOp>(
          module, activation.getActorAttr());
      auto copy = state.copyActivations.find(actor);
      auto frame = state.analyses.find(actor);
      auto offsets = frame == state.analyses.end()
                         ? std::nullopt
                         : captureOffsets(actor, *frame->second);
      if (!actor || copy == state.copyActivations.end() || !offsets ||
          !state.tableProcesses.contains(actor) ||
          !boundActors.insert(actor).second ||
          actor.getFunctionType() != activation.getSignature() ||
          copy->second.source != activation.getSource() ||
          copy->second.destination != activation.getDestination() ||
          ArrayRef<int64_t>(*offsets) != activation.getCaptureOffsets()) {
        activation.emitError(
            "transfer plan disagrees with canonical copy proof");
        return signalPassFailure();
      }
    }
    for (auto kernel : module.getOps<schedule::TransferKernelOp>()) {
      auto signature = cast<FunctionType>(kernel.getSignature());
      SmallVector<DictionaryAttr> arguments;
      for (unsigned i = 0; i < signature.getNumInputs(); ++i)
        arguments.push_back(builder.getDictionaryAttr({builder.getNamedAttr(
            sim::metadata::captureKind,
            builder.getI32IntegerAttr(
                i == 0 ? int32_t(sim::CaptureKind::Context)
                       : int32_t(sim::CaptureKind::Formal)))}));
      // The plan remains visible until process materialization. The generated
      // template has no descriptor IDs and cannot acquire another actor's
      // static storage or publication owner during representation lowering.
      builder.setInsertionPointToEnd(module.getBody());
      if (module.lookupSymbol(kernel.getSymName().str() + ".impl")) {
        kernel.emitError("transfer implementation symbol already exists");
        return signalPassFailure();
      }
      auto function = sim::SimFuncOp::create(
          builder, kernel.getLoc(), kernel.getSymName().str() + ".impl",
          signature, sim::EntryKind::PortInput,
          ArrayRef<NamedAttribute>{builder.getNamedAttr(
              "obelisk.native.unmanaged", builder.getUnitAttr())},
          arguments);
      Block *entry = &function.getBody().front();
      Block *body = new Block;
      function.getBody().push_back(body);
      builder.setInsertionPointToEnd(entry);
      cf::BranchOp::create(builder, kernel.getLoc(), body);
      builder.setInsertionPointToEnd(body);
      Value source = entry->getArgument(kernel.getSource());
      Value destination = entry->getArgument(kernel.getDestination());
      Type element = cast<sim::RefType>(source.getType()).getElementType();
      auto value =
          sim::SimRefLoadOp::create(builder, kernel.getLoc(), element, source);
      // IEEE 1800-2023 10.6.2: retain the continuous driver contribution while
      // forced so release can restore it. Use the ordinary continuous-store
      // lowering, including visible-value transition publication (9.4.2),
      // instead of a raw memory copy. The executing instance supplies owner.
      sim::SimRefStoreOp::create(builder, kernel.getLoc(), value, destination);
      // IEEE 1800-2023 4.9.1/9.4.2: evaluate before the first wait, then
      // re-arm on the complete source value, including X/Z changes.
      sim::SimSuspendChangeOp::create(
          builder, kernel.getLoc(), source, ValueRange{},
          schedule::ContinuationSiteAttr{}, sim::EventRegionAttr{}, body);
      if (failed(detail::threadProcessStateThroughCFG(function)))
        return signalPassFailure();
      auto frame =
          SimulationProcessFrameAnalysis::create(function, state.dataLayout);
      if (failed(frame))
        return signalPassFailure();
      SmallVector<int64_t> offsets{-1};
      for (auto slot : llvm::drop_begin((*frame)->getEntryCaptureLayout()))
        offsets.push_back(slot.valueOffset);
      if (ArrayRef<int64_t>(offsets) != kernel.getCaptureOffsets()) {
        kernel.emitError(
            "selected transfer kernel changed canonical capture ABI");
        return signalPassFailure();
      }
      // IEEE 1800-2023 4.9.1/9.4.2: this implementation returns wait row zero
      // after each complete transfer. Each actor's own table supplies its
      // continuation, flags and source handle; none are shared with a peer.
      // Formal handles are deliberately descriptor-independent. This is a
      // construction from the verified kernel semantics, not process-shape
      // admission performed again on a representation-lowered body.
      detail::NativeTableProcess table;
      for (const auto &wait : (*frame)->getSuspensions()) {
        wait.operation->setAttr(detail::tableWaitIndexAttr,
                                builder.getI32IntegerAttr(0));
        table.waits.push_back(
            {wait.continuationID,
             0,
             wait.waitOffset,
             wait.waitSize,
             OBELISK_RT_SUSPEND_CHANGE,
             0,
             {{uint64_t(offsets[kernel.getSource()]),
               OBELISK_RT_WAIT_EDGE_CHANGE,
               uint32_t(*analysis::getSimulationStorageBitWidth(element))}}});
        schedule::set<schedule::Field::NativeContinuation>(
            wait.operation, builder.getI32IntegerAttr(wait.continuationID));
        schedule::set<schedule::Field::NativeWaitOffset>(
            wait.operation, builder.getI64IntegerAttr(wait.waitOffset));
        schedule::set<schedule::Field::NativeWaitSize>(
            wait.operation, builder.getI64IntegerAttr(wait.waitSize));
      }
      state.tableProcesses.try_emplace(function, std::move(table));
      state.transferKernelFrames.insert({function, std::move(*frame)});
    }
    markAnalysesPreserved<Pipeline>();
  }
};
} // namespace
} // namespace obelisk
