// RUN: %split-file %s %t
// RUN: talos -O3 --vpi=off --native-scheduler=eval --mlir-timing %t/async.sv -o %t/async 2> %t/async.plan
// RUN: FileCheck %s --check-prefix=ASYNC-PLAN < %t/async.plan
// RUN: %t/async | FileCheck %s --check-prefix=ASYNC
// RUN: talos -O3 --vpi=off --mlir-timing %t/bootstrap.sv -o %t/bootstrap 2> %t/bootstrap.plan
// RUN: FileCheck %s --check-prefix=BOOTSTRAP-PLAN < %t/bootstrap.plan
// RUN: %t/bootstrap | FileCheck %s --check-prefix=BOOTSTRAP

// IEEE 1800-2023 4.4.2.3, 4.5 and 4.7: a deferred clocked kernel resumes in
// the Inactive region, after every Active event woken by its edge. Its NBAs
// become stores at their statements, so each scheduler must still run it
// after the edge's other readers.
//
// `seen` samples `q` on the same edge from an asynchronous-reset process,
// which the generated coordinator owns. The coordinator selects the lowest
// pending owner bit and gives deferred owners the highest bits, so the
// sampler reads the old `q`: `seen` trails `q` by one.

// ASYNC-PLAN: talos clocked kernel: __talos_clocked_{{[0-9_]+}} members=1 eligible=1 retained=0 snapshots=0 deferred=1
// ASYNC-PLAN: talos eval owner: bit=0 {{.*}}executor={{[^ ]*}}{{$}}
// ASYNC-PLAN-NEXT: talos eval owner: bit=1 {{.*}} deferred{{$}}
// ASYNC: {{^}}100 99{{$}}

// The bootstrap reader is an `initial` loop, which stays runtime-owned. Its
// check right after each edge must see the count from before that edge: the
// coordinator drains runtime Active work before it runs the kernel.

// BOOTSTRAP-PLAN: talos clocked kernel: __talos_clocked_{{[0-9_]+}} {{.*}}deferred=1
// BOOTSTRAP-PLAN: talos native eligibility: eligible=1 fully_eligible=0
// BOOTSTRAP: {{^}}PASS{{$}}

// Coordinator structure, including `$finish`, is checked at the MLIR level in
// ../Conversion/simulation-deferred-eval-owner.mlir.

//--- async.sv
module async_sample;
  logic clk = 0, rst_n = 0;
  logic [7:0] q = 0, seen = 0;
  always #5 clk = ~clk;
  initial #12 rst_n = 1;
  always_ff @(posedge clk) q <= q + 1;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) seen <= 0; else seen <= q;
  final $display("%0d %0d", q, seen);
  initial #1000 $finish;
endmodule

//--- bootstrap.sv
module bootstrap_reader;
  bit clk = 0;
  always #5 clk = ~clk;
  int tick = 0;
  always @(posedge clk) tick <= tick + 1;
  initial begin
    for (int n = 0; n < 20; ++n) begin
      @(posedge clk);
      if (tick != n) $fatal(1, "early store");
      #1;
    end
    $display("PASS");
    $finish;
  end
endmodule

