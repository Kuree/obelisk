// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_for;
  logic [7:0] value;
  initial begin
    for (int i = 2; i < 6; i++) begin
      if (i == 3)
        continue;
      if (i == 5)
        break;
      value = value + 1;
    end
  end
endmodule

// CHECK: obelisk_sim.func
// CHECK: arith.constant 2 : i32
// CHECK: cf.cond_br
// CHECK: arith.addi
// CHECK: cf.br
