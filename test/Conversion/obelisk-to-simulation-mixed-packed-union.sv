// RUN: obelisk -emit-slang %s | obelisk-opt --test-obelisk-native-backend='scheduler=generic stop-after=lowering' | FileCheck %s

// IEEE 1800-2023 6.11.2, 7.3.1: the members of a packed union share one
// backing value. The automatic local below is promoted to SSA: the four-state
// backing keeps X/Z, a read through the two-state member sees them as zero,
// and a write through it replaces the selected bits with known ones, clearing
// only those unknown bits.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-NOT: simulation.ref.
// CHECK-DAG: %[[ZERO:.*]] = simulation.logic.constant 0 : i12, 0 : i12 : !simulation.logic<12>
// CHECK-DAG: %[[NIBBLE:.*]] = simulation.logic.constant -6 : i4, 0 : i4 : !simulation.logic<4>
// CHECK: %[[FOUR:.*]] = simulation.packed.flatten %arg1 : {{.*}} -> !simulation.logic<16>
// CHECK-NEXT: %[[TWO:.*]] = simulation.logic.to_bits %[[FOUR]] : !simulation.logic<16> -> i16
// CHECK: %[[PREVIOUS:.*]] = simulation.bits.dyn_extract %[[TWO]] from %{{.*}} : (i16, !simulation.logic<66>) -> i4
// CHECK-NEXT: %[[WRITTEN:.*]] = simulation.logic.insert %[[NIBBLE]] into %[[FOUR]] at 4 : (!simulation.logic<16>, !simulation.logic<4>) -> !simulation.logic<16>
// CHECK-NEXT: %[[PREVIOUS_LOGIC:.*]] = simulation.logic.from_bits %[[PREVIOUS]] : i4 -> !simulation.logic<4>
// CHECK-NEXT: simulation.logic.concat %[[WRITTEN]], %[[ZERO]], %[[PREVIOUS_LOGIC]]
// CHECK-NOT: simulation.ref.
// CHECK: simulation.return

module native_tier1_mixed_packed_union;
  typedef union packed {
    logic [15:0] four;
    bit [15:0] two;
  } mixed_t;
  function automatic logic [31:0] convert(input logic [15:0] seed,
                                          input logic [3:0] index);
    mixed_t value;
    bit [3:0] previous;
    value.four = seed;
    previous = value.two[index +: 4];
    value.two[7:4] = 4'ha;
    return {value.four, 12'b0, previous};
  endfunction
  logic [3:0] index;
  logic [31:0] result;
  always_comb result = convert(16'hzx91, index);
endmodule
