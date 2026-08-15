// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | \
// RUN:   mlir-translate --mlir-to-llvmir > %t.aot.ll
// RUN: opt -S -passes=verify %t.aot.ll | FileCheck %s --check-prefix=AOT-O0
// RUN: opt -S -passes='default<O3>,verify' %t.aot.ll | \
// RUN:   FileCheck %s --check-prefix=AOT
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | \
// RUN:   FileCheck %s --check-prefix=BYTECODE
// RUN: sed 's/llvm.data_layout =/obelisk.native.optimization_level = 3 : i32, obelisk.native.max_state_domain_functions = 0 : i64, llvm.data_layout =/' %s | \
// RUN:   obelisk-opt - --convert-obelisk-sim-processes-to-llvm-coroutines | \
// RUN:   FileCheck %s --check-prefix=BUDGET

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @two_state_codegen {
    obelisk_sim.code_unit.decl 80 in 0 function hierarchy "top.add_known"
    obelisk_sim.code_unit.decl 81 in 0 initial hierarchy "top.root"
    obelisk_sim.code_unit.decl 82 in 0 function hierarchy "top.public_truth"
    obelisk_sim.code_unit.decl 83 in 0 function hierarchy "top.select_logic"
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<64> design
        hierarchy "top.result"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<64> design
        hierarchy "top.local"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<64> design
        hierarchy "top.four_state"

    obelisk_sim.func private @add_known(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<64> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<64> attributes {
          code_unit_id = 80 : i64, entry_kind = 8 : i32
        } {
      cf.br ^compute(%value : !obelisk_sim.logic<64>)
    ^compute(%current: !obelisk_sim.logic<64>):
      %one = obelisk_sim.logic.constant 1 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %sum = obelisk_sim.logic.binary add %current, %one :
          !obelisk_sim.logic<64>
      obelisk_sim.return %sum : !obelisk_sim.logic<64>
    }

    // This symbol has a known direct caller, but its public ABI remains an
    // open boundary and must accept X/Z from callers outside this design.
    obelisk_sim.func @public_truth(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<64> {obelisk_sim.capture_kind = 1 : i32})
        -> i1 attributes {code_unit_id = 82 : i64, entry_kind = 8 : i32} {
      %truth = obelisk_sim.logic.is_true %value :
          !obelisk_sim.logic<64>
      obelisk_sim.return %truth : i1
    }

    obelisk_sim.func private @select_logic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: i1 {obelisk_sim.capture_kind = 1 : i32},
        %true_value: !obelisk_sim.logic<64>
            {obelisk_sim.capture_kind = 1 : i32},
        %false_value: !obelisk_sim.logic<64>
            {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<64> attributes {
          code_unit_id = 83 : i64, entry_kind = 8 : i32
        } {
      %selected = arith.select %condition, %true_value, %false_value :
          !obelisk_sim.logic<64>
      obelisk_sim.return %selected : !obelisk_sim.logic<64>
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 81 : i64, entry_kind = 1 : i32} {
      %input = obelisk_sim.logic.constant 41 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %result = obelisk_sim.call @add_known(%ctx, %input) :
          (!obelisk_sim.context, !obelisk_sim.logic<64>) ->
          !obelisk_sim.logic<64>
      %public_truth = obelisk_sim.call @public_truth(%ctx, %input) :
          (!obelisk_sim.context, !obelisk_sim.logic<64>) -> i1
      %storage = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %result to %storage : !obelisk_sim.logic<64>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %raw = arith.constant 7 : i64
      %local = obelisk_sim.logic.from_bits %raw : i64 -> !obelisk_sim.logic<64>
      %mask = obelisk_sim.logic.constant 3 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %fast = obelisk_sim.logic.binary xor %local, %mask :
          !obelisk_sim.logic<64>
      %local_storage = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %fast to %local_storage : !obelisk_sim.logic<64>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %xz = obelisk_sim.logic.constant 0 : i64, -1 : i64 :
          !obelisk_sim.logic<64>
      %xz_not = obelisk_sim.logic.unary bit_not %xz :
          (!obelisk_sim.logic<64>) -> !obelisk_sim.logic<64>
      %xz_storage = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %xz_not to %xz_storage : !obelisk_sim.logic<64>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.return
    }
  }
}

// The final AOT SSA still has the stable two-plane function signature at O0,
// but neither the entry unknown plane nor the converted CFG unknown block
// argument participates in the computation.
// AOT-O0-LABEL: define { i64, i64 } @add_known(
// AOT-O0-SAME: i64 %[[O0_VALUE:[0-9]+]], i64 %[[O0_UNKNOWN:[0-9]+]])
// AOT-O0-NOT: %[[O0_UNKNOWN]]{{[^0-9]}}
// AOT-O0: add i64
// AOT-O0-NOT: %[[O0_UNKNOWN]]{{[^0-9]}}
// AOT-O0: ret { i64, i64 }

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
// AOT: %[[SUM:.*]] = add i64 %[[VALUE]], 1
// AOT-NOT: select i1
// AOT-NOT: %[[UNKNOWN]]{{[^0-9]}}
// AOT: insertvalue { i64, i64 } {{.*}}, i64 0, 1
// AOT: ret { i64, i64 }

// AOT-LABEL: define i1 @public_truth(
// AOT-SAME: i64 %[[PUBLIC_VALUE:[0-9]+]], i64 %[[PUBLIC_UNKNOWN:[0-9]+]])
// AOT: xor i64 %[[PUBLIC_UNKNOWN]], -1
// AOT: and i64 %[[PUBLIC_VALUE]],
// AOT: ret i1

// BYTECODE-LABEL: obelisk_sim.func private @add_known
// BYTECODE-SAME: obelisk.bytecode.two_state_logic_registers = 0 : i32
// BYTECODE-LABEL: obelisk_sim.func @root
// BYTECODE-SAME: obelisk.bytecode.two_state_logic_registers = 3 : i32

// The O3 compile-time budget conservatively retains the unknown plane instead
// of applying a whole-design two-state proof on an oversized design.
// BUDGET-NOT: obelisk.native.max_state_domain_functions
// BUDGET-NOT: obelisk.native.optimization_level
// BUDGET-LABEL: llvm.func @add_known(
// BUDGET-SAME: %[[VALUE:.*]]: i64, %[[UNKNOWN:.*]]: i64)
// BUDGET: llvm.icmp "ne" %[[UNKNOWN]],
