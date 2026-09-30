// RUN: obelisk -O3 --native-scheduler=eval --mlir-timing -emit-llvm %s \
// RUN:   -o %t.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=CADENCE < %t.timing
// RUN: FileCheck %s --check-prefix=SLOTS < %t.ll
// RUN: FileCheck %s --check-prefix=NO-QUEUE < %t.ll
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.diag
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.eval.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

module cadence_port(input logic source, output logic target);
  always_comb target = source;
endmodule

// RSD's clock pattern: a private positive-delay cadence drives public clock
// NBAs. Clock timing must survive both the NBA output and port forwarding.
module native_tier1_clock_cadence;
  logic source_clk, middle_clk, clk;
  int cycles = 0;
  initial begin
    source_clk <= 0;
    forever begin
      #5 source_clk <= 0;
      #5 source_clk <= 1;
      cycles += 1;
    end
  end
  cadence_port p0(source_clk, middle_clk);
  cadence_port p1(middle_clk, clk);
  logic [7:0] q[0:31];
  logic [127:0] wide = 0;
  for (genvar i = 0; i < 32; i++) begin
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  always @(posedge clk) wide <= wide + 128'h10000000000000001;
  initial begin
    automatic string message = "cadence";
    repeat (4) @(posedge clk);
    $display("%s pre %0d %0d %0d", message, q[31], wide[127:64], wide[63:0]);
    #1;
    $display("post %0d %0d %0d", q[31], wide[127:64], wide[63:0]);
    $finish;
  end
endmodule

// CADENCE: periodic signal proof: clocks=1 {{.*}}cadence-outputs=1 periodic-bits={{[2-9]|[1-9][0-9]+}}
// SLOTS: @__obelisk_eval_nba_slots_
// SLOTS: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// NO-QUEUE-NOT: call {{.*}}@obelisk_rt_v1_eval_nba_reserve
// OUTPUT: cadence pre 34 3 3
// OUTPUT-NEXT: post 35 4 4
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_fallbacks=0
// DIAG-SAME: eval_dispatches={{[1-9][0-9]*}}
