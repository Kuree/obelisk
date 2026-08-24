// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 8.15: `super.member` selects an inherited property; a method
// called through the property's value still dispatches on that value.
virtual class super_member_target;
  virtual function int value();
    return 0;
  endfunction
endclass

class super_member_derived_target extends super_member_target;
  virtual function int value();
    return 42;
  endfunction
endclass

virtual class super_member_base;
  super_member_target target;
endclass

class super_member_child extends super_member_base;
  function new();
    super_member_derived_target derived = new;
    target = derived;
  endfunction

  function int read();
    return super.target.value();
  endfunction
endclass

module class_super_member_call;
  initial begin
    automatic super_member_child child = new;
    assert (child.read() == 42);
    $display("super member call passed");
  end

  // CHECK: super member call passed
endmodule
