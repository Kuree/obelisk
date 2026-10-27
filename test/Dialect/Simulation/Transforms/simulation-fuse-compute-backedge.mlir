// RUN: %split-file %s %t
// RUN: talos -O3 -emit-sim %t/backedge.sv -o %t/backedge.mlir
// RUN: FileCheck %s --check-prefix=BACKEDGE-IR < %t/backedge.mlir

// The two clocked processes fuse in source order. The observer of `published`
// stays its own process, woken through its edge wait by the fused body's
// blocking store (IEEE 1800-2023 9.4.2), so `X` follows `A`; it is not folded
// into the body ahead of the store.
// BACKEDGE-IR-LABEL: simulation.func @__talos_root(
// BACKEDGE-IR: %[[PUBLISHED:.*]] = simulation.context.storage %arg0[1]
// BACKEDGE-IR-NEXT: simulation.spawn @[[OBSERVER:unit_[0-9]+]](%arg0, %[[PUBLISHED]])
// BACKEDGE-IR: simulation.spawn @[[FUSED:__talos_fused_[0-9_]+]](%arg0, %{{.*}}, %[[PUBLISHED]])
// BACKEDGE-IR: simulation.func private @[[OBSERVER]](
// BACKEDGE-IR: simulation.suspend.edge posedge %arg1
// BACKEDGE-IR: simulation.bytes.constant "X"
// BACKEDGE-IR: simulation.func private @[[FUSED]](
// BACKEDGE-IR: simulation.suspend.edge posedge %arg1
// BACKEDGE-IR-NOT: "X"
// BACKEDGE-IR: simulation.bytes.constant "A"
// BACKEDGE-IR-NOT: "X"
// BACKEDGE-IR: simulation.ref.store %{{.*}} to %arg2
// BACKEDGE-IR-NOT: "X"
// BACKEDGE-IR: simulation.bytes.constant "B"

//--- backedge.sv
module activating_backedge;
  bit clock;
  bit published;

  always @(posedge published)
    $display("X");

  always @(posedge clock) begin
    $display("A");
    published = 1;
  end

  always @(posedge clock)
    $display("B");

  initial
    #1 clock = 1;
endmodule
