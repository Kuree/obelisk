// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

class conditional_base;
  int value;
  function new(int value);
    this.value = value;
  endfunction
endclass

class conditional_derived extends conditional_base;
  function new(int value);
    super.new(value);
  endfunction
endclass

module class_conditional_null;
  initial begin
    automatic conditional_base base = new(1);
    automatic conditional_derived derived = new(2);

    base = 1'b1 ? derived : null;
    assert (base != null && base.value == 2);
    base = 1'b0 ? derived : null;
    assert (base == null);
    base = 1'bx ? null : null;
    assert (base == null);
    $display("class conditional null passed");
  end

  // CHECK: class conditional null passed
endmodule
