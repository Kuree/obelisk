// RUN: not obelisk -fno-lto -O0 %s -o %t 2>&1 | FileCheck %s

module system_timing_check_clock_control_invalid(
    input wire reference, data);
  specify
    $width(edge [01] reference, 5, 0);
    $skew(posedge reference, posedge data, -1);
  endspecify
endmodule

// IEEE 1800-2017 31.4.4 needs a unique inverse for the implicit $width data
// event. A proper Clause 31.5 descriptor subset cannot use the standard-edge
// subscription ABI and must remain diagnosed instead of being approximated.
// A negative 31.4.1 limit is likewise outside the static nonnegative tranche.
// CHECK-COUNT-2: error: IEEE 1800-2017 Clause 31 system timing checks are retained in semantic IR but are not executable yet
