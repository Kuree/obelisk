// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' -o %t.group.mlir
// RUN: FileCheck %s --check-prefix=GROUP < %t.group.mlir
// RUN: sed 's/schedule.native_scheduler = 1 : i32/schedule.native_scheduler = 0 : i32/' %s > %t.auto.mlir
// RUN: obelisk-opt %t.auto.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=GROUP
// RUN: obelisk-opt %t.group.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | obelisk-translate --mlir-to-llvmir > %t.ll
// RUN: FileCheck %s --check-prefix=NATIVE < %t.ll
// RUN: sed 's/cf.br ^wait(%carry : i32)/cf.br ^wait(%next : i32)/g' %s > %t.changing.mlir
// RUN: obelisk-opt %t.changing.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING
// RUN: sed 's/schedule.native_scheduler = 1 : i32/schedule.native_scheduler = 3 : i32/' %t.changing.mlir > %t.eval.mlir
// RUN: obelisk-opt %t.eval.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING
// RUN: sed 's/cf.br ^wait(%carry : i32)/cf.cond_br %condition, ^wait(%carry : i32), ^wait(%carry : i32)/g' %s > %t.duplicate.mlir
// RUN: obelisk-opt %t.duplicate.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=GROUP
// RUN: sed 's/cf.br ^wait(%carry : i32)/cf.cond_br %condition, ^wait(%carry : i32), ^wait(%next : i32)/g' %s > %t.conflicting.mlir
// RUN: obelisk-opt %t.conflicting.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING
// RUN: sed '/^      %next = arith.addi %carry, %one : i32/a\      obelisk_sim.ref.store %condition to %clk : i1, !clockref' %s > %t.clock-write.mlir
// RUN: obelisk-opt %t.clock-write.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING
// RUN: sed 's/obelisk_sim.design @local_group_task {/obelisk_sim.design @local_group_task attributes {schedule.static_body_fusion = [#schedule.fusion<id = 0, fragments = [2, 5]>]} {/' %t.clock-write.mlir > %t.clock-plan.mlir
// RUN: obelisk-opt %t.clock-plan.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHANGING

// Runtime behavior is checked in ../Runtime/simulation-local-group-task-runtime.test.

// The common loop runs the task that drives the clock and owns both NBA
// barriers. The two compute actors share one compiled activation even though
// this design is not eligible for a closed generated evaluator. Entry values
// threaded through waits must be proven invariant, never assumed invariant
// merely because the module requests Eval. Changing backedge values retain
// their original actors. The bytecode oracle deliberately skips body fusion.
// The clock-write variant also supplies a candidate plan directly (the wait
// fragments are 2 and 5), checking materialization's independent rearm proof.
// GROUP: obelisk_sim.spawn @__obelisk_fused_
// GROUP-NOT: obelisk_sim.func private @a(
// GROUP-NOT: obelisk_sim.func private @b(
// GROUP: obelisk_sim.func private @driver(
// GROUP: obelisk_sim.task.call @tick
// GROUP: obelisk_sim.task.call @tick
// GROUP: obelisk_sim.func private @tick(
// GROUP: obelisk_sim.func private @consumer(
// GROUP: obelisk_sim.suspend.any
// GROUP: obelisk_sim.nba.enqueue
// GROUP: obelisk_sim.func private @__obelisk_fused_
// GROUP-SAME: schedule.native.region_body
// GROUP: obelisk_sim.suspend.edge posedge
// GROUP: obelisk_sim.nba.enqueue
// GROUP: obelisk_sim.nba.enqueue
// GROUP-NOT: obelisk_sim.suspend.edge
// NATIVE: @__obelisk_fused_{{[0-9_]+}}.__obelisk_process_descriptor = constant
// NATIVE-SAME: ptr @__obelisk_fused_{{[0-9_]+}}.__obelisk_native_execute
// NATIVE: define void @__obelisk_fused_{{[0-9_]+}}.__obelisk_group_body(
// NATIVE-NOT: llvm.coro
// NATIVE: define i32 @__obelisk_fused_{{[0-9_]+}}.__obelisk_native_requirements(
// NATIVE-NOT: llvm.coro
// NATIVE: define i32 @__obelisk_fused_{{[0-9_]+}}.__obelisk_native_execute(
// NATIVE-NOT: llvm.coro
// NATIVE: call void @__obelisk_fused_{{[0-9_]+}}.__obelisk_group_body(
// NATIVE-NOT: llvm.coro
// NATIVE: define void @__obelisk_fused_{{[0-9_]+}}.__obelisk_native_destroy(
// NATIVE-NEXT: ret void
// CHANGING-NOT: __obelisk_fused_
// CHANGING: obelisk_sim.func private @a(
// CHANGING: arith.addi
// CHANGING: obelisk_sim.func private @b(
// CHANGING: arith.addi
// CHANGING-NOT: __obelisk_fused_

