// RUN: obelisk-opt %S/../Runtime/Inputs/request-response-loop.mlir --mlir-print-op-on-diagnostic=false --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},obelisk-sim-materialize-compute-fusion))' -o %t 2>&1 | FileCheck %s --check-prefix=PROOF
// RUN: FileCheck %s --check-prefix=FUSED < %t
// RUN: obelisk-opt %S/../Runtime/Inputs/request-response-loop.mlir --test-obelisk-activation-regions='functions=producer_and_consumer,transform publications=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=VALUES
// RUN: obelisk-opt %S/../Runtime/Inputs/request-response-loop.mlir --test-obelisk-activation-regions='functions=producer_and_consumer,transform publications=true reaching-values=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=LANES
// Value equations are acyclic: q0=v, q1=v+1, r0=q0, r1=q0+q1.
// The producer reads responses between request stores, so statement order is
// cyclic. Each store depends only on earlier data, and `sample` reads the
// response only at a clock edge, never waiting on it. The cohort therefore
// runs once in data-dependence order (IEEE 1800-2023 4.3); loops stay loops.
// PROOF: 1 plans (1 data-dependence order)
// FUSED-NOT: simulation.spawn @producer_and_consumer
// FUSED-NOT: simulation.spawn @transform
// FUSED: simulation.spawn @__obelisk_region_kernel_0_0
// FUSED: simulation.spawn @sample
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
