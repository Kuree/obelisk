// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' -o %t.group.mlir
// RUN: FileCheck %s --check-prefix=GROUP < %t.group.mlir
// RUN: FileCheck %s --check-prefix=NO-SNAPSHOTS < %t.group.mlir
// RUN: obelisk-opt %t.group.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: FileCheck %s --check-prefix=NATIVE < %t.ll
// RUN: sed 's/entry_kind = 4 : i32/entry_kind = 6 : i32/g; s/always_comb/always_latch/g' %s > %t.latch.mlir
// RUN: obelisk-opt %t.latch.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: sed '/^      simulation.ref.store %next to %output : !word, !ref$/p' %s > %t.repeated.mlir
// RUN: obelisk-opt %t.repeated.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: sed 's/%raw = simulation.ref.load %input/%raw = simulation.ref.load %output/' %s > %t.retained.mlir
// RUN: obelisk-opt %t.retained.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: sed '/simulation.func @first(/s/descriptor_id = 0/descriptor_id = 2/; s/@first(%ctx, %input, %middle)/@first(%ctx, %output, %middle)/' %s > %t.feedback.mlir
// RUN: obelisk-opt %t.feedback.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: sed '/%bufferDest = simulation.context.storage/s/\[0\]/[1]/' %s > %t.task-writer.mlir
// RUN: obelisk-opt %t.task-writer.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE

// Runtime behavior is checked in ../Runtime/simulation-local-ranked-task.test.

// Conditional computation and a reconvergent chain run in one ranked native
// activation while a task supplies stimulus. Publications and timed task work
// remain owned by the shared loop. Initial X, known, X and known transitions
// must all reach the output. No test relies on cross-process race ordering.
// GROUP: simulation.spawn @__obelisk_region_kernel_
// GROUP-NOT: simulation.func @first(
// GROUP-NOT: simulation.func @second(
// GROUP: simulation.func private @__obelisk_region_kernel_
// GROUP-SAME: entry_kind = 4 : i32
// GROUP-SAME: schedule.native.region_body
// GROUP-DAG: cf.cond_br
// GROUP-DAG: simulation.suspend.any
// NO-SNAPSHOTS: simulation.func private @__obelisk_region_kernel_
// NO-SNAPSHOTS-NOT: simulation.logic.compare case_ne
// NO-SNAPSHOTS-NOT: arith.andi
// SEPARATE-NOT: __obelisk_region_kernel_
// SEPARATE: simulation.func @first(
// SEPARATE: simulation.func @second(
// SEPARATE-NOT: __obelisk_region_kernel_
// NATIVE: define void @driver.__obelisk_group_body(
// NATIVE-NOT: llvm.coro
// NATIVE: call i64 @stimulus.__obelisk_activate
// NATIVE-NOT: llvm.coro
// NATIVE: define i32 @driver.__obelisk_native_execute
// NATIVE: define i32 @__obelisk_region_kernel_{{[0-9_]+}}.__obelisk_table_body(
// NATIVE-NOT: llvm.coro
// NATIVE: ret i32

!word = !simulation.logic<8>
!ref = !simulation.ref<!word>
!record = !simulation.packed_struct<[
  #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 0, packedOffset = 4>,
  #simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 1, packedOffset = 0>
]>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @local_ranked {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "first"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "second"
    simulation.code_unit.decl 4 in 0 initial hierarchy "driver"
    simulation.code_unit.decl 5 in 0 task hierarchy "stimulus"
    simulation.code_unit.decl 6 in 0 task hierarchy "initialize"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %middle = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !ref
      %a = simulation.spawn @first(%ctx, %input, %middle) : !simulation.context, !ref, !ref -> !simulation.process
      %b = simulation.spawn @second(%ctx, %input, %middle, %output) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %d = simulation.spawn @driver(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %raw = simulation.ref.load %input : !ref -> !word
      %record = simulation.packed.unflatten %raw : (!word) -> !record
      %value = simulation.packed.flatten %record : (!record) -> !word
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !word
      %knownzero = simulation.logic.compare case_eq %value, %zero : (!word, !word) -> i1
      cf.cond_br %knownzero, ^zero, ^other
    ^zero:
      %special = simulation.logic.constant 7 : i8, 0 : i8 : !word
      cf.br ^publish(%special : !word)
    ^other:
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %incremented = simulation.logic.binary add %value, %one : !word
      cf.br ^publish(%incremented : !word)
    ^publish(%next: !word):
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %middle: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %a = simulation.ref.load %input : !ref -> !word
      %b = simulation.ref.load %middle : !ref -> !word
      %next = simulation.logic.binary add %a, %b : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.any %input, %middle edges [0, 0] to ^body : !ref, !ref
    }
    simulation.func @driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %now = simulation.time.now %ctx
      %base = arith.constant 41 : i64
      %seed = arith.addi %now, %base : i64
      %bufferDest = simulation.context.storage %ctx[0] : !ref
      simulation.task.call @stimulus(%ctx, %bufferDest, %seed) arguments 2 to ^done : !simulation.context, !ref, i64
    ^done(%saved: i64):
      %one = arith.constant 1 : i64
      %next = arith.addi %saved, %one : i64
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "caller %d"
      simulation.display %ctx to %stdout(%format, %next) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i64
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
    simulation.func @initialize(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %buffer: !ref {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 6 : i64} {
      %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !word
      simulation.ref.store %unknown to %buffer : !word, !ref
      simulation.return
    }
    simulation.func @stimulus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %buffer: !ref {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 5 : i64} {
      simulation.task.call @initialize(%ctx, %buffer) arguments 2 to ^start : !simulation.context, !ref
    ^start:
      %startupDelay = simulation.time.constant 1
      simulation.suspend.delay %startupDelay to ^initial
    ^initial:
      %input = simulation.context.storage %ctx[0] : !ref
      %output = simulation.context.storage %ctx[2] : !ref
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "result %h"
      %initial = simulation.ref.load %output : !ref -> !word
      simulation.display %ctx to %stdout(%format, %initial) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      simulation.ref.store %one to %input : !word, !ref
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^known
    ^known:
      %inputK = simulation.context.storage %ctx[0] : !ref
      %outputK = simulation.context.storage %ctx[2] : !ref
      %stdoutK = arith.constant -2147483647 : i32
      %formatK = simulation.bytes.constant "result %h"
      %valueK = simulation.ref.load %outputK : !ref -> !word
      simulation.display %ctx to %stdoutK(%formatK, %valueK) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !word
      %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !word
      simulation.ref.store %unknown to %inputK : !word, !ref
      %delayK = simulation.time.constant 1
      simulation.suspend.delay %delayK to ^unknown
    ^unknown:
      %inputU = simulation.context.storage %ctx[0] : !ref
      %outputU = simulation.context.storage %ctx[2] : !ref
      %stdoutU = arith.constant -2147483647 : i32
      %formatU = simulation.bytes.constant "result %h"
      %valueU = simulation.ref.load %outputU : !ref -> !word
      simulation.display %ctx to %stdoutU(%formatU, %valueU) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !word
      %two = simulation.logic.constant 2 : i8, 0 : i8 : !word
      simulation.ref.store %two to %inputU : !word, !ref
      %delayU = simulation.time.constant 1
      simulation.suspend.delay %delayU to ^final
    ^final:
      %outputF = simulation.context.storage %ctx[2] : !ref
      %stdoutF = arith.constant -2147483647 : i32
      %formatF = simulation.bytes.constant "result %h"
      %valueF = simulation.ref.load %outputF : !ref -> !word
      simulation.display %ctx to %stdoutF(%formatF, %valueF) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !word
      simulation.return
    }
  }
}
