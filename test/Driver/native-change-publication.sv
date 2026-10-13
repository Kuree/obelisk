// RUN: obelisk -O3 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

// LRM 9.4.2: all four-state value transitions wake change observers; only
// Table 9-2 transitions on the LSB wake edge observers. Repeated stores do
// neither, including an indexed word that crosses a machine-word boundary.
module native_change_publication;
  logic clk = 0;
  logic [3:0] payload = 0;
  logic [3:0] fixed_value = 0;
  logic [129:0] indexed_value = 0;
  int index = 63;
  int changes = 0, rises = 0, falls = 0, indexed_changes = 0;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    fixed_value = payload;
    indexed_value[index +: 4] = payload;
  end
  always @(fixed_value) changes++;
  always @(posedge fixed_value) rises++;
  always @(negedge fixed_value) falls++;
  always @(indexed_value) indexed_changes++;
  initial begin
    #11; // Initial repeated zero stores do not publish.
    payload = 4'b000x;
    #10;
    payload = 4'b000z;
    #10;
    payload = 4'b0001;
    #20; // Repeat exactly the same known value.
    payload = 4'b1001; // A high-bit change is not a vector edge.
    #10;
    payload = 0;
    #20;
    $display("changes=%0d rises=%0d falls=%0d indexed=%0d",
             changes, rises, falls, indexed_changes);
    $finish;
  end
endmodule

// CHECK: changes=5 rises=2 falls=1 indexed=5
