#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/Transforms/InliningUtils.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

#include "obelisk/Dialect/Schedule/ScheduleDialect.cpp.inc"
#include "obelisk/Dialect/Schedule/ScheduleEnums.cpp.inc"
namespace obelisk::schedule {
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
} // namespace obelisk::schedule
#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Schedule/ScheduleAttrs.cpp.inc"
namespace obelisk::schedule {
namespace {
struct ReadyInlinerInterface final : DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;
  bool isLegalToInline(Operation *op, Region *, bool, IRMapping &) const final {
    // Cloning preserves ordered accesses to the same pending set. Regions
    // are formed after body inlining; other schedule actions retain their
    // existing callable/activation restrictions.
    return isa<NativeReadyReadOp, NativeReadyUpdateOp, NativeReadyCommitOp>(op);
  }
};
} // namespace
void ScheduleDialect::initialize() {
  addInterfaces<ReadyInlinerInterface>();
  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/Schedule/ScheduleOps.cpp.inc"
      >();
  addAttributes<
#define GET_ATTRDEF_LIST
#include "obelisk/Dialect/Schedule/ScheduleAttrs.cpp.inc"
      >();
}
} // namespace obelisk::schedule
