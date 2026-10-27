// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null
// wrong_width: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{concatenation result width disagrees with operands}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }

// -----

// zero: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, op_type = 11 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }

// -----

// negative: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, op_type = 11 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }

// -----

// unknown: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = logic, isSigned = true, isFourState = true, range = [31, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : !simulation.logic<32> design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : !simulation.logic<32> {vpi_type = #count, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : !simulation.logic<32> {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [0 : i32, 1 : i32], isSigned = true> : !simulation.logic<32>}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : !simulation.logic<32> {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : !simulation.logic<32>}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }

// -----

// signed_overflow: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [63, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i64 design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i64 {vpi_type = #count, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i64 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [9223372036854775807 : i64, 0 : i64], isSigned = true> : i64}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i64 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i64, 0 : i64], isSigned = true> : i64}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }

// -----

// unsigned_overflow: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [64, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i65 design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i65 {vpi_type = #count, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i65 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [18446744073709551616 : i65, 0 : i65], isSigned = false> : i65}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i65 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [0 : i65, 0 : i65], isSigned = false> : i65}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }

// -----

// mutable_leaf: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }

// -----

// nested_context: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @a id 102 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 : i65 {vpi_type = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [64, 0], children = [], childNames = []>, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @a>
  simulation.vpi_expression.decl @wide0 id 105 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @a> selector 97 iterate ordinal 0 : i65 {vpi_type = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [64, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [1 : i65, 0 : i65], isSigned = false> : i65}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @a> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @wide0>
  simulation.vpi_expression.decl @wide1 id 106 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @a> selector 97 iterate ordinal 1 : i65 {vpi_type = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [64, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [1 : i65, 0 : i65], isSigned = false> : i65}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @a> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @wide1>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }

// -----

// cycle: arithmetic must prove a known positive count and exact width.
#count = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bits4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#bits8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.mutable" {vpi_type = #count}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // expected-error @+1 {{replication requires a positive constant multiplier}}
  simulation.vpi_expression.decl @repeat id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #bits8, op_type = 34 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @repeat>
  simulation.vpi_expression.decl @count id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 : i32 {vpi_type = #count, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @count>
  simulation.vpi_expression.decl @b id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 : i32 {vpi_type = #count, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @count> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @b>
  simulation.vpi_expression.decl @bits id 104 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 : i4 {vpi_type = #bits4, immutable_value = #simulation.frozen_constant<value = [5 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @repeat> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @bits>
} }
