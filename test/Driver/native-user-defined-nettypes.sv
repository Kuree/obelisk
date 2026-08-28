// XFAIL: *
// Slang v11 does not accept package-qualified user-defined nettypes here.
// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

// IEEE 1800-2017 6.6.7 and 6.7.3: user-defined nettypes are atomic,
// resolve all driver contributions, and invoke resolution at time zero.

package udnt_types;
  typedef logic [7:0] byte_t;
  typedef struct {
    logic [7:0] value;
    logic valid;
  } payload_t;

  function automatic byte_t maximum(input byte_t drivers[]);
    maximum = '0;
    foreach (drivers[i])
      if (drivers[i] > maximum)
        maximum = drivers[i];
  endfunction

  function automatic payload_t select_valid(input payload_t drivers[]);
    select_valid = '{default: '0};
    foreach (drivers[i])
      if (drivers[i].valid)
        select_valid = drivers[i];
  endfunction

  function automatic real real_sum(input real drivers[]);
    real_sum = 0.0;
    foreach (drivers[i])
      real_sum += drivers[i];
  endfunction

  function automatic shortreal short_sum(input shortreal drivers[]);
    short_sum = 0.0;
    foreach (drivers[i])
      short_sum += drivers[i];
  endfunction

  function automatic byte_t empty_default(input byte_t drivers[]);
    return drivers.size() ? drivers[0] : 8'ha5;
  endfunction

  nettype byte_t resolved_byte with maximum;
  nettype resolved_byte resolved_byte_alias;
  nettype payload_t resolved_payload with select_valid;
  nettype real resolved_real with real_sum;
  nettype shortreal resolved_shortreal with short_sum;
  nettype byte_t resolved_empty with empty_default;
endpackage

module real_source #(real VALUE = 0.0)
    (output udnt_types::resolved_real out);
  assign out = VALUE;
endmodule

program reactive_source(output udnt_types::resolved_real out);
  real value = 2.0;
  assign out = value;
  initial begin
    #1 value = 4.0;
    // The module-side checks finish at time 3. Keep this program instance
    // alive until then instead of invoking its implicit 24.7 $exit at time 1.
    #10;
  end
endprogram

module native_user_defined_nettypes;
  import udnt_types::*;

  logic [7:0] packed_a = 8'h12;
  logic [7:0] packed_b = 8'h34;
  resolved_byte_alias packed_net;
  payload_t aggregate_a = '{8'h12, 1'b0};
  payload_t aggregate_b = '{8'h56, 1'b1};
  resolved_payload aggregate_net;
  real real_a = 1.25;
  real real_b = 2.75;
  resolved_real real_net;
  shortreal short_a = 1.25;
  shortreal short_b = 2.75;
  resolved_shortreal short_net;
  resolved_real port_net;
  resolved_real reactive_net;
  resolved_empty empty_net;

  assign packed_net = packed_a;
  assign packed_net = packed_b;
  assign aggregate_net = aggregate_a;
  assign aggregate_net = aggregate_b;
  assign real_net = real_a;
  assign real_net = real_b;
  assign short_net = short_a;
  assign short_net = short_b;
  real_source #(.VALUE(1.25)) port_a(port_net);
  real_source #(.VALUE(2.75)) port_b(port_net);
  assign reactive_net = 1.0;
  reactive_source reactive_driver(reactive_net);

  initial begin
    #0;
    if (empty_net !== 8'ha5)
      $fatal(1, "zero-driver resolver did not run at time zero");
    #2;
    if (packed_net !== 8'h34 || aggregate_net.value !== 8'h56 ||
        aggregate_net.valid !== 1'b1 || real_net != 4.0 ||
        short_net != 4.0 || port_net != 4.0 || reactive_net != 5.0)
      $fatal(1, "user-defined nettype resolution mismatch");
    packed_b = 8'h08;
    real_a = 5.0;
    #1;
    if (packed_net !== 8'h12 || real_net != 7.75)
      $fatal(1, "updated user-defined nettype resolution mismatch");
    $display("user-defined-nettypes: PASS");
    $finish;
  end
endmodule

// CHECK: user-defined-nettypes: PASS
