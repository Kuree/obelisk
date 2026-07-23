// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module simulation_wide_delay;
  logic [127:0] amount;
  initial
    #(amount) amount = 0;
endmodule

// CHECK: dynamic delay wider than 64 bits is not executable
