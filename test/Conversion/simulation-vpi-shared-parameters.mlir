// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.SharedInstanceParameters:GeneratedDesignDatabase.SharedParameterImageSize
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#byte = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#range = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = false, range = [7, 0], children = [#bit], childNames = []>
#alias = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = [], typedefAliases = [@word]>
#three = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32
#four = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32
#five = #simulation.frozen_constant<value = [5 : i32, 0 : i32], isSigned = true> : i32
#minus = #simulation.frozen_constant<value = [254 : i8, 0 : i8], isSigned = false> : !simulation.packed_array<7 : 0 x i1>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @parameters {
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_typespec.decl @word id 0 in 0 owner @pkg hierarchy "pkg::Word" debug "Word" {target_type = #alias}
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N" {has_original_default}
    simulation.vpi_expression.decl @originalN id 0 type 7 owned_by <kind = symbol, symbol = @N> selector 102 handle ordinal 0 : i32 {definition = @cell, immutable_value = #three, vpi_type = #int}
    simulation.vpi_relation.decl <kind = symbol, symbol = @N> selector 102 handle ordinal 0 to <kind = symbol, symbol = @originalN>
    simulation.vpi_definition_member.decl @port of @cell type 28 ordinal 1 name "port" direction input
    simulation.vpi_definition_member.decl @L of @cell type 41 ordinal 2 name "L.p" {is_local_param, has_explicit_parameter_range}
    simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 3 name "T" {unavailable_default}
    simulation.vpi_definition_specialization.decl @a of @cell
    simulation.vpi_definition_member.specialize @a member @N type #int {immutable_value = #four}
    simulation.vpi_definition_member.specialize @a member @port type #bit
    simulation.vpi_definition_member.specialize @a member @L type #range {immutable_value = #minus}
    simulation.vpi_definition_member.specialize @a member @T type #alias
    simulation.vpi_definition_specialization.decl @b of @cell
    simulation.vpi_definition_member.specialize @b member @N type #int {immutable_value = #five}
    simulation.vpi_definition_member.specialize @b member @port type #bit
    simulation.vpi_definition_member.specialize @b member @L type #range {immutable_value = #minus}
    simulation.vpi_definition_member.specialize @b member @T type #byte
    simulation.vpi_definition.decl @zbus type 601 name "bus"
    simulation.vpi_definition_member.decl @K of @zbus type 41 ordinal 0 name "K"
    simulation.vpi_definition_specialization.decl @zbus_a of @zbus
    simulation.vpi_definition_member.specialize @zbus_a member @K type #int {immutable_value = #four}
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "left" vpi_kind 32 definition @cell specialization @a
    simulation.scope.decl 2 parent 0 hierarchy "right" vpi_kind 32 definition @cell specialization @a
    simulation.scope.decl 3 parent 0 hierarchy "different" vpi_kind 32 definition @cell specialization @b
    simulation.scope.decl 4 parent 0 hierarchy "bus" vpi_kind 601 definition @zbus specialization @zbus_a
    simulation.code_unit.decl 1 in 1 initial hierarchy "left.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
