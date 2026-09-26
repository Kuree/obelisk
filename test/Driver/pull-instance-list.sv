// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// IEEE 1800-2017 A.3.1 and 28.10: every pull_gate_instance has exactly one
// output terminal. Several instances sharing strength are written as a
// comma-separated instance list outside each terminal's closing parenthesis.
module pull_instance_list;
  wire up0, up1;
  wire [2:0] down;

  pullup (weak1) p0(up0), p1(up1);
  pulldown pd2(down[2]), pd1(down[1]), pd0(down[0]);

  initial begin
    #1;
    if (up0 !== 1'b1 || up1 !== 1'b1 || down !== 3'b000)
      $fatal(0, "pull instance list mismatch: %b%b %b", up0, up1, down);
    $display("PULL LIST PASS");
  end
endmodule

// CHECK: PULL LIST PASS
