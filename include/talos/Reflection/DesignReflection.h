//===- DesignReflection.h - generated design reflection schema -*- C++ -*-===//

#ifndef TALOS_REFLECTION_DESIGNREFLECTION_H
#define TALOS_REFLECTION_DESIGNREFLECTION_H

#include "talos/Reflection/DesignReflectionLayout.h.inc"

#include <cstdint>

namespace talos::reflection {

// ExpressionRootBinding.Mode keeps the traversal mode in bit zero. Bit one
// makes OwnerIndexAndTable a DefinitionMember index in its own namespace.
inline constexpr uint16_t expressionRootDefinitionMember = UINT16_C(1) << 1;
// Bit two selects the original concrete TypeParameter default. TemplateIndex
// is then a semantic type index, FirstLeaf a source filename offset (UINT32_MAX
// when absent), and Ordinal the source line. No recipe or leaf is referenced.
inline constexpr uint16_t expressionRootConcreteTypeDefault = UINT16_C(1) << 2;

enum class FrozenValueKind : uint8_t {
  Packed = 1,
};

inline constexpr uint32_t frozenValueKindMask = UINT32_C(0xff);
inline constexpr uint32_t frozenValueSigned = UINT32_C(1) << 8;
inline constexpr uint32_t frozenValueFourState = UINT32_C(1) << 9;

} // namespace talos::reflection

#endif // TALOS_REFLECTION_DESIGNREFLECTION_H
