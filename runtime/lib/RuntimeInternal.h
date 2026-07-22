//===- RuntimeInternal.h - Shared native runtime internals -------*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H
#define OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H

#include "obelisk/Runtime/Runtime.h"

#include <array>
#include <cstdio>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

struct FileEntry {
  FILE *stream = nullptr;
  int lastError = 0;
  bool writable = false;
};

struct obelisk_rt_context {
  std::mutex mutex;
  std::array<FileEntry, 31> mcd;
  // 0x80000000, 0x80000001, and 0x80000002 are the IEEE predefined stdin,
  // stdout, and stderr descriptors. Dynamic descriptors begin at index 3.
  std::vector<FileEntry> files;
  std::vector<uint32_t> freeFiles;
  std::vector<uint32_t> freeMCDs;
  std::unordered_map<std::thread::id, std::string> lastErrors;

  obelisk_rt_context();
};

void setLastErrorUnlocked(obelisk_rt_context *context, std::string message);
void setLastError(obelisk_rt_context *context, std::string message);

template <typename Callable>
obelisk_rt_status guarded(obelisk_rt_context *context,
                          Callable &&callable) noexcept {
  try {
    return callable();
  } catch (const std::bad_alloc &) {
    setLastError(context, "runtime allocation failed");
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    setLastError(context, "unexpected runtime exception");
    return OBELISK_RT_IO_ERROR;
  }
}

obelisk_rt_status makeBuffer(std::string_view source,
                             obelisk_rt_buffer_v1 *output);
bool validBytes(const void *data, uint64_t size);
std::string hostErrorMessage(int error);

obelisk_rt_status writeUnlocked(obelisk_rt_context *context,
                                uint32_t descriptor, const void *data,
                                uint64_t size, uint64_t *outWritten);

// Fully validate immutable bytecode metadata without executing or mutating a
// process frame. Missing continuations are tier-unavailable; malformed
// programs are invalid bytecode.
obelisk_rt_status
obelisk_rt_validate_bytecode_program(const obelisk_rt_bytecode_v1 &program,
                                     uint32_t continuation) noexcept;

#endif // OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H
