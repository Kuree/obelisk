// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 8.8: an implicit constructor evaluates instance-property
// initializers with the newly allocated object as `this`.
interface class implicit_owner;
  pure virtual function int get();
endclass

class implicit_peer;
  implicit_owner owner;
  function new(implicit_owner owner);
    this.owner = owner;
  endfunction
endclass

class implicit_object implements implicit_owner;
  int value = 42;
  implicit_peer peer = new(this);

  virtual function int get();
    return value;
  endfunction

  function implicit_object clone();
    implicit_object result = new this;
    return result;
  endfunction
endclass

module class_implicit_constructor_this;
  initial begin
    automatic implicit_object object = new;
    automatic implicit_object copy = object.clone();
    assert (object.peer.owner.get() == 42);
    assert (copy.value == 42);
    object.peer.owner = null;
    copy.peer.owner = null;
    $display("implicit constructor this passed");
  end

  // CHECK: implicit constructor this passed
endmodule
