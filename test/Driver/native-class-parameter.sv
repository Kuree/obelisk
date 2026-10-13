// RUN: obelisk -O3 --std=1800-2017 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --std=1800-2017 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

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
