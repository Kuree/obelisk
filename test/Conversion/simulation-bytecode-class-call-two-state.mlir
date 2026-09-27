// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py --metadata \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @class_call_two_state {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "Base.direct"
    simulation.code_unit.decl 2 in 0 function hierarchy "Base.virtual"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.call"

    simulation.class.decl @Base id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.method @Base_virtual of @Base slot 0 signature_id 17
        implemented_by @virtual_impl :
      (!simulation.context, !simulation.class_handle<@Base>,
       !simulation.logic<64>) -> !simulation.logic<64> {
        is_final = true, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    simulation.func private @direct(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Base>
          {simulation.capture_kind = 1 : i32},
        %value: !simulation.logic<64>
          {simulation.capture_kind = 1 : i32}) -> !simulation.logic<64>
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      simulation.return %value : !simulation.logic<64>
    }

    simulation.func private @virtual_impl(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Base>
          {simulation.capture_kind = 1 : i32},
        %value: !simulation.logic<64>
          {simulation.capture_kind = 1 : i32}) -> !simulation.logic<64>
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      simulation.return %value : !simulation.logic<64>
    }

    simulation.func private @call(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@Base>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %compact = simulation.logic.constant 2 : i64, 0 : i64
          : !simulation.logic<64>
      %zero = simulation.logic.constant 0 : i64, 0 : i64
          : !simulation.logic<64>
      %direct = simulation.class.direct_call @direct %receiver(%zero) :
          (!simulation.class_handle<@Base>, !simulation.logic<64>)
          -> !simulation.logic<64>
      %one = simulation.logic.constant 1 : i64, 0 : i64
          : !simulation.logic<64>
      %virtual = simulation.class.virtual_call
          %receiver[@Base_virtual] slot 0 signature_id 17(%one) :
          (!simulation.class_handle<@Base>, !simulation.logic<64>)
          -> !simulation.logic<64>
      simulation.return
    }
  }
}

// The caller is function 2 by stable code-unit ID. Both otherwise-proven
// logic operands and results must retain the two-plane logic ABI (kind 2,
// size 16) at direct and virtual class-call boundaries. The unrelated local
// remains compact one-plane bits (kind 1), proving specialization is active.
// CHECK: function 2: id=3
// CHECK: layout function=2 register=2 kind=1 flags=0 width=64
// CHECK-SAME: size=8
// CHECK: layout function=2 register=3 kind=2 flags=0 width=64
// CHECK-SAME: size=16
// CHECK: layout function=2 register=4 kind=2 flags=0 width=64
// CHECK-SAME: size=16
// CHECK: layout function=2 register=5 kind=2 flags=0 width=64
// CHECK-SAME: size=16
// CHECK: layout function=2 register=6 kind=2 flags=0 width=64
// CHECK-SAME: size=16
// CHECK: opcode=31
// CHECK: opcode=41
