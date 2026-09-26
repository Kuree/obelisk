// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
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
  obelisk_sim.design @multiclock_cohort_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.a"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.b"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.enable"
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.gate"
    obelisk_sim.code_unit.decl 9980000 in 0 root_initializer
        hierarchy "top.root"
    obelisk_sim.code_unit.decl 9980001 in 0 always hierarchy "top.observe"
    obelisk_sim.code_unit.decl 9980002 in 0 initial hierarchy "top.drive"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9980000 : i64} {
      %a = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %b = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %enable = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %gate = obelisk_sim.context.storage %ctx[3] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %observer = obelisk_sim.spawn @observe(
          %ctx, %a, %b, %enable, %gate) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %driver = obelisk_sim.spawn @drive(%ctx, %a, %b, %enable, %gate) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @observe(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32},
        %enable: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32},
        %gate: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 9980001 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    obelisk_sim.multiclock_sequence_coordinator} {
      %state = arith.constant 0 : i64
      cf.br ^wait(%state : i64)
    ^wait(%wait_state: i64):
      obelisk_sim.suspend.clock_set %a, %b, %wait_state conditions 0
          edges [1, 1] indices [-1, -1] site 9980003 to ^resume
          {resume_region = 8 : i32} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>, i64
    ^resume(%next: i64):
      %cohort = obelisk_sim.assert.clock_occurrence.consume %ctx site 9980003
      %zero = arith.constant 0 : i64
      %has_cohort = arith.cmpi ne, %cohort, %zero : i64
      cf.cond_br %has_cohort, ^filter, ^retry
    ^retry:
      cf.br ^wait(%next : i64)
    ^filter:
      %enable_value = obelisk_sim.ref.load %enable :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %gate_value = obelisk_sim.ref.load %gate :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %enable_true = obelisk_sim.logic.is_true %enable_value :
          !obelisk_sim.logic<1>
      %gate_true = obelisk_sim.logic.is_true %gate_value :
          !obelisk_sim.logic<1>
      %computed = arith.andi %enable_true, %gate_true : i1
      %all = arith.constant -1 : i64
      %clear_zero = arith.constant -2 : i64
      %qualified = arith.select %computed, %all, %clear_zero : i64
      %filtered = arith.andi %cohort, %qualified : i64
      %two = arith.constant 2 : i64
      %ok = arith.cmpi eq, %filtered, %two : i64
      cf.cond_br %ok, ^pass, ^fail
    ^pass:
      %pass_message = obelisk_sim.bytes.constant
          "multiclock cohort runtime: PASS"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%pass_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      %finish = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %finish
      obelisk_sim.return
    ^fail:
      %fail_message = obelisk_sim.bytes.constant
          "multiclock cohort runtime: FAIL"
      %fail_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %fail_stdout(%fail_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      %fail_finish = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %fail_finish
      obelisk_sim.return
    }

    obelisk_sim.func private @drive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32},
        %enable: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32},
        %gate: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9980002 : i64} {
      %false = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %true = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      obelisk_sim.ref.store %false to %enable :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %true to %gate :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %true to %a :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %true to %b :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}
