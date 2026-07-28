// RUN: obelisk -emit-obelisk %s \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --implicit-check-not=obelisk.sv.

interface coverage_interface;
  logic [1:0] value;
  covergroup interface_group with function sample(input logic [1:0] sample_value);
    cp: coverpoint sample_value {
      bins low = {0, 1};
      bins high = {[2:3]};
    }
  endgroup
  interface_group c;
  initial begin
    c = new;
    c.sample(value);
  end
endinterface

program coverage_program;
  logic value;
  covergroup program_group;
    cp: coverpoint value {
      bins clear = {0};
      bins set = {1};
    }
  endgroup
  program_group c;
  initial begin
    c = new;
    c.sample();
  end
endprogram

module coverage_module;
  logic value;
  coverage_interface intf();
  covergroup module_group;
    cp: coverpoint value {
      bins clear = {0};
      bins set = {1};
    }
  endgroup
  module_group c;
  initial begin
    c = new;
    c.sample();
  end
endmodule

// CHECK-COUNT-3: obelisk_sim.covergroup.decl
// CHECK: obelisk_sim.covergroup.create
// CHECK: obelisk_sim.covergroup.sample_enabled
// CHECK: obelisk_sim.covergroup.sample
