// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' -o %t.group.mlir
// RUN: FileCheck %s --check-prefix=GROUP < %t.group.mlir
// RUN: sed 's/schedule.native_scheduler = 1 : i32/schedule.native_scheduler = 0 : i32/' %s > %t.auto.mlir
// RUN: obelisk-opt %t.auto.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=GROUP
// RUN: obelisk-opt %t.group.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | obelisk-translate --mlir-to-llvmir > %t.ll
// RUN: FileCheck %s --check-prefix=NATIVE < %t.ll
// RUN: sed 's/cf.br ^wait(%carry : i32)/cf.br ^wait(%next : i32)/g' %s > %t.changing.mlir
// RUN: obelisk-opt %t.changing.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING
// RUN: sed 's/schedule.native_scheduler = 1 : i32/schedule.native_scheduler = 3 : i32/' %t.changing.mlir > %t.eval.mlir
// RUN: obelisk-opt %t.eval.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING
// RUN: sed 's/cf.br ^wait(%carry : i32)/cf.cond_br %condition, ^wait(%carry : i32), ^wait(%carry : i32)/g' %s > %t.duplicate.mlir
// RUN: obelisk-opt %t.duplicate.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=GROUP
// RUN: sed 's/cf.br ^wait(%carry : i32)/cf.cond_br %condition, ^wait(%carry : i32), ^wait(%next : i32)/g' %s > %t.conflicting.mlir
// RUN: obelisk-opt %t.conflicting.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING
// RUN: sed '/^      %next = arith.addi %carry, %one : i32/a\      simulation.ref.store %condition to %clk : i1, !clockref' %s > %t.clock-write.mlir
// RUN: obelisk-opt %t.clock-write.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING
// RUN: sed 's/simulation.design @local_group_task {/simulation.design @local_group_task attributes {schedule.static_body_fusion = [#schedule.fusion<id = 0, fragments = [2, 5]>]} {/' %t.clock-write.mlir > %t.clock-plan.mlir
// RUN: obelisk-opt %t.clock-plan.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING

// Runtime behavior is checked in ../Runtime/simulation-local-group-task-runtime.test.

// The common loop runs the task that drives the clock and owns both NBA
// barriers. The two compute actors share one compiled activation even though
// this design is not eligible for a closed generated evaluator. Entry values
// threaded through waits must be proven invariant, never assumed invariant
// merely because the module requests Eval. Changing backedge values retain
// their original actors. The bytecode oracle deliberately skips body fusion.
// The clock-write variant also supplies a candidate plan directly (the wait
// fragments are 2 and 5), checking materialization's independent rearm proof.
// GROUP: simulation.spawn @__obelisk_fused_
// GROUP-NOT: simulation.func private @a(
// GROUP-NOT: simulation.func private @b(
// GROUP: simulation.func private @driver(
// GROUP: simulation.task.call @tick
// GROUP: simulation.task.call @tick
// GROUP: simulation.func private @tick(
// GROUP: simulation.func private @consumer(
// GROUP: simulation.suspend.any
// GROUP: simulation.nba.enqueue
// GROUP: simulation.func private @__obelisk_fused_
// GROUP-SAME: schedule.native.region_body
// GROUP: simulation.suspend.edge posedge
// GROUP: simulation.nba.enqueue
// GROUP: simulation.nba.enqueue
// GROUP-NOT: simulation.suspend.edge
// NATIVE: @__obelisk_fused_{{[0-9_]+}}.__obelisk_process_descriptor = constant
// NATIVE-SAME: ptr @__obelisk_native_zero_requirements_v1
// NATIVE-SAME: ptr @obelisk_rt_v1_table_process_execute
// NATIVE-SAME: ptr @__obelisk_native_noop_destroy_v1
// NATIVE: define i32 @__obelisk_fused_{{[0-9_]+}}.__obelisk_table_body(
// NATIVE-NOT: llvm.coro
// NATIVE: ret i32
// CHANGING-NOT: __obelisk_fused_
// CHANGING: simulation.func private @a(
// CHANGING: arith.addi
// CHANGING: simulation.func private @b(
// CHANGING: arith.addi
// CHANGING-NOT: __obelisk_fused_

