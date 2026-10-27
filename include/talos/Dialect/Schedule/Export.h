#ifndef TALOS_DIALECT_SCHEDULE_EXPORT_H
#define TALOS_DIALECT_SCHEDULE_EXPORT_H
#include "talos/Dialect/Schedule/ScheduleAttrs.h"
#include "llvm/Support/JSON.h"
namespace talos::schedule {
/// Structured presentation of the verified graph. This is an inspection
/// format, not a second executable IR or a parser for MLIR assembly.
llvm::json::Object
exportGraph(ComputeGraphAttr graph, llvm::StringRef name,
            llvm::function_ref<mlir::FileLineColLoc(ComputeFragmentAttr)>
                sourceLocation);
} // namespace talos::schedule
#endif
