// RUN: %split-file %s %t
// RUN: %talos --std=1800-2023 -emit-talos %t/acyclic.sv | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=ACCEPT
// RUN: %talos --std=1800-2023 -emit-talos %t/cycle.sv | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=CYCLE
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.9: partially overlapping selected paths form one
// transitive ordering node. Shared downstream nodes remain acyclic, while
// an edge back to any overlapping member is a cycle.
// ACCEPT: simulation.random.solve
// ACCEPT-SAME: SAMP
// CYCLE: error: solve before ordering contains a cycle

//--- acyclic.sv
class C;
  rand bit [7:0] lanes;
  constraint legal { lanes != 0; }
  constraint ordered {
    solve lanes[1:0] before lanes[4:3];
    solve lanes[2:1] before lanes[4:3];
    solve lanes[4:3] before lanes[5];
  }
endclass

module top;
  C obj = new;
  initial void'(obj.randomize());
endmodule

//--- cycle.sv
class C;
  rand bit [7:0] lanes;
  constraint ordered {
    solve lanes[1:0] before lanes[4:3];
    solve lanes[2:1] before lanes[4:3];
    solve lanes[4:3] before lanes[0];
  }
endclass

module top;
  C obj = new;
  initial void'(obj.randomize());
endmodule
