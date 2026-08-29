// RUN: not obelisk -fno-lto -O0 %s -o %t 2>&1 | FileCheck %s

module system_timing_check_event_skew_invalid(
    input wire reference, data);
  reg notifier;
  specify
    $timeskew(posedge reference, posedge data, 2, notifier);
    $timeskew(posedge reference, posedge data, 2, notifier, 0, 0);
    $timeskew(posedge reference, posedge data, 2, notifier, 1'bx, 0);
    $fullskew(posedge reference, posedge data, 2, 3, notifier, 0, 0);
  endspecify
endmodule

// IEEE 1800-2017 31.4.2/.3 default to timer mode; absent and explicit-zero
// event_based flags are executable. An unknown mode remains diagnostic rather
// than selecting timer or event behavior dynamically.
// CHECK-COUNT-1: error: IEEE 1800-2017 Clause 31 system timing checks are retained in semantic IR but are not executable yet
