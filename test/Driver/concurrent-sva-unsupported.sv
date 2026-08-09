// RUN: not obelisk --std=1800-2023 -O0 -DASSERT_DIRECTIVE %s -o %t.assert 2>&1 | FileCheck %s --check-prefix=OBSERVABLE
// RUN: not obelisk --std=1800-2023 -O0 -DASSUME_DIRECTIVE %s -o %t.assume 2>&1 | FileCheck %s --check-prefix=OBSERVABLE
// RUN: not obelisk --std=1800-2023 -O0 -DCOVER_DIRECTIVE %s -o %t.cover 2>&1 | FileCheck %s --check-prefix=OBSERVABLE
// RUN: not obelisk --std=1800-2023 -O0 -DEXPECT_DIRECTIVE %s -o %t.expect 2>&1 | FileCheck %s --check-prefix=OBSERVABLE
// RUN: not obelisk --std=1800-2023 -O0 -DDISABLE_IFF %s -o %t.disable 2>&1 | FileCheck %s --check-prefix=DISABLE
// RUN: not obelisk --std=1800-2023 -O0 -DRANGED_DELAY %s -o %t.range 2>&1 | FileCheck %s --check-prefix=RANGE

module concurrent_sva_unsupported;
  logic clk, a, b, reset;
  default clocking cb @(posedge clk); endclocking

`ifdef ASSERT_DIRECTIVE
  assert property (a);
`elsif ASSUME_DIRECTIVE
  assume property (a);
`elsif COVER_DIRECTIVE
  cover property (a);
`elsif EXPECT_DIRECTIVE
  initial expect (@(posedge clk) a);
`elsif DISABLE_IFF
  restrict property (disable iff (reset) a);
`elsif RANGED_DELAY
  restrict property (a ##[1:2] b);
`endif
endmodule

// OBSERVABLE: error: observable concurrent assert/assume/cover/expect directives require ordered Observed-result and Reactive-report dispatch
// DISABLE: error: disable iff requires asynchronous monitor cancellation
// RANGE: error: AOT concurrent monitors currently support boolean terms, fixed ## delays
