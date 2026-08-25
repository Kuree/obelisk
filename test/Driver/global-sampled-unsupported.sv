// RUN: not obelisk --std=1800-2023 -emit-sim -DMULTICYCLE %s 2>&1 | FileCheck %s --check-prefix=MULTICYCLE
// RUN: not obelisk --std=1800-2023 -emit-sim -DCOVER %s 2>&1 | FileCheck %s --check-prefix=COVER

module global_sampled_unsupported;
  logic gclk, endpoint, a;
  global clocking gcb @(posedge gclk); endclocking
`ifdef MULTICYCLE
  assert property (@(posedge endpoint) $future_gclk(a) ##1 a);
`elsif COVER
  cover property (@(posedge endpoint) $future_gclk(a));
`endif
endmodule

// MULTICYCLE: error: global-future sampled values currently require a one-cycle Boolean property/sequence or same-tick overlapped implication without match items
// COVER: error: global-future sampled values are not lowered through expect or coverage monitors