!ref = !simulation.ref<i32>
!clockref = !simulation.ref<i1>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @local_group_task {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.storage.decl 3 in 0 : i32 design
    simulation.storage.decl 4 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "a"
    simulation.code_unit.decl 3 in 0 always hierarchy "b"
    simulation.code_unit.decl 4 in 0 initial hierarchy "driver"
    simulation.code_unit.decl 5 in 0 task hierarchy "tick"
    simulation.code_unit.decl 6 in 0 always hierarchy "consumer"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %a = simulation.context.storage %ctx[1] : !ref
      %b = simulation.context.storage %ctx[2] : !ref
      %published = simulation.context.storage %ctx[3] : !ref
      %seen = simulation.context.storage %ctx[4] : !ref
      %zero = arith.constant 0 : i32
      %low = arith.constant false
      simulation.ref.store %low to %clk : i1, !clockref
      simulation.ref.store %zero to %a : i32, !ref
      simulation.ref.store %zero to %b : i32, !ref
      simulation.ref.store %zero to %published : i32, !ref
      simulation.ref.store %zero to %seen : i32, !ref
      %pa = simulation.spawn @a(%ctx, %clk, %a, %published) : !simulation.context, !clockref, !ref, !ref -> !simulation.process
      %pc = simulation.spawn @consumer(%ctx, %published, %seen) : !simulation.context, !ref, !ref -> !simulation.process
      %pb = simulation.spawn @b(%ctx, %clk, %b) : !simulation.context, !clockref, !ref -> !simulation.process
      %driver = simulation.spawn @driver(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %published: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %condition = arith.constant true
      %one = arith.constant 1 : i32
      cf.br ^wait(%one : i32)
    ^wait(%state: i32):
      simulation.suspend.edge posedge %clk to ^body(%state : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.ref.store %carry to %published : i32, !ref
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%carry : i32)
    }
    simulation.func private @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %condition = arith.constant true
      %two = arith.constant 2 : i32
      %one = arith.constant 1 : i32
      cf.br ^wait(%two : i32)
    ^wait(%state: i32):
      %waitvalue = arith.constant 2 : i32
      simulation.suspend.edge posedge %clk to ^body(%waitvalue : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%state : i32)
    }
    simulation.func private @driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^first
    ^first:
      simulation.task.call @tick(%ctx) arguments 1 to ^second : !simulation.context
    ^second:
      simulation.task.call @tick(%ctx) arguments 1 to ^done : !simulation.context
    ^done:
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
    simulation.func private @tick(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 5 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %a = simulation.context.storage %ctx[1] : !ref
      %b = simulation.context.storage %ctx[2] : !ref
      %seen = simulation.context.storage %ctx[4] : !ref
      %high = arith.constant true
      simulation.ref.store %high to %clk : i1, !clockref
      %av = simulation.ref.load %a : !ref -> i32
      %bv = simulation.ref.load %b : !ref -> i32
      %sv = simulation.ref.load %seen : !ref -> i32
      %before = simulation.bytes.constant "before %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%before, %av, %bv, %sv) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i32, i32, i32
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^after
    ^after:
      %aa = simulation.ref.load %a : !ref -> i32
      %ba = simulation.ref.load %b : !ref -> i32
      %sa = simulation.ref.load %seen : !ref -> i32
      %after = simulation.bytes.constant "after %0d %0d %0d"
      simulation.display %ctx to %channel(%after, %aa, %ba, %sa) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i32, i32, i32
      %low = arith.constant false
      simulation.ref.store %low to %clk : i1, !clockref
      %again = simulation.time.constant 1
      simulation.suspend.delay %again to ^done
    ^done:
      simulation.return
    }
    // The first member wakes this outside actor. Its NBA in turn wakes it
    // again, requiring another Active/NBA iteration in the same time slot.
    simulation.func private @consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %published: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      %one = arith.constant 1 : i32
      %three = arith.constant 3 : i32
      cf.br ^wait
    ^wait:
      simulation.suspend.any %published, %seen edges [0, 0] to ^body : !ref, !ref
    ^body:
      %value = simulation.ref.load %seen : !ref -> i32
      %more = arith.cmpi ult, %value, %three : i32
      cf.cond_br %more, ^update, ^wait
    ^update:
      %next = arith.addi %value, %one : i32
      simulation.nba.enqueue %next to %seen : (i32, !ref) -> ()
      cf.br ^wait
    }
  }
}
