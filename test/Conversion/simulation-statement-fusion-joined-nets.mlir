// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=LLVM < %t.llvm.mlir
// RUN: obelisk-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s --check-prefix=OUTPUT

// Three conditional net reads and a computed fourth arm canonicalize to a
// three-predecessor net-handle phi. The native load must accept every proved
// static alternative, retaining the actual CFG choice and read ordering.
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-LABEL: simulation.func private @__obelisk_region_kernel_0_0
// CHECK: ^bb{{[0-9]+}}(%[[NET:.*]]: !simulation.net<i8>):
// CHECK-NEXT: %{{.*}} = simulation.net.read %[[NET]] {{.*}} : !simulation.net<i8> -> i8
// LLVM-LABEL: llvm.func @__obelisk_region_kernel_0_0.__obelisk_eval_body_0(
// LLVM-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// LLVM: llvm.return
// OUTPUT-NOT: FAIL
// OUTPUT: JOINED NETS PASS
// OUTPUT-NOT: FAIL
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  schedule.native_scheduler = 3 : i32,
  native.two_state
} {
  simulation.design @joined_nets {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i2 design
    simulation.net.decl 0 in 0 : i8 design
    simulation.net.decl 1 in 0 : i8 design
    simulation.net.decl 2 in 0 : i8 design
    simulation.net.decl 3 in 0 : i8 design
    simulation.driver.decl 0 in 0 drives 0 : i8 design
    simulation.driver.decl 1 in 0 drives 1 : i8 design
    simulation.driver.decl 2 in 0 drives 2 : i8 design
    simulation.driver.decl 3 in 0 drives 3 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "choose"
    simulation.code_unit.decl 3 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 4 in 0 initial hierarchy "stimulus"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 3 : i64} {
      %a = simulation.context.net %ctx[0] : !simulation.net<i8>
      %b = simulation.context.net %ctx[1] : !simulation.net<i8>
      %c = simulation.context.net %ctx[2] : !simulation.net<i8>
      %p = simulation.spawn @producer(%ctx) : !simulation.context -> !simulation.process
      %q = simulation.spawn @choose(%ctx, %a, %b, %c) : !simulation.context, !simulation.net<i8>, !simulation.net<i8>, !simulation.net<i8> -> !simulation.process
      %s = simulation.spawn @stimulus(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<i8>
      %response = simulation.context.net %ctx[3] : !simulation.net<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %result = simulation.net.read %response : !simulation.net<i8> -> i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.driver.drive %driver = %value : !simulation.driver<i8>, i8
      simulation.suspend.any %input, %response edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.net<i8>
    }
    simulation.func @choose(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.net<i8> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %b: !simulation.net<i8> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64}, %c: !simulation.net<i8> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %response = simulation.context.driver %ctx[3] : !simulation.driver<i8>
      %selector = simulation.context.storage %ctx[2] : !simulation.ref<i2>
      %zero = arith.constant 0 : i2
      %one = arith.constant 1 : i2
      %two = arith.constant 2 : i2
      %increment = arith.constant 1 : i8
      cf.br ^body
    ^body:
      %choice = simulation.ref.load %selector : !simulation.ref<i2> -> i2
      %is_a = arith.cmpi eq, %choice, %zero : i2
      cf.cond_br %is_a, ^read_a, ^test_b
    ^read_a:
      %va = simulation.net.read %a : !simulation.net<i8> -> i8
      cf.br ^join(%va : i8)
    ^test_b:
      %is_b = arith.cmpi eq, %choice, %one : i2
      cf.cond_br %is_b, ^read_b, ^test_c
    ^read_b:
      %vb = simulation.net.read %b : !simulation.net<i8> -> i8
      cf.br ^join(%vb : i8)
    ^test_c:
      %is_c = arith.cmpi eq, %choice, %two : i2
      cf.cond_br %is_c, ^read_c, ^compute
    ^read_c:
      %vc = simulation.net.read %c : !simulation.net<i8> -> i8
      cf.br ^join(%vc : i8)
    ^compute:
      %incoming = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %computed = arith.addi %incoming, %increment : i8
      cf.br ^join(%computed : i8)
    ^join(%value: i8):
      simulation.driver.drive %response = %value : !simulation.driver<i8>, i8
      simulation.suspend.any %a, %b, %c, %input, %selector edges [0, 0, 0, 0, 0] to ^body : !simulation.net<i8>, !simulation.net<i8>, !simulation.net<i8>, !simulation.ref<i8>, !simulation.ref<i2>
    }
    simulation.func @stimulus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %selector = simulation.context.storage %ctx[2] : !simulation.ref<i2>
      %b_driver = simulation.context.driver %ctx[1] : !simulation.driver<i8>
      %c_driver = simulation.context.driver %ctx[2] : !simulation.driver<i8>
      %a = arith.constant 17 : i8
      %b = arith.constant 42 : i8
      %c = arith.constant 66 : i8
      %sum = arith.constant 18 : i8
      %next = arith.constant 99 : i8
      %next_sum = arith.constant 100 : i8
      %zero = arith.constant 0 : i2
      %one = arith.constant 1 : i2
      %two = arith.constant 2 : i2
      %three = arith.constant 3 : i2
      %tick = simulation.time.constant 1
      simulation.driver.drive %b_driver = %b : !simulation.driver<i8>, i8
      simulation.driver.drive %c_driver = %c : !simulation.driver<i8>, i8
      simulation.ref.store %a to %input : i8, !simulation.ref<i8>
      simulation.ref.store %zero to %selector : i2, !simulation.ref<i2>
      simulation.suspend.delay %tick to ^check_a
    ^check_a:
      %first = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %first_ok = arith.cmpi eq, %first, %a : i8
      cf.cond_br %first_ok, ^select_b, ^fail
    ^select_b:
      simulation.ref.store %one to %selector : i2, !simulation.ref<i2>
      simulation.suspend.delay %tick to ^check_b
    ^check_b:
      %second = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %second_ok = arith.cmpi eq, %second, %b : i8
      cf.cond_br %second_ok, ^select_c, ^fail
    ^select_c:
      simulation.ref.store %two to %selector : i2, !simulation.ref<i2>
      simulation.suspend.delay %tick to ^check_c
    ^check_c:
      %third = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %third_ok = arith.cmpi eq, %third, %c : i8
      cf.cond_br %third_ok, ^select_computed, ^fail
    ^select_computed:
      simulation.ref.store %three to %selector : i2, !simulation.ref<i2>
      simulation.suspend.delay %tick to ^check_computed
    ^check_computed:
      %fourth = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %fourth_ok = arith.cmpi eq, %fourth, %sum : i8
      cf.cond_br %fourth_ok, ^update_a, ^fail
    ^update_a:
      simulation.ref.store %next to %input : i8, !simulation.ref<i8>
      simulation.suspend.delay %tick to ^check_updated
    ^check_updated:
      %fifth = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %fifth_ok = arith.cmpi eq, %fifth, %next_sum : i8
      cf.cond_br %fifth_ok, ^select_a, ^fail
    ^select_a:
      simulation.ref.store %zero to %selector : i2, !simulation.ref<i2>
      simulation.suspend.delay %tick to ^check_next
    ^check_next:
      %sixth = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %sixth_ok = arith.cmpi eq, %sixth, %next : i8
      cf.cond_br %sixth_ok, ^pass, ^fail
    ^pass:
      %pass_text = simulation.bytes.constant "JOINED NETS PASS"
      %pass_stdout = arith.constant 1 : i32
      simulation.display %ctx to %pass_stdout(%pass_text) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^fail:
      %fail_text = simulation.bytes.constant "JOINED NETS FAIL"
      %fail_stdout = arith.constant 1 : i32
      simulation.display %ctx to %fail_stdout(%fail_text) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