!ref = !obelisk_sim.ref<i32>
!clockref = !obelisk_sim.ref<i1>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  obelisk_sim.design @local_group_task {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i32 design
    obelisk_sim.storage.decl 2 in 0 : i32 design
    obelisk_sim.storage.decl 3 in 0 : i32 design
    obelisk_sim.storage.decl 4 in 0 : i32 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "a"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "b"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "driver"
    obelisk_sim.code_unit.decl 5 in 0 task hierarchy "tick"
    obelisk_sim.code_unit.decl 6 in 0 always hierarchy "consumer"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !clockref
      %a = obelisk_sim.context.storage %ctx[1] : !ref
      %b = obelisk_sim.context.storage %ctx[2] : !ref
      %published = obelisk_sim.context.storage %ctx[3] : !ref
      %seen = obelisk_sim.context.storage %ctx[4] : !ref
      %zero = arith.constant 0 : i32
      %low = arith.constant false
      obelisk_sim.ref.store %low to %clk : i1, !clockref
      obelisk_sim.ref.store %zero to %a : i32, !ref
      obelisk_sim.ref.store %zero to %b : i32, !ref
      obelisk_sim.ref.store %zero to %published : i32, !ref
      obelisk_sim.ref.store %zero to %seen : i32, !ref
      %pa = obelisk_sim.spawn @a(%ctx, %clk, %a, %published) : !obelisk_sim.context, !clockref, !ref, !ref -> !obelisk_sim.process
      %pc = obelisk_sim.spawn @consumer(%ctx, %published, %seen) : !obelisk_sim.context, !ref, !ref -> !obelisk_sim.process
      %pb = obelisk_sim.spawn @b(%ctx, %clk, %b) : !obelisk_sim.context, !clockref, !ref -> !obelisk_sim.process
      %driver = obelisk_sim.spawn @driver(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @a(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clockref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %out: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %published: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %condition = arith.constant true
      %one = arith.constant 1 : i32
      cf.br ^wait(%one : i32)
    ^wait(%state: i32):
      obelisk_sim.suspend.edge posedge %clk to ^body(%state : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      obelisk_sim.ref.store %carry to %published : i32, !ref
      obelisk_sim.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%carry : i32)
    }
    obelisk_sim.func private @b(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clockref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %out: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %condition = arith.constant true
      %two = arith.constant 2 : i32
      %one = arith.constant 1 : i32
      cf.br ^wait(%two : i32)
    ^wait(%state: i32):
      %waitvalue = arith.constant 2 : i32
      obelisk_sim.suspend.edge posedge %clk to ^body(%waitvalue : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      obelisk_sim.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%state : i32)
    }
    obelisk_sim.func private @driver(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^first
    ^first:
      obelisk_sim.task.call @tick(%ctx) arguments 1 to ^second : !obelisk_sim.context
    ^second:
      obelisk_sim.task.call @tick(%ctx) arguments 1 to ^done : !obelisk_sim.context
    ^done:
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }
    obelisk_sim.func private @tick(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 5 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !clockref
      %a = obelisk_sim.context.storage %ctx[1] : !ref
      %b = obelisk_sim.context.storage %ctx[2] : !ref
      %seen = obelisk_sim.context.storage %ctx[4] : !ref
      %high = arith.constant true
      obelisk_sim.ref.store %high to %clk : i1, !clockref
      %av = obelisk_sim.ref.load %a : !ref -> i32
      %bv = obelisk_sim.ref.load %b : !ref -> i32
      %sv = obelisk_sim.ref.load %seen : !ref -> i32
      %before = obelisk_sim.bytes.constant "before %0d %0d %0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%before, %av, %bv, %sv) newline = true radix = 10 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, i32, i32, i32
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^after
    ^after:
      %aa = obelisk_sim.ref.load %a : !ref -> i32
      %ba = obelisk_sim.ref.load %b : !ref -> i32
      %sa = obelisk_sim.ref.load %seen : !ref -> i32
      %after = obelisk_sim.bytes.constant "after %0d %0d %0d"
      obelisk_sim.display %ctx to %channel(%after, %aa, %ba, %sa) newline = true radix = 10 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, i32, i32, i32
      %low = arith.constant false
      obelisk_sim.ref.store %low to %clk : i1, !clockref
      %again = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %again to ^done
    ^done:
      obelisk_sim.return
    }
    // The first member wakes this outside actor. Its NBA in turn wakes it
    // again, requiring another Active/NBA iteration in the same time slot.
    obelisk_sim.func private @consumer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %published: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %seen: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      %one = arith.constant 1 : i32
      %three = arith.constant 3 : i32
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.any %published, %seen edges [0, 0] to ^body : !ref, !ref
    ^body:
      %value = obelisk_sim.ref.load %seen : !ref -> i32
      %more = arith.cmpi ult, %value, %three : i32
      cf.cond_br %more, ^update, ^wait
    ^update:
      %next = arith.addi %value, %one : i32
      obelisk_sim.nba.enqueue %next to %seen : (i32, !ref) -> ()
      cf.br ^wait
    }
  }
}
