// RUN: talos -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.timing
// RUN: talos -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic > %t.generic.out
// RUN: %t.auto | diff -u %t.generic.out -

// The loop wakes on the clock and reads `a`, which an NBA of the same edge
// updates, so it must see the pre-NBA value (IEEE 1800-2023 4.4.2, 10.4.2) and
// stays under runtime Active ordering. The report names the actor.
module native_aot_waiter_pre_nba_read;
  logic clk = 0;
  logic [15:0] a = 0;
  int cycle, sum;

  always #8 clk = ~clk;
  always_ff @(posedge clk) a <= a + 1;

  initial begin
    #1;
    forever begin
      @(posedge clk);
      sum += a;
      cycle += 1;
      if (cycle % 500 == 0) $display("%0d cycle sum=%0d", cycle, sum);
      if (cycle == 2000) begin
        $display("done %0d", sum);
        $finish;
      end
    end
  end
endmodule

// ADMISSION: native withheld actor: cost={{[0-9]+}} at=loc("{{.*}}native-aot-waiter-pre-nba-read.sv":[[# @LINE - 15]]:3) reason=initial signal waiter requires Active ordering
// ADMISSION: native eligibility: eligible=1 fully_eligible=0
// ADMISSION: native boundary: initial signal waiter requires Active ordering
