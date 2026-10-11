// RUN: %llvm_dist/bin/llvm-nm %runtime_archive | FileCheck %s

// This is a link-boundary regression, not SystemVerilog source. Z3 may be
// linked into compiler tools when enabled, but no Z3 C or C++ API symbol may
// enter the target runtime archive.

// CHECK-NOT: Z3_
// CHECK-NOT: _ZN2z3
// CHECK-NOT: libz3
// CHECK: obelisk_rt_v1_context_create
// CHECK-NOT: Z3_
// CHECK-NOT: _ZN2z3
// CHECK-NOT: libz3
