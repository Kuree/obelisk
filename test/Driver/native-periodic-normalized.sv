// RUN: talos -O3 --native-scheduler=auto %s -o %t.auto
// RUN: talos -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.reference > %t.reference.out
// RUN: env TALOS_RT_SIGNAL_DIAGNOSTICS=1 %t.auto 2> %t.auto.diag | diff -u %t.reference.out -
// RUN: FileCheck %s < %t.reference.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.auto.diag

// Integration coverage for two extracted clocks and their CDC consumers.
// The MLIR tests cover the rewrite, rejection proofs, and NBA/phase semantics.
module native_periodic_normalized;
  logic fast, slow = 0;
  int cycles;
  initial begin
    fast <= 0;
    cycles <= 0;
    forever begin
      #3 fast <= 0;
      #3 fast <= 1;
      cycles++;
    end
  end
  always begin
    #5 slow <= 0;
    #5 slow <= 1;
  end
  bit [31:0] q[2], sync1[2], sync2[2];
  for (genvar i = 0; i < 2; i++) begin : g
    always @(posedge fast) q[i] <= q[i] + cycles + sync2[i];
    always @(posedge slow) begin
      sync1[i] <= q[i];
      sync2[i] <= sync1[i];
    end
  end
  initial begin
    #61;
    $display("cycles=%0d q=%0d sync=%0d/%0d", cycles, q[0], sync1[0], sync2[0]);
    #60;
    $display("cycles=%0d q=%0d sync=%0d/%0d", cycles, q[0], sync1[0], sync2[0]);
    $finish;
  end
endmodule

// CHECK: cycles=10
// CHECK-NEXT: cycles=20
// DIAG: periodic_preparations={{[1-9][0-9]*}}
// DIAG-SAME: periodic_clocks_high_water=2
