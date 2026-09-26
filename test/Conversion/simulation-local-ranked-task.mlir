// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' -o %t.group.mlir
// RUN: FileCheck %s --check-prefix=GROUP < %t.group.mlir
// RUN: FileCheck %s --check-prefix=NO-SNAPSHOTS < %t.group.mlir
// RUN: obelisk-opt %t.group.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: FileCheck %s --check-prefix=NATIVE < %t.ll
// RUN: sed 's/entry_kind = 4 : i32/entry_kind = 6 : i32/g; s/always_comb/always_latch/g' %s > %t.latch.mlir
// RUN: obelisk-opt %t.latch.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: sed '/^      obelisk_sim.ref.store %next to %output : !word, !ref$/p' %s > %t.repeated.mlir
// RUN: obelisk-opt %t.repeated.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: sed 's/%raw = obelisk_sim.ref.load %input/%raw = obelisk_sim.ref.load %output/' %s > %t.retained.mlir
// RUN: obelisk-opt %t.retained.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: sed '/obelisk_sim.func @first(/s/descriptor_id = 0/descriptor_id = 2/; s/@first(%ctx, %input, %middle)/@first(%ctx, %output, %middle)/' %s > %t.feedback.mlir
// RUN: obelisk-opt %t.feedback.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE
// RUN: sed '/%bufferDest = obelisk_sim.context.storage/s/\[0\]/[1]/' %s > %t.task-writer.mlir
// RUN: obelisk-opt %t.task-writer.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SEPARATE

// Runtime behavior is checked in ../Runtime/simulation-local-ranked-task.test.

// Conditional computation and a reconvergent chain run in one ranked native
// activation while a task supplies stimulus. Publications and timed task work
// remain owned by the shared loop. Initial X, known, X and known transitions
// must all reach the output. No test relies on cross-process race ordering.
// GROUP: obelisk_sim.spawn @__obelisk_region_kernel_
// GROUP-NOT: obelisk_sim.func @first(
// GROUP-NOT: obelisk_sim.func @second(
// GROUP: obelisk_sim.func private @__obelisk_region_kernel_
// GROUP-SAME: entry_kind = 4 : i32
// GROUP-SAME: obelisk.native.region_body
// GROUP-DAG: cf.cond_br
// GROUP-DAG: obelisk_sim.suspend.any
// NO-SNAPSHOTS: obelisk_sim.func private @__obelisk_region_kernel_
// NO-SNAPSHOTS-NOT: obelisk_sim.logic.compare case_ne
// NO-SNAPSHOTS-NOT: arith.andi
// SEPARATE-NOT: __obelisk_region_kernel_
// SEPARATE: obelisk_sim.func @first(
// SEPARATE: obelisk_sim.func @second(
// SEPARATE-NOT: __obelisk_region_kernel_
// NATIVE: define void @driver.__obelisk_group_body(
// NATIVE-NOT: llvm.coro
// NATIVE: call i64 @stimulus.__obelisk_activate
// NATIVE-NOT: llvm.coro
// NATIVE: define i32 @driver.__obelisk_native_requirements
// NATIVE: define void @__obelisk_region_kernel_{{[0-9_]+}}.__obelisk_group_body(
// NATIVE-NOT: llvm.coro
// NATIVE: define i32 @__obelisk_region_kernel_{{[0-9_]+}}.__obelisk_native_requirements

