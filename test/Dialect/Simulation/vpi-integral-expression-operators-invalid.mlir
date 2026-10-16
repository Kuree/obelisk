// RUN: obelisk-opt %s --split-input-file --verify-diagnostics
#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#s8 = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#u16 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [15, 0], children = [], childNames = []>
#f8 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
  simulation.storage.decl 1 in 1 : !simulation.logic<8> design hierarchy "top.x" {vpi_type = #f8}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // nested width
  // expected-error @+1 {{operator operands disagree with the elaborated result context}}
  simulation.vpi_expression.decl @root id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i16 {vpi_type = #u16, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @root>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @child>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_expression.decl @child id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 0 : i8 {vpi_type = #u8, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @child> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @child> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
} }

// -----

#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#s8 = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#u16 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [15, 0], children = [], childNames = []>
#f8 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
  simulation.storage.decl 1 in 1 : !simulation.logic<8> design hierarchy "top.x" {vpi_type = #f8}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // nested state
  // expected-error @+1 {{operator operands disagree with the elaborated result context}}
  simulation.vpi_expression.decl @root id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @root>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @child>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64>
  simulation.vpi_expression.decl @child id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 0 : i8 {vpi_type = #u8, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @child> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @child> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
} }

// -----

#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#s8 = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#u16 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [15, 0], children = [], childNames = []>
#f8 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
  simulation.storage.decl 1 in 1 : !simulation.logic<8> design hierarchy "top.x" {vpi_type = #f8}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // signed result from unsigned leaves
  // expected-error @+1 {{operator operands disagree with the elaborated result context}}
  simulation.vpi_expression.decl @root id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #s8, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @root>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
} }

// -----

#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#s8 = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#u16 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [15, 0], children = [], childNames = []>
#f8 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
  simulation.storage.decl 1 in 1 : !simulation.logic<8> design hierarchy "top.x" {vpi_type = #f8}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // two-state divide
  // expected-error @+1 {{operator operands disagree with the elaborated result context}}
  simulation.vpi_expression.decl @root id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 12 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @root>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
} }

// -----

#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#s8 = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#u16 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [15, 0], children = [], childNames = []>
#f8 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
  simulation.storage.decl 1 in 1 : !simulation.logic<8> design hierarchy "top.x" {vpi_type = #f8}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // nonboolean comparison
  // expected-error @+1 {{operator operands disagree with the elaborated result context}}
  simulation.vpi_expression.decl @root id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 14 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @root>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
} }

// -----

#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#s8 = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#u16 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [15, 0], children = [], childNames = []>
#f8 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
module { simulation.design @invalid {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
  simulation.storage.decl 1 in 1 : !simulation.logic<8> design hierarchy "top.x" {vpi_type = #f8}
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 3
  simulation.statement_site.decl 111 on 11 phase 0
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  // missing binary operand
  // expected-error @+1 {{operator operands disagree with the elaborated result context}}
  simulation.vpi_expression.decl @root id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 24 : i32}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @root>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @root> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
} }
