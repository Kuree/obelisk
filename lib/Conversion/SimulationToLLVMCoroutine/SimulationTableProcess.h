//===- SimulationTableProcess.h - Table process admission and lowering ----===//
#ifndef OBELISK_SIMULATIONTABLEPROCESS_H
#define OBELISK_SIMULATIONTABLEPROCESS_H

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Runtime/Runtime.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

namespace obelisk::detail {
inline constexpr llvm::StringLiteral tableWaitIndexAttr =
    "obelisk.native.table_wait";
struct NativeTableWait {
  uint32_t continuation, actionFlags;
  uint64_t frameOffset, frameSize;
  uint32_t kind, flags;
  llvm::SmallVector<obelisk_rt_table_watch_v1> watches;
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
} // namespace obelisk::detail
#endif
