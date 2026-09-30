// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.diag
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.eval.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

// A pure clocked body can stage literal X/Z and later read that register.
// Its generated path predicate must preserve unknown values without an actor
// checkpoint. The suspended testbench shares the canonical NBA barrier.
module native_tier1_fixed_nba_xz;
  logic clk = 0;
  int cycles = 0;
  always #5 clk = ~clk;
  always @(posedge clk) cycles <= cycles + 1;
  logic [7:0] q[32];
  for (genvar i = 0; i < 32; i++) begin
    initial q[i] = i;
    always @(posedge clk) begin
      if (cycles == 2) q[i] <= 'x;
      else if (cycles == 4) q[i] <= 'z;
      else if (cycles == 6) q[i] <= i;
      else q[i] <= q[i] + 1;
    end
  end
  initial begin
    automatic string message = "xz";
    repeat (8) begin
      @(posedge clk);
      #1;
      $display("%s %0d %h", message, cycles, q[31]);
    end
    $finish;
  end
endmodule

// OUTPUT: xz 1 20
// OUTPUT-NEXT: xz 2 21
// OUTPUT-NEXT: xz 3 xx
// OUTPUT-NEXT: xz 4 xx
// OUTPUT-NEXT: xz 5 zz
// OUTPUT-NEXT: xz 6 xx
// OUTPUT-NEXT: xz 7 1f
// OUTPUT-NEXT: xz 8 20
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_fallbacks=0
// DIAG-SAME: aot_checkpoints=0
// DIAG-SAME: eval_dispatches={{[1-9][0-9]*}}
