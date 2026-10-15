// XFAIL: *
// RUN: %obelisk -emit-slang %s -o /dev/null

// IEEE 1800-2023 25.7: a default argument needed by a call through a modport
// import is specified in the prototype. Slang v11 binds arguments against the
// declaration, which has no default, and rejects the call.
interface Bus;
  function int f(input int x);
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
