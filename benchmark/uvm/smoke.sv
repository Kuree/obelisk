`include "uvm_macros.svh"

import uvm_pkg::*;

class obelisk_smoke_test extends uvm_test;
  `uvm_component_utils(obelisk_smoke_test)

  function new(string name = "obelisk_smoke_test",
               uvm_component parent = null);
    super.new(name, parent);
  endfunction

  virtual task run_phase(uvm_phase phase);
    phase.raise_objection(this);
    #1ns;
    phase.drop_objection(this);
    if ($realtime != 1ns)
      `uvm_fatal("OBELISK_SMOKE", "run_phase resumed at the wrong time")
    `uvm_info("OBELISK_SMOKE", "run_phase completed at 1ns", UVM_LOW)
  endtask
endclass

module obelisk_uvm_smoke_top;
  initial run_test("obelisk_smoke_test");
endmodule
