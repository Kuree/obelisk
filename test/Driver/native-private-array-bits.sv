// RUN: obelisk -O3 --execution-tier=native %s -o %t.native
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out
module private_array_bits(input logic [1:0] source, input integer cell_index,
                          input logic signed [31:0] bit_index,
                          output logic [15:0] result);
  logic [7:0] temporary[0:1];
  always_comb begin
    temporary[cell_index][bit_index] = source[0];
    temporary[cell_index][bit_index + 1] = source[1];
    result = {temporary[1], temporary[0]};
  end
endmodule
module native_private_array_bits;
  logic [1:0] source;
  integer cell_index;
  logic signed [31:0] bit_index;
  wire [15:0] result;
  private_array_bits worker(source, cell_index, bit_index, result);
  initial begin
    source = 2'b01; cell_index = 0; bit_index = 2;
    #1 $display("array %b", result);
    source = 2'b10; cell_index = 1; bit_index = 6;
    #1 $display("array %b", result);
    source = 2'b11; cell_index = 2;
    #1 $display("array %b", result);
    cell_index = -1;
    #1 $display("array %b", result);
    cell_index = 0; bit_index = -1;
    #1 $display("array %b", result);
    source = 2'bxz; cell_index = 1; bit_index = 4;
    #1 $display("array %b", result);
    bit_index = 'x;
    #1 $display("array %b", result);
    $finish;
  end
endmodule
// CHECK: array xxxxxxxxxxxx01xx
// CHECK-NEXT: array 10xxxxxxxxxx01xx
// CHECK-NEXT: array 10xxxxxxxxxx01xx
// CHECK-NEXT: array 10xxxxxxxxxx01xx
// CHECK-NEXT: array 10xxxxxxxxxx01x1
// CHECK-NEXT: array 10xzxxxxxxxx01x1
// CHECK-NEXT: array 10xzxxxxxxxx01x1
