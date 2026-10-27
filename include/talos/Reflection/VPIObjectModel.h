//===- VPIObjectModel.h - generated IEEE VPI inventory ----------*- C++ -*-===//

#ifndef TALOS_REFLECTION_VPIOBJECTMODEL_H
#define TALOS_REFLECTION_VPIOBJECTMODEL_H

#include "talos/Reflection/VPIObjectModel.h.inc"

namespace talos::reflection {
// Reverse uses and connectivity are derived from the owning source slot;
// only these forward relations belong to a compact expression's syntax tree.
inline constexpr bool isVPIExpressionChildRelation(uint32_t selector) {
  switch (static_cast<VPIRelationKind>(selector)) {
  case VPIRelationKind::OperandRel:
  case VPIRelationKind::ParentRel:
  case VPIRelationKind::IndexRel:
  case VPIRelationKind::LeftRangeRel:
  case VPIRelationKind::RightRangeRel:
  case VPIRelationKind::BaseExprRel:
  case VPIRelationKind::WidthExprRel:
    return true;
  default:
    return false;
  }
}

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
  // 37.59 detail 5 requires an exact target Typespec for every cast. An
  // unbound source-only recipe cannot provide that relation.
  if (operation == 67)
    return false;
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
// Integral expression results include their elaborated context. Declaration
// leaves retain their declared types; context-sensitive child operations must
// already include the propagated context before their arithmetic is evaluated
// (11.6 and 11.8.2). No implicit conversion occurrence is invented.
struct VPIIntegralExpressionShape {
  uint64_t width = 0;
  bool isSigned = false;
  bool fourState = false;
  uint32_t operation = 0; // Zero for declarations and self-determined leaves.
};

enum class VPIIntegralOperatorRule {
  Unsupported,
  Unary,
  Binary,
  Left,
  Comparison,
  Reduction,
  Logical,
  Conditional,
  Cast
};

inline constexpr VPIIntegralOperatorRule integralOperatorRule(uint32_t code) {
  using Rule = VPIIntegralOperatorRule;
  switch (code) {
  case 1:
  case 2:
  case 4:
    return Rule::Unary;
  case 11:
  case 12:
  case 13:
  case 24:
  case 25:
  case 28:
  case 29:
  case 30:
  case 31:
    return Rule::Binary;
  case 22:
  case 23:
  case 41:
  case 42:
  case 43:
    return Rule::Left;
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
  case 19:
  case 20:
  case 21:
  case 69:
  case 70:
    return Rule::Comparison;
  case 3:
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
  case 10:
    return Rule::Reduction;
  case 26:
  case 27:
  case 50:
    return Rule::Logical;
  case 32:
    return Rule::Conditional;
  case 67:
    return Rule::Cast;
  default:
    return Rule::Unsupported;
  }
}

inline constexpr bool hasIntegralOperatorContext(uint32_t code) {
  using Rule = VPIIntegralOperatorRule;
  auto rule = integralOperatorRule(code);
  return rule == Rule::Unary || rule == Rule::Binary || rule == Rule::Left ||
         rule == Rule::Conditional;
}

// 11.4.12 and 11.8: concatenations have unsigned results and self-determined
// operands.
// The multiplier is an immutable value proof supplied by the caller; its state
// domain does not contribute to the result. Zero-width forms are not yet part
// of the retained integral-type contract.
inline constexpr bool
validVPIIntegralConcatenation(uint32_t code, VPIIntegralExpressionShape result,
                              const VPIIntegralExpressionShape *operands,
                              uint32_t count, uint64_t repetitions = 1) {
  bool repeated = code == 34;
  if ((code != 33 && !repeated) || count < (repeated ? 2u : 1u) ||
      !result.width || result.width > UINT32_MAX || result.isSigned ||
      !repetitions || repetitions > UINT32_MAX ||
      (!repeated && repetitions != 1))
    return false;
  uint64_t width = 0;
  bool state = false;
  for (uint32_t index = 0; index != count; ++index) {
    if (!operands[index].width || operands[index].width > UINT32_MAX)
      return false;
    if (repeated && index == 0)
      continue;
    width += operands[index].width;
    if (width > UINT32_MAX)
      return false;
    state |= operands[index].fourState;
  }
  return width <= UINT32_MAX / repetitions &&
         width * repetitions == result.width && result.fourState == state;
}

inline constexpr bool
validVPIIntegralOperator(uint32_t code, VPIIntegralExpressionShape result,
                         const VPIIntegralExpressionShape *operands,
                         uint32_t count) {
  using Rule = VPIIntegralOperatorRule;
  auto rule = integralOperatorRule(code);
  auto descriptor = findVPISourceOperator(code);
  if (rule == Rule::Unsupported || !descriptor || descriptor->arity != count ||
      !result.width || result.width > UINT32_MAX)
    return false;
  bool state = false;
  for (uint32_t index = 0; index != count; ++index) {
    if (!operands[index].width || operands[index].width > UINT32_MAX)
      return false;
    state |= operands[index].fourState;
  }
  // 6.24.1: casting is assignment into the target type. Context-sensitive
  // arithmetic is evaluated at max(natural width, target width), retaining
  // the operand's signing and state domain until the final conversion.
  // The cast itself is self-determined with respect to its parent.
  if (rule == Rule::Cast)
    return !hasIntegralOperatorContext(operands[0].operation) ||
           operands[0].width >= result.width;
  if (code == 12 || code == 13 || code == 43)
    state = true; // Division by zero and integral negative powers can yield X.
  if (code == 16 || code == 17)
    state = false; // Case equality is always known.
  if (code == 69 || code == 70)
    state = operands[0].fourState; // RHS X/Z positions are wildcards.

  if (!hasIntegralOperatorContext(code)) {
    if (result.width != 1 || result.isSigned || result.fourState != state)
      return false;
  } else if (state && !result.fourState)
    return false;

  uint32_t first = rule == Rule::Conditional ? 1 : 0;
  uint32_t end = rule == Rule::Unary || rule == Rule::Left ? 1 : count;
  if (rule == Rule::Reduction || rule == Rule::Logical)
    return true; // Every operand is self-determined.
  VPIIntegralExpressionShape context = result;
  if (rule == Rule::Comparison) {
    context.width = operands[0].width > operands[1].width ? operands[0].width
                                                          : operands[1].width;
    context.isSigned = operands[0].isSigned && operands[1].isSigned;
    context.fourState = operands[0].fourState || operands[1].fourState;
  }
  for (uint32_t index = first; index != end; ++index) {
    auto operand = operands[index];
    if (operand.width > context.width ||
        (context.isSigned && !operand.isSigned))
      return false;
    if (hasIntegralOperatorContext(operand.operation) &&
        (operand.width != context.width ||
         operand.isSigned != context.isSigned ||
         operand.fourState != context.fourState))
      return false;
  }
  return true;
}
// The public bit-object class follows its declaration, not index constancy.
// This bounded scalar contract excludes wider packed elements and aggregates.
inline constexpr uint32_t vpiScalarBitSelectionKind(uint32_t parent) {
  using Kind = VPIObjectKind;
  switch (static_cast<Kind>(parent)) {
  case Kind::Net:
  case Kind::BitNet:
    return uint32_t(Kind::NetBit);
  case Kind::Reg:
  case Kind::BitVar:
    return uint32_t(Kind::RegBit);
  case Kind::Parameter:
  case Kind::SpecParam:
    return uint32_t(Kind::BitSelect);
  default:
    return 0;
  }
}
inline constexpr bool isVPIBitSelection(uint32_t kind) {
  return kind == uint32_t(VPIObjectKind::NetBit) ||
         kind == uint32_t(VPIObjectKind::RegBit) ||
         kind == uint32_t(VPIObjectKind::BitSelect);
}
} // namespace talos::reflection

#endif // TALOS_REFLECTION_VPIOBJECTMODEL_H
