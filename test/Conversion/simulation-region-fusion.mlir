// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' -o %t.fused
// RUN: FileCheck %s < %t.fused
// RUN: FileCheck %s --check-prefix=OWNERS < %t.fused
// RUN: obelisk-opt %t.fused --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o /dev/null
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANKED

// Retain induction loops in one source-ordered DAG. The bank transform is
// between address production and result assembly, so only the external input
// remains in the union wait. Clock/NBA sampling stays a separate activation.
// CHECK: simulation.spawn @__obelisk_region_kernel_
// CHECK: simulation.spawn @sample
// CHECK-NOT: simulation.func @producer_and_consumer
// CHECK-NOT: simulation.func @transform
// CHECK: simulation.func private @__obelisk_region_kernel_
// CHECK: simulation.context.storage %arg0[0]
// CHECK: arith.cmpi slt
// CHECK: simulation.ref.dyn_extract
// CHECK: simulation.ref.store
// CHECK: simulation.logic.binary add
// CHECK: arith.cmpi slt
// CHECK: simulation.ref.dyn_extract
// CHECK: simulation.ref.store
// CHECK: simulation.suspend.any %{{[0-9]+}} edges [0]
// CHECK-NOT: scf.
// Original ownership survives nested loop cloning and CFG lowering. A later
// scheduling query can recover process order without treating this kernel as
// one original process. The helper's aggregate inventory remains available.
// OWNERS: simulation.func private @__obelisk_region_kernel_
// OWNERS-SAME: schedule.eval.source_owners = [#schedule.source_owner<codeUnit = 1 : i64, continuation = [[A:[0-9]+]] : i32>, #schedule.source_owner<codeUnit = 2 : i64, continuation = [[B:[0-9]+]] : i32>]
// OWNERS: simulation.ref.store {{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 1 : i64, continuation = [[A]] : i32>}
// OWNERS: simulation.ref.store {{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 2 : i64, continuation = [[B]] : i32>}
// OWNERS: simulation.ref.store {{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 1 : i64, continuation = [[A]] : i32>}

// Bounded dynamic accesses and internal blocking reads do not turn a proved
// finite activation DAG into feedback. The source proof survives native
// lowering in the preserved pipeline analysis and the same graph inventory.
// RANKED: obelisk ranked activation: __obelisk_region_kernel_0_0 valid=1 complete=1
// RANKED: obelisk ranked groups: candidates=2 groups=2 segments=0 members=2 clocked_members=1
// RANKED: obelisk eval dependency: owner=0 island=0 body=__obelisk_region_kernel_0_0.__obelisk_eval_body_0 successors=
// RANKED: obelisk eval dependency: owner=1 island=1 body=sample.__obelisk_eval_body_0 successors=

// Bounded address-copy and result-assembly loops, with a clocked NBA observer.
// The result initializer and lane stores retain their original transitions.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32,
  debug.native_timing
} {
  simulation.design @address_then_result {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design
    simulation.storage.decl 5 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always hierarchy "sample"
    simulation.code_unit.decl 4 in 0 initial hierarchy "driver"
    simulation.code_unit.decl 5 in 0 always hierarchy "clock"
    simulation.code_unit.decl 6 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 6 : i64} {
      %clock = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %zero to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %c = simulation.spawn @clock(%ctx) : !simulation.context -> !simulation.process
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %s = simulation.spawn @sample(%ctx, %clock) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %d = simulation.spawn @driver(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 2 : i32
      %four = arith.constant 4 : i32
      %seed = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      cf.br ^copy(%zero : i32)
    ^resume:
      %reset = arith.constant {simulation.rematerialized} 0 : i32
      cf.br ^copy(%reset : i32)
    ^copy(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^copy_body, ^initialize
    ^copy_body:
      %low = arith.muli %i, %four : i32
      %src = simulation.ref.dyn_extract %address from %low : (!simulation.ref<!simulation.logic<8>>, i32) -> !simulation.ref<!simulation.logic<4>>
      %dst = simulation.ref.dyn_extract %bank_address from %low : (!simulation.ref<!simulation.logic<8>>, i32) -> !simulation.ref<!simulation.logic<4>>
      %part = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %part to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %next = arith.addi %i, %one : i32
      cf.br ^copy(%next : i32)
    ^initialize:
      simulation.ref.store %seed to %result : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^assemble(%zero : i32)
    ^assemble(%j: i32):
      %again = arith.cmpi slt, %j, %limit : i32
      cf.cond_br %again, ^assemble_body, ^wait
    ^assemble_body:
      %offset = arith.muli %j, %four : i32
      %data = simulation.ref.dyn_extract %bank_result from %offset : (!simulation.ref<!simulation.logic<8>>, i32) -> !simulation.ref<!simulation.logic<4>>
      %out = simulation.ref.dyn_extract %result from %offset : (!simulation.ref<!simulation.logic<8>>, i32) -> !simulation.ref<!simulation.logic<4>>
      %value = simulation.ref.load %data : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %value to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %following = arith.addi %j, %one : i32
      cf.br ^assemble(%following : i32)
    ^wait:
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^resume : !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %r = simulation.logic.binary add %a, %one : !simulation.logic<8>
      simulation.ref.store %r to %result : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %address to ^body : !simulation.ref<!simulation.logic<8>>
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %result = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %sample = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<8>>
      %r = simulation.ref.load %result : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.nba.enqueue %r to %sample : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      %clock = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %delay = simulation.time.constant 1
      cf.br ^wait
    ^wait:
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %sample = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<8>>
      %fmt = simulation.bytes.constant "result %h sample %h"
      %channel = arith.constant 1 : i32
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^step0
    ^step0:
      %read0 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample0 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read0, %sample0) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value0 = simulation.logic.constant 7 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value0 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step1
    ^step1:
      %read1 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample1 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read1, %sample1) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value1 = simulation.logic.constant 0 : i8, 255 : i8 : !simulation.logic<8>
      simulation.ref.store %value1 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step2
    ^step2:
      %read2 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample2 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read2, %sample2) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value2 = simulation.logic.constant 255 : i8, 255 : i8 : !simulation.logic<8>
      simulation.ref.store %value2 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step3
    ^step3:
      %read3 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample3 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read3, %sample3) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value3 = simulation.logic.constant 255 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value3 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step4
    ^step4:
      %read4 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample4 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read4, %sample4) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value4 = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value4 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step5
    ^step5:
      %read5 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample5 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read5, %sample5) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
