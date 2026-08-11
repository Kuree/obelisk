// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

!bundle = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Base>,
                     ordinal = 0, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @virtual_tasks {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.caller"
    obelisk_sim.code_unit.decl 2 in 0 task hierarchy "Base.run"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "top.class_caller"

    obelisk_sim.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @Base id 2 implements [@Runner] {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.method @Runner_run of @Runner slot 4294967295
      signature_id 17 interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Runner>, i32,
       !bundle) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @Base_run of @Base slot 0 signature_id 17
      implemented_by @base_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>, i32,
       !bundle) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }

    obelisk_sim.func @base_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Base>
          {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32},
        %bundle: !bundle {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %receiver = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Base>
      %interface = obelisk_sim.class.cast %receiver :
        !obelisk_sim.class_handle<@Base> to
        !obelisk_sim.class_handle<@Runner>
      %value = arith.constant 42 : i32
      %empty = obelisk_sim.aggregate.default : !bundle
      %bundle = obelisk_sim.aggregate.insert %receiver into %empty[0] :
        (!bundle, !obelisk_sim.class_handle<@Base>) -> !bundle
      obelisk_sim.class.virtual_task_call
        %interface[@Runner_run] slot 4294967295 signature_id 17
        (%value, %bundle, %bundle) arguments 2 to ^done :
        (!obelisk_sim.class_handle<@Runner>, i32, !bundle, !bundle) -> ()
    ^done(%continued: !bundle):
      obelisk_sim.return
    }

    obelisk_sim.func @class_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %receiver = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Base>
      %value = arith.constant 9 : i32
      %empty = obelisk_sim.aggregate.default : !bundle
      %bundle = obelisk_sim.aggregate.insert %receiver into %empty[0] :
        (!bundle, !obelisk_sim.class_handle<@Base>) -> !bundle
      obelisk_sim.class.virtual_task_call
        %receiver[@Base_run] slot 0 signature_id 17
        (%value, %bundle) arguments 2 to ^done :
        (!obelisk_sim.class_handle<@Base>, i32, !bundle) -> ()
    ^done:
      obelisk_sim.return
    }
  }
}

// Opcode 57 carries interface-dispatch record 8, receiver register 2,
// argument-map offset 9/count 4, continuation 1, and signature ID 17. The
// dispatch record stores interface ID 1 and ordinal 0.
// The managed continuation is stored before dispatch, restored on resume, and
// then removed from the canonical frame's precise root inventory.
// CHECK: obelisk.bytecode.image = array<i8:
// CHECK-SAME: 24, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
// CHECK-SAME: 57, 0, 0, 0, 8, 0, 0, 0, 2, 0, 0, 0, 9, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 17, 0, 0, 0, 0, 0, 0, 0
// CHECK-SAME: 23, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
// CHECK-SAME: 42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
// CHECK-SAME: 55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0
// CHECK-SAME: 54, 0, 0, 0, 0, 0, 0, 0
// CHECK: obelisk.bytecode.function = 1 : i32
// CHECK: obelisk.bytecode.function = 0 : i32
