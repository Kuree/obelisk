// RUN: talos -emit-sim --std=1800-2023 -DLOCAL_INVOCATION %s | FileCheck %s --check-prefix=LOCAL
// RUN: not talos --std=1800-2023 -O0 -DRECURSIVE_INVOCATION %s -o %t.recursive 2>&1 | FileCheck %s --check-prefix=RECURSIVE
// RUN: talos --std=1800-2023 -O0 -DRANGED_INVOCATION %s -o %t.ranged

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
  // An assertion keeps its report effect; a restrict has none in simulation
  // (IEEE 1800-2023 16.14.4) and its effect-free process is removed.
  assert property (with_local(request));
`elsif RECURSIVE_INVOCATION
  restrict property (recursive(countdown));
`elsif RANGED_INVOCATION
  restrict property (ranged(request));
`endif
endmodule

// LOCAL: simulation.func private @{{.*}} attributes {{.*}}home_region = 8 : i32
// LOCAL: simulation.suspend.edge posedge
// LOCAL-NOT: talos.sv.
// RECURSIVE: error: nested SVA implication/followed-by is not executable yet
