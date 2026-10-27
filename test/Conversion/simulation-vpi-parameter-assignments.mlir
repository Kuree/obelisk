// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.SharedParameterAssignments*
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#byte = #simulation.vpi_type<kind = byte, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#two = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32
#three = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32
#five = #simulation.frozen_constant<value = [5 : i32, 0 : i32], isSigned = true> : i32
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @assignments {
    simulation.vpi_definition.decl @parent type 32 name "parent"
    simulation.vpi_definition_member.decl @P of @parent type 41 ordinal 0 name "P"
    simulation.vpi_definition_member.decl @T of @parent type 609 ordinal 1 name "T"
    simulation.vpi_definition_member.decl @literal of @parent type 40 ordinal 2 name "" {has_original_rhs}
    simulation.vpi_definition_member.decl @reference of @parent type 40 ordinal 3 name "" {param_assign_by_name, has_original_rhs}
    simulation.vpi_definition_member.decl @concrete of @parent type 40 ordinal 4 name "" {param_assign_by_name, param_assign_type, has_original_rhs}
    simulation.vpi_definition_member.decl @type_ref of @parent type 40 ordinal 5 name "" {param_assign_type, has_original_rhs}
    simulation.vpi_definition_member.decl @unsupported of @parent type 40 ordinal 6 name "" {param_assign_by_name, unavailable_rhs}
    simulation.vpi_definition_member.decl @empty of @parent type 40 ordinal 7 name "" {param_assign_by_name, param_assign_type, param_assign_default, unavailable_rhs}
    simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @literal> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int} loc("override.sv":12:10)
    simulation.vpi_relation.decl <kind = symbol, symbol = @literal> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
    simulation.vpi_relation.decl <kind = symbol, symbol = @reference> selector 82 handle ordinal 0 to <kind = symbol, symbol = @P>
    simulation.vpi_definition_type_default @concrete type #byte loc("override.sv":15:9)
    simulation.vpi_relation.decl <kind = symbol, symbol = @type_ref> selector 82 handle ordinal 0 to <kind = symbol, symbol = @T>
    simulation.vpi_definition_specialization.decl @parent2 of @parent
    simulation.vpi_definition_member.specialize @parent2 member @P type #int {immutable_value = #two}
    simulation.vpi_definition_member.specialize @parent2 member @T type #int
    simulation.vpi_definition_specialization.decl @parent5 of @parent
    simulation.vpi_definition_member.specialize @parent5 member @P type #int {immutable_value = #five}
    simulation.vpi_definition_member.specialize @parent5 member @T type #bit
    simulation.vpi_definition.decl @child type 32 name "child"
    simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N" {has_original_default}
    simulation.vpi_definition_member.decl @U of @child type 609 ordinal 1 name "U" {has_original_default}
    simulation.vpi_expression.decl @defaultN id 1 type 7 owned_by <kind = symbol, symbol = @N> selector 102 handle ordinal 0 : i32 {definition = @child, immutable_value = #three, vpi_type = #int}
    simulation.vpi_relation.decl <kind = symbol, symbol = @N> selector 102 handle ordinal 0 to <kind = symbol, symbol = @defaultN>
    simulation.vpi_definition_type_default @U type #int
    simulation.vpi_definition_specialization.decl @child7 of @child
    simulation.vpi_definition_member.specialize @child7 member @N type #int {immutable_value = #seven}
    simulation.vpi_definition_member.specialize @child7 member @U type #byte
    simulation.vpi_definition_specialization.decl @child2 of @child
    simulation.vpi_definition_member.specialize @child2 member @N type #int {immutable_value = #two}
    simulation.vpi_definition_member.specialize @child2 member @U type #int
    simulation.vpi_definition_specialization.decl @child5 of @child
    simulation.vpi_definition_member.specialize @child5 member @N type #int {immutable_value = #five}
    simulation.vpi_definition_member.specialize @child5 member @U type #bit
    simulation.vpi_definition_specialization.decl @child3 of @child
    simulation.vpi_definition_member.specialize @child3 member @N type #int {immutable_value = #three}
    simulation.vpi_definition_member.specialize @child3 member @U type #int
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "left" vpi_kind 32 definition @parent specialization @parent2
    simulation.scope.decl 2 parent 1 hierarchy "left.a" vpi_kind 32 definition @child specialization @child7
    simulation.scope.decl 3 parent 1 hierarchy "left.b" vpi_kind 32 definition @child specialization @child2
    simulation.scope.decl 4 parent 1 hierarchy "left.c" vpi_kind 32 definition @child specialization @child3
    simulation.vpi_definition_member.bind scope 1 member @literal expr <kind = definition_member, id = 2, symbol = @N>
    simulation.vpi_definition_member.bind scope 1 member @reference expr <kind = definition_member, id = 3, symbol = @N>
    simulation.vpi_definition_member.bind scope 1 member @concrete expr <kind = definition_member, id = 2, symbol = @U>
    simulation.vpi_definition_member.bind scope 1 member @type_ref expr <kind = definition_member, id = 3, symbol = @U>
    simulation.vpi_definition_member.bind scope 1 member @unsupported expr <kind = definition_member, id = 4, symbol = @N>
    simulation.vpi_definition_member.bind scope 1 member @empty expr <kind = definition_member, id = 4, symbol = @U>
    simulation.scope.decl 5 parent 0 hierarchy "right" vpi_kind 32 definition @parent specialization @parent5
    simulation.scope.decl 6 parent 5 hierarchy "right.a" vpi_kind 32 definition @child specialization @child7
    simulation.scope.decl 7 parent 5 hierarchy "right.b" vpi_kind 32 definition @child specialization @child5
    simulation.scope.decl 8 parent 5 hierarchy "right.c" vpi_kind 32 definition @child specialization @child3
    simulation.vpi_definition_member.bind scope 5 member @literal expr <kind = definition_member, id = 6, symbol = @N>
    simulation.vpi_definition_member.bind scope 5 member @reference expr <kind = definition_member, id = 7, symbol = @N>
    simulation.vpi_definition_member.bind scope 5 member @concrete expr <kind = definition_member, id = 6, symbol = @U>
    simulation.vpi_definition_member.bind scope 5 member @type_ref expr <kind = definition_member, id = 7, symbol = @U>
    simulation.vpi_definition_member.bind scope 5 member @unsupported expr <kind = definition_member, id = 8, symbol = @N>
    simulation.vpi_definition_member.bind scope 5 member @empty expr <kind = definition_member, id = 8, symbol = @U>
    simulation.code_unit.decl 1 in 1 initial hierarchy "left.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
