// RUN: obelisk -O0 --vpi=off %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.bc
// RUN: %t.bc | FileCheck %s

// Whole variable ports exercise the default generated native scheduler.
// IEEE 1800-2023 6.8, 9.4.2, 23.3.3: initialization, X/Z and clock edges.
module alias_leaf(input var logic clk, input var logic [3:0] d,
                  output var logic [3:0] q);
  int edges = 0;
  always_ff @(posedge clk) begin
    q <= d;
    edges <= edges + 1;
  end
endmodule

module alias_middle(input var logic clk, input var logic [3:0] d,
                    output var logic [3:0] q);
  alias_leaf inner(clk, d, q);
endmodule

module port_storage_alias_clock;
  logic clk = 0;
  logic [3:0] data = 5;
  logic [3:0] result;
  alias_middle dut(clk, data, result);
  logic [3:0] initial_sink = dut.inner.d;
  always #5 clk = ~clk;
  initial begin
    #6;
    $display("init=%b q=%b edges=%0d", initial_sink, result, dut.inner.edges);
    data = 4'b10xz;
    #10;
    $display("q=%b edges=%0d", result, dut.inner.edges);
    $finish;
  end
  // CHECK: init=xxxx q=0101 edges=1
  // CHECK: q=10xz edges=2
endmodule
