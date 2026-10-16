// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-instrument-vpi-frames))' -o %t
// RUN: FileCheck %s < %t
// RUN: obelisk-opt %t --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: obelisk-opt %t --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// A control token is an internal physical result. Every implementation and
// pure declaration of a virtual signature agrees, while retained source ABI
// facts keep their original visible result exactly.
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @abi {
    simulation.scope.decl 0
    simulation.class.decl @Base id 1 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.decl @Child id 2 extends @Base {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @Other id 3 extends @Base {is_abstract = false, is_final = false, is_interface = false}
    simulation.code_unit.decl 1 in 0 function hierarchy "Child.f"
    simulation.code_unit.decl 2 in 0 function hierarchy "Other.f"
    simulation.code_unit.decl 3 in 0 function hierarchy "caller"
    simulation.code_unit.decl 4 in 0 function hierarchy "direct"
    simulation.vpi_callable.decl 4 signature (!simulation.class_handle<@Child>) -> i32 directions [0]
    simulation.vpi_callable.decl 1 signature () -> i32 directions []
    simulation.vpi_callable.decl 2 signature () -> i32 directions []
    simulation.vpi_callable.decl 3 signature (!simulation.class_handle<@Base>) -> i32 directions [0]
    simulation.class.method @Base_f of @Base slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@Base>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Child_f of @Child slot 0 signature_id 17 implemented_by @child : (!simulation.context, !simulation.class_handle<@Child>) -> i32 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Other_f of @Other slot 0 signature_id 17 implemented_by @other : (!simulation.context, !simulation.class_handle<@Other>) -> i32 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.func @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Child> {simulation.capture_kind = 1 : i32}) -> i32 attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %selected = simulation.control.disable_selected 7 {hierarchical = true}
      %zero = arith.constant 0 : i32
      simulation.control.nonlocal_exit 7 selected %selected
      simulation.return %zero : i32
    }
    simulation.func @other(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Other> {simulation.capture_kind = 1 : i32}) -> i32 attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 23 : i32
      simulation.return %value : i32
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) -> i32 attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %value = simulation.class.virtual_call %this [@Base_f] slot 0 signature_id 17() : (!simulation.class_handle<@Base>) -> i32
      simulation.return %value : i32
    }
    simulation.func @direct(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Child> {simulation.capture_kind = 1 : i32}) -> i32 attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %value = simulation.call @child(%ctx, %this) : (!simulation.context, !simulation.class_handle<@Child>) -> i32
      simulation.return %value : i32
    }
  }
}
// CHECK: simulation.vpi_callable.decl 1 signature () -> i32 directions []
// CHECK: simulation.vpi_callable.decl 2 signature () -> i32 directions []
// CHECK: simulation.vpi_callable.decl 3 signature (!simulation.class_handle<@Base>) -> i32 directions [0]
// CHECK-DAG: simulation.class.method @Base_f {{.*}} -> (i32, i64)
// CHECK-DAG: simulation.class.method @Child_f {{.*}} -> (i32, i64)
// CHECK-DAG: simulation.class.method @Other_f {{.*}} -> (i32, i64)
// CHECK-LABEL: simulation.func @child
// CHECK-SAME: -> (i32, i64)
// CHECK: %[[SELECTED:.*]] = simulation.control.disable_selected 7
// CHECK: simulation.return %{{.*}}, %[[SELECTED]] : i32, i64
// CHECK-LABEL: simulation.func @other
// CHECK-SAME: -> (i32, i64)
// CHECK: %[[NORMAL:.*]] = arith.constant 0 : i64
// CHECK: simulation.return %{{.*}}, %[[NORMAL]] : i32, i64
// CHECK-LABEL: simulation.func @caller
// CHECK-SAME: -> (i32, i64)
// CHECK: %[[RESULT:.*]]:2 = simulation.class.virtual_call {{.*}} -> (i32, i64)
// CHECK: arith.cmpi eq, %[[RESULT]]#1
// CHECK: simulation.return %{{.*}}, %[[RESULT]]#1 : i32, i64
// CHECK-NOT: simulation.control.nonlocal_exit
// CHECK-LABEL: simulation.func @direct
// CHECK-SAME: -> (i32, i64)
// CHECK: %[[DIRECT:.*]]:2 = simulation.call @child{{.*}} -> (i32, i64)
// CHECK: arith.cmpi eq, %[[DIRECT]]#1
// CHECK: simulation.return %{{.*}}, %[[DIRECT]]#1 : i32, i64
