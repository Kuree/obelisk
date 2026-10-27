// RUN: talos -O3 --execution-tier=native --native-scheduler=auto %s -o %t.auto
// RUN: env TALOS_RT_SIGNAL_DIAGNOSTICS=1 %t.auto 2> %t.auto.diag | FileCheck %s
// RUN: FileCheck %s --check-prefix=DIAG < %t.auto.diag

// Finite runtime waiters keep periodic bootstrap active among generated
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
  bit [31:0] q[8];
  for (genvar i = 0; i < 8; i++)
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
// DIAG: talos-signal-diagnostics
// DIAG-SAME: periodic_preparations={{[1-9][0-9]*}}
// DIAG-SAME: periodic_clocks_high_water=1
