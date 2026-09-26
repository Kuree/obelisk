module top;
  integer value;
  initial begin
    if (0) value = 1; value = 2;
    $finish;
  end
endmodule

// The constant-dead assignment remains an obligation. The physical line is
// partial because its if/control and second assignment execute while the
// controlled assignment does not.
// RUN: obelisk -O0 --coverage=line %s -o %t.native
// RUN: %t.native --coverage-output=%t.native.obcov
// RUN: obelisk-cov report --format=text %t.native.obcov | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode --coverage=line %s -o %t.bytecode
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov
// RUN: obelisk-cov report --format=text %t.bytecode.obcov | FileCheck %s

// CHECK: line: 50.00% (1/2), partial 1
