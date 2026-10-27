// XFAIL: *
// RUN: %talos -emit-slang %s -o - | FileCheck %s

// IEEE 1800-2023 25.7: when a prototype and the subroutine declaration both
// give an argument a default, a call through the prototype uses the
// prototype's default. Slang v11 binds the declaration's default instead.
interface Bus;
  function int f(input int x = 1);
    return x;
  endfunction
  modport client(import function int f(input int x = 2));
endinterface
module Reader(Bus.client b);
  int value;
  initial value = b.f();
endmodule
module top;
  Bus bus();
  Reader r(bus);
endmodule

// CHECK: slang.expression.call attributes {{{.*}}callee_name = "f"
// CHECK-NEXT: slang.expression.integer_literal attributes {constant_value = "2"
