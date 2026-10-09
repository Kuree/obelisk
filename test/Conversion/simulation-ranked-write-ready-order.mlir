// RUN: %python %S/../Runtime/Inputs/add-ranked-bounded-writer.py %S/../Runtime/Inputs/ranked-ready-order.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace

// A bounded writer only updates the high half. Even a directly admitted helper
// must use its precise may-writes, or it appears to feed the low-half consumer.
// TRACE: obelisk ranked activation: step5 valid=1 complete=1 inputs=1
// TRACE: obelisk eval dependency: owner=4 island=0 body=step5.__obelisk_eval_body_0 successors={{$}}
