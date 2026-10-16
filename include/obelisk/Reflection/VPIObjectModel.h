//===- VPIObjectModel.h - generated IEEE VPI inventory ----------*- C++ -*-===//

#ifndef OBELISK_REFLECTION_VPIOBJECTMODEL_H
#define OBELISK_REFLECTION_VPIOBJECTMODEL_H

#include "obelisk/Reflection/VPIObjectModel.h.inc"

namespace obelisk::reflection {
// Source-only recipes preserve syntax, independently of any elaborated value.
// Concatenation and event lists have variable arity; ordinary operator arities
// come from the generated source-operator inventory.
inline constexpr VPISourceOperatorDescriptor vpiSourceEventOperators[] = {
    {"event_or", 35, 0, "or", 0, false},
    {"posedge", 39, 1, "posedge", 14, false},
    {"negedge", 40, 1, "negedge", 14, false},
    {"iff", 68, 2, "iff", 1, false}};
inline constexpr const VPISourceOperatorDescriptor *
findVPISourceOnlyOperator(uint32_t operation) {
  if (auto descriptor = findVPISourceOperator(operation))
    return descriptor;
  for (const auto &descriptor : vpiSourceEventOperators)
    if (descriptor.value == operation)
      return &descriptor;
  return nullptr;
}
inline constexpr bool validVPISourceOnlyArity(uint32_t operation,
                                              uint32_t operands) {
  if (auto descriptor = findVPISourceOperator(operation))
    return operands == descriptor->arity;
  switch (operation) {
  case 33: // vpiConcatOp
    return operands >= 1;
  case 34: // vpiMultiConcatOp
  case 35: // vpiEventOrOp
    return operands >= 2;
  case 39: // vpiPosedgeOp
  case 40: // vpiNegedgeOp
    return operands == 1;
  case 68: // vpiIffOp
    return operands == 2;
  default:
    return false;
  }
}
} // namespace obelisk::reflection

#endif // OBELISK_REFLECTION_VPIOBJECTMODEL_H
