//===- SignalSemantics.h - Shared signal occurrence semantics --*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_SIGNALSEMANTICS_H
#define OBELISK_RUNTIME_LIB_SIGNALSEMANTICS_H

#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include <cstdint>

namespace obelisk::runtime {

inline bool rangesOverlap(uint64_t left, uint64_t leftWidth, uint64_t right,
                          uint64_t rightWidth) {
  obelisk_rt_stable_handle_v1 leftHandle, rightHandle;
  if (!obelisk_rt_stable_handle_decode(left, &leftHandle) ||
      !obelisk_rt_stable_handle_decode(right, &rightHandle) ||
      leftHandle.kind != rightHandle.kind ||
      ((leftHandle.kind == OBELISK_RT_STABLE_HANDLE_AUTOMATIC ||
        leftHandle.kind == OBELISK_RT_STABLE_HANDLE_STATIC) &&
       leftHandle.id != rightHandle.id))
    return false;
  __int128 leftEnd = static_cast<__int128>(leftHandle.offset) + leftWidth;
  __int128 rightEnd = static_cast<__int128>(rightHandle.offset) + rightWidth;
  return leftWidth != 0 && rightWidth != 0 &&
         static_cast<__int128>(leftHandle.offset) < rightEnd &&
         static_cast<__int128>(rightHandle.offset) < leftEnd;
}

inline bool signalEdgeMatches(uint32_t requested, uint32_t observed) {
  switch (requested) {
  case OBELISK_RT_WAIT_EDGE_CHANGE:
    return (observed & OBELISK_RT_SIGNAL_CHANGE) != 0;
  case OBELISK_RT_WAIT_EDGE_POSEDGE:
    return (observed & OBELISK_RT_SIGNAL_POSEDGE) != 0;
  case OBELISK_RT_WAIT_EDGE_NEGEDGE:
    return (observed & OBELISK_RT_SIGNAL_NEGEDGE) != 0;
  case OBELISK_RT_WAIT_EDGE_BOTH:
    return (observed &
            (OBELISK_RT_SIGNAL_POSEDGE | OBELISK_RT_SIGNAL_NEGEDGE)) != 0;
  default:
    return false;
  }
}

inline uint32_t transitionEdges(bool oldValue, bool oldUnknown, bool newValue,
                                bool newUnknown) {
  if (oldValue == newValue && oldUnknown == newUnknown)
    return 0;
  uint32_t result = OBELISK_RT_SIGNAL_CHANGE;
  bool oldZero = !oldUnknown && !oldValue;
  bool oldOne = !oldUnknown && oldValue;
  bool newZero = !newUnknown && !newValue;
  bool newOne = !newUnknown && newValue;
  if ((oldZero && !newZero) || (oldUnknown && newOne))
    result |= OBELISK_RT_SIGNAL_POSEDGE;
  if ((oldOne && !newOne) || (oldUnknown && newZero))
    result |= OBELISK_RT_SIGNAL_NEGEDGE;
  return result;
}

} // namespace obelisk::runtime

#endif // OBELISK_RUNTIME_LIB_SIGNALSEMANTICS_H
