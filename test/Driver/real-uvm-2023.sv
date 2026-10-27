// REQUIRES: real-uvm
// RUN: talos -emit-slang --std=1800-2023 --single-unit -I%uvm \
// RUN:   %uvm/uvm_pkg.sv %s > %t.slang.mlir
// RUN: talos-opt --convert-slang-to-talos %t.slang.mlir \
// RUN:   | FileCheck %s

// Keep one full, unmodified UVM package elaboration for this language mode.
// Converting the emitted text also checks that the Slang dialect's printed
// form of the whole package parses back.
// This case concentrates on parameterized UVM classes, constructors, virtual
// tasks, calls, and timing controls.
class talos_uvm_test extends uvm_pkg::uvm_test;
  function new(string name = "talos_uvm_test",
               uvm_pkg::uvm_component parent = null);
    super.new(name, parent);
  endfunction

  virtual task run_phase(uvm_pkg::uvm_phase phase);
    phase.raise_objection(this);
    #1ns;
    phase.drop_objection(this);
  endtask
endclass

module talos_real_uvm_2023;
  initial begin
    uvm_pkg::uvm_event#(uvm_pkg::uvm_object) event_handle;
    talos_uvm_test test_handle = new;
    event_handle = new("event_handle");
  end
endmodule

// CHECK: talos.sv.symbol.package
// CHECK: talos.sv.type.class_type
// CHECK: hierarchical_name = "talos_uvm_test"
// CHECK: talos.sv.symbol.subroutine
// CHECK: hierarchical_name = "talos_uvm_test::run_phase"
// CHECK: talos.sv.timing.delay
// CHECK: talos.sv.symbol.instance
// CHECK-NOT: slang.
// CHECK-NOT: !slang.
