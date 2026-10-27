#include "talos/Dialect/Schedule/ScheduleDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/Transforms/InliningUtils.h"
#include "talos/Dialect/Schedule/ScheduleAttrs.h"
#include "talos/Dialect/Schedule/ScheduleOps.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

#include "talos/Dialect/Schedule/ScheduleDialect.cpp.inc"
#include "talos/Dialect/Schedule/ScheduleEnums.cpp.inc"
namespace talos::schedule {
static FailureOr<SmallVector<uint64_t>> parseCoveragePoints(AsmParser &parser) {
  SmallVector<uint64_t> points;
  if (failed(parser.parseCommaSeparatedList(AsmParser::Delimiter::Square, [&] {
        uint64_t point;
        if (failed(parser.parseInteger(point)))
          return failure();
        points.push_back(point);
        return success();
      })))
    return failure();
  return points;
}
} // namespace talos::schedule
#define GET_ATTRDEF_CLASSES
#include "talos/Dialect/Schedule/ScheduleAttrs.cpp.inc"
namespace talos::schedule {
namespace {
struct ReadyInlinerInterface final : DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;
  bool isLegalToInline(Operation *op, Region *, bool, IRMapping &) const final {
    // Cloning preserves ordered accesses to the same pending set. Regions
    // are formed after body inlining; other schedule actions retain their
    // existing callable/activation restrictions.
    return isa<NativeReadyReadOp, NativeReadyUpdateOp, NativeReadyCommitOp,
               NativeReadyMergeOp>(op);
  }
};
} // namespace
void ScheduleDialect::initialize() {
  addInterfaces<ReadyInlinerInterface>();
  addOperations<
#define GET_OP_LIST
#include "talos/Dialect/Schedule/ScheduleOps.cpp.inc"
      >();
  addAttributes<
#define GET_ATTRDEF_LIST
#include "talos/Dialect/Schedule/ScheduleAttrs.cpp.inc"
      >();
}
} // namespace talos::schedule
