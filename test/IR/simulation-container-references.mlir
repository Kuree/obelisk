// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @container_references {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.capture"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.managed_ref"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.equivalent_packed_ref"

    simulation.func @capture(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %queue: !simulation.queue<i64, 0> {simulation.capture_kind = 1 : i32},
        %index: i64 {simulation.capture_kind = 1 : i32},
        %owner: !simulation.argument_ref<!simulation.queue<i64, 0>> {simulation.capture_kind = 1 : i32},
        %text: !simulation.string {simulation.capture_kind = 1 : i32},
        %text_owner: !simulation.argument_ref<!simulation.string> {simulation.capture_kind = 1 : i32},
        %dynamic: !simulation.dynamic_array<i32> {simulation.capture_kind = 1 : i32},
        %fixed: !simulation.unpacked_array<1 : 4 x i32> {simulation.capture_kind = 1 : i32},
        %fixed_owner: !simulation.argument_ref<!simulation.unpacked_array<1 : 4 x i32>> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %path = simulation.reference_path.index %ctx, %queue[%index] watching %owner :
        (!simulation.context, !simulation.queue<i64, 0>, i64,
         !simulation.argument_ref<!simulation.queue<i64, 0>>) ->
        !simulation.reference_path<i64>
      %reference = simulation.argument_ref.from_path %path :
        !simulation.reference_path<i64> ->
        !simulation.argument_ref<i64>
      simulation.container.swap %queue[%index, %index] :
        !simulation.queue<i64, 0>
      %character_path = simulation.reference_path.string_character
        %ctx, %text[%index] watching %text_owner :
        (!simulation.context, !simulation.string, i64,
         !simulation.argument_ref<!simulation.string>) ->
        !simulation.reference_path<i8>
      %aggregate_path = simulation.reference_path.aggregate_element
          %ctx, %fixed_owner, %index {
            alignment = 1 : i64,
            bit_width = 32 : i64,
            element_flags = #simulation.element_flags<none>,
            element_kind = #simulation.element_kind<bits>,
            element_span = 32 : i64,
            left = 1 : i64,
            right = 4 : i64,
            trace_kinds = array<i32>,
            trace_offsets = array<i64>,
            type_id = 1 : i64,
            value_size = 4 : i64
          } : (!simulation.context,
               !simulation.argument_ref<!simulation.unpacked_array<1 : 4 x i32>>,
               i64) -> !simulation.reference_path<i32>
      simulation.container.import_fixed %dynamic, %fixed {
        element_span = 32 : i64
      } : (!simulation.dynamic_array<i32>,
           !simulation.unpacked_array<1 : 4 x i32>) -> ()
      %updated = simulation.container.export_fixed %dynamic {
        element_span = 32 : i64
      } : (!simulation.dynamic_array<i32>) ->
          !simulation.unpacked_array<1 : 4 x i32>
      simulation.return
    }

    simulation.func @managed_ref(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %reference: !simulation.argument_ref<!simulation.assoc_array<i32, i64, true, false>>
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %value = simulation.argument_ref.load %reference :
          !simulation.argument_ref<!simulation.assoc_array<i32, i64, true, false>> ->
          !simulation.assoc_array<i32, i64, true, false>
      simulation.argument_ref.store %value to %reference :
          !simulation.assoc_array<i32, i64, true, false>,
          !simulation.argument_ref<!simulation.assoc_array<i32, i64, true, false>>
      simulation.return
    }

    // IEEE 1800-2017 6.22.2(c) and 13.5.2 allow a ref formal to view an
    // equivalent packed type while preserving the original alias.
    simulation.func @equivalent_packed_ref(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %reference: !simulation.argument_ref<i32>
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %view = simulation.argument_ref.retype %reference :
          !simulation.argument_ref<i32> ->
          !simulation.argument_ref<!simulation.packed_array<31 : 0 x i1>>
      %value = simulation.argument_ref.load %view :
          !simulation.argument_ref<!simulation.packed_array<31 : 0 x i1>> ->
          !simulation.packed_array<31 : 0 x i1>
      simulation.argument_ref.store %value to %view :
          !simulation.packed_array<31 : 0 x i1>,
          !simulation.argument_ref<!simulation.packed_array<31 : 0 x i1>>
      simulation.return
    }
  }
}

// CHECK: simulation.reference_path.index
// CHECK: !simulation.reference_path<i64>
// CHECK: simulation.argument_ref.from_path
// CHECK: simulation.container.swap
// CHECK: simulation.reference_path.string_character
// CHECK: simulation.reference_path.aggregate_element
// CHECK: simulation.container.import_fixed
// CHECK: simulation.container.export_fixed
// CHECK: simulation.argument_ref.load
// CHECK: simulation.argument_ref.store
// CHECK: simulation.argument_ref.retype
// NATIVE: llvm.call @obelisk_rt_v1_reference_path_index_create
// NATIVE: llvm.call @obelisk_rt_v1_container_swap
// NATIVE: llvm.call @obelisk_rt_v1_reference_path_string_character_create
// NATIVE: llvm.call @obelisk_rt_v1_reference_path_aggregate_element_create
// NATIVE: llvm.call @obelisk_rt_v1_container_import_fixed
// NATIVE: llvm.call @obelisk_rt_v1_container_export_fixed
// NATIVE: %[[LOAD_KIND:.*]] = llvm.mlir.constant(1 : i32) : i32
// NATIVE-NEXT: llvm.call @obelisk_rt_v1_argument_ref_load
// NATIVE-SAME: %[[LOAD_KIND]]
// NATIVE: %[[STORE_KIND:.*]] = llvm.mlir.constant(1 : i32) : i32
// NATIVE: llvm.call @obelisk_rt_v1_argument_ref_store
// NATIVE-SAME: %[[STORE_KIND]]
// NATIVE-NOT: simulation.argument_ref.retype
// BYTECODE: obelisk.bytecode.image
