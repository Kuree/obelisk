// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s --implicit-check-not=FAIL
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s --implicit-check-not=FAIL

// Exercise the Simulation-level cohort primitive and the postfilter shape
// emitted for a computed multi-clock iff. Both posedges occur in one
// publication wave. The computed false condition clears only clock zero, so
// the finalized mask must retain clock one.
// CHECK-COUNT-1: multiclock cohort runtime: PASS

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @multiclock_cohort_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.a"
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
        hierarchy "top.b"
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
        hierarchy "top.enable"
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design
        hierarchy "top.gate"
    simulation.code_unit.decl 9980000 in 0 root_initializer
        hierarchy "top.root"
    simulation.code_unit.decl 9980001 in 0 always hierarchy "top.observe"
    simulation.code_unit.decl 9980002 in 0 initial hierarchy "top.drive"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9980000 : i64} {
      %a = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %b = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %enable = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<1>>
      %gate = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<1>>
      %observer = simulation.spawn @observe(
          %ctx, %a, %b, %enable, %gate) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %driver = simulation.spawn @drive(%ctx, %a, %b, %enable, %gate) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @observe(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32},
        %b: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32},
        %enable: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32},
        %gate: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 9980001 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    simulation.multiclock_sequence_coordinator} {
      %state = arith.constant 0 : i64
      cf.br ^wait(%state : i64)
    ^wait(%wait_state: i64):
      simulation.suspend.clock_set %a, %b, %wait_state conditions 0
          edges [1, 1] indices [-1, -1] site 9980003 to ^resume
          {resume_region = 8 : i32} :
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>, i64
    ^resume(%next: i64):
      %cohort = simulation.assert.clock_occurrence.consume %ctx site 9980003
      %zero = arith.constant 0 : i64
      %has_cohort = arith.cmpi ne, %cohort, %zero : i64
      cf.cond_br %has_cohort, ^filter, ^retry
    ^retry:
      cf.br ^wait(%next : i64)
    ^filter:
      %enable_value = simulation.ref.load %enable :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %gate_value = simulation.ref.load %gate :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %enable_true = simulation.logic.is_true %enable_value :
          !simulation.logic<1>
      %gate_true = simulation.logic.is_true %gate_value :
          !simulation.logic<1>
      %computed = arith.andi %enable_true, %gate_true : i1
      %all = arith.constant -1 : i64
      %clear_zero = arith.constant -2 : i64
      %qualified = arith.select %computed, %all, %clear_zero : i64
      %filtered = arith.andi %cohort, %qualified : i64
      %two = arith.constant 2 : i64
      %ok = arith.cmpi eq, %filtered, %two : i64
      cf.cond_br %ok, ^pass, ^fail
    ^pass:
      %pass_message = simulation.bytes.constant
          "multiclock cohort runtime: PASS"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%pass_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      %finish = arith.constant 0 : i32
      simulation.finish %ctx, %finish
      simulation.return
    ^fail:
      %fail_message = simulation.bytes.constant
          "multiclock cohort runtime: FAIL"
      %fail_stdout = arith.constant 1 : i32
      simulation.display %ctx to %fail_stdout(%fail_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      %fail_finish = arith.constant 0 : i32
      simulation.finish %ctx, %fail_finish
      simulation.return
    }

    simulation.func private @drive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32},
        %b: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32},
        %enable: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32},
        %gate: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9980002 : i64} {
      %false = simulation.logic.constant false, false :
          !simulation.logic<1>
      %true = simulation.logic.constant true, false :
          !simulation.logic<1>
      simulation.ref.store %false to %enable :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %true to %gate :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %true to %a :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %true to %b :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}
