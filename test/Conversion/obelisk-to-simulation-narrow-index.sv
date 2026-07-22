// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_narrow_index;
  logic [15:8] declared_range;
  logic [7:0] zero_based;
  logic unsigned_index;
  logic signed signed_index;
  bit bit_index;
  logic result;
  logic [3:0] part;

  initial begin
    // Boundary 8 and adjustment 3 do not fit in the one-bit source indices.
    // Widening before subtraction prevents their truncation from aliasing an
    // out-of-range source index into a valid normalized offset.
    result = declared_range[unsigned_index];
    part = zero_based[unsigned_index -: 4];
    result = zero_based[signed_index];
    result = declared_range[bit_index];
  end
endmodule

// CHECK-DAG: %[[THREE_LOGIC:.*]] = obelisk_sim.logic.constant 3 : i66, 0 : i66 : !obelisk_sim.logic<66>

// CHECK: %[[UNSIGNED_RAW:.*]] = obelisk_sim.ref.load {{.*}} -> !obelisk_sim.logic<1>
// CHECK: %[[UNSIGNED:.*]] = obelisk_sim.logic.resize %[[UNSIGNED_RAW]] signed = false : !obelisk_sim.logic<1> -> !obelisk_sim.logic<65>
// CHECK: obelisk_sim.array.extract_dynamic {{.*}}[%[[UNSIGNED]]]

// CHECK: %[[PART_RAW:.*]] = obelisk_sim.ref.load {{.*}} -> !obelisk_sim.logic<1>
// CHECK: %[[PART_INDEX:.*]] = obelisk_sim.logic.resize %[[PART_RAW]] signed = false : !obelisk_sim.logic<1> -> !obelisk_sim.logic<66>
// CHECK: %[[PART_LOW:.*]] = obelisk_sim.logic.binary sub %[[PART_INDEX]], %[[THREE_LOGIC]] : !obelisk_sim.logic<66>
// CHECK: obelisk_sim.logic.dyn_extract {{.*}} from %[[PART_LOW]]

// CHECK: %[[SIGNED_RAW:.*]] = obelisk_sim.ref.load {{.*}} -> !obelisk_sim.logic<1>
// CHECK: %[[SIGNED:.*]] = obelisk_sim.logic.resize %[[SIGNED_RAW]] signed = true : !obelisk_sim.logic<1> -> !obelisk_sim.logic<65>
// CHECK: obelisk_sim.array.extract_dynamic {{.*}}[%[[SIGNED]]]

// CHECK: %[[BIT_RAW:.*]] = obelisk_sim.ref.load {{.*}} -> i1
// CHECK: %[[BIT_INDEX:.*]] = arith.extui %[[BIT_RAW]] : i1 to i65
// CHECK: obelisk_sim.array.extract_dynamic {{.*}}[%[[BIT_INDEX]]]
