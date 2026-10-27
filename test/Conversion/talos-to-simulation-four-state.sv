// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir --lower-talos-to-sim='vpi=read' > %t.threaded.mlir
// RUN: talos-opt %t.talos.mlir --lower-talos-to-sim='vpi=read' --mlir-disable-threading > %t.single.mlir
// RUN: diff -u %t.single.mlir %t.threaded.mlir
// RUN: FileCheck %s < %t.threaded.mlir

// CHECK-DAG: simulation.logic.constant 0 : i32, -1 : i32
// CHECK-DAG: arith.constant 5 : i8
// CHECK-DAG: arith.constant 6 : i32
// CHECK-DAG: arith.constant 99 : i32
// CHECK-COUNT-3: simulation.logic.is_true
// CHECK: simulation.logic.to_bits
// CHECK-DAG: simulation.logic.binary sub {{.*}} : !simulation.logic<66>
// CHECK-DAG: simulation.logic.dyn_extract {{.*}} from {{.*}}!simulation.logic<66>
// CHECK-DAG: simulation.logic.dyn_extract {{.*}} from {{.*}}i66
// CHECK-DAG: simulation.ref.dyn_extract {{.*}} from {{.*}}!simulation.logic<66>
// CHECK-DAG: simulation.ref.dyn_extract {{.*}} from {{.*}}i66
// CHECK-DAG: simulation.bits.dyn_extract {{.*}} from {{.*}}!simulation.logic<66>
// CHECK-DAG: simulation.bits.dyn_extract {{.*}} from {{.*}}i66
// CHECK-NOT: talos.sv.

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
    // X and Z conditions take the false branch and leave the loops.
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
    // Two-state destinations take the two-state projection of X and Z.
    bits = four_state_input;
    literal_bits = 8'bxxzz0101;
    // Four-state and two-state, logic and bit indexed selects and writes,
    // including a declared range that does not start at zero.
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
    // An X index reads X and drops the write.
    logic_index = 'x;
    logic_read = value[logic_index +: 4];
    bit_read = bits[logic_index];
    value[logic_index] = 1'b1;
    // Out-of-range constant indices.
    integer_index = 6;
    logic_read = value[integer_index +: 4];
    integer_index = 99;
    value[integer_index] = 1'b0;
  end
endmodule
