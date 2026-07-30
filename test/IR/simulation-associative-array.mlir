// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @associative_array {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.assoc"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.logic_assoc"

    obelisk_sim.func @assoc(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %owner: !obelisk_sim.argument_ref<!obelisk_sim.assoc_array<i32, i64, true, false>> {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %key = arith.constant 4 : i32
      %value = arith.constant 42 : i64
      %array = obelisk_sim.assoc.create {
        alignment = 1 : i64,
        bit_width = 64 : i64,
        element_flags = 0 : i32,
        element_kind = 1 : i32,
        key_kind = 2 : i32,
        key_width = 32 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 1 : i64,
        value_size = 8 : i64
      } : () -> !obelisk_sim.assoc_array<i32, i64, true, false>
      %is_null = obelisk_sim.managed.is_null %array :
        (!obelisk_sim.assoc_array<i32, i64, true, false>) -> i1
      obelisk_sim.assoc.write %array, %key, %value :
        (!obelisk_sim.assoc_array<i32, i64, true, false>, i32, i64) -> ()
      %read = obelisk_sim.assoc.read %array, %key :
        (!obelisk_sim.assoc_array<i32, i64, true, false>, i32) -> i64
      %exists = obelisk_sim.assoc.exists %array, %key :
        (!obelisk_sim.assoc_array<i32, i64, true, false>, i32) -> i1
      obelisk_sim.assoc.set_default %array, %value :
        (!obelisk_sim.assoc_array<i32, i64, true, false>, i64) -> ()
      %next, %valid = obelisk_sim.assoc.traverse %array, %key {
        direction = 1 : i32, endpoint = false
      } : (!obelisk_sim.assoc_array<i32, i64, true, false>, i32) -> (i32, i1)
      %path = obelisk_sim.reference_path.assoc %ctx, %array[%key] watching %owner :
        (!obelisk_sim.context, !obelisk_sim.assoc_array<i32, i64, true, false>,
         i32, !obelisk_sim.argument_ref<!obelisk_sim.assoc_array<i32, i64, true, false>>) ->
        !obelisk_sim.reference_path<i64>
      obelisk_sim.assoc.delete %array, %next :
        (!obelisk_sim.assoc_array<i32, i64, true, false>, i32) -> ()
      obelisk_sim.container.delete %array :
        (!obelisk_sim.assoc_array<i32, i64, true, false>) -> ()
      obelisk_sim.return
    }

    // Exercise one-to-many conversion results directly. A four-state element
    // lowers to value and unknown planes, while traversal replaces both its
    // key and success results.
    obelisk_sim.func @logic_assoc(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %key = arith.constant 4 : i32
      %value = obelisk_sim.logic.constant 5 : i4, 2 : i4 :
          !obelisk_sim.logic<4>
      %array = obelisk_sim.assoc.create {
        alignment = 1 : i64,
        bit_width = 4 : i64,
        element_flags = 1 : i32,
        element_kind = 2 : i32,
        key_kind = 2 : i32,
        key_width = 32 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 2 : i64,
        value_size = 1 : i64
      } : () -> !obelisk_sim.assoc_array<i32, !obelisk_sim.logic<4>, true, false>
      obelisk_sim.assoc.write %array, %key, %value :
        (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<4>, true, false>,
         i32, !obelisk_sim.logic<4>) -> ()
      %read = obelisk_sim.assoc.read %array, %key :
        (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<4>, true, false>,
         i32) -> !obelisk_sim.logic<4>
      obelisk_sim.assoc.set_default %array, %read :
        (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<4>, true, false>,
         !obelisk_sim.logic<4>) -> ()
      %next, %valid = obelisk_sim.assoc.traverse %array, %key {
        direction = 1 : i32, endpoint = false
      } : (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<4>, true, false>,
           i32) -> (i32, i1)
      obelisk_sim.assoc.delete %array, %next :
        (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<4>, true, false>,
         i32) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.assoc.create
// CHECK: obelisk_sim.assoc.write
// CHECK: obelisk_sim.assoc.read
// CHECK: obelisk_sim.assoc.exists
// CHECK: obelisk_sim.assoc.set_default
// CHECK: obelisk_sim.assoc.traverse
// CHECK: obelisk_sim.reference_path.assoc
// CHECK: obelisk_sim.assoc.delete
// NATIVE-DAG: llvm.call @obelisk_rt_v1_assoc_create_typed
// NATIVE-DAG: llvm.call @obelisk_rt_v1_assoc_write_checked
// NATIVE-DAG: llvm.call @obelisk_rt_v1_assoc_read_checked
// NATIVE-DAG: llvm.call @obelisk_rt_v1_assoc_exists
// NATIVE-DAG: llvm.call @obelisk_rt_v1_assoc_set_default_checked
// NATIVE-DAG: llvm.call @obelisk_rt_v1_assoc_next
// NATIVE-DAG: llvm.call @obelisk_rt_v1_reference_path_assoc_create
// NATIVE-DAG: llvm.call @obelisk_rt_v1_assoc_delete
// NATIVE-NOT: unrealized_conversion_cast
// BYTECODE: obelisk.bytecode.image
