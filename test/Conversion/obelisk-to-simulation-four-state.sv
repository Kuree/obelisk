// RUN: obelisk -emit-obelisk %s > %t.semantic.mlir
// RUN: obelisk-opt %t.semantic.mlir --lower-obelisk-to-sim > %t.threaded.mlir
// RUN: obelisk-opt %t.semantic.mlir --lower-obelisk-to-sim --mlir-disable-threading > %t.single.mlir
// RUN: diff -u %t.single.mlir %t.threaded.mlir
// RUN: FileCheck %s < %t.threaded.mlir

module simulation_four_state;
  logic [7:0] four_state_input;
  logic condition;
  logic [7:0] value;
  logic [15:8] declared_range;
  logic [3:0] logic_read;
  bit [7:0] bits;
  bit [7:0] literal_bits;
  bit [3:0] bits_read;
  bit bit_read;
  logic [31:0] logic_index;
  int integer_index;

  initial begin
    if (condition)
      value = 1;
    while (condition) begin
      value = value + 1;
      break;
    end
    integer_index = 0;
    for (; condition; integer_index++) begin
      value = value + 1;
      break;
    end

    // Explicit four-state to two-state assignment keeps a conversion in IR.
    bits = four_state_input;
    literal_bits = 8'bxxzz_0101;

    // Integer and four-state indices are valid on rvalue and lvalue selects.
    logic_read = value[logic_index +: 4];
    logic_read = value[integer_index +: 4];
    logic_read = declared_range[logic_index +: 4];
    bit_read = value[logic_index];
    bit_read = value[integer_index];
    value[logic_index] = 1'b1;
    value[integer_index] = 1'b0;
    value[logic_index +: 4] = logic_read;
    value[integer_index +: 4] = logic_read;
    bit_read = bits[logic_index];
    bit_read = bits[integer_index];
    bits_read = bits[logic_index +: 4];
    bits_read = bits[integer_index +: 4];
    bit_read = literal_bits[logic_index];

    // Unknown, out-of-range, and partially out-of-range selections remain
    // explicit so the dynamic operation supplies X/zero/no-write behavior.
    logic_index = 'x;
    logic_read = value[logic_index +: 4];
    bit_read = bits[logic_index];
    value[logic_index] = 1'b1;
    integer_index = 6;
    logic_read = value[integer_index +: 4];
    integer_index = 99;
    value[integer_index] = 1'b0;
  end
endmodule

// CHECK-DAG: obelisk_sim.logic.constant 0 : i32, -1 : i32
// CHECK-DAG: arith.constant 5 : i8
// CHECK-DAG: arith.constant 6 : i32
// CHECK-DAG: arith.constant 99 : i32
// CHECK: obelisk_sim.logic.is_true
// CHECK: obelisk_sim.logic.is_true
// CHECK: obelisk_sim.logic.is_true
// CHECK: obelisk_sim.logic.to_bits
// CHECK-DAG: obelisk_sim.logic.binary sub {{.*}} : !obelisk_sim.logic<66>
// CHECK-DAG: obelisk_sim.logic.dyn_extract {{.*}} from {{.*}}!obelisk_sim.logic<66>
// CHECK-DAG: obelisk_sim.logic.dyn_extract {{.*}} from {{.*}}i66
// CHECK-DAG: obelisk_sim.ref.dyn_extract {{.*}} from {{.*}}!obelisk_sim.logic<66>
// CHECK-DAG: obelisk_sim.ref.dyn_extract {{.*}} from {{.*}}i66
// CHECK-DAG: obelisk_sim.bits.dyn_extract {{.*}} from {{.*}}!obelisk_sim.logic<66>
// CHECK-DAG: obelisk_sim.bits.dyn_extract {{.*}} from {{.*}}i66
