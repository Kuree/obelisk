// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s --check-prefix=LOWER < %t.mlir
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=read},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=TRACE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' 2> %t.full.trace | FileCheck %s --check-prefix=VPI --implicit-check-not=__obelisk_eval_ranked_group_
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.high set={driven_low = 5 : i64, driven_width = 1 : i64}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=PARTIAL
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.high set={strength0 = 3 : i32, strength1 = 3 : i32}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.net set={resolution_kind = 1 : i32}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY

// Small version of BP unit_7183: read a sparse packed tree row, extract two
// demanded bits, and drive a separate two-bit net. Preserve the whole-row
// sensitivity. Sparse contributors occupy bits 0 and 4; every other bit is Z.
// IEEE 1800-2023 6.6/6.7, 6.6.1, 4.9.1 and 9.4.2.
// TRACE: obelisk sparse scheduling nets: 1
// TRACE: obelisk ranked groups: candidates=4 groups=1 segments=0 members=4
// Each sparse driver owns its bit alone (6.6.1), so every member is repeatable
// and the group runs all of them without ready predicates.
// TRACE: obelisk repeatable ranked members: 4 unconditional=4
// TRACE: obelisk eval dependency: owner=0 island=0 body=source_low.__obelisk_eval_body_0 successors=2
// TRACE: obelisk eval dependency: owner=1 island=0 body=source_high.__obelisk_eval_body_0 successors=2
// TRACE: obelisk eval dependency: owner=2 island=0 body=copy.__obelisk_eval_body_0 successors=3
// TRACE: obelisk eval dependency: owner=3 island=0 body=sink.__obelisk_eval_body_0 successors=
// TRACE: obelisk group dataflow: __obelisk_eval_ranked_group_0.dataflow accepted=0
// Sparse scheduling must not supply the stronger complete-coverage fact used
// to elide driver unknown-plane accesses, even in a selected two-state body.
// LOWER-LABEL: llvm.func @source_low.__obelisk_eval_body_0.__obelisk_two_state_0(
// LOWER: llvm.mlir.addressof @__obelisk_state_unknown
// LOWER: llvm.store
// LOWER: llvm.call preserve_mostcc @__obelisk_eval_promotion_publish_unknown_v1
// LOWER-LABEL: llvm.func @source_high.__obelisk_eval_body_0.__obelisk_two_state_0(
// LOWER: llvm.mlir.addressof @__obelisk_state_unknown
// LOWER: llvm.store
// LOWER: llvm.call preserve_mostcc @__obelisk_eval_promotion_publish_unknown_v1
// LOWER-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// LOWER-SAME: schedule.eval.ranked_members = array<i32: 0, 1, 2, 3>
// LOWER: llvm.mlir.addressof @__obelisk_state_unknown
// The out-of-contribution drive remains a boundary with its outgoing edge.
// PARTIAL: obelisk eval boundary: owner={{[0-9]+}} island={{[0-9]+}} body=source_high.__obelisk_eval_body_0 successors={{[0-9]+}}
// BOUNDARY-DAG: obelisk eval boundary: owner={{[0-9]+}} island={{[0-9]+}} body=source_low.__obelisk_eval_body_0
// BOUNDARY-DAG: obelisk eval boundary: owner={{[0-9]+}} island={{[0-9]+}} body=source_high.__obelisk_eval_body_0
// BOUNDARY-DAG: obelisk eval boundary: owner={{[0-9]+}} island={{[0-9]+}} body=copy.__obelisk_eval_body_0
// VPI-LABEL: llvm.func @__obelisk_eval_dispatch_v1(

