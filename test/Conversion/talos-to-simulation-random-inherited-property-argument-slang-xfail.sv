// XFAIL: *
// RUN: %talos --std=1800-2023 -emit-talos %s -o %t.mlir

// IEEE 1800-2023 18.11 lets randomize() name properties of the object, and a
// derived object has its base class's properties (8.13). Pristine Slang v11.0
// reports "expected class property name" for an inherited property argument
// called through a derived-class handle, though it accepts the base handle.
class Base;
  rand bit [3:0] m;
  rand bit [3:0] n;
endclass

class Derived extends Base;
endclass

module top;
  Derived d = new;
  initial void'(d.randomize(m));
endmodule
