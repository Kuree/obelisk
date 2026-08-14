// RUN: obelisk -fno-lto --std=1800-2017 -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto --std=1800-2017 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto --std=1800-2017 -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto --std=1800-2017 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

class parameterized #(parameter int VALUE = 12);
endclass

module native_class_parameter;
  parameterized #(34) object;

  initial begin
    object = new;
    $display("instance=%0d scoped=%0d", object.VALUE,
             parameterized #(34)::VALUE);
  end
endmodule

// CHECK: instance=34 scoped=34
