// RUN: obelisk -emit-slang %s 2>/dev/null | FileCheck %s

// IEEE 1800-2023 20.4.1: an explicit $unit names the compilation unit, which
// elaborates to no instance scope, so the call records the unit's time scale.
// An instance target is resolved through its elaborated scope instead.
timeunit 100ps / 10fs;
module top;
  timeunit 1ns / 1ps;
  initial $display($timeunit($unit), $timeprecision(top));
endmodule

// CHECK: callee_name = "$timeunit"
// CHECK-SAME: target_time_precision_fs = 10 : i64, target_time_unit_fs = 100000 : i64
// CHECK: callee_name = "$timeprecision"
// CHECK-NOT: target_time
