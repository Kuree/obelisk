// RUN: obelisk-opt %s | FileCheck %s
// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceOnlyExpressions
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @source_only {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.word" {vpi_type = #int}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.arithmetic"
    simulation.statement.decl 11 in 44 scope 1 type 3
    simulation.statement_site.decl 111 on 11 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
    simulation.func @run_arithmetic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    simulation.code_unit.decl 45 in 1 initial hierarchy "top.event"
    simulation.statement.decl 12 in 45 scope 1 type 13
    simulation.statement_site.decl 112 on 12 phase 0
    simulation.vpi_statement_relation.decl code_unit 45 type 24 selector 104 ordinal 0 modes 1 to 12
    simulation.func @run_event(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} { simulation.return }
    simulation.vpi_expression.decl @product id 100 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 {source_only, op_type = 25 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @product>
    simulation.vpi_expression.decl @sum id 101 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @product> selector 97 iterate ordinal 0 {source_only, op_type = 24 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @product> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @sum>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @sum> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "word"}
    simulation.vpi_expression.decl @two id 102 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @sum> selector 97 iterate ordinal 1 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @sum> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @two>
    simulation.vpi_expression.decl @three id 103 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @product> selector 97 iterate ordinal 1 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @product> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @three>
    simulation.vpi_expression.decl @events id 104 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 12 : i64> selector 71 handle ordinal 0 {source_only, op_type = 35 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 12 : i64> selector 71 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @events>
    simulation.vpi_expression.decl @iff id 107 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @events> selector 97 iterate ordinal 0 {source_only, op_type = 68 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @events> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @iff>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @iff> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "word"}
    simulation.vpi_expression.decl @rise id 105 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @iff> selector 97 iterate ordinal 0 {source_only, op_type = 39 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @iff> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @rise>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @rise> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "word"}
    simulation.vpi_expression.decl @fall id 106 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @events> selector 97 iterate ordinal 1 {source_only, op_type = 40 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @events> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @fall>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @fall> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "word"}
  }
}
// CHECK: simulation.vpi_expression.decl @product
// CHECK-SAME: source_only
// CHECK: simulation.vpi_expression.decl @events
// CHECK-SAME: source_only
