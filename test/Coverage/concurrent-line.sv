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
// evaluation attempt in Observed, not when its monitor process is spawned.
// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic --coverage=line %s -o %t.o0-generic
// RUN: %t.o0-generic --coverage-output=%t.o0-generic.obcov
// RUN: obelisk-cov report --format=lcov %t.o0-generic.obcov -o %t.o0-generic.lcov
// RUN: FileCheck %s --check-prefix=COVERAGE < %t.o0-generic.lcov
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=generic --coverage=line %s -o %t.o3-generic
// RUN: %t.o3-generic --coverage-output=%t.o3-generic.obcov
// RUN: obelisk-cov report --format=lcov %t.o3-generic.obcov -o %t.o3-generic.lcov
// RUN: diff -u %t.o0-generic.lcov %t.o3-generic.lcov
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=eval --coverage=line %s -o %t.o3-eval
// RUN: %t.o3-eval --coverage-output=%t.o3-eval.obcov
// RUN: obelisk-cov report --format=lcov %t.o3-eval.obcov -o %t.o3-eval.lcov
// RUN: diff -u %t.o0-generic.lcov %t.o3-eval.lcov
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot --coverage=line %s -o %t.o3-aot
// RUN: %t.o3-aot --coverage-output=%t.o3-aot.obcov
// RUN: obelisk-cov report --format=lcov %t.o3-aot.obcov -o %t.o3-aot.lcov
// RUN: diff -u %t.o0-generic.lcov %t.o3-aot.lcov
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode --coverage=line %s -o %t.bytecode
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov
// RUN: obelisk-cov report --format=lcov %t.bytecode.obcov -o %t.bytecode.lcov
// RUN: diff -u %t.o0-generic.lcov %t.bytecode.lcov

// COVERAGE: DA:5,0
// COVERAGE: DA:6,3
// COVERAGE-NOT: DA:7,
