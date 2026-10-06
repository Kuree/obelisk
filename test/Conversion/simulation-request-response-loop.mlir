// RUN: obelisk-opt %S/../Runtime/Inputs/request-response-loop.mlir --mlir-print-op-on-diagnostic=false --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},obelisk-sim-materialize-compute-fusion))' -o %t 2>&1 | FileCheck %s --check-prefix=PROOF
// RUN: FileCheck %s --check-prefix=KEEP --implicit-check-not=__obelisk_region_kernel_ < %t
// RUN: obelisk-opt %S/../Runtime/Inputs/request-response-loop.mlir --test-obelisk-activation-regions='functions=producer_and_consumer,transform publications=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=VALUES
// RUN: obelisk-opt %S/../Runtime/Inputs/request-response-loop.mlir --test-obelisk-activation-regions='functions=producer_and_consumer,transform publications=true reaching-values=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=LANES
// Value equations are acyclic: q0=v, q1=v+1, r0=q0, r1=q0+q1.
// Whole loops are indivisible in the current scheduling proof. The producer
// reads responses between request stores; the consumer publishes the complete
// response after its loop. A safe single sweep needs a finer dependence proof.
// PROOF: statement component rejected: structured dependence cycle
// PROOF-SAME: SSA publication graph: acyclic
// PROOF-SAME: cycle edge:
// PROOF-SAME: via s1[0,16)
// PROOF-SAME: cycle edge:
// PROOF-SAME: via s2[0,16)
// KEEP: simulation.spawn @producer_and_consumer
// KEEP: simulation.spawn @transform
// KEEP: simulation.spawn @sample
// Following SSA yields separates independent publications within one loop.
// The value graph alone does not authorize reordering source publications.
// VALUES-LABEL: regions @producer_and_consumer valid=1
// VALUES: publication read:s0[0,8) write:s1[0,16){{$}}
// VALUES-NEXT: publication read:s2[0,16) write:s3[0,16){{$}}
// VALUES-LABEL: regions @transform valid=1
// VALUES: publication read:s1[0,16) write:s2[0,16){{$}}
// VALUES-NEXT: publication graph: acyclic
// The prefix's first result uses only the first request. The second result
// uses both requests; loop-carried values retain iteration correlation.
// LANES-LABEL: regions @transform valid=1
// LANES: publication read:s1[0,8) write:s2[0,8){{$}}
// LANES-NEXT: publication read:s1[0,16) write:s2[8,16){{$}}
// LANES-NEXT: publication graph: acyclic
