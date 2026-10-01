// RUN: obelisk -O3 --native-scheduler=eval --mlir-timing -emit-llvm %s -o %t.ll 2> %t.log
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.log
// RUN: FileCheck %s --check-prefix=NO-CHECKPOINT < %t.ll
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.eval > %t.eval.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s < %t.eval.out

// IEEE 1800-2023 6.21, 7.4.5, 11.5.1: automatic array arguments and
// temporaries retain initialization, declared-range indexing and overlapping
// field updates. Invalid indices read defaults and leave writes unchanged.
module native_tier1_local_array_views;
  typedef struct packed {logic [7:0] high, low;} pair_t;
  typedef pair_t rows_t [2:3];
  logic clk = 0;
  logic signed [31:0] index = 2;
  rows_t source = '{16'h1234, 16'h5678};
  logic [31:0] result;
  logic [31:0] clocked;
  function automatic logic [31:0] update(input rows_t rows,
                                        input logic signed [31:0] selected);
    rows_t local_rows = rows;
    local_rows[selected].low = 8'hxz;
    local_rows[selected].high = rows[selected].low;
    return {local_rows[2], local_rows[3]};
  endfunction
  always #5 clk = ~clk;
  always_comb result = update(source, index);
  always @(posedge clk) clocked <= result;
  initial begin
    #1; $display("%h", result);
    index = 3;
    #1; $display("%h", result);
    index = 4;
    #1; $display("%h", result);
    index = 'x;
    #1; $display("%h", result);
    #2; $display("clocked %h", clocked);
    $finish;
  end
endmodule

// ADMISSION: eval executor inventory: {{.*}}runtime_checkpoints=0
// NO-CHECKPOINT-NOT: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// CHECK: 34xz5678
// CHECK-NEXT: 123478xz
// CHECK-NEXT: 12345678
// CHECK-NEXT: 12345678
// CHECK-NEXT: clocked 12345678
