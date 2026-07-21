// This smoke test uses the small mock UVM API under Inputs/mock_uvm. Its
// purpose is to keep UVM-shaped frontend constructs in the driver test suite
// without importing the external UVM source tree.
//
// RUN: obelisk -emit-slang --single-unit -I%S/Inputs/mock_uvm \
// RUN:   %S/Inputs/mock_uvm/uvm_pkg.sv %s \
// RUN:   | FileCheck %s --check-prefix=SLANG
// RUN: obelisk --single-unit -I%S/Inputs/mock_uvm \
// RUN:   %S/Inputs/mock_uvm/uvm_pkg.sv %s | obelisk-opt \
// RUN:   | FileCheck %s --check-prefix=OBELISK
//
// Each semantic scope is IsolatedFromAbove, so MLIR verifies the module with
// its thread pool. Multithreaded verification (the default) must accept the
// UVM-shaped hierarchy and produce exactly the same IR as a single-threaded
// run; a difference or failure would mean a scope leaks state across its
// isolation boundary.
// RUN: obelisk --single-unit -I%S/Inputs/mock_uvm \
// RUN:   %S/Inputs/mock_uvm/uvm_pkg.sv %s \
// RUN:   | obelisk-opt --mlir-disable-threading > %t.single
// RUN: obelisk --single-unit -I%S/Inputs/mock_uvm \
// RUN:   %S/Inputs/mock_uvm/uvm_pkg.sv %s | obelisk-opt > %t.threaded
// RUN: diff %t.single %t.threaded

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

// SLANG: slang.type.class_type attributes {{.*}}hierarchical_name = "uvm_pkg::uvm_object"
// SLANG: slang.type.class_type attributes {{.*}}hierarchical_name = "uvm_pkg::uvm_phase"
// SLANG: slang.type.class_type attributes {{.*}}hierarchical_name = "uvm_pkg::uvm_component"
// SLANG: slang.type.class_type attributes {{.*}}hierarchical_name = "uvm_pkg::uvm_test"
// SLANG: slang.type.class_type attributes {{.*}}hierarchical_name = "smoke_test"
// SLANG: slang.symbol.subroutine attributes {{.*}}hierarchical_name = "smoke_test::run_phase"
// SLANG: slang.timing.delay
// SLANG: slang.symbol.instance attributes {{.*}}hierarchical_name = "uvm_smoke"

// OBELISK: obelisk.sv.type.class_type attributes {{.*}}hierarchical_name = "uvm_pkg::uvm_object"
// OBELISK: obelisk.sv.type.class_type attributes {{.*}}hierarchical_name = "uvm_pkg::uvm_phase"
// OBELISK: obelisk.sv.type.class_type attributes {{.*}}hierarchical_name = "smoke_test"
// OBELISK: obelisk.sv.symbol.subroutine attributes {{.*}}hierarchical_name = "smoke_test::run_phase"
// OBELISK: obelisk.sv.timing.delay
// OBELISK: obelisk.sv.symbol.instance attributes {{.*}}hierarchical_name = "uvm_smoke"
// OBELISK: !obelisk.class_handle<@{{.*}}smoke_test>
// OBELISK-NOT: slang.
