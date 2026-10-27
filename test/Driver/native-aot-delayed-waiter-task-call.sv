// RUN: talos -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.timing
// RUN: FileCheck %s --check-prefix=SAMPLER < %t.timing
// RUN: talos -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic > %t.generic.out
// RUN: env TALOS_RT_SIGNAL_DIAGNOSTICS=1 %t.auto 2> %t.auto.diag | diff -u %t.generic.out -
// RUN: FileCheck %s --check-prefix=DIAG < %t.auto.diag

// A task without time control completes inside the caller's activation (IEEE
// 1800-2023 13.3), so calling one is an ordinary call for the native plan: the
// sampling loop below stays a plan actor. A task that itself waits keeps its
// callers out of the plan; the clock generator's reset task shows the report.
module native_aot_delayed_waiter_task_call;
  logic clk = 0, rst = 1;
  logic [15:0] a = 0, led = 0;
  logic [3:0] v [8] = '{default: 0};
  int cycle, commits, calls;

  always #8 clk = ~clk;
  always_ff @(posedge clk) begin
    if (rst) begin
      a <= 0;
      led <= 0;
    end else begin
      a <= a + 1;
      led <= a ^ led;
      for (int i = 0; i < 8; i++)
        if (a[i]) v[i] <= v[i] + 1;
    end
  end

  task automatic count_commits(input int limit);
    for (int i = 0; i < 8; i++)
      if (v[i][0] && commits < limit) commits += 1;
    calls += 1;
  endtask

  task automatic wait_cycles(input int count);
    repeat (count) @(posedge clk);
  endtask

  initial begin
    wait_cycles(3);
    rst = 0;
  end

  initial begin
    #3;
    forever begin
      #16;
      count_commits(100000);
      cycle += 1;
      if (cycle % 500 == 0)
        $display("%0d cycle %0t commits=%0d calls=%0d", cycle, $time, commits, calls);
      if (cycle == 2000 || led == 16'h1234) begin
        $display("done %0d", commits);
        $finish;
      end
    end
  end
endmodule

// ADMISSION: native withheld actor: cost={{[0-9]+}} at=loc("{{.*}}native-aot-delayed-waiter-task-call.sv":[[# @LINE - 21]]:3) reason=task, await, or join control is present
// SAMPLER-NOT: native withheld actor: {{.*}}native-aot-delayed-waiter-task-call.sv":[[# @LINE - 17]]:3)
// ADMISSION: native eligibility: eligible=1 fully_eligible=0
// DIAG: talos-signal-diagnostics
// DIAG-SAME: aot_fallbacks=0
