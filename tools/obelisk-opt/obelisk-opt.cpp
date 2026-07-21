//===- obelisk-opt.cpp - Obelisk IR parser and optimizer driver ----------===//

#include "obelisk/Conversion/SlangToObelisk.h"
#include "obelisk/Dialect/Sim/ObeliskDialect.h"
#include "obelisk/Dialect/Slang/SlangDialect.h"

#include "mlir/IR/DialectRegistry.h"
#include "mlir/InitAllDialects.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

int main(int argc, char **argv) {
  obelisk::registerObeliskConversionPasses();

  mlir::DialectRegistry registry;
  mlir::registerAllDialects(registry);
  registry.insert<obelisk::slangir::SlangDialect,
                  obelisk::ir::ObeliskDialect>();

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "Obelisk simulation IR optimizer\n", registry));
}
