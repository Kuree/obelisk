module top;
  logic clk = 0;
  logic never_clk = 0;
  logic enabled = 1;
  cover property (@(posedge never_clk) enabled);
  cover property (@(posedge clk) enabled);
  restrict property (@(posedge clk) enabled);

  initial begin
    repeat (3) begin
      #1 clk = 1;
      #1 clk = 0;
    end
    #1 $finish;
  end
endmodule

// A concurrent statement executes when its leading clock starts an
// evaluation attempt in Observed, not when its monitor process is spawned
// (IEEE 1800-2023 16.14.3): the cover on the clock that never ticks is a line
// point that stays at zero and the one on the ticking clock counts 3 attempts.
// A restrict statement (16.14.4) is not a coverable line.
// RUN: %talos --std=1800-2023 -O0 --native-scheduler=generic --coverage=line %s -o %t
// RUN: %t --coverage-output=%t.obcov
// RUN: talos-cov report --format=lcov %t.obcov | FileCheck %s

// CHECK: DA:5,0
// CHECK: DA:6,3
// CHECK-NOT: DA:7,
