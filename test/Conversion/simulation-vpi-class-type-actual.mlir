// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.dump %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.Dump

// A concrete type RHS owns its type identity while the enum constant retains
// the exact intrinsic reverse EnumTypespec edge. It is not a second RHS owner.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0 {rhs = @enum, is_named}
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1
    simulation.vpi_typespec.decl @enum id 1 in 0 owner @a hierarchy "" debug "" {
      origin = 5 : i32,
      target_type = #simulation.vpi_type<kind = enum, isSigned = false, isFourState = false, name = "actual_enum", range = [], children = [#simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>], childNames = []>
    }
    simulation.vpi_enum_const.decl 0 enum @enum ordinal 0 name "A" value "2'b01"
    simulation.code_unit.decl 1 in 0 initial hierarchy "$root.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
