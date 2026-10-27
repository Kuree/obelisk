`include "uvm_macros.svh"

import uvm_pkg::*;

class talos_smoke_test extends uvm_test;
  `uvm_component_utils(talos_smoke_test)

  function new(string name = "talos_smoke_test",
               uvm_component parent = null);
    super.new(name, parent);
  endfunction

  virtual task run_phase(uvm_phase phase);
    phase.raise_objection(this);
    #1ns;
    phase.drop_objection(this);
    if ($realtime != 1ns)
      `uvm_fatal("TALOS_SMOKE", "run_phase resumed at the wrong time")
    `uvm_info("TALOS_SMOKE", "run_phase completed at 1ns", UVM_LOW)
  endtask
endclass

module talos_uvm_smoke_top;
  initial run_test("talos_smoke_test");
endmodule
