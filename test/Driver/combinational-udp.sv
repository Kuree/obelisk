// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ns

primitive udp_nonansi (out, a, b);
  output out;
  input a, b;
  table
    0 0 : 0;
    0 ? : 0;
    1 b : 1;
    x 0 : 1;
  endtable
endprimitive

primitive udp_ansi(output out, input in);
  table
    0 : 1;
    1 : 0;
    x : x;
  endtable
endprimitive

module combinational_udp;
  logic a, b;
  wire y;
  udp_nonansi u(y, a, b);

  logic [3:0] array_a, array_b;
  wire [3:0] array_y;
  udp_nonansi ua[3:0] (array_y, array_a, array_b);

  logic [3:0] ansi_in;
  wire [3:0] ansi_y;
  udp_ansi uansi[3:0] (ansi_y, ansi_in);

  // Explicit UDP drive strengths participate in ordinary net resolution.
  wire strength0_y, strength1_y;
  udp_nonansi (weak0, strong1) us0(strength0_y, a, b);
  pullup (pull1) strength0_competitor(strength0_y);
  udp_nonansi (strong0, weak1) us1(strength1_y, a, b);
  pulldown (pull0) strength1_competitor(strength1_y);

  logic delayed_a, delayed_b;
  wire delayed_y;
  udp_nonansi #(3, 5) ud(delayed_y, delayed_a, delayed_b);

  logic single_a, single_b;
  wire single_y;
  udp_nonansi #2 usingle(single_y, single_a, single_b);

  task automatic check(logic av, logic bv, logic expected);
    a = av;
    b = bv;
    #1;
    if (y !== expected)
      $fatal(0, "UDP mismatch: a=%b b=%b y=%b expected=%b", a, b, y,
             expected);
  endtask

  initial begin
    // Exact 0/1/X matching, ? wildcard, b known-only wildcard, missing-row X,
    // source-order overlap with the same output, and Z-to-X normalization.
    check(0, 0, 0);
    check(0, 1, 0);
    check(0, 'x, 0);
    check(0, 'z, 0);
    check(1, 0, 1);
    check(1, 1, 1);
    check(1, 'x, 'x);
    check(1, 'z, 'x);
    check('x, 0, 1);
    check('z, 0, 1);
    check('x, 1, 'x);
    check('z, 1, 'x);

    // Primitive arrays are scalarized in declaration order; this vector also
    // makes input ordering observable (`x0` matches while `0x` takes `?`).
    array_a = 4'b01xz;
    array_b = 4'b0010;
    ansi_in = 4'b01xz;
    #1;
    if (array_y !== 4'b01x1)
      $fatal(0, "UDP array mismatch: %b", array_y);
    if (ansi_y !== 4'b10xx)
      $fatal(0, "ANSI UDP array mismatch: %b", ansi_y);

    a = 0;
    b = 0;
    #1;
    if (strength0_y !== 1 || strength1_y !== 0)
      $fatal(0, "UDP strength0 mismatch: %b %b", strength0_y, strength1_y);
    a = 1;
    #1;
    if (strength0_y !== 1 || strength1_y !== 0)
      $fatal(0, "UDP strength1 mismatch: %b %b", strength0_y, strength1_y);

    delayed_a = 0;
    delayed_b = 0;
    #6;
    if (delayed_y !== 0) $fatal(0, "two-delay UDP initialization");
    delayed_a = 1;
    #2 if (delayed_y !== 0) $fatal(0, "UDP rise fired early");
    #1 if (delayed_y !== 1) $fatal(0, "UDP rise delay mismatch");
    delayed_a = 0;
    #4 if (delayed_y !== 1) $fatal(0, "UDP fall fired early");
    #1 if (delayed_y !== 0) $fatal(0, "UDP fall delay mismatch");

    // A same-value reevaluation cancels a pending inertial transition.
    delayed_a = 1;
    #1 delayed_a = 0;
    #6 if (delayed_y !== 0) $fatal(0, "UDP inertial cancellation mismatch");

    single_a = 0;
    single_b = 0;
    #3 if (single_y !== 0) $fatal(0, "single-delay UDP initialization");
    single_a = 1;
    #1 if (single_y !== 0) $fatal(0, "single-delay UDP fired early");
    #1 if (single_y !== 1) $fatal(0, "single-delay UDP mismatch");

    $display("COMBINATIONAL UDP PASS");
  end
endmodule

// CHECK: COMBINATIONAL UDP PASS
