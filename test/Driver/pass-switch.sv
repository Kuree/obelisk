// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// IEEE 1800-2017 28.9 and 28.12.1: an unconditional tran is bidirectional,
// preserves bit correspondence (including reversed selects), and limits a
// strength crossing the switch to strong without weakening the local driver.
module pass_switch;
  logic left_drive, right_drive;
  wire left, right;
  wire [3:0] forward;
  wire [0:3] reversed;
  wire chain0, chain1, chain2;

  assign (supply1, supply0) left = left_drive;
  assign (supply1, supply0) right = right_drive;
  assign (supply1, supply0) chain0 = left_drive;
  assign forward = 4'b10xz;

  tran scalar_switch(left, right);
  tran vector_switch(forward, reversed);
  tran chain_switch0(chain0, chain1);
  tran chain_switch1(chain1, chain2);

  task check(input logic l, input logic r, input logic expected_left,
             input logic expected_right);
    left_drive = l;
    right_drive = r;
    #1;
    if (left !== expected_left || right !== expected_right)
      $fatal(0, "tran resolution: %b %b -> %b %b", l, r, left, right);
  endtask

  initial begin
    check(1'bz, 1'b1, 1'b1, 1'b1);
    check(1'b0, 1'b1, 1'b0, 1'b1);
    check(1'b1, 1'b0, 1'b1, 1'b0);
    check(1'bx, 1'bz, 1'bx, 1'bx);
    check(1'b1, 1'bz, 1'b1, 1'b1);
    if (chain0 !== 1'b1 || chain1 !== 1'b1 || chain2 !== 1'b1)
      $fatal(0, "tran chain did not resolve: %b %b %b", chain0, chain1,
             chain2);
    if (reversed !== 4'b10xz)
      $fatal(0, "reversed tran mapping: %b", reversed);
    left_drive = 1'bz;
    right_drive = 1'bz;
    #1;
    force left = 1'b0;
    #1;
    if (left !== 1'b0 || right !== 1'b0)
      $fatal(0, "force did not propagate through tran: %b %b", left, right);
    release left;
    #1;
    if (left !== 1'bz || right !== 1'bz)
      $fatal(0, "release did not resolve tran component: %b %b", left,
             right);
    $display("PASS SWITCH PASS");
  end
endmodule

// CHECK: PASS SWITCH PASS
