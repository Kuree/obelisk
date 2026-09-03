// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @container_references {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.capture"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.managed_ref"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.equivalent_packed_ref"

    obelisk_sim.func @capture(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %queue: !obelisk_sim.queue<i64, 0> {obelisk_sim.capture_kind = 1 : i32},
        %index: i64 {obelisk_sim.capture_kind = 1 : i32},
        %owner: !obelisk_sim.argument_ref<!obelisk_sim.queue<i64, 0>> {obelisk_sim.capture_kind = 1 : i32},
        %text: !obelisk_sim.string {obelisk_sim.capture_kind = 1 : i32},
        %text_owner: !obelisk_sim.argument_ref<!obelisk_sim.string> {obelisk_sim.capture_kind = 1 : i32},
        %dynamic: !obelisk_sim.dynamic_array<i32> {obelisk_sim.capture_kind = 1 : i32},
        %fixed: !obelisk_sim.unpacked_array<1 : 4 x i32> {obelisk_sim.capture_kind = 1 : i32},
        %fixed_owner: !obelisk_sim.argument_ref<!obelisk_sim.unpacked_array<1 : 4 x i32>> {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %path = obelisk_sim.reference_path.index %ctx, %queue[%index] watching %owner :
        (!obelisk_sim.context, !obelisk_sim.queue<i64, 0>, i64,
         !obelisk_sim.argument_ref<!obelisk_sim.queue<i64, 0>>) ->
        !obelisk_sim.reference_path<i64>
      %reference = obelisk_sim.argument_ref.from_path %path :
        !obelisk_sim.reference_path<i64> ->
        !obelisk_sim.argument_ref<i64>
      obelisk_sim.container.swap %queue[%index, %index] :
        !obelisk_sim.queue<i64, 0>
      %character_path = obelisk_sim.reference_path.string_character
        %ctx, %text[%index] watching %text_owner :
        (!obelisk_sim.context, !obelisk_sim.string, i64,
         !obelisk_sim.argument_ref<!obelisk_sim.string>) ->
        !obelisk_sim.reference_path<i8>
      %aggregate_path = "obelisk_sim.reference_path.aggregate_element"(
          %ctx, %fixed_owner, %index) {
            alignment = 1 : i64,
            bit_width = 32 : i64,
            element_flags = 0 : i32,
            element_kind = 1 : i32,
            element_span = 32 : i64,
            left = 1 : i64,
            right = 4 : i64,
            trace_kinds = array<i32>,
            trace_offsets = array<i64>,
            type_id = 1 : i64,
            value_size = 4 : i64
          } : (!obelisk_sim.context,
               !obelisk_sim.argument_ref<!obelisk_sim.unpacked_array<1 : 4 x i32>>,
               i64) -> !obelisk_sim.reference_path<i32>
      "obelisk_sim.container.import_fixed"(%dynamic, %fixed) {
        element_span = 32 : i64
      } : (!obelisk_sim.dynamic_array<i32>,
           !obelisk_sim.unpacked_array<1 : 4 x i32>) -> ()
      %updated = "obelisk_sim.container.export_fixed"(%dynamic) {
        element_span = 32 : i64
      } : (!obelisk_sim.dynamic_array<i32>) ->
          !obelisk_sim.unpacked_array<1 : 4 x i32>
      obelisk_sim.return
    }

    obelisk_sim.func @managed_ref(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %reference: !obelisk_sim.argument_ref<!obelisk_sim.assoc_array<i32, i64, true, false>>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %value = obelisk_sim.argument_ref.load %reference :
          !obelisk_sim.argument_ref<!obelisk_sim.assoc_array<i32, i64, true, false>> ->
          !obelisk_sim.assoc_array<i32, i64, true, false>
      obelisk_sim.argument_ref.store %value to %reference :
          !obelisk_sim.assoc_array<i32, i64, true, false>,
          !obelisk_sim.argument_ref<!obelisk_sim.assoc_array<i32, i64, true, false>>
      obelisk_sim.return
    }

    // IEEE 1800-2017 6.22.2(c) and 13.5.2 allow a ref formal to view an
    // equivalent packed type while preserving the original alias.
    obelisk_sim.func @equivalent_packed_ref(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %reference: !obelisk_sim.argument_ref<i32>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %view = obelisk_sim.argument_ref.retype %reference :
          !obelisk_sim.argument_ref<i32> ->
          !obelisk_sim.argument_ref<!obelisk_sim.packed_array<31 : 0 x i1>>
      %value = obelisk_sim.argument_ref.load %view :
          !obelisk_sim.argument_ref<!obelisk_sim.packed_array<31 : 0 x i1>> ->
          !obelisk_sim.packed_array<31 : 0 x i1>
      obelisk_sim.argument_ref.store %value to %view :
          !obelisk_sim.packed_array<31 : 0 x i1>,
          !obelisk_sim.argument_ref<!obelisk_sim.packed_array<31 : 0 x i1>>
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.reference_path.index
// CHECK: !obelisk_sim.reference_path<i64>
// CHECK: obelisk_sim.argument_ref.from_path
// CHECK: obelisk_sim.container.swap
// CHECK: obelisk_sim.reference_path.string_character
// CHECK: obelisk_sim.reference_path.aggregate_element
// CHECK: obelisk_sim.container.import_fixed
// CHECK: obelisk_sim.container.export_fixed
// CHECK: obelisk_sim.argument_ref.load
// CHECK: obelisk_sim.argument_ref.store
// CHECK: obelisk_sim.argument_ref.retype
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
// NATIVE-NOT: obelisk_sim.argument_ref.retype
// BYTECODE: obelisk.bytecode.image
