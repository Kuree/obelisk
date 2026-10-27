// RUN: talos-opt %s --split-input-file --verify-diagnostics
#u3 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
#u1 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#u2 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#u4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#u6 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [5, 0], children = [], childNames = []>
#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#u32 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [31, 0], children = [], childNames = []>
#asc = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 7], children = [], childNames = []>
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#index = #simulation.vpi_type<kind = integer, isSigned = true, isFourState = true, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @selections {
    simulation.vpi_definition.decl @source type 32 name "source"
    simulation.vpi_definition_member.decl @P of @source type 41 ordinal 0 name "P"
    simulation.vpi_definition_specialization.decl @values of @source
    simulation.vpi_definition_member.specialize @values member @P type #u8 {immutable_value = #simulation.frozen_constant<value = [165 : i8, 0 : i8], isSigned = false> : i8}
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @source specialization @values
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32 definition @source specialization @values
    simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
    simulation.storage.decl 1 in 1 : i8 design hierarchy "top.b" {vpi_type = #u8}
    simulation.storage.decl 2 in 1 : i8 design hierarchy "top.ascword" {vpi_type = #asc}
    simulation.storage.decl 3 in 1 : !simulation.logic<32> design hierarchy "top.index" {vpi_type = #index}
    simulation.net.decl 0 in 1 : i8 design hierarchy "top.netword" {vpi_type = #u8}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.first"
    simulation.statement.decl 100 in 44 scope 1 type 3
    simulation.statement_site.decl 1100 on 100 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 100
    simulation.func @run_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    // expected-error @+1 {{bit selection requires its exact declaration kind and source index}}
    simulation.vpi_expression.decl @first id 1000 type 106 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @first_index id 1001 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first_index>
  }
}

// -----
#u3 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
#u1 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#u2 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#u4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#u6 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [5, 0], children = [], childNames = []>
#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#u32 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [31, 0], children = [], childNames = []>
#asc = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 7], children = [], childNames = []>
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#index = #simulation.vpi_type<kind = integer, isSigned = true, isFourState = true, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @selections {
    simulation.vpi_definition.decl @source type 32 name "source"
    simulation.vpi_definition_member.decl @P of @source type 41 ordinal 0 name "P"
    simulation.vpi_definition_specialization.decl @values of @source
    simulation.vpi_definition_member.specialize @values member @P type #u8 {immutable_value = #simulation.frozen_constant<value = [165 : i8, 0 : i8], isSigned = false> : i8}
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @source specialization @values
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32 definition @source specialization @values
    simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
    simulation.storage.decl 1 in 1 : i8 design hierarchy "top.b" {vpi_type = #u8}
    simulation.storage.decl 2 in 1 : i8 design hierarchy "top.ascword" {vpi_type = #asc}
    simulation.storage.decl 3 in 1 : !simulation.logic<32> design hierarchy "top.index" {vpi_type = #index}
    simulation.net.decl 0 in 1 : i8 design hierarchy "top.netword" {vpi_type = #u8}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.first"
    simulation.statement.decl 100 in 44 scope 1 type 3
    simulation.statement_site.decl 1100 on 100 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 100
    simulation.func @run_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    // expected-error @+1 {{bit selection requires its exact declaration kind and source index}}
    simulation.vpi_expression.decl @first id 1000 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 : i2 {vpi_type = #u2}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @first_index id 1001 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first_index>
  }
}

// -----
#u3 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
#u1 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#u2 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#u4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#u6 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [5, 0], children = [], childNames = []>
#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#u32 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [31, 0], children = [], childNames = []>
#asc = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 7], children = [], childNames = []>
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#index = #simulation.vpi_type<kind = integer, isSigned = true, isFourState = true, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @selections {
    simulation.vpi_definition.decl @source type 32 name "source"
    simulation.vpi_definition_member.decl @P of @source type 41 ordinal 0 name "P"
    simulation.vpi_definition_specialization.decl @values of @source
    simulation.vpi_definition_member.specialize @values member @P type #u8 {immutable_value = #simulation.frozen_constant<value = [165 : i8, 0 : i8], isSigned = false> : i8}
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @source specialization @values
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32 definition @source specialization @values
    simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
    simulation.storage.decl 1 in 1 : i8 design hierarchy "top.b" {vpi_type = #u8}
    simulation.storage.decl 2 in 1 : i8 design hierarchy "top.ascword" {vpi_type = #asc}
    simulation.storage.decl 3 in 1 : !simulation.logic<32> design hierarchy "top.index" {vpi_type = #index}
    simulation.net.decl 0 in 1 : i8 design hierarchy "top.netword" {vpi_type = #u8}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.first"
    simulation.statement.decl 100 in 44 scope 1 type 3
    simulation.statement_site.decl 1100 on 100 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 100
    simulation.func @run_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    // expected-error @+1 {{literal, operator and selection metadata must match kind}}
    simulation.vpi_expression.decl @first id 1000 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @first_index id 1001 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first_index>
  }
}

// -----
#logic = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>
#u3 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
#u1 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#u2 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#u4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#u6 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [5, 0], children = [], childNames = []>
#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#u32 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [31, 0], children = [], childNames = []>
#asc = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 7], children = [], childNames = []>
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#index = #simulation.vpi_type<kind = integer, isSigned = true, isFourState = true, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @selections {
    simulation.vpi_definition.decl @source type 32 name "source"
    simulation.vpi_definition_member.decl @P of @source type 41 ordinal 0 name "P"
    simulation.vpi_definition_specialization.decl @values of @source
    simulation.vpi_definition_member.specialize @values member @P type #u8 {immutable_value = #simulation.frozen_constant<value = [165 : i8, 0 : i8], isSigned = false> : i8}
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @source specialization @values
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32 definition @source specialization @values
    simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
    simulation.storage.decl 1 in 1 : i8 design hierarchy "top.b" {vpi_type = #u8}
    simulation.storage.decl 2 in 1 : i8 design hierarchy "top.ascword" {vpi_type = #asc}
    simulation.storage.decl 3 in 1 : !simulation.logic<32> design hierarchy "top.index" {vpi_type = #index}
    simulation.net.decl 0 in 1 : i8 design hierarchy "top.netword" {vpi_type = #u8}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.first"
    simulation.statement.decl 100 in 44 scope 1 type 3
    simulation.statement_site.decl 1100 on 100 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 100
    simulation.func @run_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    // expected-error @+1 {{bit selection requires an unsigned scalar in the parent state domain}}
    simulation.vpi_expression.decl @first id 1000 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 : !simulation.logic<1> {vpi_type = #logic}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @first_index id 1001 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first_index>
  }
}
