// RUN: obelisk -O3 --native-scheduler=eval -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.eval > %t.eval.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.eval.out
// RUN: FileCheck %s < %t.eval.out

// The generated eval body reads a 128-bit window of a 256-bit vector at a
// dynamic offset, and a fixed byte of a dynamically indexed unpacked element,
// directly from the state planes. A window that overhangs the vector keeps
// its in-range bits and reads X for the rest (IEEE 1800-2023 11.5.1); both
// must agree with the interpreter.
// LLVM: define i32 @__obelisk_eval_dispatch_v1
// CHECK: 00ff00ff00ff00ff1122334455667788 1a
// CHECK: xxxxxxxxxxxxxxxxxxxxxxxxxxxx0123 1a

module native_eval_wide_dynamic_load;
  logic clk = 0;
  logic [255:0] data = {64'h0123456789abcdef, 64'hfedcba9876543210,
                        64'h00ff00ff00ff00ff, 64'h1122334455667788};
  logic [15:0] lanes [0:3] = '{16'h1a2b, 16'h3c4d, 16'h5e6f, 16'h7081};
  logic [7:0] index = 8'd0;
  logic [1:0] lane = 2'd0;
  logic [127:0] window;
  logic [7:0] high;

  always #5 clk = ~clk;
  always @(posedge clk) begin
    window <= data[index +: 128];
    high <= lanes[lane][15:8];
  end
  always @(negedge clk) begin
    index <= index + 8'd60;
    lane <= lane + 2'd3;
  end
  always @(negedge clk) $display("%h %h", window, high);
  initial #70 $finish;
endmodule
