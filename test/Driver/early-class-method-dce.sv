// RUN: obelisk -O0 -emit-sim %s -o - | FileCheck %s --implicit-check-not='simulation.hierarchical_name = "worker::dead"' --implicit-check-not='simulation.hierarchical_name = "worker::dead_virtual"'

class worker;
  function int live();
    return 7;
  endfunction

  function int dead();
    return 9;
  endfunction

  virtual function int dead_virtual();
    return 11;
  endfunction

  virtual function int live_virtual();
    return 13;
  endfunction
endclass

module early_class_method_dce;
  initial begin
    worker value;
    value = new;
    if (value.live() != 7)
      $fatal(1, "live method was not retained");
    if (value.live_virtual() != 13)
      $fatal(1, "live virtual method was not retained");
  end
endmodule

// CHECK: simulation.func private
// CHECK-SAME: simulation.hierarchical_name = "worker::live"
// CHECK: simulation.class.method
// CHECK-SAME: slot 0
// CHECK-SAME: debug_name = "live_virtual"
