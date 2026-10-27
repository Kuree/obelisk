// RUN: talos -O3 --execution-tier=native --native-scheduler=auto %s -o %t.auto
// RUN: %t.auto | FileCheck %s

// The testbench `initial` loop carries `n` and the `$value$plusargs` result
// `k` across `@(posedge clk)`, so it is planned as a bootstrap prefix and
// stays runtime-owned, but it never ends: periodic ownership is declined and
// the generated clock keeps running under the node loop. Its wait on the
// forwarded clock is a runtime-routed fanout entry, which the generated
// clock's edges must still resume (IEEE 1800-2023 9.4.2).
module native_periodic_bootstrap_forever_gen(output logic clk, output logic rst);
  initial begin
    clk <= 1'b0;
    forever begin
      #8 clk <= 1'b0;
      #8 clk <= 1'b1;
    end
  end
  initial begin
    rst <= 1'b1;
    for (int i = 0; i < 4; i++) @(posedge clk);
    rst <= 1'b0;
  end
endmodule

module native_periodic_bootstrap_forever;
  logic clk, rst, rst_q;
  native_periodic_bootstrap_forever_gen gen(.clk(clk), .rst(rst));
  always_ff @(posedge clk) rst_q <= rst;
  initial begin
    automatic int n = 0;
    automatic int k = 0;
    if ($value$plusargs("K=%d", k)) begin end
    @(negedge rst_q);
    forever begin
      @(posedge clk);
      #14;
      n++;
      $display("%0t iteration %0d k=%0d", $time, n, k);
      if (n == 3) $finish;
    end
  end
endmodule

// CHECK: 110 iteration 1 k=0
// CHECK-NEXT: 126 iteration 2 k=0
// CHECK-NEXT: 142 iteration 3 k=0
