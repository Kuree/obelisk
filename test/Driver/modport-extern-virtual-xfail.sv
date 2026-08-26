// XFAIL: *
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null

// Slang currently requires an implementation on the compile-time-only
// virtual-interface type instance, even though the executable interface has a
// sole real provider. MLIR tests cover the backend dispatch until the frontend
// preserves this legal source form.
interface I;
  extern function int foo();
  modport provider(export foo);
  modport consumer(import function int foo());
endinterface

module provider(I.provider i);
  function int i.foo();
    return 1;
  endfunction
endmodule

module top;
  I x();
  provider p(x);
  virtual I.consumer vif = x;
  initial $display("%0d", vif.foo());
endmodule
