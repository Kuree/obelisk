// RUN: obelisk -O3 --vpi=off --native-scheduler=generic %s \
// RUN:   -o %t.native
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s \
// RUN:   -o %t.bytecode
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s \
// RUN:   -o %t.bytecode-o0
// RUN: obelisk -O3 --vpi=off --native-scheduler=aot %s \
// RUN:   -o %t.aot
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode \
// RUN:   -G N=17 -G SLOW=1024 -G CYCLES=3 %s -o %t.slow-dominant
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.aot > %t.aot.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.slow-dominant \
// RUN:   > %t.slow-dominant.out 2> %t.slow-dominant.diag
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode-o0.out
// RUN: diff -u %t.native.out %t.aot.out
// RUN: FileCheck %s < %t.bytecode.out
// RUN: FileCheck %s --check-prefix=SLOW < %t.slow-dominant.out
// RUN: FileCheck %s --check-prefix=SLOW-DIAG < %t.slow-dominant.diag

// More than the bytecode scheduler's cohort threshold of independent direct
// posedge waiters must resume, execute, and resuspend on every clock wave.
// Comparing all three tiers catches lost, duplicated, or misordered resumes.
module bytecode_direct_signal_cohort;
  parameter int N = 32;
  parameter int CYCLES = 3;
  parameter int SLOW = 0;
  bit clock;
  bit [N-1:0] hits;

  always #1 clock = ~clock;

  genvar i;
  generate
    for (i = 0; i < N; ++i)
      // A scheduler-time read keeps these as independent actors. Otherwise
      // legal same-clock body fusion would stop exercising the bytecode
      // scheduler's many-waiter cohort path.
      always @(posedge clock)
        if ($time != 0)
          hits[i] = ~hits[i];
  endgenerate

  genvar j;
  generate
    for (j = 0; j < SLOW; ++j)
      initial #1000000;
  endgenerate

  initial begin
    repeat (CYCLES) @(negedge clock);
    $display("cohort hits=%h", hits);
    $finish;
  end
endmodule

// CHECK: cohort hits=ffffffff
// SLOW: cohort hits=1ffff
// SLOW-DIAG: subscriptions_high_water=18
// SLOW-DIAG-SAME: readiness_calls={{[1-9][0-9]*}} candidate_scans={{[1-9][0-9]*}}
