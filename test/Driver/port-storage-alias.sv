// RUN: talos -O0 --vpi=off %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 --vpi=off --native-scheduler=generic %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: talos -O3 --vpi=off --execution-tier=bytecode %s -o %t.bc
// RUN: %t.bc | FileCheck %s

// IEEE 1800-2023 6.8, 9.4.2, 10.6.2, 23.3.3.2: static initialization,
// four-state copies, hierarchical clock edges, NBA outputs into
// array elements, and independent force/release on an input variable.
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

module port_storage_alias;
  logic clk = 0;
  logic [3:0] data[2] = '{1, 5};
  logic [3:0] result[2];
  alias_middle dut(clk, data[1], result[0]);
  logic [3:0] initial_sink = dut.inner.d;
  always #5 clk = ~clk;
  initial begin
    #6;
    $display("init=%b q=%b edges=%0d", initial_sink, result[0], dut.inner.edges);
    data[1] = 4'b10xz;
    #10;
    $display("q=%b edges=%0d", result[0], dut.inner.edges);
    data[0] = 4'hf;
    #1;
    $display("sibling q=%b edges=%0d", result[0], dut.inner.edges);
    force dut.d = 4'ha;
    #10;
    $display("forced source=%b q=%b", data[1], result[0]);
    release dut.d;
    #10;
    $display("released q=%b edges=%0d", result[0], dut.inner.edges);
    $finish;
  end
  // CHECK: init=xxxx q=0101 edges=1
  // CHECK: q=10xz edges=2
  // CHECK: sibling q=10xz edges=2
  // CHECK: forced source=10xz q=1010
  // CHECK: released q=10xz edges=4
endmodule
