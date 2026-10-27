//===- DesignBytecodeRoots.h - Bytecode managed roots ----------*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_DESIGNBYTECODEROOTS_H
#define TALOS_RUNTIME_LIB_DESIGNBYTECODEROOTS_H

#include "DesignBytecodeExecution.h"
#include "RuntimeInternal.h"

namespace talos::designbytecode {

class ScopedBytecodeFrameRoots {
public:
  ScopedBytecodeFrameRoots(const Image &image, Frame &frame,
                           talos_rt_context *context);
  ScopedBytecodeFrameRoots(const ScopedBytecodeFrameRoots &) = delete;
  ScopedBytecodeFrameRoots &
  operator=(const ScopedBytecodeFrameRoots &) = delete;
  ~ScopedBytecodeFrameRoots();

  talos_rt_status getStatus() const { return status; }

private:
  struct Roots {
    const Image *image;
    Frame *frame;
  } roots;
  static void enumerate(void *environment, ManagedRootVisit visit,
                        void *visitorEnvironment);
  ManagedRootProvider provider;
  talos_rt_gc_lane_v1 *lane = nullptr;
  talos_rt_status status = TALOS_RT_OK;
  bool pushed = false;
};

} // namespace talos::designbytecode

#endif // TALOS_RUNTIME_LIB_DESIGNBYTECODEROOTS_H
