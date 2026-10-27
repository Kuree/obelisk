// RUN: talos -O3 --mlir-timing %s -o %t.runtime-calendar 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: talos -O3 -DGENERATED_CALENDAR %s -o %t.generated-calendar
// RUN: talos -O0 --native-scheduler=generic %s -o %t.reference
// RUN: not %t.reference > %t.reference.out 2>&1
// RUN: not %t.runtime-calendar > %t.runtime-calendar.out 2>&1 && diff -u %t.reference.out %t.runtime-calendar.out
// RUN: not %t.generated-calendar > %t.generated-calendar.out 2>&1 && diff -u %t.reference.out %t.generated-calendar.out
// RUN: FileCheck %s < %t.reference.out

// A deferred assertion queues its report on the executing process
// (IEEE 1800-2023 16.4.1), so its always_comb stays in the generated eval
// loop instead of becoming a runtime checkpoint actor. Only a failing
// activation reaches the runtime: a two-state one through a cold checkpoint,
// a four-state one (`v` carries an X bit) by queuing the report directly. A
// later resume would flush it (16.4.2) through the process's epoch. The
// reports mature in the Observed and Postponed regions of their own time
// slot, also when a DPI export leaves the calendar to the generated loop.
module native_eval_deferred_assertion_report;
`ifdef GENERATED_CALENDAR
  export "DPI-C" function host_period;
  function int host_period();
    return 10;
  endfunction
`endif
  logic clk = 0;
  logic [2:0] ptr_r = 0, ptr_n;
  logic [3:0] v = 4'bx000;
  always #5 clk = ~clk;
  always_ff @(posedge clk) ptr_r <= ptr_n;
  always_ff @(posedge clk) v[2:0] <= v[2:0] + 1;
  assign ptr_n = ptr_r + 1;
  always_comb begin
    p: assert final (ptr_n != 3'd4) else $error("final ptr_n=%0d", ptr_n);
    q: assert #0 (ptr_n != 3'd6) else $error("observed ptr_n=%0d", ptr_n);
  end
  always_comb begin
    r: assert final (v[2:0] != 3'd4) else $error("final v=%b", v);
  end
  initial begin
    #100 $display("done ptr_r=%0d v=%b", ptr_r, v);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1
// TIER: talos eval executor inventory: {{.*}} runtime_checkpoints=0
// CHECK: simulation time 25: final ptr_n=4
// CHECK: simulation time 35: final v=x100
// CHECK: simulation time 45: observed ptr_n=6
// CHECK: done ptr_r=2 v=x010
