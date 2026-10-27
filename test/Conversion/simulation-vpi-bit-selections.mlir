// RUN: talos-opt %s | FileCheck %s
// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.BitSelections
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
    simulation.driver.decl 0 in 1 drives 0 : i8 design {driven_low = 7 : i64, driven_width = 1 : i64, strength0 = 3 : i32, strength1 = 3 : i32}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.first"
    simulation.statement.decl 100 in 44 scope 1 type 3
    simulation.statement_site.decl 1100 on 100 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 100
    simulation.func @run_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    simulation.vpi_expression.decl @first id 1000 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @first_index id 1001 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @first> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @first_index>
    simulation.code_unit.decl 45 in 1 initial hierarchy "top.again"
    simulation.statement.decl 101 in 45 scope 1 type 3
    simulation.statement_site.decl 1101 on 101 phase 0
    simulation.vpi_statement_relation.decl code_unit 45 type 24 selector 104 ordinal 0 modes 1 to 101
    simulation.func @run_again(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} { simulation.return }
    simulation.vpi_expression.decl @again id 1002 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 101 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 101 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @again>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @again> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @again_index id 1003 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @again> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @again> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @again_index>
    simulation.code_unit.decl 46 in 1 initial hierarchy "top.low"
    simulation.statement.decl 102 in 46 scope 1 type 3
    simulation.statement_site.decl 1102 on 102 phase 0
    simulation.vpi_statement_relation.decl code_unit 46 type 24 selector 104 ordinal 0 modes 1 to 102
    simulation.func @run_low(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 46 : i64} { simulation.return }
    simulation.vpi_expression.decl @low id 1004 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 102 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 102 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @low>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @low> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @low_index id 1005 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @low> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @low> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @low_index>
    simulation.code_unit.decl 47 in 1 initial hierarchy "top.dynamic"
    simulation.statement.decl 103 in 47 scope 1 type 3
    simulation.statement_site.decl 1103 on 103 phase 0
    simulation.vpi_statement_relation.decl code_unit 47 type 24 selector 104 ordinal 0 modes 1 to 103
    simulation.func @run_dynamic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 47 : i64} { simulation.return }
    simulation.vpi_expression.decl @dynamic id 1006 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 103 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 103 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @dynamic>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @dynamic> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @dynamic> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 3 : i64>
    simulation.code_unit.decl 48 in 1 initial hierarchy "top.ascending"
    simulation.statement.decl 104 in 48 scope 1 type 3
    simulation.statement_site.decl 1104 on 104 phase 0
    simulation.vpi_statement_relation.decl code_unit 48 type 24 selector 104 ordinal 0 modes 1 to 104
    simulation.func @run_ascending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 48 : i64} { simulation.return }
    simulation.vpi_expression.decl @ascending id 1007 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 104 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 104 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 2 : i64>
    simulation.vpi_expression.decl @ascending_index id 1008 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @ascending> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @ascending> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @ascending_index>
    simulation.code_unit.decl 49 in 1 initial hierarchy "top.net"
    simulation.statement.decl 105 in 49 scope 1 type 3
    simulation.statement_site.decl 1105 on 105 phase 0
    simulation.vpi_statement_relation.decl code_unit 49 type 24 selector 104 ordinal 0 modes 1 to 105
    simulation.func @run_net(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 49 : i64} {
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<i8>
      %one = arith.constant 128 : i8
      simulation.driver.drive %driver = %one : !simulation.driver<i8>, i8
      simulation.return
    }
    simulation.vpi_expression.decl @net id 1009 type 37 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 105 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 105 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @net_index id 1010 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @net> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @net> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @net_index>
    simulation.code_unit.decl 50 in 1 initial hierarchy "top.parameter"
    simulation.statement.decl 106 in 50 scope 1 type 3
    simulation.statement_site.decl 1106 on 106 phase 0
    simulation.vpi_statement_relation.decl code_unit 50 type 24 selector 104 ordinal 0 modes 1 to 106
    simulation.func @run_parameter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 50 : i64} { simulation.return }
    simulation.vpi_expression.decl @parameter id 1011 type 106 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 106 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 106 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @parameter>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @parameter> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 1 : i64, symbol = @P>
    simulation.vpi_expression.decl @parameter_index id 1012 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @parameter> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @parameter> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @parameter_index>
    simulation.code_unit.decl 51 in 1 initial hierarchy "top.other_parameter"
    simulation.statement.decl 107 in 51 scope 1 type 3
    simulation.statement_site.decl 1107 on 107 phase 0
    simulation.vpi_statement_relation.decl code_unit 51 type 24 selector 104 ordinal 0 modes 1 to 107
    simulation.func @run_other_parameter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 51 : i64} { simulation.return }
    simulation.vpi_expression.decl @other_parameter id 1013 type 106 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 107 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 107 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @other_parameter>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @other_parameter> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = definition_member, id = 2 : i64, symbol = @P>
    simulation.vpi_expression.decl @other_parameter_index id 1014 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @other_parameter> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @other_parameter> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @other_parameter_index>
    simulation.code_unit.decl 52 in 1 initial hierarchy "top.invalid"
    simulation.statement.decl 108 in 52 scope 1 type 3
    simulation.statement_site.decl 1108 on 108 phase 0
    simulation.vpi_statement_relation.decl code_unit 52 type 24 selector 104 ordinal 0 modes 1 to 108
    simulation.func @run_invalid(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 52 : i64} { simulation.return }
    simulation.vpi_expression.decl @invalid id 1015 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 108 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 108 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @invalid>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @invalid> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @invalid_index id 1016 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @invalid> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [8 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @invalid> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @invalid_index>
    simulation.code_unit.decl 53 in 1 initial hierarchy "top.negative"
    simulation.statement.decl 109 in 53 scope 1 type 3
    simulation.statement_site.decl 1109 on 109 phase 0
    simulation.vpi_statement_relation.decl code_unit 53 type 24 selector 104 ordinal 0 modes 1 to 109
    simulation.func @run_negative(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 53 : i64} { simulation.return }
    simulation.vpi_expression.decl @negative id 1017 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 109 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 109 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @negative>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @negative> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @negative_index id 1018 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @negative> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [-1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @negative> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @negative_index>
    simulation.code_unit.decl 54 in 1 initial hierarchy "top.unknown"
    simulation.statement.decl 110 in 54 scope 1 type 3
    simulation.statement_site.decl 1110 on 110 phase 0
    simulation.vpi_statement_relation.decl code_unit 54 type 24 selector 104 ordinal 0 modes 1 to 110
    simulation.func @run_unknown(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 54 : i64} { simulation.return }
    simulation.vpi_expression.decl @unknown id 1019 type 49 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 110 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 110 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @unknown>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @unknown> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @unknown_index id 1020 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @unknown> selector 78 handle ordinal 0 : !simulation.logic<32> {vpi_type = #index, immutable_value = #simulation.frozen_constant<value = [0 : i32, 1 : i32], isSigned = true> : !simulation.logic<32>}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @unknown> selector 78 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @unknown_index>
    simulation.code_unit.decl 55 in 1 initial hierarchy "top.overlap"
    simulation.statement.decl 111 in 55 scope 1 type 3
    simulation.statement_site.decl 1111 on 111 phase 0
    simulation.vpi_statement_relation.decl code_unit 55 type 24 selector 104 ordinal 0 modes 1 to 111
    simulation.func @run_overlap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 55 : i64} { simulation.return }
    simulation.vpi_expression.decl @overlap id 1021 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 111 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 111 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @overlap>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @overlap> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @overlap_left id 1022 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @overlap> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @overlap> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @overlap_left>
    simulation.vpi_expression.decl @overlap_right id 1023 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @overlap> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @overlap> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @overlap_right>
    simulation.code_unit.decl 56 in 1 initial hierarchy "top.nonoverlap"
    simulation.statement.decl 112 in 56 scope 1 type 3
    simulation.statement_site.decl 1112 on 112 phase 0
    simulation.vpi_statement_relation.decl code_unit 56 type 24 selector 104 ordinal 0 modes 1 to 112
    simulation.func @run_nonoverlap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 56 : i64} { simulation.return }
    simulation.vpi_expression.decl @nonoverlap id 1024 type 42 in 1 owned_by #simulation.vpi_ref<kind = statement, id = 112 : i64> selector 82 handle ordinal 0 : i3 {vpi_type = #u3}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 112 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @nonoverlap>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @nonoverlap> selector 81 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @nonoverlap_left id 1025 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @nonoverlap> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @nonoverlap> selector 79 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @nonoverlap_left>
    simulation.vpi_expression.decl @nonoverlap_right id 1026 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @nonoverlap> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @nonoverlap> selector 83 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @nonoverlap_right>
    simulation.code_unit.decl 57 in 1 initial hierarchy "top.whole"
    simulation.statement.decl 113 in 57 scope 1 type 3
    simulation.statement_site.decl 1113 on 113 phase 0
    simulation.vpi_statement_relation.decl code_unit 57 type 24 selector 104 ordinal 0 modes 1 to 113
    simulation.func @run_whole(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 57 : i64} { simulation.return }
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 113 : i64> selector 82 handle ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = storage, id = 0 : i64> selector 101 iterate ordinal 0 to #simulation.vpi_ref<kind = statement, id = 113 : i64>
  }
}
// CHECK: simulation.vpi_expression.decl @first
// CHECK: simulation.vpi_expression.decl @parameter
