// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null
// zero concatenated multiplier
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#count = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i2 {vpi_type = #count, op_type = 33 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i1 {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [0 : i1, 0 : i1], isSigned = false> : i1}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i1 {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [0 : i1, 0 : i1], isSigned = false> : i1}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }


// -----

// unknown concatenated multiplier
#bit = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>
#count = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : !simulation.logic<2> {vpi_type = #count, op_type = 33 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : !simulation.logic<1> {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [1 : i1, 1 : i1], isSigned = false> : !simulation.logic<1>}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : !simulation.logic<1> {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [0 : i1, 0 : i1], isSigned = false> : !simulation.logic<1>}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }


// -----

// mismatched concatenation width during early bound proof
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#count = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i3 {vpi_type = #count, op_type = 33 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i1 {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [1 : i1, 0 : i1], isSigned = false> : i1}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i1 {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [0 : i1, 0 : i1], isSigned = false> : i1}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }


// -----

// signed concatenation result during early bound proof
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#count = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [1, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i2 {vpi_type = #count, op_type = 33 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i1 {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [1 : i1, 0 : i1], isSigned = false> : i1}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i1 {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [0 : i1, 0 : i1], isSigned = false> : i1}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }


// -----

// noncontiguous variable-arity operands
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#count = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i2 {vpi_type = #count, op_type = 33 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i1 {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [1 : i1, 0 : i1], isSigned = false> : i1}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i1 {vpi_type = #bit, immutable_value = #simulation.frozen_constant<value = [0 : i1, 0 : i1], isSigned = false> : i1}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 2 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }
