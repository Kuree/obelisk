// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @associative_array {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.assoc"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.logic_assoc"
    simulation.code_unit.decl 3 in 0 initial hierarchy "top.wide_assoc"
    simulation.code_unit.decl 4 in 0 initial hierarchy "top.class_assoc"
    simulation.code_unit.decl 5 in 0 initial hierarchy "top.process_assoc"
    simulation.class.decl @Key id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }

    simulation.func @assoc(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %owner: !simulation.argument_ref<!simulation.assoc_array<i32, i64, true, false>> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %key = arith.constant 4 : i32
      %value = arith.constant 42 : i64
      %array = simulation.assoc.create {
        alignment = 1 : i64,
        bit_width = 64 : i64,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<bits>,
        key_kind = #simulation.assoc_key_kind<signed>,
        key_width = 32 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 1 : i64,
        value_size = 8 : i64
      } : () -> !simulation.assoc_array<i32, i64, true, false>
      %is_null = simulation.managed.is_null %array :
        (!simulation.assoc_array<i32, i64, true, false>) -> i1
      simulation.assoc.write %array, %key, %value :
        (!simulation.assoc_array<i32, i64, true, false>, i32, i64) -> ()
      %read = simulation.assoc.read %array, %key :
        (!simulation.assoc_array<i32, i64, true, false>, i32) -> i64
      %exists = simulation.assoc.exists %array, %key :
        (!simulation.assoc_array<i32, i64, true, false>, i32) -> i1
      simulation.assoc.set_default %array, %value :
        (!simulation.assoc_array<i32, i64, true, false>, i64) -> ()
      %next, %valid = simulation.assoc.traverse %array, %key {
        direction = #simulation.assoc_traversal_direction<forward>, endpoint = false
      } : (!simulation.assoc_array<i32, i64, true, false>, i32) -> (i32, i1)
      %path = simulation.reference_path.assoc %ctx, %array[%key] watching %owner :
        (!simulation.context, !simulation.assoc_array<i32, i64, true, false>,
         i32, !simulation.argument_ref<!simulation.assoc_array<i32, i64, true, false>>) ->
        !simulation.reference_path<i64>
      simulation.assoc.delete %array, %next :
        (!simulation.assoc_array<i32, i64, true, false>, i32) -> ()
      simulation.container.delete %array :
        (!simulation.assoc_array<i32, i64, true, false>) -> ()
      simulation.return
    }

    // Exercise one-to-many conversion results directly. A four-state element
    // lowers to value and unknown planes, while traversal replaces both its
    // key and success results.
    simulation.func @logic_assoc(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %key = arith.constant 4 : i32
      %value = simulation.logic.constant 5 : i4, 2 : i4 :
          !simulation.logic<4>
      %array = simulation.assoc.create {
        alignment = 1 : i64,
        bit_width = 4 : i64,
        element_flags = #simulation.element_flags<four_state>,
        element_kind = #simulation.element_kind<logic>,
        key_kind = #simulation.assoc_key_kind<signed>,
        key_width = 32 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 2 : i64,
        value_size = 1 : i64
      } : () -> !simulation.assoc_array<i32, !simulation.logic<4>, true, false>
      simulation.assoc.write %array, %key, %value :
        (!simulation.assoc_array<i32, !simulation.logic<4>, true, false>,
         i32, !simulation.logic<4>) -> ()
      %read = simulation.assoc.read %array, %key :
        (!simulation.assoc_array<i32, !simulation.logic<4>, true, false>,
         i32) -> !simulation.logic<4>
      simulation.assoc.set_default %array, %read :
        (!simulation.assoc_array<i32, !simulation.logic<4>, true, false>,
         !simulation.logic<4>) -> ()
      %next, %valid = simulation.assoc.traverse %array, %key {
        direction = #simulation.assoc_traversal_direction<forward>, endpoint = false
      } : (!simulation.assoc_array<i32, !simulation.logic<4>, true, false>,
           i32) -> (i32, i1)
      simulation.assoc.delete %array, %next :
        (!simulation.assoc_array<i32, !simulation.logic<4>, true, false>,
         i32) -> ()
      simulation.return
    }

    // Integral associative indices are not limited to a host word. Exercise
    // lowering of a key whose distinguishing bit lies above bit 64, including
    // traversal output and an escaping element reference.
    simulation.func @wide_assoc(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %owner: !simulation.argument_ref<!simulation.assoc_array<!simulation.logic<129>, i32, true, false>> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %key = simulation.logic.constant 18446744073709551616 : i129, 0 : i129 :
        !simulation.logic<129>
      %value = arith.constant 42 : i32
      %array = simulation.assoc.create {
        alignment = 1 : i64,
        bit_width = 32 : i64,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<bits>,
        key_kind = #simulation.assoc_key_kind<signed>,
        key_width = 129 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 3 : i64,
        value_size = 4 : i64
      } : () -> !simulation.assoc_array<!simulation.logic<129>, i32, true, false>
      simulation.assoc.write %array, %key, %value :
        (!simulation.assoc_array<!simulation.logic<129>, i32, true, false>,
         !simulation.logic<129>, i32) -> ()
      %read = simulation.assoc.read %array, %key :
        (!simulation.assoc_array<!simulation.logic<129>, i32, true, false>,
         !simulation.logic<129>) -> i32
      %exists = simulation.assoc.exists %array, %key :
        (!simulation.assoc_array<!simulation.logic<129>, i32, true, false>,
         !simulation.logic<129>) -> i1
      %first, %valid = simulation.assoc.traverse %array, %key {
        direction = #simulation.assoc_traversal_direction<forward>, endpoint = true
      } : (!simulation.assoc_array<!simulation.logic<129>, i32, true, false>,
           !simulation.logic<129>) -> (!simulation.logic<129>, i1)
      %path = simulation.reference_path.assoc %ctx, %array[%key] watching %owner :
        (!simulation.context,
         !simulation.assoc_array<!simulation.logic<129>, i32, true, false>,
         !simulation.logic<129>,
         !simulation.argument_ref<!simulation.assoc_array<!simulation.logic<129>, i32, true, false>>) ->
        !simulation.reference_path<i32>
      simulation.assoc.delete %array, %first :
        (!simulation.assoc_array<!simulation.logic<129>, i32, true, false>,
         !simulation.logic<129>) -> ()
      simulation.return
    }

    simulation.func @class_assoc(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %key: !simulation.class_handle<@Key> {simulation.capture_kind = 1 : i32},
        %owner: !simulation.argument_ref<!simulation.assoc_array<!simulation.class_handle<@Key>, i32, false, false>> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 1 : i32} {
      %value = arith.constant 7 : i32
      %array = simulation.assoc.create {
        alignment = 1 : i64,
        bit_width = 32 : i64,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<bits>,
        key_kind = #simulation.assoc_key_kind<class>,
        key_width = 0 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 4 : i64,
        value_size = 4 : i64
      } : () -> !simulation.assoc_array<!simulation.class_handle<@Key>, i32, false, false>
      simulation.assoc.write %array, %key, %value :
        (!simulation.assoc_array<!simulation.class_handle<@Key>, i32, false, false>,
         !simulation.class_handle<@Key>, i32) -> ()
      %first, %valid = simulation.assoc.traverse %array, %key {
        direction = #simulation.assoc_traversal_direction<forward>, endpoint = true
      } : (!simulation.assoc_array<!simulation.class_handle<@Key>, i32, false, false>,
           !simulation.class_handle<@Key>) -> (!simulation.class_handle<@Key>, i1)
      %path = simulation.reference_path.assoc %ctx, %array[%key] watching %owner :
        (!simulation.context,
         !simulation.assoc_array<!simulation.class_handle<@Key>, i32, false, false>,
         !simulation.class_handle<@Key>,
         !simulation.argument_ref<!simulation.assoc_array<!simulation.class_handle<@Key>, i32, false, false>>) ->
        !simulation.reference_path<i32>
      simulation.assoc.delete %array, %first :
        (!simulation.assoc_array<!simulation.class_handle<@Key>, i32, false, false>,
         !simulation.class_handle<@Key>) -> ()
      simulation.return
    }

    simulation.func @process_assoc(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %key: !simulation.process {simulation.capture_kind = 1 : i32},
        %owner: !simulation.argument_ref<!simulation.assoc_array<!simulation.process, i32, false, false>> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 1 : i32} {
      %value = arith.constant 9 : i32
      %array = simulation.assoc.create {
        alignment = 1 : i64,
        bit_width = 32 : i64,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<bits>,
        key_kind = #simulation.assoc_key_kind<process>,
        key_width = 0 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 5 : i64,
        value_size = 4 : i64
      } : () -> !simulation.assoc_array<!simulation.process, i32, false, false>
      simulation.assoc.write %array, %key, %value :
        (!simulation.assoc_array<!simulation.process, i32, false, false>,
         !simulation.process, i32) -> ()
      %first, %valid = simulation.assoc.traverse %array, %key {
        direction = #simulation.assoc_traversal_direction<forward>, endpoint = true
      } : (!simulation.assoc_array<!simulation.process, i32, false, false>,
           !simulation.process) -> (!simulation.process, i1)
      %path = simulation.reference_path.assoc %ctx, %array[%key] watching %owner :
        (!simulation.context,
         !simulation.assoc_array<!simulation.process, i32, false, false>,
         !simulation.process,
         !simulation.argument_ref<!simulation.assoc_array<!simulation.process, i32, false, false>>) ->
        !simulation.reference_path<i32>
      simulation.assoc.delete %array, %first :
        (!simulation.assoc_array<!simulation.process, i32, false, false>,
         !simulation.process) -> ()
      simulation.return
    }
  }
}

// CHECK: simulation.assoc.create
// CHECK: simulation.assoc.write
// CHECK: simulation.assoc.read
// CHECK: simulation.assoc.exists
// CHECK: simulation.assoc.set_default
// CHECK: simulation.assoc.traverse
// CHECK: simulation.reference_path.assoc
// CHECK: simulation.assoc.delete
// CHECK: simulation.func @process_assoc
// CHECK: key_kind = #simulation.assoc_key_kind<process>
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
