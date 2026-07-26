// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module declaration_initializers;
  reg [7:0] packed_value = 8'hA5;
  reg signed [7:0] signed_value = -8'sd50;
  reg [7:0] high_impedance = 8'bz;
  int values[3] = '{1, 2, 3};
  string message = "hello";

  wire one = 1'b1;
  wire zero = 1'b0;
  wire combined = one & zero;

  function int accumulate(input int a, input int b);
    static int total = 0;
    total = total + a + b;
    return total;
  endfunction

  initial begin
    #1;
    $display("packed=%h signed=%0d high_z=%h", packed_value,
             signed_value, high_impedance);
    $display("values=%0d,%0d,%0d message=%s", values[0], values[1],
             values[2], message);
    $display("nets=%b,%b,%b", one, zero, combined);
    $display("function=%0d,%0d", accumulate(1, 2), accumulate(3, 4));
  end

  // CHECK: packed=a5 signed=-50 high_z=zz
  // CHECK: values=1,2,3 message=hello
  // CHECK: nets=1,0,0
  // CHECK: function=3,10
endmodule
