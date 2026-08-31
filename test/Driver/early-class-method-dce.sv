// RUN: obelisk -O0 -emit-sim %s -o - | FileCheck %s --implicit-check-not='obelisk_sim.hierarchical_name = "worker::dead"'

class worker;
  function int live();
    return 7;
  endfunction

  function int dead();
    return 9;
  endfunction

  virtual function int keep_vtable();
    return 11;
  endfunction
endclass

module early_class_method_dce;
  initial begin
    worker value;
    value = new;
    if (value.live() != 7)
      $fatal(1, "live method was not retained");
  end
endmodule

// CHECK: obelisk_sim.func private
// CHECK-SAME: obelisk_sim.hierarchical_name = "worker::live"
// CHECK: obelisk_sim.class.method
// CHECK-SAME: debug_name = "keep_vtable"
