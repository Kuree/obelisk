// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// IEEE 1800-2017 28.8 and 28.14: rtran is an always-enabled bidirectional
// channel. Every crossing applies Table 28-8, and parallel paths preserve the
// strongest propagated contribution. Array instances map vector bits onto
// their corresponding scalar pass devices.
module rtran_primitives;
  logic drive;
  wire n0, n1, n2, n3, n4, n5;
  assign (supply1, supply0) n0 = drive;
  rtran r0(n0, n1);
  rtran r1(n1, n2);
  rtran r2(n2, n3);
  rtran r3(n3, n4);
  rtran r4(n4, n5);

  wire mixed0, mixed1, mixed2;
  assign (supply1, supply0) mixed0 = drive;
  rtran rm0(mixed0, mixed1);
  tran tm0(mixed1, mixed2);

  wire alternate0, alternate_mid, alternate_out;
  assign (supply1, supply0) alternate0 = drive;
  tran ta0(alternate0, alternate_out);
  rtran ra0(alternate0, alternate_mid);
  rtran ra1(alternate_mid, alternate_out);

  logic reverse_drive;
  wire reverse_left, reverse_right;
  assign (supply1, supply0) reverse_right = reverse_drive;
  rtran reverse_path(reverse_left, reverse_right);

  wire [3:0] array_source;
  wire [3:0] array_result;
  logic array_oppose;
  assign (supply1, supply0) array_source[0] = drive;
  assign (supply1, supply0) array_source[1] = drive;
  assign (supply1, supply0) array_source[2] = drive;
  assign (supply1, supply0) array_source[3] = drive;
  assign (strong1, strong0) array_result = {4{array_oppose}};
  rtran arrayed[3:0](array_source, array_result);

  initial begin
    drive = 1'b1;
    reverse_drive = 1'b1;
    array_oppose = 1'b0;
    #1;
    if ({n0, n1, n2, n3, n4, n5} !== 6'b111111 ||
        {mixed0, mixed1, mixed2} !== 3'b111 ||
        {alternate0, alternate_mid, alternate_out} !== 3'b111 ||
        {reverse_left, reverse_right} !== 2'b11 ||
        array_source !== 4'b1111 || array_result !== 4'b0000)
      $fatal(0, "rtran logical propagation mismatch");
    $display("chain %v %v %v %v %v %v", n0, n1, n2, n3, n4, n5);
    $display("mixed %v %v %v", mixed0, mixed1, mixed2);
    $display("alternate %v %v %v", alternate0, alternate_mid,
             alternate_out);
    $display("reverse %v %v", reverse_left, reverse_right);
    $display("array source=%b result=%b", array_source, array_result);
  end
endmodule

// CHECK: chain Su1 Pu1 We1 Me1 Sm1 Sm1
// CHECK: mixed Su1 Pu1 Pu1
// CHECK: alternate Su1 Pu1 St1
// CHECK: reverse Pu1 Su1
// CHECK: array source=1111 result=0000
