//===- ManagedClassLayoutAnalysisTestPass.cpp - Print class layouts -------===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

class ManagedClassLayoutAnalysisTestPass
    : public PassWrapper<ManagedClassLayoutAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      ManagedClassLayoutAnalysisTestPass)

  StringRef getArgument() const final {
    return "test-obelisk-managed-class-layout-analysis";
  }
  StringRef getDescription() const final {
    return "print shared managed class layout facts";
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layoutAttr) {
      module.emitError("managed class analysis test requires llvm.data_layout");
      return signalPassFailure();
    }
    llvm::Expected<llvm::DataLayout> dataLayout =
        llvm::DataLayout::parse(layoutAttr.getValue());
    if (!dataLayout) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(dataLayout.takeError());
      return signalPassFailure();
    }

    SmallVector<obelisk::sim::SimDesignOp> designs;
    module.walk(
        [&](obelisk::sim::SimDesignOp design) { designs.push_back(design); });
    if (designs.size() != 1) {
      module.emitError(
          "managed class analysis test requires one simulation design");
      return signalPassFailure();
    }
    FailureOr<obelisk::analysis::ManagedClassLayoutAnalysis> analysis =
        obelisk::analysis::ManagedClassLayoutAnalysis::compute(designs.front(),
                                                               *dataLayout);
    if (failed(analysis))
      return signalPassFailure();
    for (const auto &layout : analysis->classes) {
      obelisk::sim::SimClassDeclOp declaration = layout.declaration;
      llvm::errs() << "managed-class " << declaration.getSymName()
                   << " id=" << declaration.getId() << " size=" << layout.size
                   << " alignment=" << layout.alignment;
      if (layout.weakReferentOffset)
        llvm::errs() << " weak-referent-offset=" << *layout.weakReferentOffset;
      llvm::errs() << "\n";
      for (const auto &field : layout.fields) {
        obelisk::sim::SimClassFieldDeclOp fieldDeclaration = field.declaration;
        llvm::errs() << "  field " << fieldDeclaration.getSymName()
                     << " offset=" << field.offset
                     << " size=" << field.storage.size
                     << " alignment=" << field.storage.alignment << " planes="
                     << obelisk::analysis::getSimulationPhysicalStorageCount(
                            field.storage)
                     << "\n";
      }
    }
    markAllAnalysesPreserved();
  }
};

} // namespace

namespace obelisk {

void registerManagedClassLayoutAnalysisTestPass() {
  PassRegistration<ManagedClassLayoutAnalysisTestPass>();
}

} // namespace obelisk
