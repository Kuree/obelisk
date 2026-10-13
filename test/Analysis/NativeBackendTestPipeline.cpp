//===- NativeBackendTestPipeline.cpp - Slang input to native LLVM ---------===//
//
// Tests of native scheduling and code generation take SystemVerilog input
// through `obelisk -emit-slang` and continue here, so their checked-in input
// stays the small source design rather than its lowered simulation IR. The
// pipeline sets the module attributes the native driver sets around lowering
// (scheduler, target layout, optimization level, closed executable) and runs
// the backend passes of an executable build.
//
//===----------------------------------------------------------------------===//

#include "AnalysisTestPasses.h"

#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

struct NativeBackendTestPipelineOptions
    : public PassPipelineOptions<NativeBackendTestPipelineOptions> {
  Option<std::string> scheduler{
      *this, "scheduler",
      llvm::cl::desc("native scheduler: auto, generic, aot or eval"),
      llvm::cl::init("eval")};
  Option<unsigned> optLevel{*this, "opt-level",
                            llvm::cl::desc("native optimization level"),
                            llvm::cl::init(3)};
  Option<bool> closedExecutable{
      *this, "closed-executable",
      llvm::cl::desc("lower as a linked executable, which the driver does "
                     "above -O0"),
      llvm::cl::init(true)};
  Option<bool> timing{
      *this, "timing",
      llvm::cl::desc("print native planning traces, as --mlir-timing does"),
      llvm::cl::init(false)};
  Option<std::string> stopAfter{
      *this, "stop-after",
      llvm::cl::desc("stop after 'lowering' (simulation IR with the backend "
                     "attributes) or 'graph' (plus the verified compute "
                     "graph that native analyses read)"),
      llvm::cl::init("")};
};

void buildNativeBackendTestPipeline(
    OpPassManager &manager, const NativeBackendTestPipelineOptions &options) {
  std::optional<int> mode =
      llvm::StringSwitch<std::optional<int>>(options.scheduler)
          .Case("auto", 0)
          .Case("generic", 1)
          .Case("aot", 2)
          .Case("eval", 3)
          .Default(std::nullopt);
  if (!mode)
    llvm::report_fatal_error("test-obelisk-native-backend: unknown scheduler '" +
                             Twine(options.scheduler) + "'");
  StringRef stopAfter = options.stopAfter;
  if (!stopAfter.empty() && stopAfter != "lowering" && stopAfter != "graph")
    llvm::report_fatal_error("test-obelisk-native-backend: unknown stop-after '" +
                             Twine(stopAfter) + "'");
  std::string backendAttributes =
      llvm::formatv("schedule.native_scheduler = {0} : i32, "
                    "obelisk.native.optimization_level = {1} : i32",
                    *mode, options.optLevel)
          .str();
  if (options.closedExecutable)
    backendAttributes += ", obelisk.native.closed_executable";
  if (options.timing)
    backendAttributes += ", obelisk.debug.native_timing";
  // Lowering keeps one actor per process (no fusion or clocked kernels) under
  // the generic scheduler; the backend then clones evaluators itself, so the
  // input stays independent of the simulation optimization pipeline.
  std::string pipeline =
      "convert-slang-to-obelisk,"
      "test-obelisk-set-module-attributes{attrs=\"{"
      "schedule.native_scheduler = 1 : i32, "
      "llvm.data_layout = \\\"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\\\", "
      "llvm.target_triple = \\\"x86_64-unknown-linux-gnu\\\"}\"},"
      "lower-obelisk-to-sim{opt-level=0 vpi=off static-specialization=off "
      "clocked-kernels=off early-symbol-dce=true},"
      "test-obelisk-set-module-attributes{attrs=\"{" +
      backendAttributes + "}\"}";
  if (stopAfter == "graph") {
    pipeline += ",simulation.design(simulation.func("
                "obelisk-sim-thread-process-cfg),"
                "obelisk-sim-build-compute-graph,"
                "obelisk-sim-verify-compute-graph)";
  } else if (stopAfter.empty() && *mode == 1) {
    pipeline += ",obelisk-sim-insert-gc-polls,"
                "convert-obelisk-sim-processes-to-llvm-coroutines";
  } else if (stopAfter.empty()) {
    // Mirrors the driver for a hybrid (non-generic) build: one SSA shape for
    // both tiers, bytecode for fragments that need it, and native-only
    // region rewrites.
    pipeline +=
        ",obelisk-sim-insert-gc-polls,"
        "simulation.design(simulation.func(obelisk-sim-thread-process-cfg),"
        "obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,"
        "obelisk-sim-materialize-compute-fusion{materialize-eval-bodies=true},"
        "obelisk-sim-build-compute-graph,"
        "obelisk-sim-materialize-graph-regions,"
        "obelisk-sim-specialize-static-state-nba,"
        "obelisk-sim-plan-static-superstep),"
        "encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},"
        "simulation.design(obelisk-sim-optimize-native-regions),"
        "convert-obelisk-sim-processes-to-llvm-coroutines";
  }
  std::string error;
  llvm::raw_string_ostream errorStream(error);
  if (failed(parsePassPipeline(pipeline, manager, errorStream)))
    llvm::report_fatal_error("test-obelisk-native-backend: " + Twine(error));
}

} // namespace

namespace obelisk {
void registerNativeBackendTestPipeline() {
  PassPipelineRegistration<NativeBackendTestPipelineOptions>(
      "test-obelisk-native-backend",
      "Lower Slang dialect input to native LLVM as an executable build does",
      buildNativeBackendTestPipeline);
}
} // namespace obelisk
