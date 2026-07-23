// RUN: obelisk -O0 -DTEST_NBA %s -o %t.nba.o0
// RUN: obelisk -O3 -DTEST_NBA %s -o %t.nba.o3
// RUN: %t.nba.o0 > %t.nba.o0.out && %t.nba.o3 > %t.nba.o3.out
// RUN: diff -u %t.nba.o0.out %t.nba.o3.out
// RUN: FileCheck %s --check-prefix=NBA < %t.nba.o3.out
// RUN: obelisk -O0 -DTEST_TIMING %s -o %t.timing.o0
// RUN: obelisk -O3 -DTEST_TIMING %s -o %t.timing.o3
// RUN: %t.timing.o0 > %t.timing.o0.out && %t.timing.o3 > %t.timing.o3.out
// RUN: diff -u %t.timing.o0.out %t.timing.o3.out
// RUN: FileCheck %s --check-prefix=TIMING < %t.timing.o3.out
// RUN: obelisk -O0 -DTEST_CHANGE %s -o %t.change.o0
// RUN: obelisk -O3 -DTEST_CHANGE %s -o %t.change.o3
// RUN: %t.change.o0 > %t.change.o0.out && %t.change.o3 > %t.change.o3.out
// RUN: diff -u %t.change.o0.out %t.change.o3.out
// RUN: FileCheck %s --check-prefix=CHANGE < %t.change.o3.out
// RUN: obelisk -O0 -DTEST_NET %s -o %t.net.o0
// RUN: obelisk -O3 -DTEST_NET %s -o %t.net.o3
// RUN: %t.net.o0 > %t.net.o0.out && %t.net.o3 > %t.net.o3.out
// RUN: diff -u %t.net.o0.out %t.net.o3.out
// RUN: FileCheck %s --check-prefix=NET < %t.net.o3.out
// RUN: obelisk -O0 -DTEST_SELECTIVE %s -o %t.selective.o0
// RUN: obelisk -O3 -DTEST_SELECTIVE %s -o %t.selective.o3
// RUN: %t.selective.o0 > %t.selective.o0.out && %t.selective.o3 > %t.selective.o3.out
// RUN: diff -u %t.selective.o0.out %t.selective.o3.out
// RUN: FileCheck %s --check-prefix=SELECTIVE < %t.selective.o3.out
// RUN: obelisk -O0 -DTEST_EDGE %s -o %t.edge.o0
// RUN: obelisk -O3 -DTEST_EDGE %s -o %t.edge.o3
// RUN: %t.edge.o0 > %t.edge.o0.out && %t.edge.o3 > %t.edge.o3.out
// RUN: diff -u %t.edge.o0.out %t.edge.o3.out
// RUN: FileCheck %s --check-prefix=EDGE < %t.edge.o3.out
// RUN: obelisk -O0 -DTEST_OOB %s -o %t.oob.o0
// RUN: obelisk -O3 -DTEST_OOB %s -o %t.oob.o3
// RUN: %t.oob.o0 > %t.oob.o0.out && %t.oob.o3 > %t.oob.o3.out
// RUN: diff -u %t.oob.o0.out %t.oob.o3.out
// RUN: FileCheck %s --check-prefix=OOB < %t.oob.o3.out
// RUN: obelisk -O0 -DTEST_PARTIAL_OOB %s -o %t.partial-oob.o0
// RUN: obelisk -O3 -DTEST_PARTIAL_OOB %s -o %t.partial-oob.o3
// RUN: %t.partial-oob.o0 > %t.partial-oob.o0.out && %t.partial-oob.o3 > %t.partial-oob.o3.out
// RUN: diff -u %t.partial-oob.o0.out %t.partial-oob.o3.out
// RUN: FileCheck %s --check-prefix=PARTIAL-OOB < %t.partial-oob.o3.out
// RUN: obelisk -O0 -DTEST_UNDRIVEN %s -o %t.undriven.o0
// RUN: obelisk -O3 -DTEST_UNDRIVEN %s -o %t.undriven.o3
// RUN: %t.undriven.o0 > %t.undriven.o0.out && %t.undriven.o3 > %t.undriven.o3.out
// RUN: diff -u %t.undriven.o0.out %t.undriven.o3.out
// RUN: FileCheck %s --check-prefix=UNDRIVEN < %t.undriven.o3.out
// RUN: obelisk -O0 -DTEST_NBA_ORDER %s -o %t.nba-order.o0
// RUN: obelisk -O3 -DTEST_NBA_ORDER %s -o %t.nba-order.o3
// RUN: %t.nba-order.o0 > %t.nba-order.o0.out && %t.nba-order.o3 > %t.nba-order.o3.out
// RUN: diff -u %t.nba-order.o0.out %t.nba-order.o3.out
// RUN: FileCheck %s --check-prefix=NBA-ORDER < %t.nba-order.o3.out
// RUN: obelisk -O0 -DTEST_FOUR_STATE %s -o %t.four-state.o0
// RUN: obelisk -O3 -DTEST_FOUR_STATE %s -o %t.four-state.o3
// RUN: %t.four-state.o0 > %t.four-state.o0.out && %t.four-state.o3 > %t.four-state.o3.out
// RUN: diff -u %t.four-state.o0.out %t.four-state.o3.out
// RUN: FileCheck %s --check-prefix=FOUR-STATE < %t.four-state.o3.out
// RUN: obelisk -O0 -DTEST_CONVERGENCE %s -o %t.convergence.o0
// RUN: obelisk -O3 -DTEST_CONVERGENCE %s -o %t.convergence.o3
// RUN: %t.convergence.o0 > %t.convergence.o0.out && %t.convergence.o3 > %t.convergence.o3.out
// RUN: diff -u %t.convergence.o0.out %t.convergence.o3.out
// RUN: FileCheck %s --check-prefix=CONVERGENCE < %t.convergence.o3.out
// RUN: obelisk -O0 -DTEST_IO %s -o %t.io.o0
// RUN: obelisk -O3 -DTEST_IO %s -o %t.io.o3
// RUN: %t.io.o0 | tee %t.io.o0.out | FileCheck %s --check-prefix=IO
// RUN: %t.io.o3 | tee %t.io.o3.out | FileCheck %s --check-prefix=IO
// RUN: diff -u %t.io.o0.out %t.io.o3.out

