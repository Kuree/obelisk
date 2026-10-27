// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},talos-sim-materialize-compute-fusion,talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' -o %t 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: FileCheck %s < %t
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements,talos-sim-materialize-compute-fusion,simulation.func(talos-sim-thread-suspension),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off prune-native=true},convert-talos-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=LLVM < %t.llvm.mlir
// RUN: talos-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s --check-prefix=OUTPUT

// Integer conditionals can canonicalize to a select between net handles.
// Both resolved nets and the selector are dependencies of that read. Retain
// the request -> transform -> response order, including when the net and
// output storage have the same descriptor number.
// PLAN: 1 plans (1 data-dependence order)
// CHECK: simulation.spawn @__talos_region_kernel_0_0
// CHECK-NOT: simulation.spawn @producer_and_consumer
// CHECK-NOT: simulation.spawn @transform
// CHECK: simulation.func private @__talos_region_kernel_0_0
// CHECK: simulation.driver.drive
// CHECK: arith.select {{.*}} : !simulation.net<i8>
// CHECK: simulation.net.read
// CHECK: simulation.driver.drive
// CHECK: simulation.ref.store
// A select of known net handles must become a select of loaded values in the
// hot body, without passing through the arbitrary-handle runtime ABI.
// LLVM-LABEL: llvm.func @__talos_region_kernel_0_0.__talos_eval_body_0(
// LLVM-NOT: llvm.call @talos_rt_v1_native_state_load_plane
// LLVM: llvm.select {{.*}} : i1, i8
// LLVM-NOT: llvm.call @talos_rt_v1_native_state_load_plane
// LLVM: llvm.return
// OUTPUT-NOT: FAIL
// OUTPUT: SELECTED NETS PASS
// OUTPUT-NOT: FAIL
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  schedule.native_scheduler = 3 : i32,
  native.two_state
} {
  simulation.design @selected_nets {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.net.decl 0 in 0 : i8 design
    simulation.net.decl 1 in 0 : i8 design
    simulation.net.decl 2 in 0 : i8 design
    simulation.driver.decl 0 in 0 drives 0 : i8 design
    simulation.driver.decl 1 in 0 drives 1 : i8 design
    simulation.driver.decl 2 in 0 drives 2 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 4 in 0 initial hierarchy "stimulus"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 3 : i64} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %request = simulation.context.net %ctx[1] : !simulation.net<i8>
      %alternate = simulation.context.net %ctx[2] : !simulation.net<i8>
      %b = simulation.spawn @transform(%ctx, %request, %alternate) : !simulation.context, !simulation.net<i8>, !simulation.net<i8> -> !simulation.process
      %s = simulation.spawn @stimulus(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %request = simulation.context.driver %ctx[1] : !simulation.driver<i8>
      %response = simulation.context.net %ctx[0] : !simulation.net<i8>
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.net.read %response : !simulation.net<i8> -> i8
      simulation.ref.store %r to %out : i8, !simulation.ref<i8>
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.driver.drive %request = %v : !simulation.driver<i8>, i8
      simulation.suspend.any %input, %response edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.net<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %request: !simulation.net<i8> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64}, %alternate: !simulation.net<i8> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %choose = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %response = simulation.context.driver %ctx[0] : !simulation.driver<i8>
      cf.br ^body
    ^body:
      %c = simulation.ref.load %choose : !simulation.ref<i1> -> i1
      cf.cond_br %c, ^left, ^right
    ^left:
      %a = simulation.net.read %request : !simulation.net<i8> -> i8
      cf.br ^join(%a : i8)
    ^right:
      %b = simulation.net.read %alternate : !simulation.net<i8> -> i8
      cf.br ^join(%b : i8)
    ^join(%value: i8):
      simulation.driver.drive %response = %value : !simulation.driver<i8>, i8
      simulation.suspend.any %request, %alternate, %choose edges [0, 0, 0] to ^body : !simulation.net<i8>, !simulation.net<i8>, !simulation.ref<i1>
    }
    // Exercise both select arms, a changed unselected input, and switching
    // back after that change. Positive delays let each activation settle.
    simulation.func @stimulus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %choose = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %alternate = simulation.context.driver %ctx[2] : !simulation.driver<i8>
      %a = arith.constant 17 : i8
      %b = arith.constant 42 : i8
      %next = arith.constant 99 : i8
      %true = arith.constant true
      %false = arith.constant false
      %one = simulation.time.constant 1
      simulation.driver.drive %alternate = %b : !simulation.driver<i8>, i8
      simulation.ref.store %a to %input : i8, !simulation.ref<i8>
      simulation.ref.store %true to %choose : i1, !simulation.ref<i1>
      simulation.suspend.delay %one to ^check_a
    ^check_a:
      %first = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %first_ok = arith.cmpi eq, %first, %a : i8
      cf.cond_br %first_ok, ^select_b, ^fail
    ^select_b:
      simulation.ref.store %false to %choose : i1, !simulation.ref<i1>
      simulation.suspend.delay %one to ^check_b
    ^check_b:
      %second = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %second_ok = arith.cmpi eq, %second, %b : i8
      cf.cond_br %second_ok, ^update_a, ^fail
    ^update_a:
      simulation.ref.store %next to %input : i8, !simulation.ref<i8>
      simulation.suspend.delay %one to ^check_unchanged
    ^check_unchanged:
      %third = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %third_ok = arith.cmpi eq, %third, %b : i8
      cf.cond_br %third_ok, ^select_a, ^fail
    ^select_a:
      simulation.ref.store %true to %choose : i1, !simulation.ref<i1>
      simulation.suspend.delay %one to ^check_next
    ^check_next:
      %fourth = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %fourth_ok = arith.cmpi eq, %fourth, %next : i8
      cf.cond_br %fourth_ok, ^pass, ^fail
    ^pass:
      %pass_text = simulation.bytes.constant "SELECTED NETS PASS"
      %pass_stdout = arith.constant 1 : i32
      simulation.display %ctx to %pass_stdout(%pass_text) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^fail:
      %fail_text = simulation.bytes.constant "SELECTED NETS FAIL"
      %fail_stdout = arith.constant 1 : i32
      simulation.display %ctx to %fail_stdout(%fail_text) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
