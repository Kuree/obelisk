//===- PassPlugin.cpp - opt plugin entry for the Talos LLVM passes -------===//
//
// The distribution ships no llvm/Passes/PassPlugin.h, so the plugin ABI record
// is spelled out here: it is the stable `PassPluginLibraryInfo` of LLVM's pass
// plugin interface (API version 2 in LLVM 22).
//
//===----------------------------------------------------------------------===//

#include "talos/LLVM/ClockedGuardLayout.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"

#include <cstdint>

namespace {
struct PassPluginLibraryInfo {
  uint32_t APIVersion;
  const char *PluginName;
  const char *PluginVersion;
  void (*RegisterPassBuilderCallbacks)(llvm::PassBuilder &);
};
} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {2, "TalosLLVMPasses", LLVM_VERSION_STRING,
          talos::registerTalosLLVMPasses};
}
