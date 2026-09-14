//===- ClockKernelReadySet.h - Generated/native ingress contract -*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_CLOCKKERNELREADYSET_H
#define OBELISK_RUNTIME_CLOCKKERNELREADYSET_H

#include "obelisk/Runtime/ReadySet.h"
#include "obelisk/Runtime/Runtime.h"

namespace obelisk::runtime {

// Opt-in use of the clock kernel's reserved field. ABI layout and leaf count
// stay unchanged. Indexed kernels append ReadySetLayout's cache/summaries;
// legacy callers with reserved == 0 continue to supply only raw leaf words.
inline constexpr uint32_t indexedClockKernelReadySet = 1;

inline ReadySetLayout clockKernelReadySetLayout(uint32_t words) {
  return ReadySetLayout(static_cast<uint32_t>(
      std::min(uint64_t{words} * 64, uint64_t{UINT32_MAX})));
}

inline void
publishClockKernelReady(const obelisk_rt_native_clock_kernel &kernel,
                        uint32_t bit) {
  if (kernel.reserved & indexedClockKernelReadySet) {
    const ReadySetLayout layout =
        clockKernelReadySetLayout(kernel.ingress_word_count);
    ReadySetView(kernel.ingress_mask, layout).set(bit);
  } else {
    kernel.ingress_mask[bit / 64] |= uint64_t{1} << (bit % 64);
  }
}

} // namespace obelisk::runtime
#endif
