// RUN: talos -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: talos -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.auto > %t.auto.out 2>&1
// RUN: %t.reference > %t.reference.out 2>&1
// RUN: diff -u %t.reference.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// The always block waits at runtime (each change forks a process, so it runs
// on the lower tier), and the generated body of `assign ptr_n` writes the
// root it watches. A DPI export keeps the calendar in the generated loop,
// which may advance time before a runtime-woken waiter runs; every event of
// a time slot runs first (IEEE 1800-2023 4.5, 9.4.2). So that writer
// publishes through a runtime checkpoint, not the runtime-calendar bridge.
module native_partial_runtime_watcher_calendar;
  export "DPI-C" function host_period;
  function int host_period();
    return 10;
  endfunction
  logic clk = 0;
  logic reset = 1;
  logic [2:0] ptr_r = 0, ptr_n;
  logic [3:0] ptr_wrap;
  logic add = 1;
  always #5 clk = ~clk;
  always_ff @(posedge clk)
    if (reset) ptr_r <= 0;
    else ptr_r <= ptr_n;
  assign ptr_wrap = {1'b0, ptr_r} - 5 + add;
  assign ptr_n = ~ptr_wrap[3] ? ptr_wrap[2:0] : ptr_r + add;
  int changes = 0;
  always @(ptr_n) begin
    fork changes = changes + 1; join_none
  end
  initial begin
    #12 reset = 0;
    #63;
    $display("ptr_r=%0d changes=%0d", ptr_r, changes);
    $finish;
  end
endmodule

// TIER: checkpoint admission: {{.*}} reason=runtime-state-access {{.*}}:30:10
// TIER-NOT: error
// CHECK: ptr_r=1 changes=7
