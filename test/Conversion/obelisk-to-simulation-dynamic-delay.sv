// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_dynamic_delay;
  logic [7:0] value;
  int amount;
  initial begin
    #(amount) value = 8'h1;
  end
endmodule

// CHECK: arith.cmpi sge
// CHECK: arith.select
// CHECK: arith.extui {{%.*}} : i32 to i64
// CHECK: arith.cmpi ule
// CHECK: arith.select
// CHECK: obelisk_sim.time.scale {{%.*}} by {{[0-9]+}} signed = false : i64
// CHECK: obelisk_sim.suspend.delay
