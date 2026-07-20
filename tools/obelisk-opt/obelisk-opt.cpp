//===- obelisk-opt.cpp - Obelisk IR parser and optimizer driver ----------===//

#include "circt/InitAllDialects.h"

#include "obelisk/Conversion/MooreToObelisk.h"
#include "obelisk/Dialect/Sim/ObeliskDialect.h"

#include "mlir/IR/DialectRegistry.h"
#include "mlir/InitAllDialects.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

int main(int argc, char **argv) {
  obelisk::registerObeliskConversionPasses();

  mlir::DialectRegistry registry;
  mlir::registerAllDialects(registry);
  circt::registerAllDialects(registry);
  registry.insert<obelisk::ir::ObeliskDialect>();

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "Obelisk simulation IR optimizer\n", registry));
}
