// RUN: obelisk-opt %s | FileCheck %s
// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ContextualExpressionParameters
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits2 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#bits3 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits5 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [4, 0], children = [], childNames = []>
#bits16 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [15, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @parameter_expressions {
    simulation.vpi_definition.decl @source type 32 name "source"
    simulation.vpi_definition_member.decl @N of @source type 41 ordinal 0 name "N"
    simulation.vpi_definition_member.decl @Word of @source type 41 ordinal 1 name "Word"
    simulation.vpi_definition_specialization.decl @values of @source
    simulation.vpi_definition_member.specialize @values member @N type #int {immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_definition_member.specialize @values member @Word type #int {immutable_value = #simulation.frozen_constant<value = [90 : i32, 0 : i32], isSigned = true> : i32}
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @source specialization @values
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.repeat"
    simulation.statement.decl 11 in 44 scope 1 type 3
    simulation.statement_site.decl 111 on 11 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
    simulation.func @run_repeat(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    simulation.code_unit.decl 45 in 1 initial hierarchy "top.part"
    simulation.statement.decl 12 in 45 scope 1 type 3
    simulation.statement_site.decl 112 on 12 phase 0
    simulation.vpi_statement_relation.decl code_unit 45 type 24 selector 104 ordinal 0 modes 1 to 12
    simulation.func @run_part(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} { simulation.return }
    simulation.code_unit.decl 46 in 1 initial hierarchy "top.indexed"
    simulation.statement.decl 13 in 46 scope 1 type 3
    simulation.statement_site.decl 113 on 13 phase 0
    simulation.vpi_statement_relation.decl code_unit 46 type 24 selector 104 ordinal 0 modes 1 to 13
    simulation.func @run_indexed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 46 : i64} { simulation.return }
    simulation.code_unit.decl 47 in 1 initial hierarchy "top.direct"
    simulation.statement.decl 14 in 47 scope 1 type 3
    simulation.statement_site.decl 114 on 14 phase 0
    simulation.vpi_statement_relation.decl code_unit 47 type 24 selector 104 ordinal 0 modes 1 to 14
    simulation.func @run_direct(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 47 : i64} { simulation.return }
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 14 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @N>
    simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @N> {simulation.vpi_expression_identifier = "N"}
    simulation.vpi_expression.decl @low4 id 101 type 42 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @low4>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @low4> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @Word> {simulation.vpi_expression_identifier = "Word"}
    simulation.vpi_expression.decl @three id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @low4> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @low4> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @three>
    simulation.vpi_expression.decl @zero id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @low4> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @low4> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @zero>
    simulation.vpi_expression.decl @part id 104 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 12 : i64> selector 82 handle ordinal 0 : i3 {vpi_type = #bits3}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 12 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @part>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @part> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @Word> {simulation.vpi_expression_identifier = "Word"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @part> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @N> {simulation.vpi_expression_identifier = "N"}
    simulation.vpi_expression.decl @zero2 id 105 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @part> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @part> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @zero2>
    simulation.vpi_expression.decl @indexed id 106 type 130 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 13 : i64> selector 82 handle ordinal 0 : i2 {vpi_type = #bits2, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 13 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @indexed>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @indexed> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @Word> {simulation.vpi_expression_identifier = "Word"}
    simulation.vpi_expression.decl @one id 107 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @indexed> selector 131 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @indexed> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @one>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @indexed> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @N> {simulation.vpi_expression_identifier = "N"}
    simulation.vpi_definition_specialization.decl @different_values of @source
    simulation.vpi_definition_member.specialize @different_values member @N type #int {immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_definition_member.specialize @different_values member @Word type #int {immutable_value = #simulation.frozen_constant<value = [90 : i32, 0 : i32], isSigned = true> : i32}
    simulation.scope.decl 2 parent 0 hierarchy "different" vpi_kind 32 definition @source specialization @different_values
    simulation.code_unit.decl 54 in 2 initial hierarchy "different.repeat"
    simulation.statement.decl 21 in 54 scope 2 type 3
    simulation.statement_site.decl 121 on 21 phase 0
    simulation.vpi_statement_relation.decl code_unit 54 type 24 selector 104 ordinal 0 modes 1 to 21
    simulation.func @different_run_repeat(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 54 : i64} { simulation.return }
    simulation.code_unit.decl 55 in 2 initial hierarchy "different.part"
    simulation.statement.decl 22 in 55 scope 2 type 3
    simulation.statement_site.decl 122 on 22 phase 0
    simulation.vpi_statement_relation.decl code_unit 55 type 24 selector 104 ordinal 0 modes 1 to 22
    simulation.func @different_run_part(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 55 : i64} { simulation.return }
    simulation.code_unit.decl 56 in 2 initial hierarchy "different.indexed"
    simulation.statement.decl 23 in 56 scope 2 type 3
    simulation.statement_site.decl 123 on 23 phase 0
    simulation.vpi_statement_relation.decl code_unit 56 type 24 selector 104 ordinal 0 modes 1 to 23
    simulation.func @different_run_indexed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 56 : i64} { simulation.return }
    simulation.vpi_expression.decl @different_repeat id 200 type 39 in 2 owned_by #simulation.vpi_ref<kind = statement, id = 21 : i64> selector 82 handle ordinal 0 : i16 {vpi_type = #bits16, op_type = 34 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 21 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_repeat>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 2 : i64, symbol = @N> {simulation.vpi_expression_identifier = "N"}
    simulation.vpi_expression.decl @different_low4 id 201 type 42 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @different_repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @different_low4>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_low4> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 2 : i64, symbol = @Word> {simulation.vpi_expression_identifier = "Word"}
    simulation.vpi_expression.decl @different_three id 202 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @different_low4> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_low4> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_three>
    simulation.vpi_expression.decl @different_zero id 203 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @different_low4> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_low4> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_zero>
    simulation.vpi_expression.decl @different_part id 204 type 42 in 2 owned_by #simulation.vpi_ref<kind = statement, id = 22 : i64> selector 82 handle ordinal 0 : i5 {vpi_type = #bits5}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 22 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_part>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_part> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 2 : i64, symbol = @Word> {simulation.vpi_expression_identifier = "Word"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_part> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 2 : i64, symbol = @N> {simulation.vpi_expression_identifier = "N"}
    simulation.vpi_expression.decl @different_zero2 id 205 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @different_part> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_part> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_zero2>
    simulation.vpi_expression.decl @different_indexed id 206 type 130 in 2 owned_by #simulation.vpi_ref<kind = statement, id = 23 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #bits4, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 23 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_indexed>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_indexed> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 2 : i64, symbol = @Word> {simulation.vpi_expression_identifier = "Word"}
    simulation.vpi_expression.decl @different_one id 207 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @different_indexed> selector 131 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_indexed> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_one>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different_indexed> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 2 : i64, symbol = @N> {simulation.vpi_expression_identifier = "N"}
  }
}
// CHECK: simulation.vpi_definition_member.specialize @values member @N
// CHECK: simulation.vpi_expression.decl @repeat
// CHECK: simulation.vpi_expression.decl @indexed
