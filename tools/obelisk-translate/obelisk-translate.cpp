//===- obelisk-translate.cpp - Obelisk source translators ---------------===//

#include "obelisk/Dialect/SDF/SDFDialect.h"
#include "obelisk/Frontend/SDF.h"

#include "mlir/IR/DialectRegistry.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/Support/SourceMgr.h"

using namespace mlir;

int main(int argc, char **argv) {
  TranslateToMLIRRegistration importSDF(
      "import-sdf", "import an IEEE 1800-2017 Clause 32 SDF delay file",
      [](llvm::SourceMgr &sourceMgr, MLIRContext *context) -> OwningOpRef<Operation *> {
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
