#ifndef TALOS_DIALECT_SCHEDULE_FIELD_ENUMS_H
#define TALOS_DIALECT_SCHEDULE_FIELD_ENUMS_H
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>
#define GET_SCHEDULE_FIELD_ENUMS
#include "talos/Dialect/Schedule/ScheduleFields.h.inc"
namespace talos::schedule {
inline bool operator==(llvm::StringRef name, Field field) {
  return symbolizeField(name) == field;
}
inline bool operator==(Field field, llvm::StringRef name) {
  return name == field;
}
inline bool operator!=(llvm::StringRef name, Field field) {
  return !(name == field);
}
} // namespace talos::schedule
#endif
