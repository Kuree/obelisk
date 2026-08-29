// RUN: %protect-obelisk --test-protect-secure-buffer | FileCheck %s
// RUN: %protect-inline-wipe-test | FileCheck %s --check-prefix=INLINE
// RUN: %python %source_root/test/Support/CheckSlangProtectWipe.py %slang_source_root

// CHECK: protected source buffer wipe passed
// INLINE: protected inline source wipe passed
