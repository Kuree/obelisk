// RUN: talos -O3 --execution-tier=native %s -o %t.native
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

module native_scalar_wide_xz;
  logic [255:0] lhs, rhs;
  logic equal;
  logic [7:0] narrowed;
  function automatic logic compare(input logic [255:0] a, b);
    return a == b;
  endfunction
  function automatic logic [7:0] narrow(input logic [255:0] a);
    return a;
  endfunction
  always_comb begin
    equal = compare(lhs, rhs);
    narrowed = narrow(lhs);
  end
  initial begin
    lhs = 'x; rhs = 'x;
    #1 $display("wide %b %b", equal, narrowed);
    if (!equal) $display("incorrect wide comparison");
    lhs = '0; rhs = '0;
    #1 $display("wide %b %b", equal, narrowed);
    lhs = '1; rhs = '0;
    #1 $display("wide %b %b", equal, narrowed);
    lhs = 'z; rhs = 'z;
    #1 $display("wide %b %b", equal, narrowed);
    if (!equal) $display("incorrect wide comparison");
    $finish;
  end
endmodule
// CHECK: wide x xxxxxxxx
// CHECK-NEXT: wide 1 00000000
// CHECK-NEXT: wide 0 11111111
// CHECK-NEXT: wide x zzzzzzzz
// CHECK-NOT: incorrect
