// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.LiveClassObjectTypes

// IEEE 1800-2023 37.33: a variable has its declared specialization; the live
// object has its allocation's specialization, including derived classes.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @live_class_types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.class.decl @base_class id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @derived_class id 2 extends @base_class {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @hidden_class id 3 extends @base_class {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @base id 1 type 652 in 1 parent @top
        ordinal 0 hierarchy "top.Base" debug "Base" {
      backing = #simulation.vpi_backing<kind = class, symbol = @base_class>
    }
    simulation.vpi_object.anchor @derived id 2 type 652 in 1 parent @top
        ordinal 1 hierarchy "top.Derived" debug "Derived" {
      backing = #simulation.vpi_backing<kind = class, symbol = @derived_class>
    }
    simulation.vpi_object.anchor @hidden id 3 type 652 in 1 parent @top
        ordinal 2 hierarchy "top.Hidden" debug "Hidden" {
      backing = #simulation.vpi_backing<kind = class, symbol = @hidden_class>,
      is_protected
    }
    simulation.vpi_typespec.decl @base_type id 0 in 1 owner @base
        hierarchy "top.Base$type" debug "Base" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @base_class, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @derived_type id 1 in 1 owner @derived
        hierarchy "top.Derived$type" debug "Derived" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @derived_class, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @hidden_type id 2 in 1 owner @hidden
        hierarchy "top.Hidden$type" debug "Hidden" {
      origin = 3 : i32, is_protected,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @hidden_class, range = [],
          children = [], childNames = []>
    }
    simulation.storage.decl 0 in 1 : !simulation.class_handle<@base_class> design
        hierarchy "top.obj" {
      vpi_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @base_class, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_relation.decl <kind = symbol, symbol = @derived_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @base_type>
    simulation.vpi_relation.decl <kind = symbol, symbol = @hidden_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @base_type>
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
