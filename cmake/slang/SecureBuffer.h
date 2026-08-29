//------------------------------------------------------------------------------
//! @file SecureBuffer.h
//! @brief Internal capacity-wide wipe for protected replacement source
//
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "slang/util/SmallVector.h"

namespace slang::detail {

/// IEEE 1800-2017 34.3.2 replacement text is plaintext. Volatile stores wipe
/// the complete inline or heap allocation rather than only the logical size.
inline void secureWipeProtectedBuffer(SmallVectorBase<char> &buffer) noexcept {
  auto *data = reinterpret_cast<volatile char *>(buffer.data());
  for (size_t index = 0; index < buffer.capacity(); ++index)
    data[index] = 0;
}

} // namespace slang::detail
