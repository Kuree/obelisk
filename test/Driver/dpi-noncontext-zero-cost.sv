// RUN: %obelisk -emit-llvm %s -o - | FileCheck %s

module dpi_noncontext_zero_cost;
  import "DPI-C" pure function int fast_add(input int value);

  int result;
  initial result = fast_add(41);
endmodule

// CHECK-NOT: __obelisk_dpi_source_
// CHECK: call i32 @obelisk_rt_v1_import_call_noncontext_guarded(
// CHECK-NOT: call i32 @obelisk_rt_v1_import_call_guarded(
