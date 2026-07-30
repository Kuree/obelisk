//===- DesignBytecodeRoots.h - Bytecode managed roots ----------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_DESIGNBYTECODEROOTS_H
#define OBELISK_RUNTIME_LIB_DESIGNBYTECODEROOTS_H

#include "DesignBytecodeExecution.h"
#include "RuntimeInternal.h"

namespace obelisk::designbytecode {

class ScopedBytecodeFrameRoots {
public:
  ScopedBytecodeFrameRoots(const Image &image, Frame &frame,
                           obelisk_rt_context *context);
  ScopedBytecodeFrameRoots(const ScopedBytecodeFrameRoots &) = delete;
  ScopedBytecodeFrameRoots &
  operator=(const ScopedBytecodeFrameRoots &) = delete;
  ~ScopedBytecodeFrameRoots();

  obelisk_rt_status getStatus() const { return status; }

private:
  struct Roots {
    const Image *image;
    Frame *frame;
  } roots;
  static void enumerate(void *environment, ManagedRootVisit visit,
                        void *visitorEnvironment);
  ManagedRootProvider provider;
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  obelisk_rt_status status = OBELISK_RT_OK;
  bool pushed = false;
};

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODEROOTS_H
