// REQUIRES: real-uvm
// RUN: obelisk -emit-slang --std=1800-2023 --single-unit -I%uvm \
// RUN:   %uvm/uvm_pkg.sv %s > %t.slang.mlir
// RUN: obelisk-opt %t.slang.mlir -o %t.roundtrip.mlir
// RUN: obelisk-opt --convert-slang-to-obelisk %t.roundtrip.mlir \
// RUN:   | FileCheck %s

// Keep one full, unmodified UVM package elaboration for this language mode.
// This case concentrates on parameterized UVM classes, constructors, virtual
// tasks, calls, and timing controls.
class obelisk_uvm_test extends uvm_pkg::uvm_test;
  function new(string name = "obelisk_uvm_test",
               uvm_pkg::uvm_component parent = null);
    super.new(name, parent);
  endfunction

  virtual task run_phase(uvm_pkg::uvm_phase phase);
    phase.raise_objection(this);
    #1ns;
    phase.drop_objection(this);
  endtask
endclass

module obelisk_real_uvm_2023;
  initial begin
    uvm_pkg::uvm_event#(uvm_pkg::uvm_object) event_handle;
    obelisk_uvm_test test_handle = new;
    event_handle = new("event_handle");
  end
endmodule

// CHECK: obelisk.sv.symbol.package
// CHECK: obelisk.sv.type.class_type
// CHECK: hierarchical_name = "obelisk_uvm_test"
// CHECK: obelisk.sv.symbol.subroutine
// CHECK: hierarchical_name = "obelisk_uvm_test::run_phase"
// CHECK: obelisk.sv.timing.delay
// CHECK: obelisk.sv.symbol.instance
// CHECK-NOT: slang.
// CHECK-NOT: !slang.
