#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "mlir/IR/Operation.h"
#define GET_SCHEDULE_FIELD_NAMES
#include "obelisk/Dialect/Schedule/ScheduleFields.h.inc"
namespace obelisk::schedule {
mlir::Attribute getAttribute(mlir::Operation *operation, Field field) {
  return operation->getAttr(getFieldName(field));
}
bool has(mlir::Operation *operation, Field field) {
  return operation->hasAttr(getFieldName(field));
}
void set(mlir::Operation *operation, Field field, mlir::Attribute value) {
  operation->setAttr(getFieldName(field), value);
}
mlir::Attribute remove(mlir::Operation *operation, Field field) {
  return operation->removeAttr(getFieldName(field));
}
} // namespace obelisk::schedule
