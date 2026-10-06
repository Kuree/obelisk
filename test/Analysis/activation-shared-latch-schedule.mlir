// RUN: obelisk-opt %S/../Runtime/Inputs/shared-latch-loop.mlir --test-obelisk-activation-regions='functions=producer_and_consumer,transform thread-control=true reaching-values=true order=true' -o %t.analyzed 2>&1 | FileCheck %s --check-prefix=PROOF
// RUN: obelisk-opt %S/../Runtime/Inputs/shared-latch-loop.mlir -o %t.original
// RUN: diff %t.original %t.analyzed
// RUN: obelisk-opt %S/../Runtime/Inputs/shared-latch-loop.mlir --mlir-print-op-on-diagnostic=false --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},obelisk-sim-materialize-compute-fusion))' -o %t 2>&1 | FileCheck %s --check-prefix=DEFAULT
// RUN: FileCheck %s --check-prefix=UNCHANGED --implicit-check-not="simulation.spawn @__obelisk_region_kernel" < %t

// Preparing a shared latch exposes a legal request/responder/consumer order
// without unrolling either loop. This proof is read-only: enabling the prepared
// schedule still requires a separate profitability decision by its consumer.
// PROOF: regions @producer_and_consumer valid=1
// PROOF: regions @transform valid=1
// PROOF: region order: acyclic
// Production clients retain their canonical CFG policy until prepared code
// generation passes the runtime performance gate.
// DEFAULT: statement scheduling rejected: termination
// UNCHANGED: simulation.spawn @producer_and_consumer
// UNCHANGED: simulation.spawn @transform
