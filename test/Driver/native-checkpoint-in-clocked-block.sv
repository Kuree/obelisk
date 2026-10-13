// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out

// A `$display` inside a clocked block is a runtime leaf reached on every
// activation, so the owner has no generated path a route probe could guard.
// Admitting it to the generated eval closure reduced the whole activation to
// a bare checkpoint publication: the nonblocking updates staged before the
// leaf were dropped and the posedge qualification was lost, so the block ran
// on both clock edges and never advanced its state. The default scheduler
// must agree with the generic oracle here.

module top;
  bit clock;
  logic [7:0] source;
  logic [7:0] latched;
  int ticks;

  always #2 clock = ~clock;

  // The nonblocking publications and the cold display leaf share one block.
  always @(posedge clock) begin
    latched <= source;
    ticks <= ticks + 1;
    $display("edge clock=%b latched=%0h", clock, latched);
  end

  initial begin
    source = 8'h5a;
    #40;
    $display("ticks=%0d latched=%0h", ticks, latched);
    $finish;
  end
endmodule

// Ten posedges in forty time units, each observing the previous cycle's
// nonblocking update. A body that runs on both edges, or one whose staged
// updates are discarded, cannot produce this transcript.
// CHECK-COUNT-10: edge clock=1
// CHECK-NOT: edge clock=0
// CHECK: ticks=10 latched=5a
