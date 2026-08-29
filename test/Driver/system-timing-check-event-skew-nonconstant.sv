// RUN: not obelisk -emit-obelisk %s -o %t 2>&1 | FileCheck %s

module system_timing_check_event_skew_nonconstant(
    input wire reference, data, dynamic_flag);
  reg notifier;
  specify
    $timeskew(posedge reference, posedge data, 2, notifier,
              dynamic_flag, 0);
    $timeskew(posedge reference, posedge data, 2, notifier,
              1, dynamic_flag);
  endspecify
endmodule

// CHECK-COUNT-2: error: reference to non-constant variable 'dynamic_flag' is not allowed in a constant expression
