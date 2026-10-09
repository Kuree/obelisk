// RUN: %python %S/../Runtime/Inputs/add-ranked-private-slot.py %S/../Runtime/Inputs/ranked-ready-order.mlir --whole-read > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace

// The value proof retains only the two nibbles actually used from a whole
// packed read. The later high-half writer must not create a false backedge.
// TRACE: obelisk ranked activation: step2 valid=1 complete=1
// TRACE: obelisk eval boundary: owner=0 island=0 body=step1.__obelisk_eval_body_0 successors=1{{$}}
// TRACE-NEXT: obelisk eval boundary: owner=1 island=0 body=step2.__obelisk_eval_body_0 successors=2{{$}}
// TRACE-NEXT: obelisk eval boundary: owner=2 island=0 body=step3.__obelisk_eval_body_0 successors=3{{$}}
// TRACE-NEXT: obelisk eval boundary: owner=3 island=0 body=step4.__obelisk_eval_body_0 successors=4{{$}}
// TRACE-NEXT: obelisk eval dependency: owner=4 island=0 body=step5.__obelisk_eval_body_0 successors={{$}}
