//===- EvalNBAQueue.h - Ordered generated NBA staging ------------*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_EVALNBAQUEUE_H
#define OBELISK_RUNTIME_EVALNBAQUEUE_H

#include "obelisk/Runtime/Runtime.h"
#include <cstdint>

namespace obelisk::runtime {

// Generated code appends and drains these POD records directly. Only capacity
// growth crosses an allocator boundary; no actor or scheduler work happens
// there. Keeping every record preserves repeated writes and intermediate edges.
struct EvalNBARecord {
  uint64_t site;
  uint64_t offset;
  uint64_t value;
  uint64_t unknown;
};

struct EvalNBAQueue {
  EvalNBARecord *data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
  uint32_t error = OBELISK_RT_OK;
};

} // namespace obelisk::runtime

extern "C" obelisk_rt_status
obelisk_rt_v1_eval_nba_reserve(obelisk_rt_context *context,
                               obelisk::runtime::EvalNBAQueue *queue);

#endif
