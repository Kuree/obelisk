// RUN: obelisk -O3 --execution-tier=native --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --execution-tier=native --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.reference > %t.reference.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.auto > %t.auto.out 2> %t.auto.diag
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.eval.diag
// RUN: diff -u %t.reference.out %t.auto.out
// RUN: diff -u %t.reference.out %t.eval.out
// RUN: FileCheck %s < %t.auto.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.auto.diag
// RUN: FileCheck %s --check-prefix=DIAG < %t.eval.diag

// Finite runtime waiters keep periodic bootstrap active among many generated
// actors. A child created after time has advanced must enter the live index,
// including a wait on a forwarded clock, and must observe the common NBA slot.
module native_periodic_bootstrap_index;
  logic clk;
  wire alias_clk = clk;
  initial begin
    clk <= 0;
    forever begin
      #5 clk <= 0;
      #5 clk <= 1;
    end
  end
  bit [31:0] q[128];
  for (genvar i = 0; i < 128; i++)
    always @(posedge clk) q[i] <= q[i] + 1;
  initial begin
    automatic string label = "bootstrap";
    #2;
    fork
      begin
        repeat (3) @(posedge clk);
        $display("%s first %0d", label, q[0]);
        repeat (2) @(negedge clk);
        $display("%s second %0d", label, q[0]);
      end
      begin
        #9;
        fork
          begin
            automatic string child = "child";
            repeat (3) @(posedge alias_clk);
            $display("%s %0d", child, q[0]);
          end
        join
      end
    join
    #1;
    $display("final %0d", q[0]);
    $finish;
  end
endmodule

// CHECK: bootstrap first 2
// CHECK-NEXT: child 3
// CHECK-NEXT: bootstrap second 4
// CHECK-NEXT: final 4
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: periodic_preparations={{[1-9][0-9]*}}
// DIAG-SAME: periodic_clocks_high_water=1
