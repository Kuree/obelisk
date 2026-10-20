// RUN: obelisk -emit-slang %s -o - | FileCheck %s --implicit-check-not="timing_delay_fs = array<i64: -9223372036854775808>"

// long double is binary64 on MSVC. INT64_MAX rounds to 2^63 there, so the
// floating-to-integer conversion must use an exclusive upper bound.
module real_delay_boundary(input wire source,
                           output wire below_limit,
                           output wire at_limit);
  timeunit 1fs;
  timeprecision 1fs;
  assign below_limit = source;
  assign at_limit = source;
  specify
    (source => below_limit) = 9223372036854774784.0;
    (source => at_limit) = 9223372036854775808.0;
  endspecify
endmodule

// CHECK: timing_delay_fs = array<i64: 9223372036854774784>
