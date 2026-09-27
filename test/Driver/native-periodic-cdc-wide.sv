// RUN: obelisk -O2 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O2 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic > %t.generic.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.diag
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s < %t.eval.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

module cdc_wide_stage(input bit clock, input logic [7:0] d,
                      output logic [7:0] q = 0);
  always @(posedge clock) q <= d + 1;
endmodule

module native_periodic_cdc_wide;
  bit [2:0] clocks;
  always #3 clocks[0] = ~clocks[0];
  always #5 clocks[1] = ~clocks[1];
  always #7 clocks[2] = ~clocks[2];
  logic [7:0] q[70];
  logic [7:0] seed = 1;
  // Seventy independently clocked instances span more than one ready word.
  // Adjacent stages cross domains and coincident edges must sample old q.
  for (genvar i = 0; i < 70; ++i) begin : g
    if (i == 0)
      cdc_wide_stage stage(clocks[i%3], seed, q[i]);
    else
      cdc_wide_stage stage(clocks[i%3], q[i-1], q[i]);
  end
  initial begin
    #301;
    $display("warm %h %h %h %h", q[0], q[31], q[64], q[69]);
    seed = 'x;
    #1001;
    $display("unknown %h %h %h %h", q[0], q[31], q[64], q[69]);
    seed = 7;
    #1001;
    $display("recovered %h %h %h %h", q[0], q[31], q[64], q[69]);
    $finish;
  end
endmodule

// CHECK: warm 02 21 35 34
// CHECK: unknown xx xx xx xx
// CHECK: recovered 08 27 48 4d
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_fallbacks=0
// DIAG-SAME: periodic_preparations={{[1-9][0-9]*}}
// DIAG-SAME: periodic_clocks_high_water=3
