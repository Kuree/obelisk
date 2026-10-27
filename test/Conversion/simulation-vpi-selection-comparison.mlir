// RUN: talos-opt %s | FileCheck %s
// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SelectionComparison
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
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.net_first"
    simulation.statement.decl 100 in 44 scope 1 type 3
    simulation.statement_site.decl 1100 on 100 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 100
    simulation.func @run_net_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    simulation.vpi_expression.decl @net_first id 1000 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_first>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_first> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @net_first_left id 1001 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net_first> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_first> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_first_left>
    simulation.vpi_expression.decl @net_first_right id 1002 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net_first> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_first> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_first_right>
    simulation.code_unit.decl 45 in 1 initial hierarchy "top.net_again"
    simulation.statement.decl 101 in 45 scope 1 type 3
    simulation.statement_site.decl 1101 on 101 phase 0
    simulation.vpi_statement_relation.decl code_unit 45 type 24 selector 104 ordinal 0 modes 1 to 101
    simulation.func @run_net_again(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} { simulation.return }
    simulation.vpi_expression.decl @net_again id 1003 type 130 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 101 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 101 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_again>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_again> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @net_again_left id 1004 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net_again> selector 131 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_again> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_again_left>
    simulation.vpi_expression.decl @net_again_right id 1005 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net_again> selector 132 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_again> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_again_right>
    simulation.code_unit.decl 46 in 1 initial hierarchy "top.net_bit"
    simulation.statement.decl 102 in 46 scope 1 type 3
    simulation.statement_site.decl 1102 on 102 phase 0
    simulation.vpi_statement_relation.decl code_unit 46 type 24 selector 104 ordinal 0 modes 1 to 102
    simulation.func @run_net_bit(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 46 : i64} { simulation.return }
    simulation.vpi_expression.decl @net_bit id 1006 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 102 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 102 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_bit>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_bit> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @net_bit_left id 1007 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net_bit> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_bit> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_bit_left>
    simulation.vpi_expression.decl @net_bit_right id 1008 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net_bit> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_bit> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_bit_right>
    simulation.code_unit.decl 47 in 1 initial hierarchy "top.net_whole"
    simulation.statement.decl 103 in 47 scope 1 type 3
    simulation.statement_site.decl 1103 on 103 phase 0
    simulation.vpi_statement_relation.decl code_unit 47 type 24 selector 104 ordinal 0 modes 1 to 103
    simulation.func @run_net_whole(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 47 : i64} { simulation.return }
    simulation.vpi_expression.decl @net_whole id 1009 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 103 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 103 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_whole>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_whole> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @net_whole_left id 1010 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net_whole> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_whole> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_whole_left>
    simulation.vpi_expression.decl @net_whole_right id 1011 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net_whole> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net_whole> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_whole_right>
    simulation.code_unit.decl 48 in 1 initial hierarchy "top.first"
    simulation.statement.decl 104 in 48 scope 1 type 3
    simulation.statement_site.decl 1104 on 104 phase 0
    simulation.vpi_statement_relation.decl code_unit 48 type 24 selector 104 ordinal 0 modes 1 to 104
    simulation.func @run_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 48 : i64} { simulation.return }
    simulation.vpi_expression.decl @first id 1012 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 104 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 104 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @first_left id 1013 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @first> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first_left>
    simulation.vpi_expression.decl @first_right id 1014 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @first> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first_right>
    simulation.code_unit.decl 49 in 1 initial hierarchy "top.again"
    simulation.statement.decl 105 in 49 scope 1 type 3
    simulation.statement_site.decl 1105 on 105 phase 0
    simulation.vpi_statement_relation.decl code_unit 49 type 24 selector 104 ordinal 0 modes 1 to 105
    simulation.func @run_again(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 49 : i64} { simulation.return }
    simulation.vpi_expression.decl @again id 1015 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 105 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 105 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @again>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @again> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @again_left id 1016 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @again> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @again> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @again_left>
    simulation.vpi_expression.decl @again_right id 1017 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @again> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @again> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @again_right>
    simulation.code_unit.decl 50 in 1 initial hierarchy "top.different"
    simulation.statement.decl 106 in 50 scope 1 type 3
    simulation.statement_site.decl 1106 on 106 phase 0
    simulation.vpi_statement_relation.decl code_unit 50 type 24 selector 104 ordinal 0 modes 1 to 106
    simulation.func @run_different(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 50 : i64} { simulation.return }
    simulation.vpi_expression.decl @different id 1018 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 106 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 106 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @different_left id 1019 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @different> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [6 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_left>
    simulation.vpi_expression.decl @different_right id 1020 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @different> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @different> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @different_right>
    simulation.code_unit.decl 51 in 1 initial hierarchy "top.plus"
    simulation.statement.decl 107 in 51 scope 1 type 3
    simulation.statement_site.decl 1107 on 107 phase 0
    simulation.vpi_statement_relation.decl code_unit 51 type 24 selector 104 ordinal 0 modes 1 to 107
    simulation.func @run_plus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 51 : i64} { simulation.return }
    simulation.vpi_expression.decl @plus id 1021 type 130 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 107 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 107 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @plus>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @plus> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @plus_left id 1022 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @plus> selector 131 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @plus> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @plus_left>
    simulation.vpi_expression.decl @plus_right id 1023 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @plus> selector 132 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @plus> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @plus_right>
    simulation.code_unit.decl 52 in 1 initial hierarchy "top.minus"
    simulation.statement.decl 108 in 52 scope 1 type 3
    simulation.statement_site.decl 1108 on 108 phase 0
    simulation.vpi_statement_relation.decl code_unit 52 type 24 selector 104 ordinal 0 modes 1 to 108
    simulation.func @run_minus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 52 : i64} { simulation.return }
    simulation.vpi_expression.decl @minus id 1024 type 130 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 108 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4, indexed_direction = 2 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 108 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @minus>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @minus> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @minus_left id 1025 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @minus> selector 131 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @minus> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @minus_left>
    simulation.vpi_expression.decl @minus_right id 1026 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @minus> selector 132 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @minus> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @minus_right>
    simulation.code_unit.decl 53 in 1 initial hierarchy "top.dynamic"
    simulation.statement.decl 109 in 53 scope 1 type 3
    simulation.statement_site.decl 1109 on 109 phase 0
    simulation.vpi_statement_relation.decl code_unit 53 type 24 selector 104 ordinal 0 modes 1 to 109
    simulation.func @run_dynamic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 53 : i64} { simulation.return }
    simulation.vpi_expression.decl @dynamic id 1027 type 130 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 109 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 109 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @dynamic>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @dynamic> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @dynamic> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 3 : i64>
    simulation.vpi_expression.decl @dynamic_right id 1028 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @dynamic> selector 132 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @dynamic> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @dynamic_right>
    simulation.code_unit.decl 54 in 1 initial hierarchy "top.bit"
    simulation.statement.decl 110 in 54 scope 1 type 3
    simulation.statement_site.decl 1110 on 110 phase 0
    simulation.vpi_statement_relation.decl code_unit 54 type 24 selector 104 ordinal 0 modes 1 to 110
    simulation.func @run_bit(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 54 : i64} { simulation.return }
    simulation.vpi_expression.decl @bit id 1029 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 110 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 110 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @bit>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @bit> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @bit_left id 1030 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @bit> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @bit> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @bit_left>
    simulation.vpi_expression.decl @bit_right id 1031 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @bit> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @bit> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @bit_right>
    simulation.code_unit.decl 55 in 1 initial hierarchy "top.whole"
    simulation.statement.decl 111 in 55 scope 1 type 3
    simulation.statement_site.decl 1111 on 111 phase 0
    simulation.vpi_statement_relation.decl code_unit 55 type 24 selector 104 ordinal 0 modes 1 to 111
    simulation.func @run_whole(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 55 : i64} { simulation.return }
    simulation.vpi_expression.decl @whole id 1032 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 111 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 111 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @whole>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @whole> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @whole_left id 1033 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @whole> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @whole> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @whole_left>
    simulation.vpi_expression.decl @whole_right id 1034 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @whole> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @whole> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @whole_right>
    simulation.code_unit.decl 56 in 1 initial hierarchy "top.invalid"
    simulation.statement.decl 112 in 56 scope 1 type 3
    simulation.statement_site.decl 1112 on 112 phase 0
    simulation.vpi_statement_relation.decl code_unit 56 type 24 selector 104 ordinal 0 modes 1 to 112
    simulation.func @run_invalid(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 56 : i64} { simulation.return }
    simulation.vpi_expression.decl @invalid id 1035 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 112 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 112 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @invalid>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @invalid> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @invalid_left id 1036 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @invalid> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [9 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @invalid> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @invalid_left>
    simulation.vpi_expression.decl @invalid_right id 1037 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @invalid> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [6 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @invalid> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @invalid_right>
    simulation.code_unit.decl 57 in 1 initial hierarchy "top.negative"
    simulation.statement.decl 113 in 57 scope 1 type 3
    simulation.statement_site.decl 1113 on 113 phase 0
    simulation.vpi_statement_relation.decl code_unit 57 type 24 selector 104 ordinal 0 modes 1 to 113
    simulation.func @run_negative(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 57 : i64} { simulation.return }
    simulation.vpi_expression.decl @negative id 1038 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 113 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 113 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @negative>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @negative> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @negative_left id 1039 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @negative> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @negative> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @negative_left>
    simulation.vpi_expression.decl @negative_right id 1040 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @negative> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [-2 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @negative> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @negative_right>
    simulation.code_unit.decl 58 in 1 initial hierarchy "top.other_value"
    simulation.statement.decl 114 in 58 scope 1 type 3
    simulation.statement_site.decl 1114 on 114 phase 0
    simulation.vpi_statement_relation.decl code_unit 58 type 24 selector 104 ordinal 0 modes 1 to 114
    simulation.func @run_other_value(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 58 : i64} { simulation.return }
    simulation.vpi_expression.decl @other_value id 1041 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 114 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 114 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @other_value>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @other_value> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 1 : i64>
    simulation.vpi_expression.decl @other_value_left id 1042 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @other_value> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @other_value> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @other_value_left>
    simulation.vpi_expression.decl @other_value_right id 1043 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @other_value> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @other_value> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @other_value_right>
    simulation.code_unit.decl 59 in 1 initial hierarchy "top.ascending"
    simulation.statement.decl 115 in 59 scope 1 type 3
    simulation.statement_site.decl 1115 on 115 phase 0
    simulation.vpi_statement_relation.decl code_unit 59 type 24 selector 104 ordinal 0 modes 1 to 115
    simulation.func @run_ascending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 59 : i64} { simulation.return }
    simulation.vpi_expression.decl @ascending id 1044 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 115 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 115 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64>
    simulation.vpi_expression.decl @ascending_left id 1045 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @ascending> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_left>
    simulation.vpi_expression.decl @ascending_right id 1046 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @ascending> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_right>
    simulation.code_unit.decl 60 in 1 initial hierarchy "top.ascending_minus"
    simulation.statement.decl 116 in 60 scope 1 type 3
    simulation.statement_site.decl 1116 on 116 phase 0
    simulation.vpi_statement_relation.decl code_unit 60 type 24 selector 104 ordinal 0 modes 1 to 116
    simulation.func @run_ascending_minus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 60 : i64} { simulation.return }
    simulation.vpi_expression.decl @ascending_minus id 1047 type 130 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 116 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4, indexed_direction = 2 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 116 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_minus>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending_minus> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64>
    simulation.vpi_expression.decl @ascending_minus_left id 1048 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @ascending_minus> selector 131 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending_minus> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_minus_left>
    simulation.vpi_expression.decl @ascending_minus_right id 1049 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @ascending_minus> selector 132 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending_minus> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_minus_right>
    simulation.code_unit.decl 61 in 1 initial hierarchy "top.ascending_plus"
    simulation.statement.decl 117 in 61 scope 1 type 3
    simulation.statement_site.decl 1117 on 117 phase 0
    simulation.vpi_statement_relation.decl code_unit 61 type 24 selector 104 ordinal 0 modes 1 to 117
    simulation.func @run_ascending_plus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 61 : i64} { simulation.return }
    simulation.vpi_expression.decl @ascending_plus id 1050 type 130 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 117 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 117 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_plus>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending_plus> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64>
    simulation.vpi_expression.decl @ascending_plus_left id 1051 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @ascending_plus> selector 131 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending_plus> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_plus_left>
    simulation.vpi_expression.decl @ascending_plus_right id 1052 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @ascending_plus> selector 132 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending_plus> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_plus_right>
    simulation.code_unit.decl 62 in 1 initial hierarchy "top.parameter"
    simulation.statement.decl 118 in 62 scope 1 type 3
    simulation.statement_site.decl 1118 on 118 phase 0
    simulation.vpi_statement_relation.decl code_unit 62 type 24 selector 104 ordinal 0 modes 1 to 118
    simulation.func @run_parameter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 62 : i64} { simulation.return }
    simulation.vpi_expression.decl @parameter id 1053 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 118 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 118 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @parameter>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @parameter> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @P>
    simulation.vpi_expression.decl @parameter_left id 1054 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @parameter> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @parameter> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @parameter_left>
    simulation.vpi_expression.decl @parameter_right id 1055 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @parameter> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @parameter> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @parameter_right>
    simulation.code_unit.decl 63 in 1 initial hierarchy "top.parameter_again"
    simulation.statement.decl 119 in 63 scope 1 type 3
    simulation.statement_site.decl 1119 on 119 phase 0
    simulation.vpi_statement_relation.decl code_unit 63 type 24 selector 104 ordinal 0 modes 1 to 119
    simulation.func @run_parameter_again(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 63 : i64} { simulation.return }
    simulation.vpi_expression.decl @parameter_again id 1056 type 130 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 119 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4, indexed_direction = 1 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 119 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @parameter_again>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @parameter_again> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @P>
    simulation.vpi_expression.decl @parameter_again_left id 1057 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @parameter_again> selector 131 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @parameter_again> selector 131 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @parameter_again_left>
    simulation.vpi_expression.decl @parameter_again_right id 1058 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @parameter_again> selector 132 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @parameter_again> selector 132 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @parameter_again_right>
    simulation.code_unit.decl 64 in 1 initial hierarchy "top.other_parameter"
    simulation.statement.decl 120 in 64 scope 1 type 3
    simulation.statement_site.decl 1120 on 120 phase 0
    simulation.vpi_statement_relation.decl code_unit 64 type 24 selector 104 ordinal 0 modes 1 to 120
    simulation.func @run_other_parameter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 64 : i64} { simulation.return }
    simulation.vpi_expression.decl @other_parameter id 1059 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 120 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 120 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @other_parameter>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @other_parameter> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 2 : i64, symbol = @P>
    simulation.vpi_expression.decl @other_parameter_left id 1060 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @other_parameter> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @other_parameter> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @other_parameter_left>
    simulation.vpi_expression.decl @other_parameter_right id 1061 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @other_parameter> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @other_parameter> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @other_parameter_right>
    simulation.code_unit.decl 65 in 1 initial hierarchy "top.whole_parameter"
    simulation.statement.decl 121 in 65 scope 1 type 3
    simulation.statement_site.decl 1121 on 121 phase 0
    simulation.vpi_statement_relation.decl code_unit 65 type 24 selector 104 ordinal 0 modes 1 to 121
    simulation.func @run_whole_parameter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 65 : i64} { simulation.return }
    simulation.vpi_expression.decl @whole_parameter id 1062 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 121 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 121 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @whole_parameter>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @whole_parameter> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @P>
    simulation.vpi_expression.decl @whole_parameter_left id 1063 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @whole_parameter> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @whole_parameter> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @whole_parameter_left>
    simulation.vpi_expression.decl @whole_parameter_right id 1064 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @whole_parameter> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @whole_parameter> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @whole_parameter_right>
    simulation.code_unit.decl 66 in 1 initial hierarchy "top.concat"
    simulation.statement.decl 122 in 66 scope 1 type 3
    simulation.statement_site.decl 1122 on 122 phase 0
    simulation.vpi_statement_relation.decl code_unit 66 type 24 selector 104 ordinal 0 modes 1 to 122
    simulation.func @run_concat(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 66 : i64} { simulation.return }
    simulation.vpi_expression.decl @concat id 1065 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 122 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 33 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 122 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @concat>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @concat> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.code_unit.decl 67 in 1 initial hierarchy "top.concat_again"
    simulation.statement.decl 123 in 67 scope 1 type 3
    simulation.statement_site.decl 1123 on 123 phase 0
    simulation.vpi_statement_relation.decl code_unit 67 type 24 selector 104 ordinal 0 modes 1 to 123
    simulation.func @run_concat_again(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 67 : i64} { simulation.return }
    simulation.vpi_expression.decl @concat_again id 1066 type 39 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 123 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 33 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 123 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @concat_again>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @concat_again> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  }
}
// CHECK: simulation.vpi_expression.decl @whole_parameter
// CHECK: simulation.vpi_expression.decl @concat_again
