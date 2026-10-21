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

// Keep uncommon feature service bodies out of the monolithic intrinsic
// dispatcher. This preserves the established bytecode interpreter layout for
// designs that never encode these intrinsics.
//
// ELF and wasm links use weak definitions. COFF needs strong definitions in
// the feature objects and weak references only in the dispatcher, which
// defines OBELISK_RT_BYTECODE_DISPATCHER before including this header, so an
// absent feature does not extract its archive member. Mach-O extracts members
// even for weak references, so its dispatcher defines weak fallbacks below.
#if defined(_WIN32) || defined(__APPLE__)
#if defined(OBELISK_RT_BYTECODE_DISPATCHER)
#define OBELISK_RT_FEATURE_SERVICE __attribute__((weak))
#else
#define OBELISK_RT_FEATURE_SERVICE
#endif
#elif defined(__clang__) || defined(__GNUC__)
#define OBELISK_RT_FEATURE_SERVICE __attribute__((weak))
#else
#define OBELISK_RT_FEATURE_SERVICE
#endif

OBELISK_RT_FEATURE_SERVICE obelisk_rt_status invokeDynamicScanIntrinsic(
    const Image &image, Frame &frame, obelisk_rt_context *context,
    IntrinsicSite site, uint32_t intrinsicId);

OBELISK_RT_FEATURE_SERVICE obelisk_rt_status invokeContainerBitstreamIntrinsic(
    const Image &image, Frame &frame, obelisk_rt_context *context,
    IntrinsicSite site, uint32_t siteIndex, uint32_t intrinsicId);

#undef OBELISK_RT_FEATURE_SERVICE

// A design that encodes these intrinsics references the feature's link
// anchor, which extracts the strong definition that replaces each fallback.
#if defined(__APPLE__) && defined(OBELISK_RT_BYTECODE_DISPATCHER)
obelisk_rt_status invokeDynamicScanIntrinsic(const Image &, Frame &,
                                             obelisk_rt_context *,
                                             IntrinsicSite, uint32_t) {
  return OBELISK_RT_INVALID_BYTECODE;
}

obelisk_rt_status invokeContainerBitstreamIntrinsic(const Image &, Frame &,
                                                    obelisk_rt_context *,
                                                    IntrinsicSite, uint32_t,
                                                    uint32_t) {
  return OBELISK_RT_INVALID_BYTECODE;
}
#endif

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODEEXECUTION_H
