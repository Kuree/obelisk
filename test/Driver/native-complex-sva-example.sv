// RUN: obelisk -fno-lto --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.o0.native
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.o3.native
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: %t.o3.aot > %t.o3.aot.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.aot.out
// RUN: FileCheck %s < %t.o0.native.out
// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=SIM

module native_complex_sva_example;
  logic clk = 0;
  logic rst = 1;
  logic req = 0;
  logic grant = 0;
  logic busy = 0;
  logic done = 0;
  logic error = 0;
  int completed = 0;

  // The ranged branch produces two possible endpoints; the fixed-delay
  // branch adds a third endpoint at the second cycle.
  sequence request_path;
    (req ##[1:2] grant) or (req ##2 busy);
  endsequence

  // Each antecedent endpoint starts three independent, nonoverlapped
  // consequent alternatives. Reset asynchronously cancels every live trace.
  property bounded_response;
    @(posedge clk) disable iff (rst)
      request_path |=> ((busy or done) or error);
  endproperty

  cover property (bounded_response) completed++;

  // Drive inputs away from the sampling edge to avoid testbench races.
  always #1 clk = ~clk;

  initial begin
    // One request produces three successful antecedent endpoints. The first
    // completes through busy; the two same-cycle endpoints complete through
    // error on the following clock.
    @(negedge clk); rst = 0; req = 1;
    @(negedge clk); req = 0; grant = 1;
    @(negedge clk); busy = 1;
    @(negedge clk); grant = 0; busy = 0; error = 1;
    @(negedge clk); error = 0; req = 1;

    // Cancel this attempt after its antecedent matches but before its
    // nonoverlapped consequent is sampled.
    @(negedge clk); req = 0; grant = 1;
    @(negedge clk); grant = 0; rst = 1; done = 1;

    // Deasserting reset permits a fresh attempt.
    @(negedge clk); rst = 0; done = 0; req = 1;
    @(negedge clk); req = 0; grant = 1;
    @(negedge clk); grant = 0; done = 1;
    @(negedge clk); done = 0;

    $display("complex-sva completed=%0d", completed);
    $finish;
  end
endmodule

// CHECK: complex-sva completed=4
// SIM-DAG: obelisk_sim.branching_antecedent_alternatives = 3 : i64
// SIM-DAG: obelisk_sim.branching_consequent_alternatives = 3 : i64
// SIM-DAG: obelisk_sim.combined_boolean_branching_pairs = 9 : i64
// SIM-DAG: obelisk_sim.concurrent_cancel
