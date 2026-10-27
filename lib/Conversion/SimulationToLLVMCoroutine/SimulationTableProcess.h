//===- SimulationTableProcess.h - Table process admission and lowering ----===//
#ifndef TALOS_SIMULATIONTABLEPROCESS_H
#define TALOS_SIMULATIONTABLEPROCESS_H

#include "talos/Analysis/SimulationProcessFrameAnalysis.h"
#include "talos/Runtime/Runtime.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

namespace talos::detail {
struct NativeTableWait {
  uint32_t continuation, actionFlags;
  uint64_t frameOffset, frameSize;
  uint32_t kind, flags;
  llvm::SmallVector<talos_rt_table_watch_v1> watches;
};
struct NativeTableProcess {
  llvm::SmallVector<NativeTableWait> waits;
};
std::optional<NativeTableProcess>
analyzeTableProcess(sim::SimFuncOp function,
                    const SimulationProcessFrameAnalysis &analysis);
struct PreparedSuspendableProcess;
void materializeTableProcess(PreparedSuspendableProcess &process);
/// Declares the runtime entry shared by every table process. Call it once per
/// module, not per process: the declaration check scans the whole module.
void declareTableProcessRuntimeABI(mlir::ModuleOp module);
} // namespace talos::detail
#endif
