// RUN: obelisk -O3 --execution-tier=native %s -o %t.native
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out
// The second element crosses a 64-bit boundary. Partial updates must retain
// the neighboring element and both planes for invalid and unknown indices.
module private_array_lanes(input logic [46:0] source,
                           input logic signed [31:0] index,
                           output logic [93:0] result);
  logic [46:0] temporary[0:1];
  always_comb begin
    temporary[index] = source;
    temporary[index][5:2] = 4'b01xz;
    result = {temporary[1], temporary[0]};
  end
endmodule
module native_private_array_lanes;
  logic [46:0] source;
  logic signed [31:0] index;
  wire [93:0] result;
  private_array_lanes worker(source, index, result);
  initial begin
    source = 47'h123; index = 0;
    #1 $display("lane %h %h", result[93:47], result[46:0]);
    source = '1; index = 1;
    #1 $display("lane %h %h", result[93:47], result[46:0]);
    source = '0; index = -1;
    #1 $display("lane %h %h", result[93:47], result[46:0]);
    index = 'x;
    #1 $display("lane %h %h", result[93:47], result[46:0]);
    source = 'z; index = 0;
    #1 $display("lane %h %h", result[93:47], result[46:0]);
    $finish;
  end
endmodule
// CHECK: lane xxxxxxxxxxxx 00000000011X
// CHECK-NEXT: lane 7fffffffffdX 00000000011X
// CHECK-NEXT: lane 7fffffffffdX 00000000011X
// CHECK-NEXT: lane 7fffffffffdX 00000000011X
// CHECK-NEXT: lane 7fffffffffdX {{[xzXZ]+}}
