// RUN: obelisk -fno-lto --std=1800-2023 -O0 -DASSERT_DIRECTIVE %s -o %t.assert
// RUN: obelisk -fno-lto --std=1800-2023 -O0 -DASSUME_DIRECTIVE %s -o %t.assume
// RUN: obelisk -fno-lto --std=1800-2023 -O0 -DCOVER_DIRECTIVE %s -o %t.cover
// RUN: not obelisk -fno-lto --std=1800-2023 -O0 -DEXPECT_DIRECTIVE %s -o %t.expect 2>&1 | FileCheck %s --check-prefix=EXPECT
// RUN: obelisk -fno-lto --std=1800-2023 -O0 -DDISABLE_IFF %s -o %t.disable
// RUN: not obelisk -fno-lto --std=1800-2023 -O0 -DAUTOMATIC_DISABLE %s -o %t.automatic-disable 2>&1 | FileCheck %s --check-prefix=AUTOMATIC-DISABLE
// RUN: not obelisk -fno-lto --std=1800-2023 -O0 -DRANGED_DELAY %s -o %t.range 2>&1 | FileCheck %s --check-prefix=RANGE

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
`elsif AUTOMATIC_DISABLE
  initial begin
    automatic logic automatic_reset = 0;
    cover property (disable iff (automatic_reset) a);
  end
`elsif RANGED_DELAY
  restrict property (a ##[1:2] b);
`endif
endmodule

// EXPECT: error: expect statements are not executable by the bounded concurrent monitor
// AUTOMATIC-DISABLE: error: disable iff cannot asynchronously observe an automatic variable
// RANGE: error: AOT concurrent monitors currently support boolean terms, fixed ## delays
