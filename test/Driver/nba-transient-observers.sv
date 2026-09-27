// RUN: obelisk -O3 -emit-sim %s -o - 2>/dev/null | FileCheck %s

// Classify NBA roots by who can observe an intermediate NBA value:
//  - single: one write per activation, clocked by a free-running clock; no
//    round trip is possible, so it needs neither ordering nor a mask.
//  - sub.single: the same, with the clock reaching it through a port.
//  - twice: two writes on one path; its change waiter needs a mask.
//  - pulsed: one write per activation, but its clock pulses twice in a
//    slot through #0 steps; it needs a mask.
//  - edged: an edge waiter sees the value sequence; it stays ordered.
// The observable set also holds the clocks clk, pulse and u.clk, which edge
// waits watch. Storage ids follow declaration order: clk=0, pulse=1,
// single=2, twice=3, pulsed=4, edged=5, then u.clk=10 and u.single=11.

// CHECK-DAG: schedule.nba.change_watched = array<i64: 3, 4>
// CHECK-DAG: schedule.nba.transient_observable = array<i64: 0, 1, 5, 10>
// CHECK-DAG: storage.decl 3 in {{[0-9]+}} : {{.*}} hierarchy "nba_transient_observers.twice"
// CHECK-DAG: storage.decl 4 in {{[0-9]+}} : {{.*}} hierarchy "nba_transient_observers.pulsed"
// CHECK-DAG: storage.decl 5 in {{[0-9]+}} : {{.*}} hierarchy "nba_transient_observers.edged"
// CHECK-DAG: storage.decl 10 in {{[0-9]+}} : {{.*}} hierarchy "nba_transient_observers.u.clk"

module sub(input logic clk);
  logic [7:0] single = 0;
  int n = 0;
  always @(single) n = n + 1;
  always @(posedge clk) single <= single + 1;
endmodule

module nba_transient_observers;
  logic clk = 0;
  logic pulse = 0;
  logic [7:0] single = 0;
  logic [7:0] twice = 0;
  logic [7:0] pulsed = 0;
  logic [7:0] edged = 0;
  int n_single = 0, n_twice = 0, n_pulsed = 0, n_edged = 0;

  sub u(.clk(clk));

  always #5 clk = ~clk;
  always @(single) n_single = n_single + 1;
  always @(twice) n_twice = n_twice + 1;
  always @(pulsed) n_pulsed = n_pulsed + 1;
  always @(posedge edged[0]) n_edged = n_edged + 1;

  always @(posedge clk) begin
    single <= single + 1;
    twice <= 1;
    twice <= 0;
    edged <= edged + 1;
  end
  always @(posedge pulse) pulsed <= pulsed + 1;

  initial begin
    #1 pulse = 1;
    #0 pulse = 0;
    #0 pulse = 1;
    #20 $finish;
  end
endmodule
