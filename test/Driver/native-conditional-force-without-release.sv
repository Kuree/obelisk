// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 10.6: a nonconstant force RHS remains continuously active.
// Conditional replacement must also retire the previous evaluator even when
// the process exits without executing a release statement.
module native_conditional_force_without_release;
  logic [1:0] destination = 2;
  logic [1:0] primary = 1;
  logic [1:0] alternate = 0;
  bit replace = 1;

  initial begin
    force destination = primary;
    if (replace)
      force destination = alternate;
    alternate = 3;
    primary = 2;
    #1;
    $display("alternate=%0d", destination);

    replace = 0;
    if (!replace)
      force destination = primary;
    alternate = 2;
    primary = 1;
    #1;
    $display("primary=%0d", destination);
  end
endmodule

// CHECK: alternate=3
// CHECK-NEXT: primary=1
