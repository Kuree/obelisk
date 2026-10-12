// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// Native and bytecode lowering emit the same sparse descriptors. Each class
// declares one handle and one sparse virtual method; each interface adds one
// parent and one method. Descriptor arrays hold direct declarations only, so
// their sizes do not grow with inheritance depth: C3 stores one method and
// reaches C2's entries through its base descriptor. Declaration order is
// reversed so emission must order bases first.
// CHECK-LABEL: @C3.__obelisk_class_descriptor()
// CHECK: llvm.mlir.addressof @C2.__obelisk_class_descriptor
// CHECK: llvm.mlir.addressof @C3.__obelisk_methods
// CHECK: llvm.return
// CHECK-DAG: @C3.__obelisk_methods(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @C2.__obelisk_methods(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @C1.__obelisk_methods(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @C0.__obelisk_methods(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @C3.__obelisk_trace_entries(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @C2.__obelisk_trace_entries(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @C1.__obelisk_trace_entries(){{.*}}: !llvm.array<2 x
// CHECK-DAG: @C0.__obelisk_trace_entries(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @C0.__obelisk_interfaces(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @I3.__obelisk_interfaces(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @I2.__obelisk_interfaces(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @I1.__obelisk_interfaces(){{.*}}: !llvm.array<1 x
// CHECK-DAG: @I3.__obelisk_interface_bases(){{.*}}: !llvm.array<1 x ptr>
// CHECK-DAG: @I2.__obelisk_interface_bases(){{.*}}: !llvm.array<1 x ptr>
// CHECK-DAG: @I1.__obelisk_interface_bases(){{.*}}: !llvm.array<1 x ptr>
// CHECK-DAG: @I3.__obelisk_interface_signatures(){{.*}}: !llvm.array<1 x i64>
// CHECK-DAG: @I2.__obelisk_interface_signatures(){{.*}}: !llvm.array<1 x i64>
// CHECK-DAG: @I1.__obelisk_interface_signatures(){{.*}}: !llvm.array<1 x i64>
// CHECK-DAG: @I0.__obelisk_interface_signatures(){{.*}}: !llvm.array<1 x i64>

module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"} {
  simulation.design @linear {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.class.decl @I3 id 4 implements [@I2] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @I3_method of @I3 slot 4294967295 signature_id 1003 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@I3>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @I2 id 3 implements [@I1] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @I2_method of @I2 slot 4294967295 signature_id 1002 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@I2>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @I1 id 2 implements [@I0] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @I1_method of @I1 slot 4294967295 signature_id 1001 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@I1>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @I0 id 1 {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @I0_method of @I0 slot 4294967295 signature_id 1000 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@I0>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @C3 id 8 extends @C2 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.field @C3_next of @C3 at 0 : !simulation.class_handle<@C0> {is_static = false, is_weak = false}
    simulation.class.method @C3_method of @C3 slot 1048579 signature_id 1003 : (!simulation.context, !simulation.class_handle<@C3>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @C2 id 7 extends @C1 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.field @C2_next of @C2 at 0 : !simulation.class_handle<@C0> {is_static = false, is_weak = false}
    simulation.class.method @C2_method of @C2 slot 1048578 signature_id 1002 : (!simulation.context, !simulation.class_handle<@C2>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @C1 id 6 extends @C0 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.field @C1_next of @C1 at 0 : !simulation.class_handle<@C0> {is_static = false, is_weak = false}
    simulation.class.method @C1_method of @C1 slot 1048577 signature_id 1001 : (!simulation.context, !simulation.class_handle<@C1>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.decl @C0 id 5 implements [@I3] {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.field @C0_next of @C0 at 0 : !simulation.class_handle<@C0> {is_static = false, is_weak = false}
    simulation.class.method @C0_method of @C0 slot 1048576 signature_id 1000 : (!simulation.context, !simulation.class_handle<@C0>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      simulation.return
    }
  }
}
