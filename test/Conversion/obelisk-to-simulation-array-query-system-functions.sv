// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

module simulation_array_query_system_functions;
  logic [3:1][2:4] matrix [7:5];
  integer dimension;
  int result;

  initial begin
    result = $dimensions(matrix);
    result = $unpacked_dimensions(matrix);
    result = $left(matrix);
    result = $right(matrix, dimension);
    result = $low(matrix, dimension);
    result = $high(matrix, dimension);
    result = $increment(matrix, dimension);
    result = $size(matrix, dimension);
  end
endmodule

// CHECK-DAG: arith.constant 3 : i32
// CHECK-DAG: arith.constant 1 : i32
// CHECK-DAG: arith.constant 7 : i32
// CHECK: obelisk_sim.logic.compare case_eq
// CHECK: arith.select
// CHECK-NOT: obelisk.sv.
