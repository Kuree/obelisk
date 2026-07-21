// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_dynamic_delay;
  logic [7:0] value;
  int amount;
  initial begin
    #(amount) value = 8'h1;
  end
endmodule

// CHECK: obelisk_sim.time.scale {{%.*}} by {{[0-9]+}} signed = true : i32
// CHECK: obelisk_sim.suspend.delay
