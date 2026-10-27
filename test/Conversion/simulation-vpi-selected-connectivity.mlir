// RUN: talos-opt %s -o /dev/null
// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SelectedNetConnectivity
#u1 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#u2 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#u4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#asc = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = false, range = [0, 7], children = [#u1], childNames = []>
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#index = #simulation.vpi_type<kind = integer, isSigned = true, isFourState = true, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @connectivity {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : i8 design hierarchy "top.q" {vpi_type = #u8}
    simulation.net.decl 1 in 1 : i8 design hierarchy "top.s" {vpi_type = #u8}
    simulation.net.decl 2 in 1 : !simulation.packed_array<0 : 7 x i1> design hierarchy "top.asc" {vpi_type = #asc}
    simulation.vpi_net_identity.decl 0 backed_by 0 in 2 : i8 hierarchy "other.alias" debug "alias" {vpi_type = #u8}
    simulation.vpi_relation.decl <kind = net_identity, id = 0 : i64> selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.storage.decl 0 in 1 : !simulation.logic<32> design hierarchy "top.index" {vpi_type = #index}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    simulation.statement.decl 100 scope 1 type 8 loc("connectivity.sv":100:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 0 modes 2 to 100
    simulation.vpi_relation.decl <kind = statement, id = 100 : i64> selector 77 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.statement.decl 101 scope 1 type 8 loc("connectivity.sv":101:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 1 modes 2 to 101
    simulation.vpi_expression.decl @l101 id 1000 type 42 in 1 owned_by <kind = statement, id = 101 : i64> selector 77 handle ordinal 0 : i4 {vpi_type = #u4} loc("connectivity.sv":101:2)
    simulation.vpi_relation.decl <kind = statement, id = 101 : i64> selector 77 handle ordinal 0 to <kind = symbol, symbol = @l101>
    simulation.vpi_relation.decl <kind = symbol, symbol = @l101> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @l101_left id 1001 type 7 in 1 owned_by <kind = symbol, symbol = @l101> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l101> selector 79 handle ordinal 0 to <kind = symbol, symbol = @l101_left>
    simulation.vpi_expression.decl @l101_right id 1002 type 7 in 1 owned_by <kind = symbol, symbol = @l101> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l101> selector 83 handle ordinal 0 to <kind = symbol, symbol = @l101_right>
    simulation.vpi_expression.decl @r101 id 1003 type 42 in 1 owned_by <kind = statement, id = 101 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4} loc("connectivity.sv":101:2)
    simulation.vpi_relation.decl <kind = statement, id = 101 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @r101>
    simulation.vpi_relation.decl <kind = symbol, symbol = @r101> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @r101_left id 1004 type 7 in 1 owned_by <kind = symbol, symbol = @r101> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r101> selector 79 handle ordinal 0 to <kind = symbol, symbol = @r101_left>
    simulation.vpi_expression.decl @r101_right id 1005 type 7 in 1 owned_by <kind = symbol, symbol = @r101> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r101> selector 83 handle ordinal 0 to <kind = symbol, symbol = @r101_right>
    simulation.statement.decl 102 scope 1 type 8 loc("connectivity.sv":102:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 2 modes 2 to 102
    simulation.vpi_expression.decl @l102 id 1006 type 42 in 1 owned_by <kind = statement, id = 102 : i64> selector 77 handle ordinal 0 : i4 {vpi_type = #u4} loc("connectivity.sv":102:2)
    simulation.vpi_relation.decl <kind = statement, id = 102 : i64> selector 77 handle ordinal 0 to <kind = symbol, symbol = @l102>
    simulation.vpi_relation.decl <kind = symbol, symbol = @l102> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @l102_left id 1007 type 7 in 1 owned_by <kind = symbol, symbol = @l102> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [5 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l102> selector 79 handle ordinal 0 to <kind = symbol, symbol = @l102_left>
    simulation.vpi_expression.decl @l102_right id 1008 type 7 in 1 owned_by <kind = symbol, symbol = @l102> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l102> selector 83 handle ordinal 0 to <kind = symbol, symbol = @l102_right>
    simulation.vpi_expression.decl @r102 id 1009 type 42 in 1 owned_by <kind = statement, id = 102 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4} loc("connectivity.sv":102:2)
    simulation.vpi_relation.decl <kind = statement, id = 102 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @r102>
    simulation.vpi_relation.decl <kind = symbol, symbol = @r102> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @r102_left id 1010 type 7 in 1 owned_by <kind = symbol, symbol = @r102> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r102> selector 79 handle ordinal 0 to <kind = symbol, symbol = @r102_left>
    simulation.vpi_expression.decl @r102_right id 1011 type 7 in 1 owned_by <kind = symbol, symbol = @r102> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r102> selector 83 handle ordinal 0 to <kind = symbol, symbol = @r102_right>
    simulation.statement.decl 103 scope 1 type 8 loc("connectivity.sv":103:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 3 modes 2 to 103
    simulation.vpi_expression.decl @l103 id 1012 type 130 in 1 owned_by <kind = statement, id = 103 : i64> selector 77 handle ordinal 0 : i2 {vpi_type = #u2, indexed_direction = 1 : i32} loc("connectivity.sv":103:2)
    simulation.vpi_relation.decl <kind = statement, id = 103 : i64> selector 77 handle ordinal 0 to <kind = symbol, symbol = @l103>
    simulation.vpi_relation.decl <kind = symbol, symbol = @l103> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @l103> selector 131 handle ordinal 0 to <kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @l103_right id 1013 type 7 in 1 owned_by <kind = symbol, symbol = @l103> selector 132 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l103> selector 132 handle ordinal 0 to <kind = symbol, symbol = @l103_right>
    simulation.vpi_expression.decl @r103 id 1014 type 42 in 1 owned_by <kind = statement, id = 103 : i64> selector 82 handle ordinal 0 : i2 {vpi_type = #u2} loc("connectivity.sv":103:2)
    simulation.vpi_relation.decl <kind = statement, id = 103 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @r103>
    simulation.vpi_relation.decl <kind = symbol, symbol = @r103> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @r103_left id 1015 type 7 in 1 owned_by <kind = symbol, symbol = @r103> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r103> selector 79 handle ordinal 0 to <kind = symbol, symbol = @r103_left>
    simulation.vpi_expression.decl @r103_right id 1016 type 7 in 1 owned_by <kind = symbol, symbol = @r103> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r103> selector 83 handle ordinal 0 to <kind = symbol, symbol = @r103_right>
    simulation.statement.decl 104 scope 2 type 8 loc("connectivity.sv":104:1)
    simulation.vpi_statement_relation.decl scope 2 type 32 selector 8 ordinal 0 modes 2 to 104
    simulation.vpi_expression.decl @l104 id 1017 type 42 in 2 owned_by <kind = statement, id = 104 : i64> selector 77 handle ordinal 0 : i4 {vpi_type = #u4} loc("connectivity.sv":104:2)
    simulation.vpi_relation.decl <kind = statement, id = 104 : i64> selector 77 handle ordinal 0 to <kind = symbol, symbol = @l104>
    simulation.vpi_relation.decl <kind = symbol, symbol = @l104> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @l104_left id 1018 type 7 in 2 owned_by <kind = symbol, symbol = @l104> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l104> selector 79 handle ordinal 0 to <kind = symbol, symbol = @l104_left>
    simulation.vpi_expression.decl @l104_right id 1019 type 7 in 2 owned_by <kind = symbol, symbol = @l104> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l104> selector 83 handle ordinal 0 to <kind = symbol, symbol = @l104_right>
    simulation.vpi_expression.decl @r104 id 1020 type 42 in 2 owned_by <kind = statement, id = 104 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4} loc("connectivity.sv":104:2)
    simulation.vpi_relation.decl <kind = statement, id = 104 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @r104>
    simulation.vpi_relation.decl <kind = symbol, symbol = @r104> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @r104_left id 1021 type 7 in 2 owned_by <kind = symbol, symbol = @r104> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r104> selector 79 handle ordinal 0 to <kind = symbol, symbol = @r104_left>
    simulation.vpi_expression.decl @r104_right id 1022 type 7 in 2 owned_by <kind = symbol, symbol = @r104> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r104> selector 83 handle ordinal 0 to <kind = symbol, symbol = @r104_right>
    simulation.statement.decl 105 scope 1 type 8 loc("connectivity.sv":105:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 4 modes 2 to 105
    simulation.vpi_expression.decl @l105 id 1023 type 42 in 1 owned_by <kind = statement, id = 105 : i64> selector 77 handle ordinal 0 : i4 {vpi_type = #u4} loc("connectivity.sv":105:2)
    simulation.vpi_relation.decl <kind = statement, id = 105 : i64> selector 77 handle ordinal 0 to <kind = symbol, symbol = @l105>
    simulation.vpi_relation.decl <kind = symbol, symbol = @l105> selector 81 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @l105_left id 1024 type 7 in 1 owned_by <kind = symbol, symbol = @l105> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l105> selector 79 handle ordinal 0 to <kind = symbol, symbol = @l105_left>
    simulation.vpi_expression.decl @l105_right id 1025 type 7 in 1 owned_by <kind = symbol, symbol = @l105> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @l105> selector 83 handle ordinal 0 to <kind = symbol, symbol = @l105_right>
    simulation.vpi_expression.decl @r105 id 1026 type 42 in 1 owned_by <kind = statement, id = 105 : i64> selector 82 handle ordinal 0 : i4 {vpi_type = #u4} loc("connectivity.sv":105:2)
    simulation.vpi_relation.decl <kind = statement, id = 105 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @r105>
    simulation.vpi_relation.decl <kind = symbol, symbol = @r105> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @r105_left id 1027 type 7 in 1 owned_by <kind = symbol, symbol = @r105> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r105> selector 79 handle ordinal 0 to <kind = symbol, symbol = @r105_left>
    simulation.vpi_expression.decl @r105_right id 1028 type 7 in 1 owned_by <kind = symbol, symbol = @r105> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r105> selector 83 handle ordinal 0 to <kind = symbol, symbol = @r105_right>
    simulation.statement.decl 106 scope 1 type 8 loc("connectivity.sv":106:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 5 modes 2 to 106
    simulation.vpi_expression.decl @l106 id 1029 type 37 in 1 owned_by <kind = statement, id = 106 : i64> selector 77 handle ordinal 0 : i1 {vpi_type = #u1} loc("connectivity.sv":106:2)
    simulation.vpi_relation.decl <kind = statement, id = 106 : i64> selector 77 handle ordinal 0 to <kind = symbol, symbol = @l106>
    simulation.vpi_relation.decl <kind = symbol, symbol = @l106> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @l106> selector 78 handle ordinal 0 to <kind = storage, id = 0 : i64>
    simulation.vpi_expression.decl @r106 id 1030 type 37 in 1 owned_by <kind = statement, id = 106 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1} loc("connectivity.sv":106:2)
    simulation.vpi_relation.decl <kind = statement, id = 106 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @r106>
    simulation.vpi_relation.decl <kind = symbol, symbol = @r106> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @r106_left id 1031 type 7 in 1 owned_by <kind = symbol, symbol = @r106> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @r106> selector 78 handle ordinal 0 to <kind = symbol, symbol = @r106_left>
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @and type 21 name "and" primitive_type 1
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 2 : i64}
    simulation.vpi_definition.bind @gate to @and
    simulation.vpi_object.anchor @term0 id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_expression.decl @term_expr0 id 1032 type 37 in 1 owned_by <kind = symbol, symbol = @term0> selector 102 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term0> selector 102 handle ordinal 0 to <kind = symbol, symbol = @term_expr0>
    simulation.vpi_relation.decl <kind = symbol, symbol = @term_expr0> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @term_expr0_left id 1033 type 7 in 1 owned_by <kind = symbol, symbol = @term_expr0> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [6 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term_expr0> selector 78 handle ordinal 0 to <kind = symbol, symbol = @term_expr0_left>
    simulation.vpi_object.anchor @term1 id 3 type 46 in 1 parent @gate ordinal 1 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_expression.decl @term_expr1 id 1034 type 37 in 1 owned_by <kind = symbol, symbol = @term1> selector 102 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term1> selector 102 handle ordinal 0 to <kind = symbol, symbol = @term_expr1>
    simulation.vpi_relation.decl <kind = symbol, symbol = @term_expr1> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @term_expr1_left id 1035 type 7 in 1 owned_by <kind = symbol, symbol = @term_expr1> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [6 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term_expr1> selector 78 handle ordinal 0 to <kind = symbol, symbol = @term_expr1_left>
    simulation.vpi_object.anchor @term2 id 4 type 46 in 1 parent @gate ordinal 2 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_expression.decl @term_expr2 id 1036 type 37 in 1 owned_by <kind = symbol, symbol = @term2> selector 102 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term2> selector 102 handle ordinal 0 to <kind = symbol, symbol = @term_expr2>
    simulation.vpi_relation.decl <kind = symbol, symbol = @term_expr2> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @term_expr2_left id 1037 type 7 in 1 owned_by <kind = symbol, symbol = @term_expr2> selector 78 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term_expr2> selector 78 handle ordinal 0 to <kind = symbol, symbol = @term_expr2_left>
    // Unrelated legal call-alias RHS must not invalidate cold enumeration.
    simulation.net.decl 3 in 1 : i8 design hierarchy "top.unrelated" {vpi_type = #u8}
    simulation.statement.decl 107 scope 1 type 8 loc("connectivity.sv":107:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 6 modes 2 to 107
    simulation.code_unit.decl 46 in 1 function hierarchy "top.callee"
    simulation.func private @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} { simulation.return }
    simulation.statement.decl 200 in 44 scope 1 type 19
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 200
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "callee" statement 200 within 200 callee 46
    simulation.vpi_relation.decl <kind = statement, id = 107 : i64> selector 77 handle ordinal 0 to <kind = net, id = 3 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 107 : i64> selector 82 handle ordinal 0 to <kind = statement, id = 200 : i64>
    // Local queries must not merge sibling interfaces through top.
    simulation.scope.decl 3 parent 1 hierarchy "top.first" vpi_kind 601
    simulation.scope.decl 4 parent 1 hierarchy "top.second" vpi_kind 601
    simulation.net.decl 4 in 3 : i8 design hierarchy "top.first.n" {vpi_type = #u8}
    simulation.vpi_net_identity.decl 1 backed_by 4 in 4 : i8 hierarchy "top.second.alias" debug "alias" {vpi_type = #u8}
    simulation.vpi_relation.decl <kind = net_identity, id = 1 : i64> selector 126 handle ordinal 0 to <kind = net, id = 4 : i64>
    simulation.vpi_net_identity.decl 2 backed_by 4 in 1 : i8 hierarchy "top.interface_alias" debug "interface_alias" {vpi_type = #u8}
    simulation.vpi_relation.decl <kind = net_identity, id = 2 : i64> selector 126 handle ordinal 0 to <kind = net, id = 4 : i64>
    simulation.statement.decl 300 scope 3 type 8 loc("connectivity.sv":300:1)
    simulation.vpi_statement_relation.decl scope 3 type 601 selector 8 ordinal 0 modes 2 to 300
    simulation.statement.decl 301 scope 4 type 8 loc("connectivity.sv":301:1)
    simulation.vpi_statement_relation.decl scope 4 type 601 selector 8 ordinal 0 modes 2 to 301
    simulation.vpi_relation.decl <kind = statement, id = 300 : i64> selector 77 handle ordinal 0 to <kind = net, id = 4 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 300 : i64> selector 82 handle ordinal 0 to <kind = net, id = 4 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 301 : i64> selector 77 handle ordinal 0 to <kind = net_identity, id = 1 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 301 : i64> selector 82 handle ordinal 0 to <kind = net_identity, id = 1 : i64>
    simulation.vpi_relation.decl <kind = net, id = 4 : i64> selector 122 iterate ordinal 0 to <kind = statement, id = 300 : i64>
    simulation.vpi_relation.decl <kind = net, id = 4 : i64> selector 123 iterate ordinal 0 to <kind = statement, id = 300 : i64>
  }
}
