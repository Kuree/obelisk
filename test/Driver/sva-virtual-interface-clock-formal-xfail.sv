// XFAIL: *
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null

// Slang currently rejects a virtual-interface member reached from a
// concurrent assertion expansion as a dynamic non-procedural reference. Keep
// the legal source case recorded until the frontend can supply its expanded
// semantic body; the MLIR-level virtual-clock test covers executable lowering.
interface clock_if;
  logic clk;
  logic value;
  clocking cb @(posedge clk);
    input value;
  endclocking
endinterface

module sva_virtual_interface_clock_formal_xfail;
  clock_if actual();
  virtual clock_if vif = actual;

  sequence sampled(event sampling, logic value);
    @sampling value;
  endsequence

  property forwarded(event sampling, logic value);
    sampled(sampling, value);
  endproperty

  assert property (forwarded(vif.cb, vif.value));
endmodule
