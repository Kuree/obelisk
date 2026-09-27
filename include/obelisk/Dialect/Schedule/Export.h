#ifndef OBELISK_DIALECT_SCHEDULE_EXPORT_H
#define OBELISK_DIALECT_SCHEDULE_EXPORT_H
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "llvm/Support/JSON.h"
namespace obelisk::schedule {
/// Structured presentation of the verified graph. This is an inspection
/// format, not a second executable IR or a parser for MLIR assembly.
llvm::json::Object
exportGraph(ComputeGraphAttr graph, llvm::StringRef name,
            llvm::function_ref<mlir::FileLineColLoc(ComputeFragmentAttr)>
                sourceLocation);
} // namespace obelisk::schedule
#endif
