// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -emit-sim -O0 --vpi=off %s -o - | FileCheck %s --check-prefix=IR

// IEEE 1800-2017 20.12: levels is an integer expression evaluated when the
// assertion-control task executes. Zero selects the named scope and all
// descendants; a positive value bounds selection to that many instance
// levels, with the selected scope itself at level one.
module assertion_control_dynamic_child(
    input logic fire,
    input int phase
);
  always @(fire)
    if (phase != 0)
      child_check: assert (1'b0) else $display("child-%0d", phase);
endmodule

module assertion_control_dynamic_levels;
  logic fire;
  int phase;
  int levels;

  assertion_control_dynamic_child child(fire, phase);

  always @(fire)
    if (phase != 0)
      top_check: assert (1'b0) else $display("top-%0d", phase);

  initial begin
    fire = 0;
    phase = 0;
    #1;

    phase = 1;
    #1;
    levels = 1;
    $assertoff(levels);
    fire = 1;
    #0;

    $asserton(0);
    phase = 2;
    #1;
    levels = 2;
    $assertoff(levels);
    fire = 0;
    #0;

    phase = 3;
    #1;
    levels = 1;
    $asserton(levels);
    fire = 1;
    #0;

    $asserton(0);

    phase = 4;
    #1;
    levels = 1;
    $assertcontrol(4, 2, 1, levels);
    fire = 0;
    #0;

    $asserton(0);
  end
endmodule

// CHECK: child-1
// CHECK-NEXT: top-3
// CHECK-NEXT: child-4
// CHECK-NOT: child-2
// CHECK-NOT: top-2
// CHECK-NOT: child-3
// CHECK-NOT: top-4

// IR: arith.cmpi eq
// IR: arith.cmpi ugt
// IR: cf.cond_br
// IR: obelisk_sim.assert.control
