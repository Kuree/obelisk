// RUN: not obelisk -O0 %s -o %t 2>&1 | FileCheck %s

module system_timing_check_nochange_invalid(
    input wire reference, data, select,
    input wire [1:0] vector_data,
    input integer dynamic_offset);
  specify
    $nochange(edge [01, 0x, x1] reference, data, 0, 0);
    $nochange(posedge reference, data, dynamic_offset, 0);
    $nochange(posedge reference, vector_data[select], 0, 0);
  endspecify
endmodule

// IEEE 1800-2017 31.4.6 forbids reference edge-control descriptors. Dynamic
// offsets and indirect data selections cannot use the specialized static,
// direct occurrence actor and remain explicit diagnostics rather than a
// generic runtime timing interpreter.
// CHECK: error: trigger for first argument to '$nochange' must be posedge or negedge only
// CHECK: error: reference to non-constant variable 'dynamic_offset' is not allowed in a constant expression
