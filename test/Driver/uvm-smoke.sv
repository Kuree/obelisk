// This smoke test uses the small mock UVM API under Inputs/mock_uvm. Its
// purpose is to keep UVM-shaped frontend constructs in the driver test suite
// without importing the external UVM source tree.
//
// RUN: obelisk -emit-moore --single-unit -I%S/Inputs/mock_uvm \
// RUN:   %S/Inputs/mock_uvm/uvm_pkg.sv %s \
// RUN:   | FileCheck %s --check-prefix=MOORE
// RUN: obelisk --single-unit -I%S/Inputs/mock_uvm \
// RUN:   %S/Inputs/mock_uvm/uvm_pkg.sv %s | obelisk-opt \
// RUN:   | FileCheck %s --check-prefix=OBELISK

`include "uvm_macros.svh"

import uvm_pkg::*;

class smoke_test extends uvm_test;
  `uvm_component_utils(smoke_test)

  function new(string name = "smoke_test", uvm_component parent = null);
    super.new(name, parent);
  endfunction

  virtual task run_phase(uvm_phase phase);
    phase.raise_objection(this);
    `uvm_info("SMOKE", "running mock UVM test", UVM_LOW)
    #1ns;
    phase.drop_objection(this);
  endtask
endclass

module uvm_smoke;
  initial begin
    uvm_phase phase;
    smoke_test test_instance;

    phase = new("run");
    test_instance = new("smoke_test");
    test_instance.run_phase(phase);
  end
endmodule

// MOORE: moore.class.classdecl @"uvm_pkg::uvm_object"
// MOORE: moore.class.classdecl @"uvm_pkg::uvm_phase"
// MOORE: moore.class.classdecl @"uvm_pkg::uvm_component"
// MOORE: moore.class.classdecl @"uvm_pkg::uvm_test"
// MOORE: moore.class.classdecl @smoke_test
// MOORE: moore.coroutine private @"smoke_test::run_phase"
// MOORE: moore.call_coroutine @"uvm_pkg::uvm_phase::raise_objection"
// MOORE: moore.module @uvm_smoke

// OBELISK: obelisk.semantic.symbol_table @"uvm_pkg::uvm_object" class.classdecl
// OBELISK: obelisk.semantic.symbol_table @"uvm_pkg::uvm_phase" class.classdecl
// OBELISK: obelisk.semantic.symbol_table @smoke_test class.classdecl
// OBELISK: obelisk.semantic.isolated_symbol @"smoke_test::run_phase" coroutine
// OBELISK: obelisk.semantic.effect call_coroutine
// OBELISK: obelisk.semantic.graph_symbol @uvm_smoke module
// OBELISK: !obelisk.class_handle<@smoke_test>
// OBELISK: !sim.dstring
