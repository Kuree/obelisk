//===- PrepareRuntimeToLLVM.cpp - Prepare target runtime materialization
//---===//

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_PREPARERUNTIMETOLLVMPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {
class PrepareRuntimeToLLVMPass
    : public impl::PrepareRuntimeToLLVMPassBase<PrepareRuntimeToLLVMPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto layout = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layout) {
      module.emitError(
          "runtime lowering requires an explicit llvm.data_layout");
      return signalPassFailure();
    }
    auto parsed = llvm::DataLayout::parse(layout.getValue());
    if (!parsed) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
      return signalPassFailure();
    }
    if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)) ||
        failed(materializeEmbeddedSimulationDesign(module, *parsed)) ||
        failed(prepareRuntimeToLLVMByteGlobals(module)))
      return signalPassFailure();
    module->setAttr(preparedRuntimeLLVMAttr, UnitAttr::get(&getContext()));
  }
};
} // namespace

LogicalResult prepareRuntimeToLLVMByteGlobals(ModuleOp module) {
  MLIRContext *context = module.getContext();
  llvm::StringSet<> reservedSymbols;
  for (Operation &operation : *module.getBody())
    if (StringAttr symbol = SymbolTable::getSymbolName(&operation))
      reservedSymbols.insert(symbol.getValue());

  uint64_t ordinal = 0;
  auto allocateName = [&]() {
    std::string name;
    do {
      name = (Twine("__obelisk_rt_bytes.") + Twine(ordinal++)).str();
    } while (!reservedSymbols.insert(name).second);
    return StringAttr::get(context, name);
  };
  module.walk([&](Operation *operation) {
    SmallVector<StringRef> values;
    if (auto bytes = dyn_cast<runtime::RTBytesConstantOp>(operation))
      values.push_back(bytes.getValue());
    else if (auto environment =
                 dyn_cast<runtime::RTFormatEnvironmentOp>(operation)) {
      values.push_back(environment.getScope());
      values.push_back(environment.getLibraryCell());
      values.push_back(environment.getTimeSuffix());
    } else {
      return;
    }
    SmallVector<Attribute> names;
    names.reserve(values.size());
    for (StringRef value : values)
      names.push_back(value.empty() ? StringAttr::get(context, "")
                                    : allocateName());
    operation->setAttr(preparedRuntimeByteGlobalsAttr,
                       ArrayAttr::get(context, names));
  });
  return success();
}

void buildRuntimeToLLVMPipeline(OpPassManager &manager) {
  manager.addPass(createPrepareRuntimeToLLVMPass());
  manager.addPass(createConvertRuntimeToLLVMPass());
}

void registerRuntimeToLLVMPipeline() {
  PassPipelineRegistration<>(
      "convert-obelisk-runtime-to-llvm",
      "Prepare and convert runtime operations to the in-tree LLVM C ABI",
      buildRuntimeToLLVMPipeline);
}
} // namespace obelisk