!bit = !simulation.logic<1>
!pair = !simulation.logic<2>
!row = !simulation.packed_array<7 : 0 x !bit>
!r = !simulation.ref<!bit>
!r2 = !simulation.ref<!pair>
!n = !simulation.net<!row>
!n2 = !simulation.net<!pair>
!d = !simulation.driver<!row>
!d1 = !simulation.driver<!bit>
!d2 = !simulation.driver<!pair>
!clock = !simulation.ref<i1>
!c = !simulation.context
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.debug_timing, native.closed_executable} {
  simulation.design @sparse_tree {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !pair design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.net.decl 0 in 0 : !row design {test.net}
    simulation.net.decl 1 in 0 : !pair design
    simulation.driver.decl 0 in 0 drives 0 : !row design {driven_low = 0 : i64, driven_width = 1 : i64}
    simulation.driver.decl 1 in 0 drives 1 : !pair design
    simulation.driver.decl 2 in 0 drives 0 : !row design {driven_low = 4 : i64, driven_width = 1 : i64, test.high}
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "source_low"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "copy"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "sink"
    simulation.code_unit.decl 6 in 0 initial hierarchy "test"
    simulation.code_unit.decl 7 in 0 function hierarchy "report"
    simulation.code_unit.decl 8 in 0 continuous hierarchy "source_high"
    simulation.func @root(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.spawn @clock(%ctx) : !c -> !simulation.process
      %sink = simulation.spawn @sink(%ctx) : !c -> !simulation.process
      %copy = simulation.spawn @copy(%ctx) : !c -> !simulation.process
      %low = simulation.spawn @source_low(%ctx) : !c -> !simulation.process
      %high = simulation.spawn @source_high(%ctx) : !c -> !simulation.process
      %test = simulation.spawn @test(%ctx) : !c -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clock
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !clock -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clk : i1, !clock
      cf.br ^wait
    }
    simulation.func @source_low(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[1] : !r
      %clk = simulation.context.storage %ctx[0] : !clock
      %whole = simulation.context.driver %ctx[0] : !d
      %driver = simulation.driver.extract %whole from 0 : !d -> !d1
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !r -> !bit
      simulation.driver.drive %driver = %v : !d1, !bit
      simulation.suspend.any %clk, %input edges [0, 0] to ^body : !clock, !r
    }
    simulation.func @source_high(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 8 : i64} {
      %input = simulation.context.storage %ctx[3] : !r
      %whole = simulation.context.driver %ctx[2] : !d
      %driver = simulation.driver.extract %whole from 4 : !d -> !d1
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !r -> !bit
      simulation.driver.drive %driver = %v : !d1, !bit
      simulation.suspend.change %input to ^body : !r
    }
    simulation.func @copy(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.net %ctx[0] : !n
      %driver = simulation.context.driver %ctx[1] : !d2
      cf.br ^body
    ^body:
      %row = simulation.net.read %input : !n -> !row
      %high = simulation.aggregate.extract %row[3] : (!row) -> !bit
      %low = simulation.aggregate.extract %row[7] : (!row) -> !bit
      %value = simulation.logic.concat %high, %low : (!bit, !bit) -> !pair
      simulation.driver.drive %driver = %value : !d2, !pair
      simulation.suspend.change %input to ^body : !n
    }
    simulation.func @sink(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      %input = simulation.context.net %ctx[1] : !n2
      %output = simulation.context.storage %ctx[2] : !r2
      cf.br ^body
    ^body:
      %value = simulation.net.read %input : !n2 -> !pair
      simulation.ref.store %value to %output {simulation.continuous_store} : !pair, !r2
      simulation.suspend.change %input to ^body : !n2
    }
    simulation.func @test(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %low = simulation.context.storage %ctx[1] : !r
      %high = simulation.context.storage %ctx[3] : !r
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !bit
      %one = simulation.logic.constant 1 : i1, 0 : i1 : !bit
      %x = simulation.logic.constant 0 : i1, 1 : i1 : !bit
      %z = simulation.logic.constant 1 : i1, 1 : i1 : !bit
      %delay = simulation.time.constant 3
      simulation.ref.store %zero to %low : !bit, !r
      simulation.ref.store %one to %high : !bit, !r
      simulation.suspend.delay %delay to ^known
    ^known:
      simulation.call @report(%ctx) : (!c) -> ()
      simulation.ref.store %one to %low : !bit, !r
      simulation.suspend.delay %delay to ^low_change
    ^low_change:
      simulation.call @report(%ctx) : (!c) -> ()
      simulation.ref.store %x to %high : !bit, !r
      simulation.suspend.delay %delay to ^unknown
    ^unknown:
      simulation.call @report(%ctx) : (!c) -> ()
      simulation.ref.store %z to %high : !bit, !r
      simulation.suspend.delay %delay to ^high_z
    ^high_z:
      simulation.call @report(%ctx) : (!c) -> ()
      simulation.ref.store %zero to %high : !bit, !r
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.call @report(%ctx) : (!c) -> ()
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
    simulation.func @report(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %output = simulation.context.storage %ctx[2] : !r2
      %net = simulation.context.net %ctx[0] : !n
      %value = simulation.ref.load %output : !r2 -> !pair
      %row = simulation.net.read %net : !n -> !row
      %bits = simulation.packed.flatten %row : (!row) -> !simulation.logic<8>
      %fmt = simulation.bytes.constant "sparse %b result %b"
      %stdout = arith.constant -2147483647 : i32
      simulation.display %ctx to %stdout(%fmt, %bits, %value) newline = true radix = <binary> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !pair
      simulation.return
    }
  }
}
