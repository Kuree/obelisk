//===- ProcessAllocation.h - Process frame allocation pool ------*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_PROCESSALLOCATION_H
#define TALOS_RUNTIME_LIB_PROCESSALLOCATION_H

#include <cstddef>

namespace talos::process {

void *allocateProcessMemory(size_t size, size_t alignment) noexcept;
void releaseProcessMemory(void *allocation, size_t size,
                          size_t alignment) noexcept;

} // namespace talos::process

#endif // TALOS_RUNTIME_LIB_PROCESSALLOCATION_H
