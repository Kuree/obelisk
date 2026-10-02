// RUN: obelisk -O3 --execution-tier=native %s -o %t.native
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out
// Private memory SSA introduces a canonical snapshot at both entry and resume.
// Outlining must repeat that snapshot and preserve partially assigned state.
module private_array_loop(input logic [1:0] source, input logic enable,
                          output logic [15:0] result);
  logic [7:0] temporary[0:1];
  always_comb begin
    for (int i = 0; i < 2; i++) begin
      if (enable)
        temporary[i][i] = source[i];
      temporary[i][i + 4] = source[i];
    end
    result = {temporary[1], temporary[0]};
  end
endmodule
module native_private_array_loop;
  logic [1:0] source;
  logic enable;
  wire [15:0] result;
  private_array_loop worker(source, enable, result);
  initial begin
    source = 2'b01; enable = 1;
    #1 $display("loop %b", result);
    source = 2'b10; enable = 0;
    #1 $display("loop %b", result);
    source = 2'bxz; enable = 1;
    #1 $display("loop %b", result);
    $finish;
  end
endmodule
// CHECK: loop xx0xxx0xxxx1xxx1
// CHECK-NEXT: loop xx1xxx0xxxx0xxx1
// CHECK-NEXT: loop xxxxxxxxxxxzxxxz
