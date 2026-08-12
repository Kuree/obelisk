//===- SimulationProcessFrameAnalysisTestPass.cpp - Print frame facts ----===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

StringRef getFieldKindName(obelisk::ProcessFrameFieldKind kind) {
  switch (kind) {
  case obelisk::ProcessFrameFieldKind::Capture:
    return "capture";
  case obelisk::ProcessFrameFieldKind::Continuation:
    return "continuation";
  case obelisk::ProcessFrameFieldKind::Live:
    return "live";
  case obelisk::ProcessFrameFieldKind::Wait:
    return "wait";
  }
  llvm_unreachable("unknown process frame field kind");
}

StringRef getFieldFlagsName(obelisk::ProcessFrameFieldFlags flags) {
  switch (flags) {
  case obelisk::ProcessFrameFieldFlags::None:
    return "none";
  case obelisk::ProcessFrameFieldFlags::FourStateValue:
    return "four-state-value";
  case obelisk::ProcessFrameFieldFlags::FourStateUnknown:
    return "four-state-unknown";
  case obelisk::ProcessFrameFieldFlags::ManagedRoot:
    return "managed-root";
  case obelisk::ProcessFrameFieldFlags::CandidateRoot:
    return "candidate-root";
  }
  llvm_unreachable("unknown process frame field flags");
}

unsigned getBlockIndex(obelisk::sim::SimFuncOp function, Block *target) {
  for (auto [index, block] : llvm::enumerate(function.getBody()))
    if (&block == target)
      return index;
  llvm_unreachable("frame analysis returned a foreign continuation block");
}

void printValueLayout(StringRef prefix, unsigned index,
                      const obelisk::ProcessFrameValue &value) {
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
    for (const obelisk::sim::ManagedHandleSlot &slot :
         value.managedRootSlots) {
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
    return "test-obelisk-simulation-process-frame-analysis";
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

    SmallVector<obelisk::sim::SimFuncOp> functions;
    module.walk([&](obelisk::sim::SimFuncOp function) {
      if (!function.isExternal())
        functions.push_back(function);
    });
    llvm::sort(functions, [](auto lhs, auto rhs) {
      return lhs.getSymName() < rhs.getSymName();
    });
    for (obelisk::sim::SimFuncOp function : functions) {
      auto frame = obelisk::SimulationProcessFrameAnalysis::create(function,
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
  static void printFrame(obelisk::sim::SimFuncOp function,
                         const obelisk::SimulationProcessFrameAnalysis &frame) {
    llvm::errs() << "frame @" << function.getSymName()
                 << " size=" << frame.getFrameSize()
                 << " align=" << frame.getFrameAlignment()
                 << " checksum=" << frame.getChecksum() << "\n";
    for (auto [index, value] : llvm::enumerate(frame.getEntryCaptureLayout()))
      printValueLayout("capture", index, value);
    for (const obelisk::ProcessFrameField &field : frame.getFields()) {
      llvm::errs() << "    field " << getFieldKindName(field.kind) << " "
                   << getFieldFlagsName(field.flags)
                   << " offset=" << field.offset << " size=" << field.size
                   << " align=" << field.alignment;
      if (field.flags == obelisk::ProcessFrameFieldFlags::CandidateRoot)
        llvm::errs() << " kinds=" << field.reserved;
      llvm::errs() << "\n";
    }
    llvm::errs() << "    continuations=";
    llvm::interleaveComma(frame.getContinuations(), llvm::errs());
    llvm::errs() << "\n";
    for (const obelisk::ProcessSuspension &suspension :
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

namespace obelisk {

void registerSimulationProcessFrameAnalysisTestPass() {
  PassRegistration<SimulationProcessFrameAnalysisTestPass>();
}

} // namespace obelisk
