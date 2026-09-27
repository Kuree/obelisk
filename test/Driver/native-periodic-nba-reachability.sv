// RUN: obelisk -O3 --compile-threads=1 --native-scheduler=generic %s -o %t.generic
// RUN: obelisk -O3 --compile-threads=8 --native-scheduler=auto %s -o %t.auto
// RUN: %t.generic > %t.generic.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.auto > %t.auto.out 2> %t.diag
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

module nba_reachability_cell(input bit clock, input logic [7:0] upper);
  logic [15:0] bus = 0;
  bit [7:0] low_value, feedback;
  logic [7:0] high_value = 0;

  always @(posedge clock) bus[7:0] <= bus[7:0] + 1;
  always @(upper) bus[15:8] <= upper;
  // The two NBA paths share a storage root but have disjoint packed ranges.
  // Reachability must retain range matching when indexing activation edges.
  always @(bus[15:8]) high_value <= bus[15:8];
  // The low-byte NBA path contains a cycle that reaches a fixed point. A newly
  // admitted owner must expose all of its fragments, including NBA successors,
  // without repeatedly visiting the cycle or losing later clock activations.
  always @(bus[7:0] or feedback) low_value <= bus[7:0] | feedback;
  always @(low_value) feedback <= low_value & 8'h0f;
endmodule

module native_periodic_nba_reachability;
  bit clock;
  logic [7:0] upper = 0;
  always #5 clock = ~clock;
  for (genvar i = 0; i < 128; ++i) begin : g
    nba_reachability_cell dut(clock, upper);
  end
  initial begin
    #22 upper = 8'ha6;
    #11 upper = 'x;
    #1;
    $display("unknown low=%h feedback=%h high=%h",
             g[0].dut.low_value, g[0].dut.feedback, g[0].dut.high_value);
    #7 upper = 8'hc3;
    #9;
    $display("first bus=%h low=%h feedback=%h high=%h",
             g[0].dut.bus, g[0].dut.low_value, g[0].dut.feedback,
             g[0].dut.high_value);
    $display("last bus=%h low=%h feedback=%h high=%h",
             g[127].dut.bus, g[127].dut.low_value, g[127].dut.feedback,
             g[127].dut.high_value);
    $finish;
  end
endmodule

// CHECK: unknown low=03 feedback=03 high=xx
// CHECK: first bus=c305 low=07 feedback=07 high=c3
// CHECK: last bus=c305 low=07 feedback=07 high=c3
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_fallbacks=0
// DIAG-SAME: periodic_preparations={{[1-9][0-9]*}}
