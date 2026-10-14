// RUN: obelisk -emit-slang %s | obelisk-opt --test-obelisk-native-backend='stop-after=graph' | obelisk-opt --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=RESOLVED
// RUN: obelisk -emit-slang -DCLOBBER %s | obelisk-opt --test-obelisk-native-backend='stop-after=graph' | obelisk-opt --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CLOBBERED

// IEEE 1800-2023 13.5 writes an `output` actual when the task returns. The
// actual reaches the task as a reference, so only the task's formal-target
// effect records the write. Resolving it through the call site keeps the NBA
// observer analysis available for the whole design instead of giving up, and
// still counts the task as a writer of that actual.
//  - resolved: the task writes `snap`, which nothing watches. `single` gets
//    one write per activation of a free-running clock and needs no mask.
//  - clobbered: the task writes `clk` instead, so the clock is no longer
//    free-running and `single` keeps its transient mask.
// Storage ids follow declaration order: clk=0, single=1, n=2.

// RESOLVED-LABEL: simulation.design @design
// RESOLVED-SAME: schedule.nba.change_watched = array<i64>
// RESOLVED-SAME: schedule.nba.transient_observable = array<i64: 0>

// CLOBBERED-LABEL: simulation.design @design
// CLOBBERED-SAME: schedule.nba.change_watched = array<i64: 1>
// CLOBBERED-SAME: schedule.nba.transient_observable = array<i64: 0>

module nba_transient_observers_formal_write;
  logic clk = 0;
  always #5 clk = ~clk;
  logic [7:0] single = 0;
  int n = 0;
  always @(single) n = n + 1;
  always @(posedge clk) single <= single + 1;

`ifdef CLOBBER
  task automatic stop(output logic value);
    value = 1'b0;
  endtask

  initial begin
    #12;
    stop(clk);
    $finish;
  end
`else
  logic [7:0] snap;

  task automatic capture(output logic [7:0] value);
    value = single;
  endtask

  initial begin
    #12;
    capture(snap);
    $finish;
  end
`endif
endmodule
