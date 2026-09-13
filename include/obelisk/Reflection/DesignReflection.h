//===- DesignReflection.h - generated design reflection schema -*- C++ -*-===//

#ifndef OBELISK_REFLECTION_DESIGNREFLECTION_H
#define OBELISK_REFLECTION_DESIGNREFLECTION_H

#include "obelisk/Reflection/DesignReflectionLayout.h.inc"

#include <cstdint>

namespace obelisk::reflection {

enum class FrozenValueKind : uint8_t {
  Packed = 1,
};

inline constexpr uint32_t frozenValueKindMask = UINT32_C(0xff);
inline constexpr uint32_t frozenValueSigned = UINT32_C(1) << 8;
inline constexpr uint32_t frozenValueFourState = UINT32_C(1) << 9;

} // namespace obelisk::reflection

#endif // OBELISK_REFLECTION_DESIGNREFLECTION_H
