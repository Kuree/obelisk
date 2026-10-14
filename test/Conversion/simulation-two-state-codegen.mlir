// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | \
// RUN:   mlir-translate --mlir-to-llvmir > %t.aot.ll
// RUN: opt -S -passes=verify %t.aot.ll | FileCheck %s --check-prefix=AOT-O0
// RUN: opt -S -passes='default<O3>,verify' %t.aot.ll | \
// RUN:   FileCheck %s --check-prefix=AOT
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | \
// RUN:   FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s '--test-obelisk-set-module-attributes=attrs="{native.optimization_level = 3 : i32, native.max_state_domain_functions = 0 : i64}"' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=BUDGET

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @two_state_codegen {
    simulation.code_unit.decl 80 in 0 function hierarchy "top.add_known"
    simulation.code_unit.decl 81 in 0 initial hierarchy "top.root"
    simulation.code_unit.decl 82 in 0 function hierarchy "top.public_truth"
    simulation.code_unit.decl 83 in 0 function hierarchy "top.select_logic"
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.logic<64> design
        hierarchy "top.result"
    simulation.storage.decl 1 in 0 : !simulation.logic<64> design
        hierarchy "top.local"
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design
        hierarchy "top.four_state"

    simulation.func private @add_known(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<64> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<64> attributes {
          code_unit_id = 80 : i64, entry_kind = 8 : i32
        } {
      cf.br ^compute(%value : !simulation.logic<64>)
    ^compute(%current: !simulation.logic<64>):
      %one = simulation.logic.constant 1 : i64, 0 : i64 :
          !simulation.logic<64>
      %sum = simulation.logic.binary add %current, %one :
          !simulation.logic<64>
      simulation.return %sum : !simulation.logic<64>
    }

    // This symbol has a known direct caller, but its public ABI remains an
    // open boundary and must accept X/Z from callers outside this design.
    simulation.func @public_truth(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<64> {simulation.capture_kind = 1 : i32})
        -> i1 attributes {code_unit_id = 82 : i64, entry_kind = 8 : i32} {
      %truth = simulation.logic.is_true %value :
          !simulation.logic<64>
      simulation.return %truth : i1
    }

    simulation.func private @select_logic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 1 : i32},
        %true_value: !simulation.logic<64>
            {simulation.capture_kind = 1 : i32},
        %false_value: !simulation.logic<64>
            {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<64> attributes {
          code_unit_id = 83 : i64, entry_kind = 8 : i32
        } {
      %selected = arith.select %condition, %true_value, %false_value :
          !simulation.logic<64>
      simulation.return %selected : !simulation.logic<64>
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 81 : i64, entry_kind = 1 : i32} {
      %input = simulation.logic.constant 41 : i64, 0 : i64 :
          !simulation.logic<64>
      %result = simulation.call @add_known(%ctx, %input) :
          (!simulation.context, !simulation.logic<64>) ->
          !simulation.logic<64>
      %public_truth = simulation.call @public_truth(%ctx, %input) :
          (!simulation.context, !simulation.logic<64>) -> i1
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<64>>
      simulation.ref.store %result to %storage : !simulation.logic<64>,
          !simulation.ref<!simulation.logic<64>>
      %raw = arith.constant 7 : i64
      %local = simulation.logic.from_bits %raw : i64 -> !simulation.logic<64>
      %mask = simulation.logic.constant 3 : i64, 0 : i64 :
          !simulation.logic<64>
      %fast = simulation.logic.binary xor %local, %mask :
          !simulation.logic<64>
      %local_storage = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<64>>
      simulation.ref.store %fast to %local_storage : !simulation.logic<64>,
          !simulation.ref<!simulation.logic<64>>
      %xz = simulation.logic.constant 0 : i64, -1 : i64 :
          !simulation.logic<64>
      %xz_not = simulation.logic.unary bit_not %xz :
          (!simulation.logic<64>) -> !simulation.logic<64>
      %xz_storage = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<64>>
      simulation.ref.store %xz_not to %xz_storage : !simulation.logic<64>,
          !simulation.ref<!simulation.logic<64>>
      simulation.return
    }
  }
}

// The final AOT SSA still has the stable two-plane function signature at O0,
// and IPO proves its only call supplies 41. The producer folds the result to
// 42 before either physical plane needs arithmetic.
// AOT-O0-LABEL: define { i64, i64 } @add_known(
// AOT-O0-SAME: i64 %[[O0_VALUE:[0-9]+]], i64 %[[O0_UNKNOWN:[0-9]+]])
// AOT-O0-NOT: %[[O0_UNKNOWN]]{{[^0-9]}}
// AOT-O0-NOT: add i64
// AOT-O0-NOT: %[[O0_UNKNOWN]]{{[^0-9]}}
// AOT-O0: ret { i64, i64 } { i64 42, i64 0 }

// The public function's unknown plane remains live despite its known direct
// caller; external callers are allowed to supply X/Z through the stable ABI.
// AOT-O0-LABEL: define i1 @public_truth(
// AOT-O0-SAME: i64 %[[O0_PUBLIC_VALUE:[0-9]+]], i64 %[[O0_PUBLIC_UNKNOWN:[0-9]+]])
// AOT-O0: xor i64 %[[O0_PUBLIC_UNKNOWN]], -1
// AOT-O0: and i64 %[[O0_PUBLIC_VALUE]],
// AOT-O0: ret i1

// Each physical plane of a one-to-many logic value is selected independently.
// AOT-O0-LABEL: define { i64, i64 } @select_logic(
// AOT-O0: select i1
// AOT-O0: select i1
// AOT-O0: ret { i64, i64 }

// AOT-LABEL: define { i64, i64 } @add_known(
// AOT-SAME: i64 %[[VALUE:[0-9]+]], i64 %[[UNKNOWN:[0-9]+]])
// AOT-NOT: %[[UNKNOWN]]{{[^0-9]}}
// AOT-NOT: add i64
// AOT-NOT: select i1
// AOT-NOT: %[[UNKNOWN]]{{[^0-9]}}
// AOT: ret { i64, i64 } { i64 42, i64 0 }

// AOT-LABEL: define i1 @public_truth(
// AOT-SAME: i64 %[[PUBLIC_VALUE:[0-9]+]], i64 %[[PUBLIC_UNKNOWN:[0-9]+]])
// AOT: xor i64 %[[PUBLIC_UNKNOWN]], -1
// AOT: and i64 %[[PUBLIC_VALUE]],
// AOT: ret i1

// BYTECODE-LABEL: simulation.func private @add_known
// BYTECODE-SAME: bytecode.two_state_logic_registers = 0 : i32
// BYTECODE-LABEL: simulation.func @root
// BYTECODE-SAME: bytecode.two_state_logic_registers = 3 : i32

// Function-owned IPO/SCEV facts remain available when the optional whole-
// design state-domain analysis is skipped by its compile-time budget.
// BUDGET-NOT: native.max_state_domain_functions
// BUDGET-NOT: native.optimization_level
// BUDGET-LABEL: llvm.func @add_known(
// BUDGET-SAME: %[[VALUE:[a-zA-Z0-9_]+]]: i64, %[[UNKNOWN:[a-zA-Z0-9_]+]]: i64)
// BUDGET-NOT: llvm.icmp "ne"
// BUDGET: llvm.br ^bb1
// BUDGET: ^bb1:
// BUDGET-NOT: llvm.icmp "ne"
// BUDGET: llvm.return
// BUDGET-LABEL: llvm.func @public_truth(
