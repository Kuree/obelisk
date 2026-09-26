// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 9.4.2.2: the implicit event expression consists of the
// readable operands in the controlled statement.  With no such operands,
// there is no event that can wake the process and its body never executes.
module implicit_event_without_dependencies;
  int value;

  always @* value = 100;

  initial begin
    #1;
    $display("value=%0d", value);
  end
endmodule

// CHECK: value=0
