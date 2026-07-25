// RUN: obelisk --std=1800-2023 -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module top;
  int base_value = 20;
  int child_value = 30;

  class base;
    virtual function int get();
      return base_value;
    endfunction
  endclass

  class child extends base;
    virtual function int get();
      return child_value;
    endfunction
  endclass

  initial begin
    automatic child actual;
    automatic base object;
    base_value = 20;
    child_value = 30;
    actual = new;
    object = actual;
    $display("%0d", object.get());
  end
endmodule

// CHECK: 30
