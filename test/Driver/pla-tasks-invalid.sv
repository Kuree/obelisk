// RUN: not obelisk -emit-sim -O0 --vpi=off %s -o /dev/null 2>&1 | FileCheck %s

module pla_tasks_invalid;
  logic [1:4] memory [1:2];
  logic [1:3] input_terms;
  logic [1:2] output_terms;
  initial $sync$and$array(memory, input_terms, output_terms);
endmodule

// CHECK: $sync$and$array requires a fixed memory whose element width equals
// CHECK-SAME: the input width and whose depth equals the output width
