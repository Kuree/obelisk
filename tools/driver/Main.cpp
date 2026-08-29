//===- Main.cpp - Production Obelisk driver entry point ------------------===//

#include "DriverMain.h"

#include "llvm/Support/InitLLVM.h"

int main(int argc, char **argv) {
  llvm::InitLLVM initLLVM(argc, argv);
  return obelisk::driver::runObeliskDriver(argc, argv);
}
