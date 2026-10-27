//===- DesignBytecodeExecution.h - Bytecode execution state -----*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_DESIGNBYTECODEEXECUTION_H
#define TALOS_RUNTIME_LIB_DESIGNBYTECODEEXECUTION_H

#include "DesignBytecodeImage.h"
#include "DesignBytecodeLogic.h"

struct talos_rt_context;

namespace talos::designbytecode {

struct Frame {
  Function function;
  uint32_t functionIndex = 0;
  uint8_t *data = nullptr;
  uint32_t id = 0;
};

Logic readLogic(const uint8_t *frame, const Layout &layout);
void writeLogic(uint8_t *frame, const Layout &layout, const Logic &value);

talos_rt_status invokeIntrinsic(const Image &image, Frame &frame,
                                  talos_rt_context *context,
                                  uint32_t siteIndex);

// Keep uncommon feature service bodies out of the monolithic intrinsic
// dispatcher. This preserves the established bytecode interpreter layout for
// designs that never encode these intrinsics.
//
// ELF and wasm links use weak definitions. COFF needs strong definitions in
// the feature objects and weak references only in the dispatcher, which
// defines TALOS_RT_BYTECODE_DISPATCHER before including this header, so an
// absent feature does not extract its archive member. Mach-O extracts members
// even for weak references, so its dispatcher defines weak fallbacks below.
#if defined(_WIN32) || defined(__APPLE__)
#if defined(TALOS_RT_BYTECODE_DISPATCHER)
#define TALOS_RT_FEATURE_SERVICE __attribute__((weak))
#else
#define TALOS_RT_FEATURE_SERVICE
#endif
#elif defined(__clang__) || defined(__GNUC__)
#define TALOS_RT_FEATURE_SERVICE __attribute__((weak))
#else
#define TALOS_RT_FEATURE_SERVICE
#endif

TALOS_RT_FEATURE_SERVICE talos_rt_status invokeDynamicScanIntrinsic(
    const Image &image, Frame &frame, talos_rt_context *context,
    IntrinsicSite site, uint32_t intrinsicId);

TALOS_RT_FEATURE_SERVICE talos_rt_status invokeContainerBitstreamIntrinsic(
    const Image &image, Frame &frame, talos_rt_context *context,
    IntrinsicSite site, uint32_t siteIndex, uint32_t intrinsicId);

#undef TALOS_RT_FEATURE_SERVICE

// A design that encodes these intrinsics references the feature's link
// anchor, which extracts the strong definition that replaces each fallback.
#if defined(__APPLE__) && defined(TALOS_RT_BYTECODE_DISPATCHER)
talos_rt_status invokeDynamicScanIntrinsic(const Image &, Frame &,
                                             talos_rt_context *,
                                             IntrinsicSite, uint32_t) {
  return TALOS_RT_INVALID_BYTECODE;
}

talos_rt_status invokeContainerBitstreamIntrinsic(const Image &, Frame &,
                                                    talos_rt_context *,
                                                    IntrinsicSite, uint32_t,
                                                    uint32_t) {
  return TALOS_RT_INVALID_BYTECODE;
}
#endif

} // namespace talos::designbytecode

#endif // TALOS_RUNTIME_LIB_DESIGNBYTECODEEXECUTION_H
