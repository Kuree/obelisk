// RUN: obelisk-opt %S/../Runtime/Inputs/ranked-ready-order.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// A whole-array dynamic read creates a coarse cycle. Dense activation dataflow
// proves the two bounded nibble reads; the physical high-half writer is later.
// TRACE: obelisk ranked activation: step2 valid=1 complete=1
// The boundary producer has a later coarse rank (8) than its consumer (1),
// but receives the earlier ready bit. Otherwise both ready bits cause step2
// to run with stale input before the original-executor boundary is visited.
// TRACE: obelisk eval owner: bit=0 actor={{[0-9]+}} continuation={{[0-9]+}} compute_node=8 group=-
// TRACE-NEXT: obelisk eval owner: bit=1 actor={{[0-9]+}} continuation={{[0-9]+}} compute_node=1 group=__obelisk_eval_ranked_group_0
// TRACE: obelisk eval boundary: owner=0 island=0 body=step1.__obelisk_eval_body_0 successors=1
// TRACE-NEXT: obelisk eval dependency: owner=1 island=0 body=step2.__obelisk_eval_body_0 successors=2
// TRACE-NEXT: obelisk eval boundary: owner=2 island=0 body=step3.__obelisk_eval_body_0 successors=3
// TRACE-NEXT: obelisk eval boundary: owner=3 island=0 body=step4.__obelisk_eval_body_0 successors=4
// TRACE-NEXT: obelisk eval dependency: owner=4 island=0 body=step5.__obelisk_eval_body_0 successors={{$}}
