// RUN: talos --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.o0
// RUN: talos --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.o3
// RUN: %t.o0 > %t.o0.out
// RUN: %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.o0.out
// RUN: talos --std=1800-2023 -O0 --native-scheduler=generic -emit-llvm %s | FileCheck %s --check-prefix=LLVM --implicit-check-not=__talos_bytecode_image_v1

module native_past_only;
  logic clk = 0;
  logic [7:0] prefix = 8'h5a;
  logic value = 0;
  logic gate = 1;
  logic previous;

  always @(posedge clk) begin
    previous = $past(value, 1, gate);
    $display("past=%b", previous);
  end

  initial begin
    #1 value = 1;
    #1 clk = 1;
    #1 clk = 0;
    #1 value = 0;
    #1 clk = 1;
    #1 $finish;
  end
endmodule

// LRM 16.5.1: the declaration initializer supplies the pre-history default.
// OUTPUT: past=0
// OUTPUT-NEXT: past=1
// LLVM: @__talos_execution_descriptor_v1 = constant
// LLVM-SAME: { i32 1, i32 1056, i64 120,
// LLVM-SAME: { i32 1, i32 72, ptr @__talos_sampled_ranges_v1, i64 1,
// LLVM: @__talos_sampled_ranges_v1 = internal constant [1 x { i64, i64, i64 }] [{ i64, i64, i64 } { i64 16, i64 0, i64 1 }]
