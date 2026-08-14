// RUN: obelisk -fno-lto --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.o0
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.o3
// RUN: %t.o0 > %t.o0.out
// RUN: %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.o0.out
// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic -emit-llvm %s | FileCheck %s --check-prefix=LLVM --implicit-check-not=__obelisk_bytecode_image_v1

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

// OUTPUT: past=x
// OUTPUT-NEXT: past=1
// LLVM: @__obelisk_execution_descriptor_v1 = constant
// LLVM-SAME: { i32 1, i32 32,
// LLVM-SAME: i64 ptrtoint (ptr @__obelisk_execution_extension_v1 to i64), ptr null, i64 0,
// LLVM: @__obelisk_execution_extension_v1 = internal constant { i32, i32, ptr, i64 } { i32 1, i32 24, ptr @__obelisk_sampled_ranges_v1, i64 1 }
// LLVM: @__obelisk_sampled_ranges_v1 = internal constant [1 x { i64, i64, i64 }] [{ i64, i64, i64 } { i64 16, i64 0, i64 2 }]
