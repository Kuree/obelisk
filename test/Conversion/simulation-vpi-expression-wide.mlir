// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.WideExpressionTemplates
// Equal source templates in two distinct instances retain separate identities.
#byte = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [64, 0], children = [], childNames = []>
#half = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [129, 0], children = [], childNames = []>
#bit = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>
#word = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = true, range = [129, 0], children = [#bit], childNames = []>
#index = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @shared_expressions {
    simulation.vpi_definition.decl @source type 32 name "source"
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "left" vpi_kind 32 {vpi_definition = @source}
    simulation.storage.decl 0 in 1 : !simulation.packed_array<129 : 0 x !simulation.logic<1>> design hierarchy "left.bits" {vpi_type = #word}
    simulation.code_unit.decl 44 in 1 initial hierarchy "left.initial"
    simulation.statement.decl 11 in 44 scope 1 type 3
    simulation.statement_site.decl 111 on 11 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
    simulation.vpi_expression.decl @left_concat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : !simulation.logic<130> {vpi_type = #half, op_type = 33 : i32} loc("shared_expression.sv":10:1)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left_concat>
    simulation.vpi_expression.decl @left_upper id 101 type 42 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @left_concat> selector 97 iterate ordinal 0 : !simulation.logic<65> {vpi_type = #byte} loc("shared_expression.sv":10:2)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @left_concat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left_upper>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @left_upper> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "bits"}
    simulation.vpi_expression.decl @left_upper_left id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @left_upper> selector 79 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [129 : i32, 0 : i32], isSigned = true> : i32} loc("shared_expression.sv":10:3)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @left_upper> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left_upper_left>
    simulation.vpi_expression.decl @left_upper_right id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @left_upper> selector 83 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [65 : i32, 0 : i32], isSigned = true> : i32} loc("shared_expression.sv":10:4)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @left_upper> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left_upper_right>
    simulation.vpi_expression.decl @left_lower id 104 type 42 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @left_concat> selector 97 iterate ordinal 1 : !simulation.logic<65> {vpi_type = #byte} loc("shared_expression.sv":10:5)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @left_concat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @left_lower>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @left_lower> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "bits"}
    simulation.vpi_expression.decl @left_lower_left id 105 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @left_lower> selector 79 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [64 : i32, 0 : i32], isSigned = true> : i32} loc("shared_expression.sv":10:6)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @left_lower> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left_lower_left>
    simulation.vpi_expression.decl @left_lower_right id 106 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @left_lower> selector 83 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32} loc("shared_expression.sv":10:7)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @left_lower> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left_lower_right>
    simulation.func @left_initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
    simulation.scope.decl 2 parent 0 hierarchy "right" vpi_kind 32 {vpi_definition = @source}
    simulation.storage.decl 1 in 2 : !simulation.packed_array<129 : 0 x !simulation.logic<1>> design hierarchy "right.bits" {vpi_type = #word}
    simulation.code_unit.decl 45 in 2 initial hierarchy "right.initial"
    simulation.statement.decl 12 in 45 scope 2 type 3
    simulation.statement_site.decl 112 on 12 phase 0
    simulation.vpi_statement_relation.decl code_unit 45 type 24 selector 104 ordinal 0 modes 1 to 12
    simulation.vpi_expression.decl @right_concat id 200 type 39 in 2 owned_by #simulation.vpi_ref<kind = statement, id = 12 : i64> selector 82 handle ordinal 0 : !simulation.logic<130> {vpi_type = #half, op_type = 33 : i32} loc("shared_expression.sv":10:1)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 12 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right_concat>
    simulation.vpi_expression.decl @right_upper id 201 type 42 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @right_concat> selector 97 iterate ordinal 0 : !simulation.logic<65> {vpi_type = #byte} loc("shared_expression.sv":10:2)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @right_concat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right_upper>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @right_upper> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "bits"}
    simulation.vpi_expression.decl @right_upper_left id 202 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @right_upper> selector 79 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [129 : i32, 0 : i32], isSigned = true> : i32} loc("shared_expression.sv":10:3)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @right_upper> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right_upper_left>
    simulation.vpi_expression.decl @right_upper_right id 203 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @right_upper> selector 83 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [65 : i32, 0 : i32], isSigned = true> : i32} loc("shared_expression.sv":10:4)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @right_upper> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right_upper_right>
    simulation.vpi_expression.decl @right_lower id 204 type 42 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @right_concat> selector 97 iterate ordinal 1 : !simulation.logic<65> {vpi_type = #byte} loc("shared_expression.sv":10:5)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @right_concat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @right_lower>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @right_lower> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "bits"}
    simulation.vpi_expression.decl @right_lower_left id 205 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @right_lower> selector 79 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [64 : i32, 0 : i32], isSigned = true> : i32} loc("shared_expression.sv":10:6)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @right_lower> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right_lower_left>
    simulation.vpi_expression.decl @right_lower_right id 206 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @right_lower> selector 83 handle ordinal 0 : i32 {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32} loc("shared_expression.sv":10:7)
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @right_lower> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right_lower_right>
    simulation.func @right_initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} {
      simulation.return
    }
  }
}
