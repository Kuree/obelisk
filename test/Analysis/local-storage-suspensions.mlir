// RUN: talos-opt %s --test-talos-local-storage=allocations=1 -o /dev/null 2>&1 | FileCheck %s
// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LLVM

// Storage dead at every suspension stays private to one invocation even in a
// suspending process (LRM 6.21). A reference live across a suspension keeps
// the runtime lifetime, which survives the suspension and a change of
// execution tier at its continuation.

!word = !simulation.packed_array<31 : 0 x i1>
!words = !simulation.unpacked_array<0 : 3 x !word>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @suspensions {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "activation_snapshot"
    simulation.code_unit.decl 2 in 0 initial hierarchy "live_across_delay"
    simulation.code_unit.decl 3 in 0 initial hierarchy "loop_carried"
    simulation.code_unit.decl 4 in 0 initial hierarchy "forwarded"
    simulation.code_unit.decl 5 in 0 initial hierarchy "entry_dynamic_index"

    // CHECK-LABEL: function activation_snapshot
    // CHECK-NEXT: local allocation
    // LLVM-LABEL: llvm.func @activation_snapshot.__talos_coro_ramp
    // LLVM-NOT: talos_rt_v1_native_state_alloc
    // LLVM: llvm.alloca %{{.*}} x !llvm.struct<(array<4 x i32>, i8)>
    // LLVM-NOT: talos_rt_v1_native_state_alloc
    // LLVM-LABEL: llvm.func @activation_snapshot.__talos_native_requirements
    simulation.func @activation_snapshot(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 1 : i32},
        %state: !simulation.ref<!words> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body : !simulation.ref<i1>
    ^body:
      %old = simulation.ref.load %state : !simulation.ref<!words> -> !words
      %copy = simulation.ref.alloc %old : !words -> !simulation.ref<!words>
      %source = simulation.ref.subelement %copy[[1]] : !simulation.ref<!words> -> !simulation.ref<!word>
      %value = simulation.ref.load %source : !simulation.ref<!word> -> !word
      %target = simulation.ref.subelement %state[[0]] : !simulation.ref<!words> -> !simulation.ref<!word>
      simulation.ref.store %value to %target : !word, !simulation.ref<!word>
      cf.br ^wait
    }

    // An element address with a runtime index computed before the first
    // suspension stays in the source entry block; the ramp moves only the
    // allocation, which then dominates resumes as well.
    // CHECK-LABEL: function entry_dynamic_index
    // CHECK-NEXT: local allocation
    // LLVM-LABEL: llvm.func @entry_dynamic_index.__talos_coro_ramp
    // LLVM-NOT: talos_rt_v1_native_state_alloc
    // LLVM: llvm.alloca %{{.*}} x !llvm.struct<(array<4 x i32>, i8)>
    // LLVM-NOT: talos_rt_v1_native_state_alloc
    // LLVM-LABEL: llvm.func @entry_dynamic_index.__talos_native_requirements
    simulation.func @entry_dynamic_index(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %slot: !simulation.ref<i32> {simulation.capture_kind = 1 : i32},
        %state: !simulation.ref<!words> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %old = simulation.ref.load %state : !simulation.ref<!words> -> !words
      %copy = simulation.ref.alloc %old : !words -> !simulation.ref<!words>
      %raw = simulation.ref.load %slot : !simulation.ref<i32> -> i32
      %index = arith.extsi %raw : i32 to i65
      %source = simulation.ref.array_element %copy[%index] : (!simulation.ref<!words>, i65) -> !simulation.ref<!word>
      %value = simulation.ref.load %source : !simulation.ref<!word> -> !word
      %target = simulation.ref.subelement %state[[0]] : !simulation.ref<!words> -> !simulation.ref<!word>
      simulation.ref.store %value to %target : !word, !simulation.ref<!word>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }

    // CHECK-LABEL: function live_across_delay
    // CHECK-NEXT: runtime
    // LLVM-LABEL: llvm.func @live_across_delay.__talos_coro_ramp
    // LLVM: llvm.call @talos_rt_v1_native_state_alloc
    simulation.func @live_across_delay(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %local = simulation.ref.alloc %capture : i64 -> !simulation.ref<i64>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resumed
    ^resumed:
      %loaded = simulation.ref.load %local : !simulation.ref<i64> -> i64
      simulation.ref.store %loaded to %local : i64, !simulation.ref<i64>
      simulation.return
    }

    // CHECK-LABEL: function loop_carried
    // CHECK-NEXT: runtime
    simulation.func @loop_carried(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %local = simulation.ref.alloc %capture : i64 -> !simulation.ref<i64>
      %delay = simulation.time.constant 1
      cf.br ^wait
    ^wait:
      simulation.suspend.delay %delay to ^body
    ^body:
      simulation.ref.store %capture to %local : i64, !simulation.ref<i64>
      cf.br ^wait
    }

    // CHECK-LABEL: function forwarded
    // CHECK-NEXT: runtime
    simulation.func @forwarded(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %local = simulation.ref.alloc %capture : i64 -> !simulation.ref<i64>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resumed(%local : !simulation.ref<i64>)
    ^resumed(%carried: !simulation.ref<i64>):
      simulation.ref.store %capture to %carried : i64, !simulation.ref<i64>
      simulation.return
    }
  }
}
