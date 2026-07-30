//===- DesignBytecodeExecution.h - Bytecode execution state -----*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_DESIGNBYTECODEEXECUTION_H
#define OBELISK_RUNTIME_LIB_DESIGNBYTECODEEXECUTION_H

#include "DesignBytecodeImage.h"
#include "DesignBytecodeLogic.h"

struct obelisk_rt_context;

namespace obelisk::designbytecode {

struct Frame {
  Function function;
  uint32_t functionIndex = 0;
  uint8_t *data = nullptr;
  uint32_t id = 0;
};

Logic readLogic(const uint8_t *frame, const Layout &layout);
void writeLogic(uint8_t *frame, const Layout &layout, const Logic &value);

obelisk_rt_status invokeIntrinsic(const Image &image, Frame &frame,
                                  obelisk_rt_context *context,
                                  uint32_t siteIndex);

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODEEXECUTION_H
