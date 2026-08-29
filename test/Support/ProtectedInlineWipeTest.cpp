//===- ProtectedInlineWipeTest.cpp - Inline wipe semantics --------------===//

#include "slang/util/SecureBuffer.h"

#include <cstdint>
#include <cstdio>

int main() {
  slang::SmallVector<char, 64> storage;
  storage.resize(64, 'I');

  uintptr_t objectBegin = reinterpret_cast<uintptr_t>(&storage);
  uintptr_t objectEnd = objectBegin + sizeof(storage);
  uintptr_t dataBegin = reinterpret_cast<uintptr_t>(storage.data());
  if (dataBegin < objectBegin || dataBegin + storage.capacity() > objectEnd)
    return 1;

  slang::detail::secureWipeProtectedBuffer(storage);
  for (size_t index = 0; index < storage.capacity(); ++index) {
    if (storage.data()[index] != 0)
      return 1;
  }

  std::puts("protected inline source wipe passed");
  return 0;
}
