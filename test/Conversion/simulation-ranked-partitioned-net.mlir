// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s --check-prefix=GROUP < %t.mlir
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.high set={driven_low = 0 : i64, driven_width = 8 : i64}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.high set={driven_low = 5 : i64, driven_width = 3 : i64}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.high set={strength0 = 3 : i32, strength1 = 3 : i32}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.net set={propagation_delays = array<i64: 1, 1, 1>}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' 2> %t.full.trace | FileCheck %s --check-prefix=VPI --implicit-check-not=__obelisk_eval_ranked_group_

// IEEE 1800-2023 6.6.1 resolves each bit independently: two strong drivers
// that partition this wire cannot conflict. Grouping retains each source's
// sensitivity and exact publications (4.9.1), so changing only the high input
// preserves the low nibble, including X/Z (6.3.1). No unconditional early
// statement fusion is part of this fixture.
// TRACE: obelisk ranked groups: candidates=4 groups=1 segments=0 members=4
// TRACE: obelisk group dataflow: __obelisk_eval_ranked_group_0.dataflow accepted=1
// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// GROUP-SAME: schedule.eval.predicated_dataflow
// GROUP-SAME: schedule.eval.ranked_members = array<i32: 0, 1, 2, 3>
// One certificate gate covers the entire input group, including the net's
// previous value. The certified path has no per-drive unknown publication.
// GROUP: llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// GROUP: llvm.cond_br
// GROUP-NOT: llvm.call
// GROUP-NOT: llvm.cond_br
// GROUP-NOT: llvm.br
// GROUP-NOT: llvm.switch
// GROUP: llvm.return
// GROUP: llvm.call @__obelisk_eval_ranked_group_0.fallback
// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_0.fallback(
// GROUP: llvm.call preserve_mostcc @__obelisk_eval_promotion_publish_unknown_v1
// GROUP: llvm.return
// Overlap, undriven holes, weak contributions, and delay fail the topology
// proof. Writable VPI retains override-aware source boundaries (38.34).
// BOUNDARY-DAG: obelisk eval boundary: owner={{[0-9]+}} island={{[0-9]+}} body=source.__obelisk_eval_body_0
// BOUNDARY-DAG: obelisk eval boundary: owner={{[0-9]+}} island={{[0-9]+}} body=source_high.__obelisk_eval_body_0
// VPI: llvm.func @__obelisk_eval_dispatch_v1(

!byte = !simulation.logic<8>
!ref = !simulation.ref<!byte>
!net = !simulation.net<!byte>
!driver = !simulation.driver<!byte>
!nibble = !simulation.logic<4>
!d4 = !simulation.driver<!nibble>
!clock = !simulation.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.debug_timing, native.closed_executable} {
  simulation.design @net_chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.storage.decl 2 in 0 : !byte design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.net.decl 0 in 0 : !byte design {test.net}
    simulation.net.decl 1 in 0 : !byte design
    simulation.driver.decl 0 in 0 drives 0 : !byte design {driven_low = 0 : i64, driven_width = 4 : i64}
    simulation.driver.decl 1 in 0 drives 1 : !byte design
    simulation.driver.decl 2 in 0 drives 0 : !byte design {driven_low = 4 : i64, driven_width = 4 : i64, test.high}
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "source"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "copy"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "sink"
    simulation.code_unit.decl 6 in 0 initial hierarchy "test"
    simulation.code_unit.decl 7 in 0 function hierarchy "report"
    simulation.code_unit.decl 8 in 0 continuous hierarchy "source_high"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.spawn @clock(%ctx) : !simulation.context -> !simulation.process
      %sink = simulation.spawn @sink(%ctx) : !simulation.context -> !simulation.process
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process
      %source = simulation.spawn @source(%ctx) : !simulation.context -> !simulation.process
      %high = simulation.spawn @source_high(%ctx) : !simulation.context -> !simulation.process
      %test = simulation.spawn @test(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
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
    simulation.func @source(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[1] : !ref
      %clk = simulation.context.storage %ctx[0] : !clock
      %whole = simulation.context.driver %ctx[0] : !driver
      %driver = simulation.driver.extract %whole from 0 : !driver -> !d4
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> !byte
      %low = simulation.logic.extract %v from 0 : !byte -> !nibble
      simulation.driver.drive %driver = %low : !d4, !nibble
      simulation.suspend.any %clk, %input edges [0, 0] to ^body : !clock, !ref
    }
    simulation.func @source_high(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 8 : i64} {
      %input = simulation.context.storage %ctx[3] : !ref
      %whole = simulation.context.driver %ctx[2] : !driver
      %driver = simulation.driver.extract %whole from 4 : !driver -> !d4
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !ref -> !byte
      %high = simulation.logic.extract %v from 4 : !byte -> !nibble
      simulation.driver.drive %driver = %high : !d4, !nibble
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.net %ctx[0] : !net
      %driver = simulation.context.driver %ctx[1] : !driver
      cf.br ^body
    ^body:
      %v = simulation.net.read %input : !net -> !byte
      simulation.driver.drive %driver = %v : !driver, !byte
      simulation.suspend.change %input to ^body : !net
    }
    simulation.func @sink(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      %input = simulation.context.net %ctx[1] : !net
      %output = simulation.context.storage %ctx[2] : !ref
      cf.br ^body
    ^body:
      %v = simulation.net.read %input : !net -> !byte
      simulation.ref.store %v to %output {simulation.continuous_store} : !byte, !ref
      simulation.suspend.change %input to ^body : !net
    }
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %input = simulation.context.storage %ctx[1] : !ref
      %upper = simulation.context.storage %ctx[3] : !ref
      %h = simulation.logic.constant 60 : i8, 0 : i8 : !byte
      %a = simulation.logic.constant 165 : i8, 0 : i8 : !byte
      %b = simulation.logic.constant 90 : i8, 0 : i8 : !byte
      %x = simulation.logic.constant 0 : i8, 255 : i8 : !byte
      %z = simulation.logic.constant 255 : i8, 255 : i8 : !byte
      %delay = simulation.time.constant 3
      simulation.ref.store %h to %upper : !byte, !ref
      simulation.ref.store %a to %input : !byte, !ref
      simulation.suspend.delay %delay to ^known
    ^known:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %x to %input : !byte, !ref
      simulation.suspend.delay %delay to ^unknown
    ^unknown:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %z to %input : !byte, !ref
      simulation.suspend.delay %delay to ^high_z
    ^high_z:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %b to %upper : !byte, !ref
      simulation.suspend.delay %delay to ^upper_only
    ^upper_only:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %b to %input : !byte, !ref
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %output = simulation.context.storage %ctx[2] : !ref
      %v = simulation.ref.load %output : !ref -> !byte
      %fmt = simulation.bytes.constant "net %h"
      %stdout = arith.constant -2147483647 : i32
      simulation.display %ctx to %stdout(%fmt, %v) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !byte
      simulation.return
    }
  }
}
