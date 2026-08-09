// RUN: not obelisk --std=1800-2023 -O0 -DLOCAL_INVOCATION %s -o %t.local 2>&1 | FileCheck %s --check-prefix=LOCAL
// RUN: not obelisk --std=1800-2023 -O0 -DRECURSIVE_INVOCATION %s -o %t.recursive 2>&1 | FileCheck %s --check-prefix=NEGATIVE
// RUN: not obelisk --std=1800-2023 -O0 -DRANGED_INVOCATION %s -o %t.ranged 2>&1 | FileCheck %s --check-prefix=NEGATIVE

module concurrent_sva_expanded_invocation_negative;
  logic clk, request, acknowledge;
  int countdown;
  default clocking cb @(posedge clk); endclocking

  sequence with_local(logic first);
    int seen = 0;
    (first, seen = seen + 1) ##1 acknowledge;
  endsequence

  property recursive(int remaining);
    remaining == 0 or (remaining > 0 |=> recursive(remaining));
  endproperty

  property ranged(logic first);
    first |-> ##[1:2] acknowledge;
  endproperty

`ifdef LOCAL_INVOCATION
  restrict property (with_local(request));
`elsif RECURSIVE_INVOCATION
  restrict property (recursive(countdown));
`elsif RANGED_INVOCATION
  restrict property (ranged(request));
`endif
endmodule

// LOCAL: error: unsupported semantic construct in the first simulation slice: obelisk.sv.symbol.local_assertion_var
// NEGATIVE: error: {{.*}}AOT
