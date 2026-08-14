// At -O1 and above the continuous drivers of a net chain are materialized
// into one generated fine-mask region kernel. That kernel decides which
// members to re-evaluate by case-comparing each watched signal against the
// snapshot it carried across the wait boundary, so its per-member dirty mask
// is only correct while the unknown plane survives the round trip. An x-to-0
// transition changes no value bit and is the case that catches a lost plane.
// RUN: obelisk -fno-lto -O2 --native-scheduler=generic %s -o %t.native
// RUN: obelisk -fno-lto -O2 --native-scheduler=aot %s -o %t.aot
// RUN: obelisk -fno-lto -O2 --execution-tier=bytecode %s -o %t.bytecode
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.unoptimized
// RUN: %t.native > %t.native.out
// RUN: %t.aot > %t.aot.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: %t.unoptimized > %t.unoptimized.out
// RUN: diff -u %t.native.out %t.aot.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.out %t.unoptimized.out
// RUN: FileCheck %s < %t.native.out

// The kernel itself must be reachable, or the tier comparison above proves
// nothing about it.
// RUN: obelisk -O2 -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=KERNEL < %t.sim.mlir

module native_region_kernel_four_state;
  logic clock = 0;
  logic started = 0;
  logic [7:0] source;
  wire [7:0] first, second;

  assign first = source + 8'd1;
  assign second = source + 8'd2;

  always_ff @(posedge clock)
    if (!started) begin
      // x -> 0 changes the unknown plane only.
      source <= 8'd0;
      started <= 1'b1;
    end else begin
      source <= source + 8'd3;
    end

  initial begin
    #1 clock = 1;
    #1 clock = 0;
    $display("source=%0d first=%0d second=%0d", source, first, second);
    #1 clock = 1;
    #1 clock = 0;
    $display("source=%0d first=%0d second=%0d", source, first, second);
    $finish;
  end
endmodule

// KERNEL: obelisk_sim.func private @__obelisk_region_kernel_

// CHECK: source=0 first=1 second=2
// CHECK: source=3 first=4 second=5