!word = !obelisk_sim.logic<8>
!ref = !obelisk_sim.ref<!word>
!record = !obelisk_sim.packed_struct<[
  #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 4>,
  #obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 0>
]>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 1 : i32
} {
  obelisk_sim.design @local_ranked {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !word design
    obelisk_sim.storage.decl 1 in 0 : !word design
    obelisk_sim.storage.decl 2 in 0 : !word design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always_comb hierarchy "first"
    obelisk_sim.code_unit.decl 3 in 0 always_comb hierarchy "second"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "driver"
    obelisk_sim.code_unit.decl 5 in 0 task hierarchy "stimulus"
    obelisk_sim.code_unit.decl 6 in 0 task hierarchy "initialize"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = obelisk_sim.context.storage %ctx[0] : !ref
      %middle = obelisk_sim.context.storage %ctx[1] : !ref
      %output = obelisk_sim.context.storage %ctx[2] : !ref
      %a = obelisk_sim.spawn @first(%ctx, %input, %middle) : !obelisk_sim.context, !ref, !ref -> !obelisk_sim.process
      %b = obelisk_sim.spawn @second(%ctx, %input, %middle, %output) : !obelisk_sim.context, !ref, !ref, !ref -> !obelisk_sim.process
      %d = obelisk_sim.spawn @driver(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @first(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %output: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %raw = obelisk_sim.ref.load %input : !ref -> !word
      %record = obelisk_sim.packed.unflatten %raw : (!word) -> !record
      %value = obelisk_sim.packed.flatten %record : (!record) -> !word
      %zero = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !word
      %knownzero = obelisk_sim.logic.compare case_eq %value, %zero : (!word, !word) -> i1
      cf.cond_br %knownzero, ^zero, ^other
    ^zero:
      %special = obelisk_sim.logic.constant 7 : i8, 0 : i8 : !word
      cf.br ^publish(%special : !word)
    ^other:
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !word
      %incremented = obelisk_sim.logic.binary add %value, %one : !word
      cf.br ^publish(%incremented : !word)
    ^publish(%next: !word):
      obelisk_sim.ref.store %next to %output : !word, !ref
      obelisk_sim.suspend.change %input to ^body : !ref
    }
    obelisk_sim.func @second(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %middle: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %output: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %a = obelisk_sim.ref.load %input : !ref -> !word
      %b = obelisk_sim.ref.load %middle : !ref -> !word
      %next = obelisk_sim.logic.binary add %a, %b : !word
      obelisk_sim.ref.store %next to %output : !word, !ref
      obelisk_sim.suspend.any %input, %middle edges [0, 0] to ^body : !ref, !ref
    }
    obelisk_sim.func @driver(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %now = obelisk_sim.time.now %ctx
      %base = arith.constant 41 : i64
      %seed = arith.addi %now, %base : i64
      %bufferDest = obelisk_sim.context.storage %ctx[0] : !ref
      obelisk_sim.task.call @stimulus(%ctx, %bufferDest, %seed) arguments 2 to ^done : !obelisk_sim.context, !ref, i64
    ^done(%saved: i64):
      %one = arith.constant 1 : i64
      %next = arith.addi %saved, %one : i64
      %stdout = arith.constant -2147483647 : i32
      %format = obelisk_sim.bytes.constant "caller %d"
      obelisk_sim.display %ctx to %stdout(%format, %next) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, i64
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }
    obelisk_sim.func @initialize(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %buffer: !ref {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 6 : i64} {
      %unknown = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !word
      obelisk_sim.ref.store %unknown to %buffer : !word, !ref
      obelisk_sim.return
    }
    obelisk_sim.func @stimulus(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %buffer: !ref {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 5 : i64} {
      obelisk_sim.task.call @initialize(%ctx, %buffer) arguments 2 to ^start : !obelisk_sim.context, !ref
    ^start:
      %startupDelay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %startupDelay to ^initial
    ^initial:
      %input = obelisk_sim.context.storage %ctx[0] : !ref
      %output = obelisk_sim.context.storage %ctx[2] : !ref
      %stdout = arith.constant -2147483647 : i32
      %format = obelisk_sim.bytes.constant "result %h"
      %initial = obelisk_sim.ref.load %output : !ref -> !word
      obelisk_sim.display %ctx to %stdout(%format, %initial) newline = true radix = 16 flags = [0, 0] : !obelisk_sim.bytes, !word
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !word
      obelisk_sim.ref.store %one to %input : !word, !ref
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^known
    ^known:
      %inputK = obelisk_sim.context.storage %ctx[0] : !ref
      %outputK = obelisk_sim.context.storage %ctx[2] : !ref
      %stdoutK = arith.constant -2147483647 : i32
      %formatK = obelisk_sim.bytes.constant "result %h"
      %valueK = obelisk_sim.ref.load %outputK : !ref -> !word
      obelisk_sim.display %ctx to %stdoutK(%formatK, %valueK) newline = true radix = 16 flags = [0, 0] : !obelisk_sim.bytes, !word
      %unknown = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !word
      obelisk_sim.ref.store %unknown to %inputK : !word, !ref
      %delayK = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delayK to ^unknown
    ^unknown:
      %inputU = obelisk_sim.context.storage %ctx[0] : !ref
      %outputU = obelisk_sim.context.storage %ctx[2] : !ref
      %stdoutU = arith.constant -2147483647 : i32
      %formatU = obelisk_sim.bytes.constant "result %h"
      %valueU = obelisk_sim.ref.load %outputU : !ref -> !word
      obelisk_sim.display %ctx to %stdoutU(%formatU, %valueU) newline = true radix = 16 flags = [0, 0] : !obelisk_sim.bytes, !word
      %two = obelisk_sim.logic.constant 2 : i8, 0 : i8 : !word
      obelisk_sim.ref.store %two to %inputU : !word, !ref
      %delayU = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delayU to ^final
    ^final:
      %outputF = obelisk_sim.context.storage %ctx[2] : !ref
      %stdoutF = arith.constant -2147483647 : i32
      %formatF = obelisk_sim.bytes.constant "result %h"
      %valueF = obelisk_sim.ref.load %outputF : !ref -> !word
      obelisk_sim.display %ctx to %stdoutF(%formatF, %valueF) newline = true radix = 16 flags = [0, 0] : !obelisk_sim.bytes, !word
      obelisk_sim.return
    }
  }
}
