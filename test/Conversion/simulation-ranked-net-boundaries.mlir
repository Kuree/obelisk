// RUN: obelisk-opt %S/../Runtime/Inputs/ranked-net-boundaries.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: sed 's/to 1\[0\] width 8 reversed = false/to 1[7] width 8 reversed = true/' %S/../Runtime/Inputs/ranked-net-boundaries.mlir > %t.reversed.mlir
// RUN: obelisk-opt %t.reversed.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2> %t.reversed.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.reversed.trace
// RUN: sed '/simulation.net.connect.decl/d' %S/../Runtime/Inputs/ranked-net-boundaries.mlir > %t.disconnected.mlir
// RUN: obelisk-opt %t.disconnected.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2> %t.disconnected.trace
// RUN: FileCheck %s --check-prefix=DISCONNECTED < %t.disconnected.trace
// Net publication effects already include connected aliases, so both boundary
// nodes participate in the directed chain without universal alias edges.
// TRACE: obelisk eval dependency: owner=0 island=0 body=step1.__obelisk_eval_body_0 successors=1
// TRACE-NEXT: obelisk eval dependency: owner=1 island=0 body=step2.__obelisk_eval_body_0 successors=2
// TRACE-NEXT: obelisk eval boundary: owner=2 island=0 body=step3.__obelisk_eval_body_0 successors=3
// TRACE-NEXT: obelisk eval boundary: owner=3 island=0 body=step4.__obelisk_eval_body_0 successors=4
// TRACE-NEXT: obelisk eval dependency: owner=4 island=0 body=step5.__obelisk_eval_body_0 successors={{$}}
// DISCONNECTED: obelisk eval boundary: owner=2 island=0 body=step3.__obelisk_eval_body_0 successors={{$}}
// DISCONNECTED-NEXT: obelisk eval boundary: owner=3 island=3 body=step4.__obelisk_eval_body_0 successors=4{{$}}
