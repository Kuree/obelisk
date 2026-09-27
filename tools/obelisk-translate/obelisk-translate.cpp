//===- obelisk-translate.cpp - Obelisk source translators ---------------===//

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "obelisk/Dialect/SDF/SDFDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Frontend/SDF.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "mlir/IR/DialectRegistry.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/Support/SourceMgr.h"

using namespace mlir;

int main(int argc, char **argv) {
  TranslateFromMLIRRegistration exportLLVM(
      "mlir-to-llvmir",
      "translate LLVM dialect IR with typed scheduling metadata",
      [](Operation *operation, llvm::raw_ostream &output) {
        llvm::LLVMContext context;
        auto module = translateModuleToLLVMIR(operation, context);
        if (!module)
          return failure();
        module->print(output, nullptr);
        return success();
      },
      [](DialectRegistry &registry) {
        registry
            .insert<LLVM::LLVMDialect, obelisk::schedule::ScheduleDialect>();
        registerBuiltinDialectTranslation(registry);
        registerLLVMDialectTranslation(registry);
      });

  TranslateToMLIRRegistration importSDF(
      "import-sdf", "import an IEEE 1800-2017 Clause 32 SDF delay file",
      [](llvm::SourceMgr &sourceMgr,
         MLIRContext *context) -> OwningOpRef<Operation *> {
        const llvm::MemoryBuffer *buffer = sourceMgr.getMemoryBuffer(1);
        auto module = obelisk::frontend::importSDF(
            buffer->getBufferIdentifier(), buffer->getBuffer(), *context);
        if (failed(module))
          return {};
        return OwningOpRef<Operation *>((*module).release().getOperation());
      },
      [](DialectRegistry &registry) {
        registry.insert<obelisk::sdf::ObeliskSDFDialect>();
      });

  return failed(mlirTranslateMain(argc, argv, "Obelisk translation driver"));
}
