//===- SimulationProcessFrameAnalysisTestPass.cpp - Print frame facts ----===//

#include "AnalysisTestPasses.h"

#include "talos/Analysis/SimulationProcessFrameAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

StringRef getFieldKindName(talos::ProcessFrameFieldKind kind) {
  switch (kind) {
  case talos::ProcessFrameFieldKind::Capture:
    return "capture";
  case talos::ProcessFrameFieldKind::Continuation:
    return "continuation";
  case talos::ProcessFrameFieldKind::Live:
    return "live";
  case talos::ProcessFrameFieldKind::Wait:
    return "wait";
  }
  llvm_unreachable("unknown process frame field kind");
}

StringRef getFieldFlagsName(talos::ProcessFrameFieldFlags flags) {
  switch (flags) {
  case talos::ProcessFrameFieldFlags::None:
    return "none";
  case talos::ProcessFrameFieldFlags::FourStateValue:
    return "four-state-value";
  case talos::ProcessFrameFieldFlags::FourStateUnknown:
    return "four-state-unknown";
  case talos::ProcessFrameFieldFlags::ManagedRoot:
    return "managed-root";
  case talos::ProcessFrameFieldFlags::CandidateRoot:
    return "candidate-root";
  }
  llvm_unreachable("unknown process frame field flags");
}

unsigned getBlockIndex(talos::sim::SimFuncOp function, Block *target) {
  for (auto [index, block] : llvm::enumerate(function.getBody()))
    if (&block == target)
      return index;
  llvm_unreachable("frame analysis returned a foreign continuation block");
}

void printValueLayout(StringRef prefix, unsigned index,
                      const talos::ProcessFrameValue &value) {
  llvm::errs() << "    " << prefix << index;
  if (value.storageSize == 0) {
    llvm::errs() << " context\n";
    return;
  }
  llvm::errs() << " value=" << value.valueOffset;
  if (value.isFourState())
    llvm::errs() << " unknown=" << value.unknownOffset;
  if (value.hasAuxiliary())
    llvm::errs() << " auxiliary=" << value.auxiliaryOffset;
  llvm::errs() << " size=" << value.storageSize << " align=" << value.alignment;
  if (value.hasManagedRoots()) {
    llvm::errs() << " roots=";
    llvm::interleaveComma(value.managedRootOffsets, llvm::errs());
    bool first = true;
    for (const talos::sim::ManagedHandleSlot &slot : value.managedRootSlots) {
      if (!slot.conditional)
        continue;
      llvm::errs() << (first ? " candidate-roots=" : ",") << slot.bitOffset
                   << ":" << slot.kindMask;
      first = false;
    }
  }
  llvm::errs() << "\n";
}

class SimulationProcessFrameAnalysisTestPass
    : public PassWrapper<SimulationProcessFrameAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      SimulationProcessFrameAnalysisTestPass)

  StringRef getArgument() const final {
    return "test-talos-simulation-process-frame-analysis";
  }
  StringRef getDescription() const final {
    return "print canonical process frame analysis facts";
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layoutAttr) {
      module.emitError("process frame analysis test requires llvm.data_layout");
      return signalPassFailure();
    }
    llvm::Expected<llvm::DataLayout> dataLayout =
        llvm::DataLayout::parse(layoutAttr.getValue());
    if (!dataLayout) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(dataLayout.takeError());
      return signalPassFailure();
    }

    SmallVector<talos::sim::SimFuncOp> functions;
    module.walk([&](talos::sim::SimFuncOp function) {
      if (!function.isExternal())
        functions.push_back(function);
    });
    llvm::sort(functions, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (talos::sim::SimFuncOp function : functions) {
      auto frame = talos::SimulationProcessFrameAnalysis::create(function,
                                                                   *dataLayout);
      if (failed(frame)) {
        signalPassFailure();
        return;
      }
      printFrame(function, **frame);
    }
    markAllAnalysesPreserved();
  }

private:
  static void printFrame(talos::sim::SimFuncOp function,
                         const talos::SimulationProcessFrameAnalysis &frame) {
    llvm::errs() << "frame @" << function.getSymName()
                 << " size=" << frame.getFrameSize()
                 << " align=" << frame.getFrameAlignment()
                 << " checksum=" << frame.getChecksum() << "\n";
    for (auto [index, value] : llvm::enumerate(frame.getEntryCaptureLayout()))
      printValueLayout("capture", index, value);
    for (const talos::ProcessFrameField &field : frame.getFields()) {
      llvm::errs() << "    field " << getFieldKindName(field.kind) << " "
                   << getFieldFlagsName(field.flags)
                   << " offset=" << field.offset << " size=" << field.size
                   << " align=" << field.alignment;
      if (field.flags == talos::ProcessFrameFieldFlags::CandidateRoot)
        llvm::errs() << " kinds=" << field.reserved;
      llvm::errs() << "\n";
    }
    llvm::errs() << "    continuations=";
    llvm::interleaveComma(frame.getContinuations(), llvm::errs());
    llvm::errs() << "\n";
    for (const talos::ProcessSuspension &suspension :
         frame.getSuspensions()) {
      llvm::errs() << "    suspend "
                   << suspension.operation->getName().getStringRef()
                   << " id=" << suspension.continuationID
                   << " bb=" << getBlockIndex(function, suspension.continuation)
                   << " wait=" << suspension.waitOffset << "+"
                   << suspension.waitSize << "\n";
      for (auto [index, value] : llvm::enumerate(
               frame.getContinuationLayout(suspension.continuationID)))
        printValueLayout("arg", index, value);
    }
  }
};

} // namespace

namespace talos {

void registerSimulationProcessFrameAnalysisTestPass() {
  PassRegistration<SimulationProcessFrameAnalysisTestPass>();
}

} // namespace talos
