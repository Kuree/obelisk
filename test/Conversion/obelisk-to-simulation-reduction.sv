// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_reduction;
  logic [7:0] value;
  bit [7:0] bits;
  logic reduction_and;
  logic reduction_or;
  logic reduction_xor;
  logic reduction_nand;
  logic reduction_nor;
  logic reduction_xnor;
  bit bit_parity;
  always_comb begin
    reduction_and = &value;
    reduction_or = |value;
    reduction_xor = ^value;
    reduction_nand = ~&value;
    reduction_nor = ~|value;
    reduction_xnor = ~^value;
    bit_parity = ^bits;
  end
endmodule

// CHECK: obelisk_sim.logic.reduction and
// CHECK: obelisk_sim.logic.reduction or
// CHECK: obelisk_sim.logic.reduction xor
// CHECK: obelisk_sim.logic.reduction nand
// CHECK: obelisk_sim.logic.reduction nor
// CHECK: obelisk_sim.logic.reduction xnor
// CHECK: arith.shrui
// CHECK: arith.trunci
