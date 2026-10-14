// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 19.3: a covergroup may be declared in a module, an interface or a
// program. Each declaration lowers to its own covergroup declaration, and
// each instance handle is typed by, created from, and sampled through its own
// declaration in its enclosing scope.
// CHECK: simulation.covergroup.decl @[[IFACE:__obelisk_covergroup_interface_group_[0-9A-F]+]]
// CHECK: simulation.covergroup.decl @[[MODULE:__obelisk_covergroup_module_group_[0-9A-F]+]]
// CHECK: simulation.covergroup.decl @[[PROGRAM:__obelisk_covergroup_program_group_[0-9A-F]+]]
// CHECK: simulation.storage.decl {{.*}}!simulation.covergroup_handle<@[[IFACE]]> design hierarchy "coverage_module.intf.c"
// CHECK: simulation.storage.decl {{.*}}!simulation.covergroup_handle<@[[MODULE]]> design hierarchy "coverage_module.c"
// CHECK: simulation.storage.decl {{.*}}!simulation.covergroup_handle<@[[PROGRAM]]> design hierarchy "coverage_program.c"
// CHECK: simulation.covergroup.create {{.*}} from @[[IFACE]]
// CHECK: simulation.covergroup.sample_enabled {{.*}}@[[IFACE]]
// CHECK: simulation.covergroup.sample {{.*}}@[[IFACE]]
// CHECK: simulation.covergroup.create {{.*}} from @[[MODULE]]
// CHECK: simulation.covergroup.sample_enabled {{.*}}@[[MODULE]]
// CHECK: simulation.covergroup.sample {{.*}}@[[MODULE]]
// CHECK: simulation.covergroup.create {{.*}} from @[[PROGRAM]]
// CHECK: simulation.covergroup.sample_enabled {{.*}}@[[PROGRAM]]
// CHECK: simulation.covergroup.sample {{.*}}@[[PROGRAM]]
// CHECK-NOT: obelisk.sv.

interface coverage_interface;
  logic value;
  covergroup interface_group;
    cp: coverpoint value { bins low = {0}; bins high = {1}; }
  endgroup
  interface_group c = new;
  initial c.sample();
endinterface

module coverage_module;
  logic value;
  coverage_interface intf();
  covergroup module_group;
    cp: coverpoint value { bins clear = {0}; bins set = {1}; }
  endgroup
  module_group c = new;
  initial c.sample();
endmodule

program coverage_program;
  logic value;
  covergroup program_group;
    cp: coverpoint value { bins clear = {0}; bins set = {1}; }
  endgroup
  program_group c = new;
  initial c.sample();
endprogram
