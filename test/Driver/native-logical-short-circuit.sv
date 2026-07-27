// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module native_logical_short_circuit;
  int calls;

  function automatic logic side_effect(input logic value);
    calls++;
    return value;
  endfunction

  initial begin
    logic result;

    calls = 0;
    result = 1'b0 && side_effect(1'b1);
    $display("false-and result=%b calls=%0d", result, calls);

    calls = 0;
    result = 1'b1 || side_effect(1'b0);
    $display("true-or result=%b calls=%0d", result, calls);

    calls = 0;
    result = 1'b1 && side_effect(1'b1);
    $display("true-and result=%b calls=%0d", result, calls);

    calls = 0;
    result = 1'b0 || side_effect(1'b1);
    $display("false-or result=%b calls=%0d", result, calls);

    calls = 0;
    result = 1'bx && side_effect(1'b0);
    $display("unknown-and-false result=%b calls=%0d", result, calls);

    calls = 0;
    result = 1'bx || side_effect(1'b1);
    $display("unknown-or-true result=%b calls=%0d", result, calls);

    calls = 0;
    result = 4'bx001 || side_effect(1'b0);
    $display("vector-true-or result=%b calls=%0d", result, calls);
  end
endmodule

// CHECK: false-and result=0 calls=0
// CHECK-NEXT: true-or result=1 calls=0
// CHECK-NEXT: true-and result=1 calls=1
// CHECK-NEXT: false-or result=1 calls=1
// CHECK-NEXT: unknown-and-false result=0 calls=1
// CHECK-NEXT: unknown-or-true result=1 calls=1
// CHECK-NEXT: vector-true-or result=1 calls=0
