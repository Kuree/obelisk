//===- EvalNBAQueue.h - Ordered generated NBA staging ------------*- C++
//-*-===//

#ifndef TALOS_RUNTIME_EVALNBAQUEUE_H
#define TALOS_RUNTIME_EVALNBAQUEUE_H

#include "talos/Runtime/Runtime.h"
#include <cstdint>

namespace talos::runtime {

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
  uint32_t error = TALOS_RT_OK;
};

} // namespace talos::runtime

extern "C" talos_rt_status
talos_rt_v1_eval_nba_reserve(talos_rt_context *context,
                               talos::runtime::EvalNBAQueue *queue);

#endif
