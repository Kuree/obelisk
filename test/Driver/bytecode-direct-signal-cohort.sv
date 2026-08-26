// RUN: obelisk -fno-lto -O3 --vpi=off --native-scheduler=generic %s \
// RUN:   -o %t.native
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s \
// RUN:   -o %t.bytecode
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s \
// RUN:   -o %t.bytecode-o0
// RUN: obelisk -fno-lto -O3 --vpi=off --native-scheduler=aot %s \
// RUN:   -o %t.aot
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.aot > %t.aot.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode-o0.out
// RUN: diff -u %t.native.out %t.aot.out
// RUN: FileCheck %s < %t.bytecode.out

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
      always @(posedge clock)
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
