// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ParameterAssignmentsInInstanceScopes
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#byte = #simulation.vpi_type<kind = byte, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#v7 = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
#v11 = #simulation.frozen_constant<value = [11 : i32, 0 : i32], isSigned = true> : i32
#v13 = #simulation.frozen_constant<value = [13 : i32, 0 : i32], isSigned = true> : i32
#v17 = #simulation.frozen_constant<value = [17 : i32, 0 : i32], isSigned = true> : i32
#v19 = #simulation.frozen_constant<value = [19 : i32, 0 : i32], isSigned = true> : i32
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @parents {
    simulation.vpi_definition.decl @top type 32 name "top"
    simulation.vpi_definition.decl @ip type 601 name "ip"
    simulation.vpi_definition_member.decl @ipP of @ip type 41 ordinal 0 name "P"
    simulation.vpi_definition_member.decl @ipT of @ip type 609 ordinal 1 name "T"
    simulation.vpi_definition_member.decl @ipref of @ip type 40 ordinal 2 name "" {param_assign_by_name, has_original_rhs}
    simulation.vpi_definition_member.decl @iplit of @ip type 40 ordinal 3 name "" {has_original_rhs}
    simulation.vpi_definition_member.decl @iptype of @ip type 40 ordinal 4 name "" {param_assign_type, has_original_rhs}
    simulation.vpi_relation.decl <kind = symbol, symbol = @ipref> selector 82 handle ordinal 0 to <kind = symbol, symbol = @ipP>
    simulation.vpi_relation.decl <kind = symbol, symbol = @iptype> selector 82 handle ordinal 0 to <kind = symbol, symbol = @ipT>
    simulation.vpi_expression.decl @iprhs id 0 type 7 owned_by <kind = symbol, symbol = @iplit> selector 82 handle ordinal 0 : i32 {definition = @ip, immutable_value = #v7, vpi_type = #int}
    simulation.vpi_relation.decl <kind = symbol, symbol = @iplit> selector 82 handle ordinal 0 to <kind = symbol, symbol = @iprhs>
    simulation.vpi_definition.decl @child type 601 name "child"
    simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
    simulation.vpi_definition_member.decl @U of @child type 609 ordinal 1 name "U"
    simulation.vpi_definition_specialization.decl @parent0 of @ip
    simulation.vpi_definition_member.specialize @parent0 member @ipP type #int {immutable_value = #v11}
    simulation.vpi_definition_member.specialize @parent0 member @ipT type #byte
    simulation.vpi_definition_specialization.decl @a0 of @child
    simulation.vpi_definition_member.specialize @a0 member @N type #int {immutable_value = #v11}
    simulation.vpi_definition_member.specialize @a0 member @U type #byte
    simulation.vpi_definition_specialization.decl @b0 of @child
    simulation.vpi_definition_member.specialize @b0 member @N type #int {immutable_value = #v7}
    simulation.vpi_definition_member.specialize @b0 member @U type #byte
    simulation.vpi_definition_specialization.decl @parent1 of @ip
    simulation.vpi_definition_member.specialize @parent1 member @ipP type #int {immutable_value = #v13}
    simulation.vpi_definition_member.specialize @parent1 member @ipT type #bit
    simulation.vpi_definition_specialization.decl @a1 of @child
    simulation.vpi_definition_member.specialize @a1 member @N type #int {immutable_value = #v13}
    simulation.vpi_definition_member.specialize @a1 member @U type #bit
    simulation.vpi_definition_specialization.decl @b1 of @child
    simulation.vpi_definition_member.specialize @b1 member @N type #int {immutable_value = #v7}
    simulation.vpi_definition_member.specialize @b1 member @U type #bit
    simulation.vpi_definition_specialization.decl @parent2 of @ip
    simulation.vpi_definition_member.specialize @parent2 member @ipP type #int {immutable_value = #v19}
    simulation.vpi_definition_member.specialize @parent2 member @ipT type #byte
    simulation.vpi_definition_specialization.decl @a2 of @child
    simulation.vpi_definition_member.specialize @a2 member @N type #int {immutable_value = #v19}
    simulation.vpi_definition_member.specialize @a2 member @U type #byte
    simulation.vpi_definition_specialization.decl @b2 of @child
    simulation.vpi_definition_member.specialize @b2 member @N type #int {immutable_value = #v7}
    simulation.vpi_definition_member.specialize @b2 member @U type #byte
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @top
    simulation.scope.decl 2 parent 1 hierarchy "top.i" vpi_kind 601 definition @ip specialization @parent0
    simulation.scope.decl 3 parent 2 hierarchy "top.i.a" vpi_kind 601 definition @child specialization @a0
    simulation.scope.decl 4 parent 2 hierarchy "top.i.b" vpi_kind 601 definition @child specialization @b0
    simulation.vpi_definition_member.bind scope 2 member @ipref expr <kind = definition_member, id = 3, symbol = @N>
    simulation.vpi_definition_member.bind scope 2 member @iplit expr <kind = definition_member, id = 4, symbol = @N>
    simulation.vpi_definition_member.bind scope 2 member @iptype expr <kind = definition_member, id = 3, symbol = @U>
    simulation.scope.decl 5 parent 1 hierarchy "top.j" vpi_kind 601 definition @ip specialization @parent1
    simulation.scope.decl 6 parent 5 hierarchy "top.j.a" vpi_kind 601 definition @child specialization @a1
    simulation.scope.decl 7 parent 5 hierarchy "top.j.b" vpi_kind 601 definition @child specialization @b1
    simulation.vpi_definition_member.bind scope 5 member @ipref expr <kind = definition_member, id = 6, symbol = @N>
    simulation.vpi_definition_member.bind scope 5 member @iplit expr <kind = definition_member, id = 7, symbol = @N>
    simulation.vpi_definition_member.bind scope 5 member @iptype expr <kind = definition_member, id = 6, symbol = @U>
    simulation.scope.decl 8 parent 0 hierarchy "loose" vpi_kind 601 definition @ip specialization @parent2
    simulation.scope.decl 9 parent 8 hierarchy "loose.a" vpi_kind 601 definition @child specialization @a2
    simulation.scope.decl 10 parent 8 hierarchy "loose.b" vpi_kind 601 definition @child specialization @b2
    simulation.vpi_definition_member.bind scope 8 member @ipref expr <kind = definition_member, id = 9, symbol = @N>
    simulation.vpi_definition_member.bind scope 8 member @iplit expr <kind = definition_member, id = 10, symbol = @N>
    simulation.vpi_definition_member.bind scope 8 member @iptype expr <kind = definition_member, id = 9, symbol = @U>
    simulation.vpi_object.anchor @top_anchor id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1>}
    simulation.vpi_object.anchor @i_anchor id 1 type 601 in 2 parent @top_anchor ordinal 0 hierarchy "top.i" debug "i" {backing = #simulation.vpi_backing<kind = scope, id = 2>}
    simulation.vpi_object.anchor @a_anchor id 2 type 601 in 3 parent @i_anchor ordinal 0 hierarchy "top.i.a" debug "a" {backing = #simulation.vpi_backing<kind = scope, id = 3>}
    simulation.scope.decl 11 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
