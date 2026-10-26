// RUN: obelisk-opt %s | FileCheck %s
// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.IntegralExpressionOperators
#u1 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#f1 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>
#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#s8 = #simulation.vpi_type<kind = bit, isSigned = true, isFourState = false, range = [7, 0], children = [], childNames = []>
#f8 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
#sf8 = #simulation.vpi_type<kind = logic, isSigned = true, isFourState = true, range = [7, 0], children = [], childNames = []>
#u16 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [15, 0], children = [], childNames = []>
#u130 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [129, 0], children = [], childNames = []>
#f130 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [129, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @operators {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.storage.decl 0 in 1 : i8 design hierarchy "top.a" {vpi_type = #u8}
    simulation.storage.decl 1 in 1 : i8 design hierarchy "top.b" {vpi_type = #u8}
    simulation.storage.decl 2 in 1 : i8 design hierarchy "top.negative" {vpi_type = #s8}
    simulation.storage.decl 3 in 1 : i8 design hierarchy "top.signed_three" {vpi_type = #s8}
    simulation.storage.decl 4 in 1 : !simulation.logic<8> design hierarchy "top.mixed" {vpi_type = #f8}
    simulation.storage.decl 5 in 1 : !simulation.logic<8> design hierarchy "top.wild" {vpi_type = #f8}
    simulation.storage.decl 6 in 1 : i8 design hierarchy "top.zero" {vpi_type = #u8}
    simulation.storage.decl 7 in 1 : !simulation.logic<8> design hierarchy "top.unknown" {vpi_type = #f8}
    simulation.storage.decl 8 in 1 : i8 design hierarchy "top.minus_one" {vpi_type = #s8}
    simulation.storage.decl 9 in 1 : i8 design hierarchy "top.minus_three" {vpi_type = #s8}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.minus"
    simulation.statement.decl 100 in 44 scope 1 type 3
    simulation.statement_site.decl 1100 on 100 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 100
    simulation.func @minus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_minus id 1000 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_minus>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_minus> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 45 in 1 initial hierarchy "top.plus"
    simulation.statement.decl 101 in 45 scope 1 type 3
    simulation.statement_site.decl 1101 on 101 phase 0
    simulation.vpi_statement_relation.decl code_unit 45 type 24 selector 104 ordinal 0 modes 1 to 101
    simulation.func @plus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_plus id 1001 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 101 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 2 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 101 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_plus>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_plus> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 46 in 1 initial hierarchy "top.logical_not"
    simulation.statement.decl 102 in 46 scope 1 type 3
    simulation.statement_site.decl 1102 on 102 phase 0
    simulation.vpi_statement_relation.decl code_unit 46 type 24 selector 104 ordinal 0 modes 1 to 102
    simulation.func @logical_not(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 46 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_logical_not id 1002 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 102 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 3 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 102 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_logical_not>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_logical_not> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 47 in 1 initial hierarchy "top.bitwise_not"
    simulation.statement.decl 103 in 47 scope 1 type 3
    simulation.statement_site.decl 1103 on 103 phase 0
    simulation.vpi_statement_relation.decl code_unit 47 type 24 selector 104 ordinal 0 modes 1 to 103
    simulation.func @bitwise_not(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 47 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_bitwise_not id 1003 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 103 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 4 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 103 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_bitwise_not>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_bitwise_not> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 48 in 1 initial hierarchy "top.reduce_and"
    simulation.statement.decl 104 in 48 scope 1 type 3
    simulation.statement_site.decl 1104 on 104 phase 0
    simulation.vpi_statement_relation.decl code_unit 48 type 24 selector 104 ordinal 0 modes 1 to 104
    simulation.func @reduce_and(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 48 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_reduce_and id 1004 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 104 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 5 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 104 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_and>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_and> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 49 in 1 initial hierarchy "top.reduce_nand"
    simulation.statement.decl 105 in 49 scope 1 type 3
    simulation.statement_site.decl 1105 on 105 phase 0
    simulation.vpi_statement_relation.decl code_unit 49 type 24 selector 104 ordinal 0 modes 1 to 105
    simulation.func @reduce_nand(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 49 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_reduce_nand id 1005 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 105 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 6 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 105 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_nand>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_nand> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 50 in 1 initial hierarchy "top.reduce_or"
    simulation.statement.decl 106 in 50 scope 1 type 3
    simulation.statement_site.decl 1106 on 106 phase 0
    simulation.vpi_statement_relation.decl code_unit 50 type 24 selector 104 ordinal 0 modes 1 to 106
    simulation.func @reduce_or(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 50 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_reduce_or id 1006 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 106 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 7 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 106 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_or>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_or> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 51 in 1 initial hierarchy "top.reduce_nor"
    simulation.statement.decl 107 in 51 scope 1 type 3
    simulation.statement_site.decl 1107 on 107 phase 0
    simulation.vpi_statement_relation.decl code_unit 51 type 24 selector 104 ordinal 0 modes 1 to 107
    simulation.func @reduce_nor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 51 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_reduce_nor id 1007 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 107 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 8 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 107 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_nor>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_nor> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 52 in 1 initial hierarchy "top.reduce_xor"
    simulation.statement.decl 108 in 52 scope 1 type 3
    simulation.statement_site.decl 1108 on 108 phase 0
    simulation.vpi_statement_relation.decl code_unit 52 type 24 selector 104 ordinal 0 modes 1 to 108
    simulation.func @reduce_xor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 52 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_reduce_xor id 1008 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 108 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 9 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 108 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_xor>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_xor> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 53 in 1 initial hierarchy "top.reduce_xnor"
    simulation.statement.decl 109 in 53 scope 1 type 3
    simulation.statement_site.decl 1109 on 109 phase 0
    simulation.vpi_statement_relation.decl code_unit 53 type 24 selector 104 ordinal 0 modes 1 to 109
    simulation.func @reduce_xnor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 53 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_reduce_xnor id 1009 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 109 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 10 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 109 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_xnor>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_reduce_xnor> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.code_unit.decl 54 in 1 initial hierarchy "top.subtract"
    simulation.statement.decl 110 in 54 scope 1 type 3
    simulation.statement_site.decl 1110 on 110 phase 0
    simulation.vpi_statement_relation.decl code_unit 54 type 24 selector 104 ordinal 0 modes 1 to 110
    simulation.func @subtract(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 54 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_subtract id 1010 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 110 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 11 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 110 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_subtract>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_subtract> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_subtract> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 55 in 1 initial hierarchy "top.divide"
    simulation.statement.decl 111 in 55 scope 1 type 3
    simulation.statement_site.decl 1111 on 111 phase 0
    simulation.vpi_statement_relation.decl code_unit 55 type 24 selector 104 ordinal 0 modes 1 to 111
    simulation.func @divide(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 55 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_divide id 1011 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 111 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 12 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 111 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_divide>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_divide> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_divide> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 56 in 1 initial hierarchy "top.modulo"
    simulation.statement.decl 112 in 56 scope 1 type 3
    simulation.statement_site.decl 1112 on 112 phase 0
    simulation.vpi_statement_relation.decl code_unit 56 type 24 selector 104 ordinal 0 modes 1 to 112
    simulation.func @modulo(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 56 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_modulo id 1012 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 112 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 13 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 112 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_modulo>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_modulo> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_modulo> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 57 in 1 initial hierarchy "top.equal"
    simulation.statement.decl 113 in 57 scope 1 type 3
    simulation.statement_site.decl 1113 on 113 phase 0
    simulation.vpi_statement_relation.decl code_unit 57 type 24 selector 104 ordinal 0 modes 1 to 113
    simulation.func @equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 57 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_equal id 1013 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 113 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 14 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 113 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 58 in 1 initial hierarchy "top.not_equal"
    simulation.statement.decl 114 in 58 scope 1 type 3
    simulation.statement_site.decl 1114 on 114 phase 0
    simulation.vpi_statement_relation.decl code_unit 58 type 24 selector 104 ordinal 0 modes 1 to 114
    simulation.func @not_equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 58 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_not_equal id 1014 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 114 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 15 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 114 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_not_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_not_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_not_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 59 in 1 initial hierarchy "top.case_equal"
    simulation.statement.decl 115 in 59 scope 1 type 3
    simulation.statement_site.decl 1115 on 115 phase 0
    simulation.vpi_statement_relation.decl code_unit 59 type 24 selector 104 ordinal 0 modes 1 to 115
    simulation.func @case_equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 59 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_case_equal id 1015 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 115 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 16 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 115 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_case_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_case_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_case_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 60 in 1 initial hierarchy "top.case_not_equal"
    simulation.statement.decl 116 in 60 scope 1 type 3
    simulation.statement_site.decl 1116 on 116 phase 0
    simulation.vpi_statement_relation.decl code_unit 60 type 24 selector 104 ordinal 0 modes 1 to 116
    simulation.func @case_not_equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 60 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_case_not_equal id 1016 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 116 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 17 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 116 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_case_not_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_case_not_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_case_not_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 61 in 1 initial hierarchy "top.greater"
    simulation.statement.decl 117 in 61 scope 1 type 3
    simulation.statement_site.decl 1117 on 117 phase 0
    simulation.vpi_statement_relation.decl code_unit 61 type 24 selector 104 ordinal 0 modes 1 to 117
    simulation.func @greater(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 61 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_greater id 1017 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 117 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 18 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 117 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_greater>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_greater> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_greater> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 62 in 1 initial hierarchy "top.greater_equal"
    simulation.statement.decl 118 in 62 scope 1 type 3
    simulation.statement_site.decl 1118 on 118 phase 0
    simulation.vpi_statement_relation.decl code_unit 62 type 24 selector 104 ordinal 0 modes 1 to 118
    simulation.func @greater_equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 62 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_greater_equal id 1018 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 118 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 19 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 118 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_greater_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_greater_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_greater_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 63 in 1 initial hierarchy "top.less"
    simulation.statement.decl 119 in 63 scope 1 type 3
    simulation.statement_site.decl 1119 on 119 phase 0
    simulation.vpi_statement_relation.decl code_unit 63 type 24 selector 104 ordinal 0 modes 1 to 119
    simulation.func @less(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 63 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_less id 1019 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 119 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 20 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 119 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_less>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_less> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_less> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 64 in 1 initial hierarchy "top.less_equal"
    simulation.statement.decl 120 in 64 scope 1 type 3
    simulation.statement_site.decl 1120 on 120 phase 0
    simulation.vpi_statement_relation.decl code_unit 64 type 24 selector 104 ordinal 0 modes 1 to 120
    simulation.func @less_equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 64 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_less_equal id 1020 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 120 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 21 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 120 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_less_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_less_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_less_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 65 in 1 initial hierarchy "top.shift_left"
    simulation.statement.decl 121 in 65 scope 1 type 3
    simulation.statement_site.decl 1121 on 121 phase 0
    simulation.vpi_statement_relation.decl code_unit 65 type 24 selector 104 ordinal 0 modes 1 to 121
    simulation.func @shift_left(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 65 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_shift_left id 1021 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 121 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 22 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 121 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_shift_left>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_shift_left> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_shift_left> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 66 in 1 initial hierarchy "top.shift_right"
    simulation.statement.decl 122 in 66 scope 1 type 3
    simulation.statement_site.decl 1122 on 122 phase 0
    simulation.vpi_statement_relation.decl code_unit 66 type 24 selector 104 ordinal 0 modes 1 to 122
    simulation.func @shift_right(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 66 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_shift_right id 1022 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 122 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 23 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 122 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_shift_right>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_shift_right> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_shift_right> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 67 in 1 initial hierarchy "top.add"
    simulation.statement.decl 123 in 67 scope 1 type 3
    simulation.statement_site.decl 1123 on 123 phase 0
    simulation.vpi_statement_relation.decl code_unit 67 type 24 selector 104 ordinal 0 modes 1 to 123
    simulation.func @add(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 67 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_add id 1023 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 123 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 24 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 123 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_add>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_add> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_add> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 68 in 1 initial hierarchy "top.multiply"
    simulation.statement.decl 124 in 68 scope 1 type 3
    simulation.statement_site.decl 1124 on 124 phase 0
    simulation.vpi_statement_relation.decl code_unit 68 type 24 selector 104 ordinal 0 modes 1 to 124
    simulation.func @multiply(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 68 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_multiply id 1024 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 124 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 25 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 124 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_multiply>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_multiply> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_multiply> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 69 in 1 initial hierarchy "top.logical_and"
    simulation.statement.decl 125 in 69 scope 1 type 3
    simulation.statement_site.decl 1125 on 125 phase 0
    simulation.vpi_statement_relation.decl code_unit 69 type 24 selector 104 ordinal 0 modes 1 to 125
    simulation.func @logical_and(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 69 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_logical_and id 1025 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 125 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 26 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 125 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_logical_and>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_logical_and> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_logical_and> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 70 in 1 initial hierarchy "top.logical_or"
    simulation.statement.decl 126 in 70 scope 1 type 3
    simulation.statement_site.decl 1126 on 126 phase 0
    simulation.vpi_statement_relation.decl code_unit 70 type 24 selector 104 ordinal 0 modes 1 to 126
    simulation.func @logical_or(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 70 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_logical_or id 1026 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 126 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 27 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 126 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_logical_or>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_logical_or> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_logical_or> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 71 in 1 initial hierarchy "top.and"
    simulation.statement.decl 127 in 71 scope 1 type 3
    simulation.statement_site.decl 1127 on 127 phase 0
    simulation.vpi_statement_relation.decl code_unit 71 type 24 selector 104 ordinal 0 modes 1 to 127
    simulation.func @and(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 71 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_and id 1027 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 127 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 28 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 127 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_and>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_and> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_and> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 72 in 1 initial hierarchy "top.or"
    simulation.statement.decl 128 in 72 scope 1 type 3
    simulation.statement_site.decl 1128 on 128 phase 0
    simulation.vpi_statement_relation.decl code_unit 72 type 24 selector 104 ordinal 0 modes 1 to 128
    simulation.func @or(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 72 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_or id 1028 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 128 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 29 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 128 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_or>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_or> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_or> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 73 in 1 initial hierarchy "top.xor"
    simulation.statement.decl 129 in 73 scope 1 type 3
    simulation.statement_site.decl 1129 on 129 phase 0
    simulation.vpi_statement_relation.decl code_unit 73 type 24 selector 104 ordinal 0 modes 1 to 129
    simulation.func @xor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 73 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_xor id 1029 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 129 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 30 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 129 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_xor>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_xor> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_xor> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 74 in 1 initial hierarchy "top.xnor"
    simulation.statement.decl 130 in 74 scope 1 type 3
    simulation.statement_site.decl 1130 on 130 phase 0
    simulation.vpi_statement_relation.decl code_unit 74 type 24 selector 104 ordinal 0 modes 1 to 130
    simulation.func @xnor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 74 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_xnor id 1030 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 130 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 31 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 130 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_xnor>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_xnor> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_xnor> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 75 in 1 initial hierarchy "top.conditional"
    simulation.statement.decl 131 in 75 scope 1 type 3
    simulation.statement_site.decl 1131 on 131 phase 0
    simulation.vpi_statement_relation.decl code_unit 75 type 24 selector 104 ordinal 0 modes 1 to 131
    simulation.func @conditional(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 75 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_conditional id 1031 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 131 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 32 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 131 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_conditional>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_conditional> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_conditional> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_conditional> selector 97 iterate ordinal 2 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 76 in 1 initial hierarchy "top.arithmetic_left"
    simulation.statement.decl 132 in 76 scope 1 type 3
    simulation.statement_site.decl 1132 on 132 phase 0
    simulation.vpi_statement_relation.decl code_unit 76 type 24 selector 104 ordinal 0 modes 1 to 132
    simulation.func @arithmetic_left(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 76 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_arithmetic_left id 1032 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 132 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #s8, op_type = 41 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 132 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_arithmetic_left>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_arithmetic_left> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64> {simulation.vpi_expression_identifier = "negative"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_arithmetic_left> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 77 in 1 initial hierarchy "top.arithmetic_right"
    simulation.statement.decl 133 in 77 scope 1 type 3
    simulation.statement_site.decl 1133 on 133 phase 0
    simulation.vpi_statement_relation.decl code_unit 77 type 24 selector 104 ordinal 0 modes 1 to 133
    simulation.func @arithmetic_right(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 77 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_arithmetic_right id 1033 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 133 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #s8, op_type = 42 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 133 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_arithmetic_right>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_arithmetic_right> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64> {simulation.vpi_expression_identifier = "negative"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_arithmetic_right> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 78 in 1 initial hierarchy "top.power"
    simulation.statement.decl 134 in 78 scope 1 type 3
    simulation.statement_site.decl 1134 on 134 phase 0
    simulation.vpi_statement_relation.decl code_unit 78 type 24 selector 104 ordinal 0 modes 1 to 134
    simulation.func @power(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 78 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_power id 1034 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 134 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 43 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 134 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_power>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_power> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_power> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 79 in 1 initial hierarchy "top.implies"
    simulation.statement.decl 135 in 79 scope 1 type 3
    simulation.statement_site.decl 1135 on 135 phase 0
    simulation.vpi_statement_relation.decl code_unit 79 type 24 selector 104 ordinal 0 modes 1 to 135
    simulation.func @implies(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 79 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_implies id 1035 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 135 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 50 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 135 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_implies>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_implies> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_implies> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 80 in 1 initial hierarchy "top.wild_equal"
    simulation.statement.decl 136 in 80 scope 1 type 3
    simulation.statement_site.decl 1136 on 136 phase 0
    simulation.vpi_statement_relation.decl code_unit 80 type 24 selector 104 ordinal 0 modes 1 to 136
    simulation.func @wild_equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 80 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_wild_equal id 1036 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 136 : i64> selector 82 handle ordinal 0 : !simulation.logic<1> {vpi_type = #f1, op_type = 69 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 136 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_wild_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_wild_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 4 : i64> {simulation.vpi_expression_identifier = "mixed"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_wild_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 5 : i64> {simulation.vpi_expression_identifier = "wild"}
    simulation.code_unit.decl 81 in 1 initial hierarchy "top.wild_not_equal"
    simulation.statement.decl 137 in 81 scope 1 type 3
    simulation.statement_site.decl 1137 on 137 phase 0
    simulation.vpi_statement_relation.decl code_unit 81 type 24 selector 104 ordinal 0 modes 1 to 137
    simulation.func @wild_not_equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 81 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_wild_not_equal id 1037 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 137 : i64> selector 82 handle ordinal 0 : !simulation.logic<1> {vpi_type = #f1, op_type = 70 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 137 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_wild_not_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_wild_not_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 4 : i64> {simulation.vpi_expression_identifier = "mixed"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_wild_not_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 5 : i64> {simulation.vpi_expression_identifier = "wild"}
    simulation.code_unit.decl 82 in 1 initial hierarchy "top.signed_divide"
    simulation.statement.decl 138 in 82 scope 1 type 3
    simulation.statement_site.decl 1138 on 138 phase 0
    simulation.vpi_statement_relation.decl code_unit 82 type 24 selector 104 ordinal 0 modes 1 to 138
    simulation.func @signed_divide(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 82 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_signed_divide id 1038 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 138 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #sf8, op_type = 12 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 138 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_signed_divide>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_signed_divide> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64> {simulation.vpi_expression_identifier = "negative"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_signed_divide> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 3 : i64> {simulation.vpi_expression_identifier = "signed_three"}
    simulation.code_unit.decl 83 in 1 initial hierarchy "top.signed_remainder"
    simulation.statement.decl 139 in 83 scope 1 type 3
    simulation.statement_site.decl 1139 on 139 phase 0
    simulation.vpi_statement_relation.decl code_unit 83 type 24 selector 104 ordinal 0 modes 1 to 139
    simulation.func @signed_remainder(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 83 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_signed_remainder id 1039 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 139 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #sf8, op_type = 13 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 139 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_signed_remainder>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_signed_remainder> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64> {simulation.vpi_expression_identifier = "negative"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_signed_remainder> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 3 : i64> {simulation.vpi_expression_identifier = "signed_three"}
    simulation.code_unit.decl 84 in 1 initial hierarchy "top.signed_less"
    simulation.statement.decl 140 in 84 scope 1 type 3
    simulation.statement_site.decl 1140 on 140 phase 0
    simulation.vpi_statement_relation.decl code_unit 84 type 24 selector 104 ordinal 0 modes 1 to 140
    simulation.func @signed_less(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 84 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_signed_less id 1040 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 140 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 20 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 140 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_signed_less>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_signed_less> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64> {simulation.vpi_expression_identifier = "negative"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_signed_less> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 3 : i64> {simulation.vpi_expression_identifier = "signed_three"}
    simulation.code_unit.decl 85 in 1 initial hierarchy "top.unknown_known_mismatch"
    simulation.statement.decl 141 in 85 scope 1 type 3
    simulation.statement_site.decl 1141 on 141 phase 0
    simulation.vpi_statement_relation.decl code_unit 85 type 24 selector 104 ordinal 0 modes 1 to 141
    simulation.func @unknown_known_mismatch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 85 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_unknown_known_mismatch id 1041 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 141 : i64> selector 82 handle ordinal 0 : !simulation.logic<1> {vpi_type = #f1, op_type = 14 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 141 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_known_mismatch>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_known_mismatch> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 4 : i64> {simulation.vpi_expression_identifier = "mixed"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_known_mismatch> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 6 : i64> {simulation.vpi_expression_identifier = "zero"}
    simulation.code_unit.decl 86 in 1 initial hierarchy "top.unknown_equal"
    simulation.statement.decl 142 in 86 scope 1 type 3
    simulation.statement_site.decl 1142 on 142 phase 0
    simulation.vpi_statement_relation.decl code_unit 86 type 24 selector 104 ordinal 0 modes 1 to 142
    simulation.func @unknown_equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 86 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_unknown_equal id 1042 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 142 : i64> selector 82 handle ordinal 0 : !simulation.logic<1> {vpi_type = #f1, op_type = 14 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 142 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_equal>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_equal> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 4 : i64> {simulation.vpi_expression_identifier = "mixed"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_equal> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 4 : i64> {simulation.vpi_expression_identifier = "mixed"}
    simulation.code_unit.decl 87 in 1 initial hierarchy "top.unknown_and_zero"
    simulation.statement.decl 143 in 87 scope 1 type 3
    simulation.statement_site.decl 1143 on 143 phase 0
    simulation.vpi_statement_relation.decl code_unit 87 type 24 selector 104 ordinal 0 modes 1 to 143
    simulation.func @unknown_and_zero(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 87 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_unknown_and_zero id 1043 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 143 : i64> selector 82 handle ordinal 0 : !simulation.logic<1> {vpi_type = #f1, op_type = 26 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 143 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_and_zero>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_and_zero> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 7 : i64> {simulation.vpi_expression_identifier = "unknown"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_and_zero> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 6 : i64> {simulation.vpi_expression_identifier = "zero"}
    simulation.code_unit.decl 88 in 1 initial hierarchy "top.known_one_dominates"
    simulation.statement.decl 144 in 88 scope 1 type 3
    simulation.statement_site.decl 1144 on 144 phase 0
    simulation.vpi_statement_relation.decl code_unit 88 type 24 selector 104 ordinal 0 modes 1 to 144
    simulation.func @known_one_dominates(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 88 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_known_one_dominates id 1044 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 144 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 32 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 144 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_known_one_dominates>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_known_one_dominates> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 4 : i64> {simulation.vpi_expression_identifier = "mixed"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_known_one_dominates> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_known_one_dominates> selector 97 iterate ordinal 2 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.code_unit.decl 89 in 1 initial hierarchy "top.ambiguous_merge"
    simulation.statement.decl 145 in 89 scope 1 type 3
    simulation.statement_site.decl 1145 on 145 phase 0
    simulation.vpi_statement_relation.decl code_unit 89 type 24 selector 104 ordinal 0 modes 1 to 145
    simulation.func @ambiguous_merge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 89 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_ambiguous_merge id 1045 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 145 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 32 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 145 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_ambiguous_merge>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_ambiguous_merge> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 7 : i64> {simulation.vpi_expression_identifier = "unknown"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_ambiguous_merge> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 4 : i64> {simulation.vpi_expression_identifier = "mixed"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_ambiguous_merge> selector 97 iterate ordinal 2 to #simulation.vpi_ref<kind = storage, id = 5 : i64> {simulation.vpi_expression_identifier = "wild"}
    simulation.code_unit.decl 90 in 1 initial hierarchy "top.zero_divisor"
    simulation.statement.decl 146 in 90 scope 1 type 3
    simulation.statement_site.decl 1146 on 146 phase 0
    simulation.vpi_statement_relation.decl code_unit 90 type 24 selector 104 ordinal 0 modes 1 to 146
    simulation.func @zero_divisor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 90 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_zero_divisor id 1046 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 146 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 12 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 146 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_zero_divisor>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_zero_divisor> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_zero_divisor> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 6 : i64> {simulation.vpi_expression_identifier = "zero"}
    simulation.code_unit.decl 91 in 1 initial hierarchy "top.unknown_shift"
    simulation.statement.decl 147 in 91 scope 1 type 3
    simulation.statement_site.decl 1147 on 147 phase 0
    simulation.vpi_statement_relation.decl code_unit 91 type 24 selector 104 ordinal 0 modes 1 to 147
    simulation.func @unknown_shift(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 91 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_unknown_shift id 1047 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 147 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 22 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 147 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_shift>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_shift> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_shift> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 7 : i64> {simulation.vpi_expression_identifier = "unknown"}
    simulation.code_unit.decl 92 in 1 initial hierarchy "top.negative_power"
    simulation.statement.decl 148 in 92 scope 1 type 3
    simulation.statement_site.decl 1148 on 148 phase 0
    simulation.vpi_statement_relation.decl code_unit 92 type 24 selector 104 ordinal 0 modes 1 to 148
    simulation.func @negative_power(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 92 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_negative_power id 1048 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 148 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #sf8, op_type = 43 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 148 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_negative_power>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_negative_power> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 8 : i64> {simulation.vpi_expression_identifier = "minus_one"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_negative_power> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 9 : i64> {simulation.vpi_expression_identifier = "minus_three"}
    simulation.code_unit.decl 93 in 1 initial hierarchy "top.zero_negative_power"
    simulation.statement.decl 149 in 93 scope 1 type 3
    simulation.statement_site.decl 1149 on 149 phase 0
    simulation.vpi_statement_relation.decl code_unit 93 type 24 selector 104 ordinal 0 modes 1 to 149
    simulation.func @zero_negative_power(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 93 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_zero_negative_power id 1049 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 149 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 43 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 149 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_zero_negative_power>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_zero_negative_power> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 6 : i64> {simulation.vpi_expression_identifier = "zero"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_zero_negative_power> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 9 : i64> {simulation.vpi_expression_identifier = "minus_three"}
    simulation.code_unit.decl 94 in 1 initial hierarchy "top.positive_negative_power"
    simulation.statement.decl 150 in 94 scope 1 type 3
    simulation.statement_site.decl 1150 on 150 phase 0
    simulation.vpi_statement_relation.decl code_unit 94 type 24 selector 104 ordinal 0 modes 1 to 150
    simulation.func @positive_negative_power(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 94 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_positive_negative_power id 1050 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 150 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 43 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 150 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_positive_negative_power>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_positive_negative_power> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_positive_negative_power> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 9 : i64> {simulation.vpi_expression_identifier = "minus_three"}
    simulation.code_unit.decl 95 in 1 initial hierarchy "top.bit_known_zero"
    simulation.statement.decl 151 in 95 scope 1 type 3
    simulation.statement_site.decl 1151 on 151 phase 0
    simulation.vpi_statement_relation.decl code_unit 95 type 24 selector 104 ordinal 0 modes 1 to 151
    simulation.func @bit_known_zero(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 95 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_bit_known_zero id 1051 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 151 : i64> selector 82 handle ordinal 0 : !simulation.logic<8> {vpi_type = #f8, op_type = 28 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 151 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_bit_known_zero>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_bit_known_zero> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 7 : i64> {simulation.vpi_expression_identifier = "unknown"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_bit_known_zero> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 6 : i64> {simulation.vpi_expression_identifier = "zero"}
    simulation.code_unit.decl 96 in 1 initial hierarchy "top.unknown_reduce_and"
    simulation.statement.decl 152 in 96 scope 1 type 3
    simulation.statement_site.decl 1152 on 152 phase 0
    simulation.vpi_statement_relation.decl code_unit 96 type 24 selector 104 ordinal 0 modes 1 to 152
    simulation.func @unknown_reduce_and(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 96 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_unknown_reduce_and id 1052 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 152 : i64> selector 82 handle ordinal 0 : !simulation.logic<1> {vpi_type = #f1, op_type = 5 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 152 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_reduce_and>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unknown_reduce_and> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 7 : i64> {simulation.vpi_expression_identifier = "unknown"}
    simulation.code_unit.decl 97 in 1 initial hierarchy "top.widened"
    simulation.statement.decl 153 in 97 scope 1 type 3
    simulation.statement_site.decl 1153 on 153 phase 0
    simulation.vpi_statement_relation.decl code_unit 97 type 24 selector 104 ordinal 0 modes 1 to 153
    simulation.func @widened(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 97 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_widened id 1053 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 153 : i64> selector 82 handle ordinal 0 : i16 {vpi_type = #u16, op_type = 22 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 153 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_widened>
    simulation.vpi_expression.decl @sum_widened id 1054 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @op_widened> selector 97 iterate ordinal 0 : i16 {vpi_type = #u16, op_type = 24 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_widened> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @sum_widened>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @sum_widened> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64> {simulation.vpi_expression_identifier = "a"}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @sum_widened> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 1 : i64> {simulation.vpi_expression_identifier = "b"}
    simulation.vpi_expression.decl @shift_one id 1055 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @op_widened> selector 97 iterate ordinal 1 : i8 {vpi_type = #u8, immutable_value = #simulation.frozen_constant<value = [1 : i8, 0 : i8], isSigned = false> : i8}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_widened> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @shift_one>
    simulation.code_unit.decl 98 in 1 initial hierarchy "top.unsigned_context"
    simulation.statement.decl 154 in 98 scope 1 type 3
    simulation.statement_site.decl 1154 on 154 phase 0
    simulation.vpi_statement_relation.decl code_unit 98 type 24 selector 104 ordinal 0 modes 1 to 154
    simulation.func @unsigned_context(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 98 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_unsigned_context id 1056 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 154 : i64> selector 82 handle ordinal 0 : i16 {vpi_type = #u16, op_type = 24 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 154 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_unsigned_context>
    simulation.vpi_expression.decl @negative_context id 1057 type 39 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @op_unsigned_context> selector 97 iterate ordinal 0 : i16 {vpi_type = #u16, op_type = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unsigned_context> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @negative_context>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @negative_context> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64> {simulation.vpi_expression_identifier = "negative"}
    simulation.vpi_expression.decl @add_one id 1058 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @op_unsigned_context> selector 97 iterate ordinal 1 : i8 {vpi_type = #u8, immutable_value = #simulation.frozen_constant<value = [1 : i8, 0 : i8], isSigned = false> : i8}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_unsigned_context> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @add_one>
    simulation.code_unit.decl 99 in 1 initial hierarchy "top.wide"
    simulation.statement.decl 155 in 99 scope 1 type 3
    simulation.statement_site.decl 1155 on 155 phase 0
    simulation.vpi_statement_relation.decl code_unit 99 type 24 selector 104 ordinal 0 modes 1 to 155
    simulation.func @wide(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 99 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_wide id 1059 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 155 : i64> selector 82 handle ordinal 0 : i130 {vpi_type = #u130, op_type = 24 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 155 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_wide>
    simulation.vpi_expression.decl @wide_left id 1060 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @op_wide> selector 97 iterate ordinal 0 : i130 {vpi_type = #u130, immutable_value = #simulation.frozen_constant<value = [340282366920938463463374607431768211455 : i130, 0 : i130], isSigned = false> : i130}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_wide> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @wide_left>
    simulation.vpi_expression.decl @wide_right id 1061 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @op_wide> selector 97 iterate ordinal 1 : i130 {vpi_type = #u130, immutable_value = #simulation.frozen_constant<value = [2 : i130, 0 : i130], isSigned = false> : i130}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_wide> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @wide_right>
    simulation.code_unit.decl 100 in 1 initial hierarchy "top.wide_unknown"
    simulation.statement.decl 156 in 100 scope 1 type 3
    simulation.statement_site.decl 1156 on 156 phase 0
    simulation.vpi_statement_relation.decl code_unit 100 type 24 selector 104 ordinal 0 modes 1 to 156
    simulation.func @wide_unknown(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 100 : i64} { simulation.return }
    simulation.vpi_expression.decl @op_wide_unknown id 1062 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 156 : i64> selector 82 handle ordinal 0 : !simulation.logic<130> {vpi_type = #f130, op_type = 25 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 156 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @op_wide_unknown>
    simulation.vpi_expression.decl @wide_x id 1063 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @op_wide_unknown> selector 97 iterate ordinal 0 : !simulation.logic<130> {vpi_type = #f130, immutable_value = #simulation.frozen_constant<value = [0 : i130, 680564733841876926926749214863536422912 : i130], isSigned = false> : !simulation.logic<130>}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_wide_unknown> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @wide_x>
    simulation.vpi_expression.decl @wide_two id 1064 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @op_wide_unknown> selector 97 iterate ordinal 1 : i130 {vpi_type = #u130, immutable_value = #simulation.frozen_constant<value = [2 : i130, 0 : i130], isSigned = false> : i130}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @op_wide_unknown> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @wide_two>
  }
}
// CHECK: simulation.vpi_expression.decl @op_widened
// CHECK: simulation.vpi_expression.decl @op_wide_unknown
