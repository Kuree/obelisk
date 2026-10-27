// RUN: talos-opt %s | FileCheck %s
// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.ClassCallDispatch
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @class_dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.class.decl @I id 1 {is_abstract = true, is_final = false, is_interface = true}
    simulation.vpi_object.anchor @I_def id 1 type 652 in 1 parent @top ordinal 0 hierarchy "top.I" debug "I" {backing = #simulation.vpi_backing<kind = class, symbol = @I>}
    simulation.vpi_typespec.decl @I_type id 0 in 1 owner @I_def hierarchy "top.I$type" debug "I" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @I, range = [], children = [], childNames = []>}
    simulation.class.decl @Base id 2 implements [@I] {is_abstract = true, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @Base_def id 2 type 652 in 1 parent @top ordinal 1 hierarchy "top.Base" debug "Base" {backing = #simulation.vpi_backing<kind = class, symbol = @Base>}
    simulation.vpi_typespec.decl @Base_type id 1 in 1 owner @Base_def hierarchy "top.Base$type" debug "Base" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @Base, range = [], children = [], childNames = []>}
    simulation.class.decl @Child id 3 extends @Base {is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @Child_def id 3 type 652 in 1 parent @top ordinal 2 hierarchy "top.Child" debug "Child" {backing = #simulation.vpi_backing<kind = class, symbol = @Child>}
    simulation.vpi_typespec.decl @Child_type id 2 in 1 owner @Child_def hierarchy "top.Child$type" debug "Child" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @Child, range = [], children = [], childNames = []>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @Child_type> selector 677 handle ordinal 0 to <kind = symbol, symbol = @Base_type>
    simulation.class.decl @Override id 4 extends @Base {is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @Override_def id 4 type 652 in 1 parent @top ordinal 3 hierarchy "top.Override" debug "Override" {backing = #simulation.vpi_backing<kind = class, symbol = @Override>}
    simulation.vpi_typespec.decl @Override_type id 3 in 1 owner @Override_def hierarchy "top.Override$type" debug "Override" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @Override, range = [], children = [], childNames = []>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @Override_type> selector 677 handle ordinal 0 to <kind = symbol, symbol = @Base_type>
    simulation.class.decl @Pure id 5 extends @Base {is_abstract = true, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @Pure_def id 5 type 652 in 1 parent @top ordinal 4 hierarchy "top.Pure" debug "Pure" {backing = #simulation.vpi_backing<kind = class, symbol = @Pure>}
    simulation.vpi_typespec.decl @Pure_type id 4 in 1 owner @Pure_def hierarchy "top.Pure$type" debug "Pure" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @Pure, range = [], children = [], childNames = []>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @Pure_type> selector 677 handle ordinal 0 to <kind = symbol, symbol = @Base_type>
    simulation.class.decl @Restored id 6 extends @Pure {is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @Restored_def id 6 type 652 in 1 parent @top ordinal 5 hierarchy "top.Restored" debug "Restored" {backing = #simulation.vpi_backing<kind = class, symbol = @Restored>}
    simulation.vpi_typespec.decl @Restored_type id 5 in 1 owner @Restored_def hierarchy "top.Restored$type" debug "Restored" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @Restored, range = [], children = [], childNames = []>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @Restored_type> selector 677 handle ordinal 0 to <kind = symbol, symbol = @Pure_type>
    simulation.class.decl @Unrelated id 7 {is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @Unrelated_def id 7 type 652 in 1 parent @top ordinal 6 hierarchy "top.Unrelated" debug "Unrelated" {backing = #simulation.vpi_backing<kind = class, symbol = @Unrelated>}
    simulation.vpi_typespec.decl @Unrelated_type id 6 in 1 owner @Unrelated_def hierarchy "top.Unrelated$type" debug "Unrelated" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @Unrelated, range = [], children = [], childNames = []>}
    simulation.class.decl @Empty id 8 {is_abstract = true, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @Empty_def id 8 type 652 in 1 parent @top ordinal 7 hierarchy "top.Empty" debug "Empty" {backing = #simulation.vpi_backing<kind = class, symbol = @Empty>}
    simulation.vpi_typespec.decl @Empty_type id 7 in 1 owner @Empty_def hierarchy "top.Empty$type" debug "Empty" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @Empty, range = [], children = [], childNames = []>}
    simulation.class.method @I_low of @I slot 4294967295 signature_id 17 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@I>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Base_low of @Base slot 0 signature_id 17 implemented_by @Base_low_body : (!simulation.context, !simulation.class_handle<@Base>) -> i32 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 101 in 1 function hierarchy "top.Base.Base_low"
    simulation.vpi_object.anchor @Base_low_anchor id 9 type 20 in 1 parent @Base_type ordinal 0 hierarchy "top.Base.Base_low" debug "Base_low" {class_method = @Base_low, backing = #simulation.vpi_backing<kind = code_unit, id = 101 : i64>}
    simulation.func private @Base_low_body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 101 : i64} {
      %zero = arith.constant 0 : i32
      simulation.return %zero : i32
    }
    simulation.class.method @Base_high of @Base slot 5 signature_id 17 implemented_by @Base_high_body : (!simulation.context, !simulation.class_handle<@Base>) -> i32 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 102 in 1 function hierarchy "top.Base.Base_high"
    simulation.vpi_object.anchor @Base_high_anchor id 10 type 20 in 1 parent @Base_type ordinal 1 hierarchy "top.Base.Base_high" debug "Base_high" {class_method = @Base_high, backing = #simulation.vpi_backing<kind = code_unit, id = 102 : i64>}
    simulation.func private @Base_high_body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 102 : i64} {
      %zero = arith.constant 0 : i32
      simulation.return %zero : i32
    }
    simulation.class.method @Override_low of @Override slot 0 signature_id 17 implemented_by @Override_low_body : (!simulation.context, !simulation.class_handle<@Override>) -> i32 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 103 in 1 function hierarchy "top.Override.Override_low"
    simulation.vpi_object.anchor @Override_low_anchor id 11 type 20 in 1 parent @Override_type ordinal 0 hierarchy "top.Override.Override_low" debug "Override_low" {class_method = @Override_low, backing = #simulation.vpi_backing<kind = code_unit, id = 103 : i64>}
    simulation.func private @Override_low_body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Override> {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 103 : i64} {
      %zero = arith.constant 0 : i32
      simulation.return %zero : i32
    }
    simulation.class.method @Pure_low of @Pure slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@Pure>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Restored_low of @Restored slot 0 signature_id 17 implemented_by @Restored_low_body : (!simulation.context, !simulation.class_handle<@Restored>) -> i32 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 104 in 1 function hierarchy "top.Restored.Restored_low"
    simulation.vpi_object.anchor @Restored_low_anchor id 12 type 20 in 1 parent @Restored_type ordinal 0 hierarchy "top.Restored.Restored_low" debug "Restored_low" {class_method = @Restored_low, backing = #simulation.vpi_backing<kind = code_unit, id = 104 : i64>}
    simulation.func private @Restored_low_body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Restored> {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 104 : i64} {
      %zero = arith.constant 0 : i32
      simulation.return %zero : i32
    }
    simulation.class.method @Base_task of @Base slot 7 signature_id 19 implemented_by @Base_task_body : (!simulation.context, !simulation.class_handle<@Base>) -> () {is_final = false, is_pure = false, is_static = false, is_task = true, is_virtual = true}
    simulation.code_unit.decl 105 in 1 task hierarchy "top.Base.Base_task"
    simulation.vpi_object.anchor @Base_task_anchor id 13 type 59 in 1 parent @Base_type ordinal 2 hierarchy "top.Base.Base_task" debug "Base_task" {class_method = @Base_task, backing = #simulation.vpi_backing<kind = code_unit, id = 105 : i64>}
    simulation.func private @Base_task_body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 105 : i64} {
      simulation.return
    }
    simulation.class.method @Override_task of @Override slot 7 signature_id 19 implemented_by @Override_task_body : (!simulation.context, !simulation.class_handle<@Override>) -> () {is_final = false, is_pure = false, is_static = false, is_task = true, is_virtual = true}
    simulation.code_unit.decl 106 in 1 task hierarchy "top.Override.Override_task"
    simulation.vpi_object.anchor @Override_task_anchor id 14 type 59 in 1 parent @Override_type ordinal 1 hierarchy "top.Override.Override_task" debug "Override_task" {class_method = @Override_task, backing = #simulation.vpi_backing<kind = code_unit, id = 106 : i64>}
    simulation.func private @Override_task_body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Override> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 106 : i64} {
      simulation.return
    }
    simulation.class.method @Unrelated_low of @Unrelated slot 0 signature_id 17 implemented_by @Unrelated_low_body : (!simulation.context, !simulation.class_handle<@Unrelated>) -> i32 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 107 in 1 function hierarchy "top.Unrelated.Unrelated_low"
    simulation.vpi_object.anchor @Unrelated_low_anchor id 15 type 20 in 1 parent @Unrelated_type ordinal 0 hierarchy "top.Unrelated.Unrelated_low" debug "Unrelated_low" {class_method = @Unrelated_low, backing = #simulation.vpi_backing<kind = code_unit, id = 107 : i64>}
    simulation.func private @Unrelated_low_body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Unrelated> {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 107 : i64} {
      %zero = arith.constant 0 : i32
      simulation.return %zero : i32
    }
    simulation.class.method @Empty_low of @Empty slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@Empty>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.statement.decl 11 in 44 scope 1 type 3
    simulation.statement_site.decl 111 on 11 phase 0
    simulation.vpi_call.decl @call0 id 20 in 44 scope 1 type 648 name "Base_low" within 11 {is_indirect}
    simulation.vpi_class_call_dispatch @call0 on !simulation.class_handle<@Base> via @Base_low
    simulation.vpi_call.decl @call1 id 21 in 44 scope 1 type 648 name "I_low" within 11 {is_indirect}
    simulation.vpi_class_call_dispatch @call1 on !simulation.class_handle<@I> via @I_low
    simulation.vpi_call.decl @call2 id 22 in 44 scope 1 type 648 name "Base_high" within 11 {is_indirect}
    simulation.vpi_class_call_dispatch @call2 on !simulation.class_handle<@Base> via @Base_high
    simulation.vpi_call.decl @call3 id 23 in 44 scope 1 type 648 name "Pure_low" within 11 {is_indirect}
    simulation.vpi_class_call_dispatch @call3 on !simulation.class_handle<@Pure> via @Pure_low
    simulation.vpi_call.decl @call4 id 24 in 44 scope 1 type 648 name "Empty_low" within 11 {is_indirect}
    simulation.vpi_class_call_dispatch @call4 on !simulation.class_handle<@Empty> via @Empty_low
    simulation.statement.decl 12 in 44 scope 1 type 649
    simulation.vpi_call.decl @call5 id 25 in 44 scope 1 type 649 name "Base_task" statement 12 within 12 {is_indirect}
    simulation.vpi_class_call_dispatch @call5 on !simulation.class_handle<@Base> via @Base_task
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
  }
}
// CHECK: simulation.vpi_class_call_dispatch @call0 on !simulation.class_handle<@Base> via @Base_low
// CHECK: simulation.vpi_class_call_dispatch @call1 on !simulation.class_handle<@I> via @I_low
