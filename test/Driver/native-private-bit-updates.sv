// RUN: obelisk -O3 --execution-tier=native %s -o %t.native
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out
module private_bits(input logic [1:0] source, input integer index,
                    output logic [7:0] result);
  logic [7:0] temporary;
  always_comb begin
    temporary[index] = source[0];
    temporary[index + 1] = source[1];
    result = temporary;
  end
endmodule
module native_private_bit_updates;
  logic [1:0] source;
  integer index;
  wire [7:0] result;
  private_bits worker(source, index, result);
  initial begin
    source = 2'b01; index = 2;
    #1 $display("private %b", result);
    source = 2'b10; index = 6;
    #1 $display("private %b", result);
    source = 2'b11; index = 8;
    #1 $display("private %b", result);
    index = -1;
    #1 $display("private %b", result);
    source = 2'bxz; index = 4;
    #1 $display("private %b", result);
    $finish;
  end
endmodule
// CHECK: private xxxx01xx
// CHECK-NEXT: private 10xx01xx
// CHECK-NEXT: private 10xx01xx
// CHECK-NEXT: private 10xx01x1
// CHECK-NEXT: private 10xz01x1
