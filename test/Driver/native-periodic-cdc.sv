// RUN: obelisk -O2 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O2 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O2 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic > %t.generic.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.auto > %t.auto.out 2> %t.auto.diag
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.eval.diag
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s < %t.eval.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.auto.diag
// RUN: FileCheck %s --check-prefix=DIAG < %t.eval.diag
// RUN: obelisk -O0 --native-scheduler=eval %s -o %t.o0
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.o0 > %t.o0.out 2> %t.o0.diag
// RUN: diff -u %t.generic.out %t.o0.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.o0.diag

module cdc_sink(input bit fast, slow, sparse, input logic [7:0] data);
  bit [7:0] produced, returned;
  bit [7:0] sync1, sync2, ack1, ack2;
  logic [7:0] data1, data2, sparse_data;
  bit [31:0] levels, history;
  int fast_count, slow_count, sparse_count, either_count;
  always @(posedge fast) begin
    produced <= produced + 1;
    ack1 <= returned;
    ack2 <= ack1;
    fast_count <= fast_count + 1;
  end
  always @(posedge slow) begin
    sync1 <= produced;
    sync2 <= sync1;
    returned <= sync2;
    data1 <= data;
    data2 <= data1;
    slow_count <= slow_count + 1;
    // A silent falling edge in another domain still changes its physical
    // level. Do not compress that fall past a CDC reader of the clock itself.
    levels <= (levels << 1) ^ {30'b0, fast, sparse};
    history <= (history << 1) ^ {24'b0, sync2};
  end
  always @(posedge sparse) begin
    sparse_data <= data;
    sparse_count <= sparse_count + 1;
  end
  // A shared owner must execute once, before the common NBA commit, even
  // when two clocks rise together (fast and sparse at t=18, 54, 90).
  always @(posedge fast or posedge sparse)
    either_count <= either_count + 1;
endmodule

module native_periodic_cdc;
  bit [2:0] clocks;
  logic [7:0] data = 8'h11;
  cdc_sink sink(clocks[0], clocks[1], clocks[2], data);
  always #2 clocks[0] = ~clocks[0];
  always #3 clocks[1] = ~clocks[1];
  always #18 clocks[2] = ~clocks[2];
  initial begin
    // Invalidate and restore CDC proofs across both off-edge checkpoints and
    // an exact clock deadline. The sparse domain remains unknown while the
    // faster domains recover and continue to run with local promotion.
    #37 data = 'x;
    #18;
    $display("unknown data=%h/%h sparse=%h", sink.data1, sink.data2,
             sink.sparse_data);
    #4 data = 'z;
    #9;
    $display("highz data=%h/%h", sink.data1, sink.data2);
    #2 data = 8'ha5;
    #13;
    $display("recovered data=%h/%h sparse=%h", sink.data1, sink.data2,
             sink.sparse_data);
    #128;
    $display("counts=%0d/%0d/%0d either=%0d", sink.fast_count,
             sink.slow_count, sink.sparse_count, sink.either_count);
    $display("cdc=%0d/%0d/%0d ack=%0d/%0d/%0d levels=%h history=%h data=%h/%h/%h",
             sink.produced, sink.sync1, sink.sync2, sink.returned,
             sink.ack1, sink.ack2, sink.levels, sink.history,
             sink.data1, sink.data2, sink.sparse_data);
    $finish;
  end
endmodule

// CHECK: unknown data=xx/xx sparse=xx
// CHECK: highz data=zz/xx
// CHECK: recovered data=a5/a5 sparse=xx
// CHECK: counts=53/35/6 either=53
// CHECK: cdc=53/52/50 ack=49/49/47 levels=49249249 history=a3d6a427 data=a5/a5/a5
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_fallbacks=0
// DIAG-SAME: periodic_preparations={{[1-9][0-9]*}}
// DIAG-SAME: periodic_clocks_high_water=3
