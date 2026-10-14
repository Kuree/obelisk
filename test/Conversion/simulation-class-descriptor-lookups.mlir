// RUN: obelisk-opt %s --test-obelisk-replicate --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=VIRTUAL < %t.mlir
// RUN: FileCheck %s --check-prefix=INTERFACE < %t.mlir
// RUN: obelisk-translate %t.mlir --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %s --test-obelisk-replicate --encode-obelisk-sim-to-bytecode='vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=BYTECODE

// IEEE 8.20 and 8.26: class field offsets and virtual and interface dispatch
// metadata are resolved once before parallel native conversion. Forty
// replicated interface/class pairs give 80 implementations and callers, more
// than one 64-function conversion chunk, and every chunk's direct, virtual and
// interface calls use the shared index.
// CHECK-LABEL: llvm.func @use39(
// CHECK: llvm.call @obelisk_rt_v1_object_read
// CHECK: llvm.call @c39(
// CHECK: llvm.mlir.constant(1000 : i64)
// CHECK: llvm.call @obelisk_rt_v1_method_invoke(
// CHECK: llvm.call @obelisk_rt_v1_object_cast
// I39 has class ID 79 and the method is its interface ordinal 0.
// CHECK: llvm.mlir.constant(79 : i64)
// CHECK-NEXT: llvm.mlir.constant(0 : i64)
// CHECK-NEXT: llvm.mlir.constant(1000 : i64)
// CHECK: llvm.call @obelisk_rt_v1_interface_method_invoke(
// VIRTUAL-COUNT-40: llvm.call @obelisk_rt_v1_method_invoke(
// VIRTUAL-NOT: llvm.call @obelisk_rt_v1_method_invoke(
// INTERFACE-COUNT-40: llvm.call @obelisk_rt_v1_interface_method_invoke(
// INTERFACE-NOT: llvm.call @obelisk_rt_v1_interface_method_invoke(

// Bytecode preparation resolves each implementation's owner through the same
// class layout index.
// BYTECODE-LABEL: @C39.__obelisk_methods()
// BYTECODE: llvm.mlir.constant(1000 : i64)
// BYTECODE: llvm.mlir.addressof @C39_method.__obelisk_native_thunk

module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"} {
  simulation.design @lookups {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.class.decl @"I#" id 1 {is_abstract = true, is_final = false, is_interface = true, test.replicate = 40, test.replicate_stride = {id = 2}}
    simulation.class.method @"I#_method" of @"I#" slot 4294967295 signature_id 1000 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@"I#">) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true, test.replicate = 40}
    simulation.class.decl @"C#" id 2 implements [@"I#"] {is_abstract = false, is_final = false, is_interface = false, test.replicate = 40, test.replicate_stride = {id = 2}}
    simulation.class.field @"C#_value" of @"C#" at 0 : i64 {is_static = false, is_weak = false, test.replicate = 40}
    simulation.class.method @"C#_method" of @"C#" slot 0 signature_id 1000 implemented_by @"c#" : (!simulation.context, !simulation.class_handle<@"C#">) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true, test.replicate = 40}
    simulation.code_unit.decl 2 in 0 function hierarchy "c#" {test.replicate = 40, test.replicate_stride = {id = 2}}
    simulation.func @"c#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@"C#"> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32, test.replicate = 40, test.replicate_stride = {code_unit_id = 2}} {
      %value = arith.constant 7 : i64
      simulation.return %value : i64
    }
    simulation.code_unit.decl 3 in 0 function hierarchy "use#" {test.replicate = 40, test.replicate_stride = {id = 2}}
    simulation.func @"use#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@"C#"> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32, test.replicate = 40, test.replicate_stride = {code_unit_id = 2}} {
      %field = simulation.class.field_ref %this[@"C#_value"] : !simulation.class_handle<@"C#"> -> !simulation.managed_ref<i64, @"C#">
      %value = simulation.managed.load %field : !simulation.managed_ref<i64, @"C#"> -> i64
      %ordinary = simulation.class.direct_call @"c#" %this() : (!simulation.class_handle<@"C#">) -> i64
      %direct = simulation.class.virtual_call %this[@"C#_method"] slot 0 signature_id 1000 () : (!simulation.class_handle<@"C#">) -> i64
      %interface = simulation.class.cast %this : !simulation.class_handle<@"C#"> to !simulation.class_handle<@"I#">
      %indirect = simulation.class.virtual_call %interface[@"I#_method"] slot 4294967295 signature_id 1000 () : (!simulation.class_handle<@"I#">) -> i64
      simulation.return %value : i64
    }
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      simulation.return
    }
  }
}
