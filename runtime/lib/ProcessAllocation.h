//===- ProcessAllocation.h - Process frame allocation pool ------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_PROCESSALLOCATION_H
#define OBELISK_RUNTIME_LIB_PROCESSALLOCATION_H

#include <cstddef>

namespace obelisk::process {

void *allocateProcessMemory(size_t size, size_t alignment) noexcept;
void releaseProcessMemory(void *allocation, size_t size,
                          size_t alignment) noexcept;

} // namespace obelisk::process

#endif // OBELISK_RUNTIME_LIB_PROCESSALLOCATION_H
