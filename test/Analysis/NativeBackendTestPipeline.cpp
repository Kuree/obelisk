//===- NativeBackendTestPipeline.cpp - Slang input to native LLVM ---------===//
//
// Tests of native scheduling and code generation take SystemVerilog input
// through `talos -emit-slang` and continue here, so their checked-in input
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
  Option<int> simOptLevel{
      *this, "sim-opt-level",
      llvm::cl::desc("lower as the driver does at this optimization level "
                     "instead of one actor per process (-1)"),
      llvm::cl::init(-1)};
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
    llvm::report_fatal_error("test-talos-native-backend: unknown scheduler '" +
                             Twine(options.scheduler) + "'");
  StringRef stopAfter = options.stopAfter;
  if (!stopAfter.empty() && stopAfter != "lowering" && stopAfter != "graph")
    llvm::report_fatal_error("test-talos-native-backend: unknown stop-after '" +
                             Twine(stopAfter) + "'");
  std::string backendAttributes =
      llvm::formatv("schedule.native_scheduler = {0} : i32, "
                    "native.optimization_level = {1} : i32",
                    *mode, options.optLevel)
          .str();
  if (options.closedExecutable)
    backendAttributes += ", native.closed_executable";
  if (options.timing)
    backendAttributes += ", native.debug_timing";
  // By default lowering keeps one actor per process (no fusion or clocked
  // kernels) under the generic scheduler; the backend then clones evaluators
  // itself, so the input stays independent of the simulation optimization
  // pipeline. With sim-opt-level, lowering instead matches a native driver
  // build at that level, which represents Auto as Eval plus auto_requested
  // until the backend resolves it.
  std::string loweringAttributes = "schedule.native_scheduler = 1 : i32";
  std::string lowering = "lower-talos-to-sim{opt-level=0 vpi=off "
                         "static-specialization=off clocked-kernels=off "
                         "early-symbol-dce=true}";
  if (options.simOptLevel >= 0) {
    loweringAttributes =
        *mode == 0 ? "schedule.native_scheduler = 3 : i32, "
                     "schedule.native_scheduler.auto_requested"
                   : llvm::formatv("schedule.native_scheduler = {0} : i32",
                                   *mode)
                         .str();
    lowering = "lower-talos-to-sim{opt-level=" +
               std::to_string(options.simOptLevel) +
               " vpi=off static-specialization=" +
               std::string(*mode == 3 ? "on" : "auto") +
               " clocked-kernels=deferred early-symbol-dce=true}";
  }
  std::string pipeline =
      "convert-slang-to-talos,"
      "test-talos-set-module-attributes{attrs=\"{" +
      loweringAttributes +
      ", llvm.data_layout = \\\"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\\\", "
      "llvm.target_triple = \\\"x86_64-unknown-linux-gnu\\\"}\"}," +
      lowering +
      ",test-talos-set-module-attributes{attrs=\"{" + backendAttributes +
      "}\"}";
  if (stopAfter == "graph") {
    pipeline += ",simulation.design(simulation.func("
                "talos-sim-thread-process-cfg),"
                "talos-sim-build-compute-graph,"
                "talos-sim-verify-compute-graph)";
  } else if (stopAfter.empty() && *mode == 1) {
    pipeline += ",talos-sim-insert-gc-polls,"
                "convert-talos-sim-processes-to-llvm-coroutines";
  } else if (stopAfter.empty()) {
    // Mirrors the driver for a hybrid (non-generic) build: one SSA shape for
    // both tiers, bytecode for fragments that need it, and native-only
    // region rewrites.
    pipeline +=
        ",talos-sim-insert-gc-polls,"
        "simulation.design(simulation.func(talos-sim-thread-process-cfg),"
        "talos-sim-build-compute-graph,talos-sim-verify-compute-graph,"
        "talos-sim-materialize-compute-fusion{materialize-eval-bodies=true},"
        "talos-sim-build-compute-graph,"
        "talos-sim-materialize-graph-regions,"
        "talos-sim-specialize-static-state-nba,"
        "talos-sim-plan-static-superstep),"
        "encode-talos-sim-to-bytecode{vpi=off prune-native=true},"
        "simulation.design(talos-sim-optimize-native-regions),"
        "convert-talos-sim-processes-to-llvm-coroutines";
  }
  std::string error;
  llvm::raw_string_ostream errorStream(error);
  if (failed(parsePassPipeline(pipeline, manager, errorStream)))
    llvm::report_fatal_error("test-talos-native-backend: " + Twine(error));
}

} // namespace

namespace talos {
void registerNativeBackendTestPipeline() {
  PassPipelineRegistration<NativeBackendTestPipelineOptions>(
      "test-talos-native-backend",
      "Lower Slang dialect input to native LLVM as an executable build does",
      buildNativeBackendTestPipeline);
}
} // namespace talos
