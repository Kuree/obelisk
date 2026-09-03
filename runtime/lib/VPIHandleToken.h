//===- VPIHandleToken.h - Non-reusable opaque handle tokens -----*- C++ -*-===//

#ifndef OBELISK_RUNTIME_VPI_HANDLE_TOKEN_H
#define OBELISK_RUNTIME_VPI_HANDLE_TOKEN_H

#include <atomic>
#include <cstdint>
#include <limits>

namespace obelisk::runtime {

inline bool allocateVPIHandleToken(std::atomic<uintptr_t> &next,
                                   uintptr_t &result) noexcept {
  uintptr_t candidate = next.load(std::memory_order_relaxed);
  const uintptr_t exhausted = std::numeric_limits<uintptr_t>::max();
  while (candidate != exhausted &&
         !next.compare_exchange_weak(candidate, candidate + 1,
                                     std::memory_order_relaxed,
                                     std::memory_order_relaxed)) {
  }
  if (candidate == exhausted)
    return false;
  result = candidate;
  return true;
}

} // namespace obelisk::runtime

#endif
