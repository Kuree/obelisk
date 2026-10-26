// RUN: obelisk-opt --split-input-file --verify-diagnostics %s -o /dev/null

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.word" {vpi_type = #int}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error@+1 {{unsupported exact expression kind}}
  simulation.vpi_expression.decl @select id 1 type 36 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i3 {vpi_type = #bits}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @select>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_expression.decl @left id 2 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left>
  simulation.vpi_expression.decl @right id 3 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.word" {vpi_type = #int}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error@+1 {{selection result requires the exact scalar integral width}}
  simulation.vpi_expression.decl @select id 1 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #bits}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @select>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_expression.decl @left id 2 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left>
  simulation.vpi_expression.decl @right id 3 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.word" {vpi_type = #int}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error@+1 {{part-select direction disagrees with selected range}}
  simulation.vpi_expression.decl @select id 1 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i3 {vpi_type = #bits}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @select>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_expression.decl @left id 2 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left>
  simulation.vpi_expression.decl @right id 3 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.word" {vpi_type = #int}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  simulation.vpi_expression.decl @select id 1 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i3 {vpi_type = #bits}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @select>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  // expected-error@+1 {{literal, operator and selection metadata must match kind}}
  simulation.vpi_expression.decl @left id 2 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 : i32 {op_type = 33 : i32, vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left>
  simulation.vpi_expression.decl @right id 3 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.word" {vpi_type = #int}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error@+1 {{selection requires exact parent and range expressions}}
  simulation.vpi_expression.decl @select id 1 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i3 {vpi_type = #bits}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @select>
  simulation.vpi_expression.decl @left id 2 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left>
  simulation.vpi_expression.decl @right id 3 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.word" {vpi_type = #int}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  simulation.vpi_expression.decl @select id 1 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i3 {vpi_type = #bits}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @select>
  // expected-error@+1 {{expression identifier requires a raw nonempty declaration-leaf name}}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a b"}
  simulation.vpi_expression.decl @left id 2 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @left>
  simulation.vpi_expression.decl @right id 3 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @select> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @right>
 }
}
