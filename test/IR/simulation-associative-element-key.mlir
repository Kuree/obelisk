// RUN: talos-opt %s | talos-opt | FileCheck %s
// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: talos-opt %s --encode-talos-sim-to-bytecode='vpi=off' --test-talos-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2023 7.8.5: an index of any type with an equality operator. The
// key `struct { logic [3:0] l; string s; }` is four-state with a string
// member, so it travels as 16-byte value and unknown planes with a traced
// string word at byte 8.
!key = !simulation.unpacked_struct<[
  #simulation.field<name = "l", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "s", type = !simulation.string, ordinal = 1, packedOffset = 0>]>
!array = !simulation.assoc_array<!key, i32, false, false>
!sequence = !simulation.assoc_array<!simulation.dynamic_array<i32>, i32, false, false>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @element_keys {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.struct_key"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.sequence_key"

    simulation.func @struct_key(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %key: !key {simulation.capture_kind = 1 : i32},
        %owner: !simulation.argument_ref<!array> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %value = arith.constant 7 : i32
      %array = simulation.assoc.create {
        alignment = 1 : i64,
        bit_width = 32 : i64,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<bits>,
        key_kind = #simulation.assoc_key_kind<element>,
        key_width = 128 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 1 : i64,
        value_size = 4 : i64
      } : () -> !array
      simulation.assoc.write %array, %key, %value : (!array, !key, i32) -> ()
      %read = simulation.assoc.read %array, %key : (!array, !key) -> i32
      %exists = simulation.assoc.exists %array, %key : (!array, !key) -> i1
      %next, %valid = simulation.assoc.traverse %array, %key {
        direction = #simulation.assoc_traversal_direction<forward>, endpoint = false
      } : (!array, !key) -> (!key, i1)
      %path = simulation.reference_path.assoc %ctx, %array[%key] watching %owner :
        (!simulation.context, !array, !key, !simulation.argument_ref<!array>) ->
        !simulation.reference_path<i32>
      simulation.assoc.delete %array, %next : (!array, !key) -> ()
      simulation.return
    }

    simulation.func @sequence_key(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %key: !simulation.dynamic_array<i32> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %value = arith.constant 9 : i32
      %array = simulation.assoc.create {
        alignment = 1 : i64,
        bit_width = 32 : i64,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<bits>,
        key_kind = #simulation.assoc_key_kind<element>,
        key_width = 64 : i64,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 1 : i64,
        value_size = 4 : i64
      } : () -> !sequence
      simulation.assoc.write %array, %key, %value :
        (!sequence, !simulation.dynamic_array<i32>, i32) -> ()
      %first, %valid = simulation.assoc.traverse %array, %key {
        direction = #simulation.assoc_traversal_direction<forward>, endpoint = true
      } : (!sequence, !simulation.dynamic_array<i32>) ->
          (!simulation.dynamic_array<i32>, i1)
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @struct_key
// CHECK: simulation.assoc.create {{.*}}key_kind = #simulation.assoc_key_kind<element>, key_width = 128
// CHECK-LABEL: simulation.func @sequence_key
// CHECK: simulation.assoc.create {{.*}}key_kind = #simulation.assoc_key_kind<element>, key_width = 64

// The struct key's equality plan compares l's four bits as four-state and s
// by value: {MAGIC | VERSION << 32, 2 records}, then {BITS, 0, 4, 1} and
// {STRING, 64, 0, 0}. Its trace names the string word at byte 8. The
// dynamic-array key's plan is a SEQUENCE of 4-byte elements whose body
// compares 32 bits.
// NATIVE-DAG: llvm.mlir.global internal constant @__talos_assoc_key_plan_[[KEY:[0-9]+]]("AKPK\01\00\00\00\02\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00@\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00")
// NATIVE-DAG: llvm.mlir.global internal constant @__talos_element_trace_[[KEY]]("\08\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00")
// NATIVE-DAG: llvm.mlir.global internal constant @__talos_assoc_key_plan_{{[0-9]+}}("AKPK\01\00\00\00\02\00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\00\00\00\00\00\00\00\00")
// NATIVE-LABEL: llvm.func @struct_key
// NATIVE: %[[KEY_TRACE:.*]] = llvm.mlir.addressof @__talos_element_trace_[[KEY]]
// NATIVE: %[[PLAN:.*]] = llvm.mlir.addressof @__talos_assoc_key_plan_[[KEY]]
// NATIVE: llvm.call @talos_rt_v1_assoc_create_element_keyed({{.*}}, %[[KEY_TRACE]], %{{.*}}, %[[PLAN]], %{{.*}}, %{{.*}})
// NATIVE: llvm.call @talos_rt_v1_assoc_write_checked
// NATIVE: llvm.call @talos_rt_v1_assoc_read_checked
// NATIVE: llvm.call @talos_rt_v1_assoc_exists
// NATIVE: llvm.call @talos_rt_v1_assoc_next
// NATIVE: llvm.call @talos_rt_v1_reference_path_assoc_create
// NATIVE: llvm.call @talos_rt_v1_assoc_delete
// NATIVE-LABEL: llvm.func @sequence_key
// NATIVE: llvm.call @talos_rt_v1_assoc_create_element_keyed
// NATIVE: llvm.call @talos_rt_v1_assoc_first
// NATIVE-NOT: unrealized_conversion_cast

// An element-keyed creation carries the key descriptor, trace and plan.
// BYTECODE: intrinsic {{.*}}id=0x00010438 inputs=17 outputs=1 flags=0
// BYTECODE-NOT: id=0x00010438 inputs=9
