// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

// IEEE 1800-2017 6.6.7 and 10.3.3: a UDNT is one atomic value and its single
// continuous-assignment delay applies to every change of that value.

package delayed_udnt;
  function automatic real sum(input real drivers[]);
    sum = 0.0;
    foreach (drivers[i])
      sum += drivers[i];
  endfunction
  nettype real resolved_real with sum;
endpackage

module native_user_defined_nettype_delay;
  import delayed_udnt::*;
  real source = 0.0;
  resolved_real value;
  integer one_seen = 0;
  integer nan_seen = 0;
  integer nan_baseline;

  assign #2 value = source;
  always @(value) begin
    if (value == 1.0)
      one_seen++;
    if (value != value)
      nan_seen++;
  end

  initial begin
    // IEEE real equality treats the two signed zeros as equal.
    #1 source = -0.0;
    #1 source = 1.0;
    #1 source = 2.0;
    #1;
    if (one_seen != 0)
      $fatal(1, "inertial cancellation published the rejected value");
    #2;
    if (value != 2.0)
      $fatal(1, "delayed real UDNT value mismatch: %0.2f", value);

    // Every publication of NaN is a change, including the same bit pattern.
    nan_baseline = nan_seen;
    source = 0.0 / 0.0;
    #4;
    if (nan_seen != nan_baseline + 1)
      $fatal(1, "first NaN publication count was %0d", nan_seen);
    source = source;
    #4;
    if (nan_seen != nan_baseline + 2)
      $fatal(1, "repeated NaN publication count was %0d", nan_seen);
    $display("user-defined-nettype-delay: PASS");
    $finish;
  end
endmodule

// CHECK: user-defined-nettype-delay: PASS