`ifdef TEST_NBA
module native_nba;
  logic [7:0] value;
  initial begin
    value = 1;
    value <= 2;
    $display("active=%0d", value);
    #1;
    $display("after=%0d", value);
  end
  final $display("final=%0d", value);
endmodule
`endif

// NBA: active=1
// NBA-NEXT: after=2
// NBA-NEXT: final=2

`ifdef TEST_TIMING
module native_timing;
  initial begin
    #5;
    $display("five");
  end
  initial begin
    #2;
    $display("two");
  end
endmodule
`endif

// TIMING: two
// TIMING-NEXT: five

`ifdef TEST_CHANGE
module native_change;
  logic value;
  initial begin
    #2;
    value = 1;
  end
  initial begin
    @(value);
    $display("changed=%0d", value);
  end
endmodule
`endif

// CHANGE: changed=1

`ifdef TEST_NET
module native_net;
  logic first;
  logic second;
  wire destination;
  assign destination = first;
  assign destination = second;
  initial begin
    first = 0;
    second = 1;
    #1;
    $display("net=%b", destination);
  end
endmodule
`endif

// NET: net=x

`ifdef TEST_SELECTIVE
module native_selective_wait;
  logic watched;
  logic unrelated;
  initial begin
    #1 unrelated = 1;
    #1 watched = 1;
  end
  initial begin
    @(watched);
    $display("woke watched=%b unrelated=%b", watched, unrelated);
  end
endmodule
`endif

// SELECTIVE: woke watched=1 unrelated=1

`ifdef TEST_EDGE
module native_edge_wait;
  logic [1:0] watched;
  logic unrelated;
  initial begin
    watched = 2'b00;
    #1 unrelated = 1;
    #1 watched[0] = 1;
    #1 watched[1] = 1;
  end
  initial begin
    @(posedge watched[1]);
    $display("posedge watched=%b", watched[1]);
  end
endmodule
`endif

// EDGE: posedge watched=1

`ifdef TEST_OOB
module native_oob_handle;
  logic [7:0] values [0:0];
  int index = 9;
  initial begin
    values[0] = 8'h5a;
    values[index][3] = 1;
    $display("value=%h", values[0]);
  end
endmodule
`endif

// OOB: value=5a

`ifdef TEST_PARTIAL_OOB
module native_partial_oob_handle;
  logic lower_guard;
  logic [3:0] value;
  logic upper_guard;
  int index;
  initial begin
    lower_guard = 1;
    value = 4'b0000;
    upper_guard = 1;
    index = -1;
    value[index +: 2] = 2'b10;
    index = 3;
    value[index +: 2] = 2'b01;
    $display("partial=%b guards=%b%b", value, lower_guard, upper_guard);
  end
endmodule
`endif

// The lower selection maps only result bit 1 to value[0], and the upper
// selection maps only result bit 0 to value[3]. Adjacent roots remain intact.
// PARTIAL-OOB: partial=1001 guards=11

`ifdef TEST_UNDRIVEN
module native_undriven_net;
  wire undriven;
  initial $display("undriven=%b", undriven);
endmodule
`endif

// UNDRIVEN: undriven=z

`ifdef TEST_NBA_ORDER
module native_nba_order;
  logic [7:0] value;
  initial begin
    value = 0;
    value <= 8'h12;
    value[3:0] <= 4'hb;
    #1;
    $display("nba-order=%h", value);
  end
endmodule
`endif

// NBA-ORDER: nba-order=1b

`ifdef TEST_FOUR_STATE
module native_four_state;
  logic [3:0] value;
  initial begin
    value = 4'bzzzz;
    value <= 4'b10xz;
    #1;
    $display("four-state=%b", value);
  end
endmodule
`endif

// FOUR-STATE: four-state=10xz

`ifdef TEST_CONVERGENCE
module native_convergence;
  logic source;
  logic middle;
  logic destination;
  always_comb middle = source;
  always_comb destination = middle;
  initial begin
    source = 1;
    #1;
    $display("converged=%b", destination);
  end
endmodule
`endif

// CONVERGENCE: converged=1

`ifdef TEST_IO
module native_io;
  int descriptor;
  initial begin
    descriptor = $fopen("/dev/stdout", "w");
    $fdisplay(descriptor, "native file io");
    $fclose(descriptor);
    $display("io=%0d", descriptor != 0);
  end
endmodule
`endif

// IO: native file io
// IO-NEXT: io=1
