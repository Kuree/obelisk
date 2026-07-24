// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

module simulation_pure_system_functions;
  logic [11:0] value;
  logic control;
  int result;

  function automatic int side_effect();
    result++;
    return result;
  endfunction

  initial begin
    // The operand of $bits is unevaluated.
    result = $bits(side_effect());
    result = $clog2(value);
    result = $countbits(value, 1'b0, control, 1'bx, 1'bz);
    result = $countones(value);
    result = $onehot(value);
    result = $onehot0(value);
    result = $isunknown(value);
    value = $unsigned($signed(value));
  end
endmodule

// CHECK: arith.constant 32 : i32
// CHECK: obelisk_sim.logic.clog2
// CHECK: obelisk_sim.logic.count_bits
// CHECK: obelisk_sim.logic.count_bits
// CHECK: obelisk_sim.logic.count_bits
// CHECK: obelisk_sim.logic.count_bits
// CHECK: obelisk_sim.logic.count_bits
// CHECK-NOT: obelisk.sv.
