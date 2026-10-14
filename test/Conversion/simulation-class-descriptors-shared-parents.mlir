// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-translate %t.mlir --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: not obelisk-opt %s '--test-obelisk-edit-attributes=op=simulation.class.method match={sym_name = "C0_1"} erase=true' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISSING

// IEEE 8.26: each join J<i> inherits two growing parent chains L<i> and R<i>,
// and C<i> also inherits the earlier joins through its base. Descriptors keep
// only direct bindings and parents in both backends: C2 binds J2 alone
// (instead of the nine interfaces it reaches), J2 binds and lists its two
// parents, and the chains list one parent each.
// CHECK-LABEL: @C2.__obelisk_interfaces(){{.*}}: !llvm.array<1 x
// CHECK: llvm.mlir.addressof @J2.__obelisk_interface_bases
// CHECK-DAG: @C1.__obelisk_interfaces(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @C0.__obelisk_interfaces(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @C2.__obelisk_methods(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @C1.__obelisk_methods(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @C0.__obelisk_methods(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @J2.__obelisk_interfaces(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @J1.__obelisk_interfaces(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @J0.__obelisk_interfaces(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @J2.__obelisk_interface_bases(){{.*}}: !llvm.array<2 x ptr>
// CHECK-DAG: @J1.__obelisk_interface_bases(){{.*}}: !llvm.array<2 x ptr>
// CHECK-DAG: @J0.__obelisk_interface_bases(){{.*}}: !llvm.array<2 x ptr>
// CHECK-DAG: @L2.__obelisk_interface_bases(){{.*}}: !llvm.array<1 x ptr>
// CHECK-DAG: @L1.__obelisk_interface_bases(){{.*}}: !llvm.array<1 x ptr>
// CHECK-DAG: @R2.__obelisk_interface_bases(){{.*}}: !llvm.array<1 x ptr>
// CHECK-DAG: @R1.__obelisk_interface_bases(){{.*}}: !llvm.array<1 x ptr>
// CHECK-DAG: @L2.__obelisk_interface_signatures(){{.*}}: !llvm.array<1 x i64>
// CHECK-DAG: @R0.__obelisk_interface_signatures(){{.*}}: !llvm.array<1 x i64>

// An inherited interface method without an implementation is rejected.
// MISSING: error: concrete class does not implement interface R0

module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"} {
  simulation.design @parents {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.class.decl @L2 id 5 implements [@L1] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @L2_method of @L2 slot 4294967295 signature_id 1004 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@L2>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @R2 id 6 implements [@R1] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @R2_method of @R2 slot 4294967295 signature_id 1005 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@R2>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @J2 id 9 implements [@L2, @R2] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.decl @C2 id 12 extends @C1 implements [@J2] {is_abstract = false, is_final = false, is_interface = false}
    simulation.code_unit.decl 6 in 0 function hierarchy "C2.0"
    simulation.class.method @C2_0 of @C2 slot 4 signature_id 1004 implemented_by @c2_0 : (!simulation.context, !simulation.class_handle<@C2>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.func @c2_0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C2> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 6 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 6 : i64
      simulation.return %value : i64
    }
    simulation.code_unit.decl 7 in 0 function hierarchy "C2.1"
    simulation.class.method @C2_1 of @C2 slot 5 signature_id 1005 implemented_by @c2_1 : (!simulation.context, !simulation.class_handle<@C2>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.func @c2_1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C2> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 7 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i64
      simulation.return %value : i64
    }
    simulation.class.decl @L1 id 3 implements [@L0] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @L1_method of @L1 slot 4294967295 signature_id 1002 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@L1>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @R1 id 4 implements [@R0] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @R1_method of @R1 slot 4294967295 signature_id 1003 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@R1>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @J1 id 8 implements [@L1, @R1] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.decl @C1 id 11 extends @C0 implements [@J1] {is_abstract = false, is_final = false, is_interface = false}
    simulation.code_unit.decl 4 in 0 function hierarchy "C1.0"
    simulation.class.method @C1_0 of @C1 slot 2 signature_id 1002 implemented_by @c1_0 : (!simulation.context, !simulation.class_handle<@C1>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.func @c1_0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C1> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 4 : i64
      simulation.return %value : i64
    }
    simulation.code_unit.decl 5 in 0 function hierarchy "C1.1"
    simulation.class.method @C1_1 of @C1 slot 3 signature_id 1003 implemented_by @c1_1 : (!simulation.context, !simulation.class_handle<@C1>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.func @c1_1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C1> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 5 : i64
      simulation.return %value : i64
    }
    simulation.class.decl @L0 id 1 {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @L0_method of @L0 slot 4294967295 signature_id 1000 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@L0>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @R0 id 2 {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @R0_method of @R0 slot 4294967295 signature_id 1001 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@R0>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @J0 id 7 implements [@L0, @R0] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.decl @C0 id 10 implements [@J0] {is_abstract = false, is_final = false, is_interface = false}
    simulation.code_unit.decl 2 in 0 function hierarchy "C0.0"
    simulation.class.method @C0_0 of @C0 slot 0 signature_id 1000 implemented_by @c0_0 : (!simulation.context, !simulation.class_handle<@C0>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.func @c0_0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C0> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 2 : i64
      simulation.return %value : i64
    }
    simulation.code_unit.decl 3 in 0 function hierarchy "C0.1"
    simulation.class.method @C0_1 of @C0 slot 1 signature_id 1001 implemented_by @c0_1 : (!simulation.context, !simulation.class_handle<@C0>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.func @c0_1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C0> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 3 : i64
      simulation.return %value : i64
    }
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      simulation.return
    }
  }
}
