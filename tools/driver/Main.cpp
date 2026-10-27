//===- Main.cpp - Production Talos driver entry point ------------------===//

#include "DriverMain.h"

#include "llvm/Support/InitLLVM.h"

int main(int argc, char **argv) {
  llvm::InitLLVM initLLVM(argc, argv);
  return talos::driver::runTalosDriver(argc, argv);
}
