// RUN: %protect-obelisk --test-protect-secure-buffer | FileCheck %s
// RUN: %protect-inline-wipe-test | FileCheck %s --check-prefix=INLINE
// RUN: FileCheck %s --check-prefix=SLANG-HEADER --implicit-check-not='const SmallVector<char> mem;' --input-file=%slang_source_root/include/slang/text/SourceManager.h
// RUN: FileCheck %s --check-prefix=SLANG-SOURCE --input-file=%slang_source_root/source/text/SourceManager.cpp

// CHECK: protected source buffer wipe passed
// INLINE: protected inline source wipe passed

// The FileData destructor of the bundled slang wipes its mutable internal
// storage through the directly tested inline helper, without casting away
// constness.
// SLANG-HEADER: {{^        }}SmallVector<char> mem;
// SLANG-SOURCE-LABEL: {{^}}SourceManager::FileData::~FileData() {
// SLANG-SOURCE-NOT: const_cast
// SLANG-SOURCE: detail::secureWipeProtectedBuffer(mem);
// SLANG-SOURCE-NOT: const_cast
// SLANG-SOURCE: {{^}}}{{$}}
