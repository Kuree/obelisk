// RUN: obelisk-opt %s -o /dev/null
// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ExpressionNetLoads
#u1 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
#u2 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>
#u3 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [2, 0], children = [], childNames = []>
#u4 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
#u5 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [4, 0], children = [], childNames = []>
#u8 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
#u9 = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [8, 0], children = [], childNames = []>
#asc = #simulation.vpi_type<kind = packed_array, isSigned = false, isFourState = false, range = [0, 7], children = [#u1], childNames = []>
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#f4 = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [3, 0], children = [], childNames = []>
#index = #simulation.vpi_type<kind = integer, isSigned = true, isFourState = true, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @loads {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.net.decl 0 in 1 : i8 design hierarchy "top.q" {vpi_type = #u8}
    simulation.net.decl 1 in 1 : i8 design hierarchy "top.r" {vpi_type = #u8}
    simulation.net.decl 2 in 1 : i8 design hierarchy "top.out" {vpi_type = #u8}
    simulation.net.decl 3 in 1 : i4 design hierarchy "top.small" {vpi_type = #u4}
    simulation.net.decl 4 in 1 : i1 design hierarchy "top.one" {vpi_type = #u1}
    simulation.net.decl 5 in 1 : !simulation.packed_array<0 : 7 x i1> design hierarchy "top.asc" {vpi_type = #asc}
    simulation.net.decl 6 in 1 : !simulation.logic<4> design hierarchy "top.addr" {vpi_type = #f4}
    simulation.net.decl 7 in 1 : i2 design hierarchy "top.pair" {vpi_type = #u2}
    simulation.net.decl 8 in 1 : i8 design hierarchy "top.gate_a" {vpi_type = #u8}
    simulation.net.decl 9 in 1 : i8 design hierarchy "top.gate_b" {vpi_type = #u8}
    simulation.net.decl 10 in 1 : i8 design hierarchy "top.gate_out" {vpi_type = #u8}
    simulation.net.decl 11 in 1 : !simulation.logic<4> design hierarchy "top.gate_index" {vpi_type = #f4}
    simulation.vpi_net_identity.decl 0 backed_by 0 in 2 : i8 hierarchy "other.alias" debug "alias" {vpi_type = #u8}
    simulation.vpi_relation.decl <kind = net_identity, id = 0 : i64> selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.storage.decl 0 in 1 : !simulation.logic<32> design hierarchy "top.index" {vpi_type = #index}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} { simulation.return }
    simulation.statement.decl 100 scope 1 type 8 loc("loads.sv":100:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 0 modes 2 to 100
    simulation.vpi_relation.decl <kind = statement, id = 100 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1000 id 1000 type 39 in 1 owned_by <kind = statement, id = 100 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 24 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 100 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1000>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1000> selector 97 iterate ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1000> selector 97 iterate ordinal 1 to <kind = net, id = 1 : i64>
    simulation.statement.decl 101 scope 1 type 8 loc("loads.sv":101:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 1 modes 2 to 101
    simulation.vpi_relation.decl <kind = statement, id = 101 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1001 id 1001 type 39 in 1 owned_by <kind = statement, id = 101 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 101 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1001>
    simulation.vpi_expression.decl @e1002 id 1002 type 42 in 1 owned_by <kind = symbol, symbol = @e1001> selector 97 iterate ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1001> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1002>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1002> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1003 id 1003 type 7 in 1 owned_by <kind = symbol, symbol = @e1002> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1002> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1003>
    simulation.vpi_expression.decl @e1004 id 1004 type 7 in 1 owned_by <kind = symbol, symbol = @e1002> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1002> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1004>
    simulation.vpi_expression.decl @e1005 id 1005 type 42 in 1 owned_by <kind = symbol, symbol = @e1001> selector 97 iterate ordinal 1 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1001> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1005>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1005> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @e1006 id 1006 type 7 in 1 owned_by <kind = symbol, symbol = @e1005> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1005> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1006>
    simulation.vpi_expression.decl @e1007 id 1007 type 7 in 1 owned_by <kind = symbol, symbol = @e1005> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1005> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1007>
    simulation.statement.decl 102 scope 1 type 8 loc("loads.sv":102:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 2 modes 2 to 102
    simulation.vpi_relation.decl <kind = statement, id = 102 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1008 id 1008 type 39 in 1 owned_by <kind = statement, id = 102 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 34 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 102 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1008>
    simulation.vpi_expression.decl @e1009 id 1009 type 7 in 1 owned_by <kind = symbol, symbol = @e1008> selector 97 iterate ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1008> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1009>
    simulation.vpi_expression.decl @e1010 id 1010 type 42 in 1 owned_by <kind = symbol, symbol = @e1008> selector 97 iterate ordinal 1 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1008> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1010>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1010> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1011 id 1011 type 7 in 1 owned_by <kind = symbol, symbol = @e1010> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1010> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1011>
    simulation.vpi_expression.decl @e1012 id 1012 type 7 in 1 owned_by <kind = symbol, symbol = @e1010> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1010> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1012>
    simulation.statement.decl 103 scope 1 type 8 loc("loads.sv":103:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 3 modes 2 to 103
    simulation.vpi_relation.decl <kind = statement, id = 103 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1013 id 1013 type 39 in 1 owned_by <kind = statement, id = 103 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 103 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1013>
    simulation.vpi_expression.decl @e1014 id 1014 type 42 in 1 owned_by <kind = symbol, symbol = @e1013> selector 97 iterate ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1013> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1014>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1014> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1015 id 1015 type 7 in 1 owned_by <kind = symbol, symbol = @e1014> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1014> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1015>
    simulation.vpi_expression.decl @e1016 id 1016 type 7 in 1 owned_by <kind = symbol, symbol = @e1014> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1014> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1016>
    simulation.vpi_expression.decl @e1017 id 1017 type 39 in 1 owned_by <kind = symbol, symbol = @e1013> selector 97 iterate ordinal 1 : i4 {vpi_type = #u4, op_type = 30 : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1013> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1017>
    simulation.vpi_expression.decl @e1018 id 1018 type 42 in 1 owned_by <kind = symbol, symbol = @e1017> selector 97 iterate ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1017> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1018>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1018> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1019 id 1019 type 7 in 1 owned_by <kind = symbol, symbol = @e1018> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1018> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1019>
    simulation.vpi_expression.decl @e1020 id 1020 type 7 in 1 owned_by <kind = symbol, symbol = @e1018> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1018> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1020>
    simulation.vpi_expression.decl @e1021 id 1021 type 42 in 1 owned_by <kind = symbol, symbol = @e1017> selector 97 iterate ordinal 1 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1017> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1021>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1021> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @e1022 id 1022 type 7 in 1 owned_by <kind = symbol, symbol = @e1021> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1021> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1022>
    simulation.vpi_expression.decl @e1023 id 1023 type 7 in 1 owned_by <kind = symbol, symbol = @e1021> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1021> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1023>
    simulation.statement.decl 104 scope 1 type 8 loc("loads.sv":104:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 4 modes 2 to 104
    simulation.vpi_relation.decl <kind = statement, id = 104 : i64> selector 77 handle ordinal 0 to <kind = net, id = 4 : i64>
    simulation.vpi_expression.decl @e1024 id 1024 type 37 in 1 owned_by <kind = statement, id = 104 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = statement, id = 104 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1024>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1024> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1024> selector 78 handle ordinal 0 to <kind = net, id = 6 : i64>
    simulation.statement.decl 105 scope 1 type 8 loc("loads.sv":105:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 5 modes 2 to 105
    simulation.vpi_expression.decl @e1025 id 1025 type 37 in 1 owned_by <kind = statement, id = 105 : i64> selector 77 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = statement, id = 105 : i64> selector 77 handle ordinal 0 to <kind = symbol, symbol = @e1025>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1025> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1025> selector 78 handle ordinal 0 to <kind = net, id = 6 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 105 : i64> selector 82 handle ordinal 0 to <kind = net, id = 4 : i64>
    simulation.statement.decl 106 scope 1 type 8 loc("loads.sv":106:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 6 modes 2 to 106
    simulation.vpi_expression.decl @e1026 id 1026 type 39 in 1 owned_by <kind = statement, id = 106 : i64> selector 77 handle ordinal 0 : i2 {vpi_type = #u2, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 106 : i64> selector 77 handle ordinal 0 to <kind = symbol, symbol = @e1026>
    simulation.vpi_expression.decl @e1027 id 1027 type 37 in 1 owned_by <kind = symbol, symbol = @e1026> selector 97 iterate ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1026> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1027>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1027> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1027> selector 78 handle ordinal 0 to <kind = net, id = 6 : i64>
    simulation.vpi_expression.decl @e1028 id 1028 type 37 in 1 owned_by <kind = symbol, symbol = @e1026> selector 97 iterate ordinal 1 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1026> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1028>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1028> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1028> selector 78 handle ordinal 0 to <kind = storage, id = 0 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 106 : i64> selector 82 handle ordinal 0 to <kind = net, id = 7 : i64>
    simulation.statement.decl 107 scope 1 type 8 loc("loads.sv":107:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 7 modes 2 to 107
    simulation.vpi_relation.decl <kind = statement, id = 107 : i64> selector 77 handle ordinal 0 to <kind = net, id = 3 : i64>
    simulation.vpi_expression.decl @e1029 id 1029 type 39 in 1 owned_by <kind = statement, id = 107 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 107 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1029>
    simulation.vpi_expression.decl @e1030 id 1030 type 42 in 1 owned_by <kind = symbol, symbol = @e1029> selector 97 iterate ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1029> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1030>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1030> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1031 id 1031 type 7 in 1 owned_by <kind = symbol, symbol = @e1030> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1030> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1031>
    simulation.vpi_expression.decl @e1032 id 1032 type 7 in 1 owned_by <kind = symbol, symbol = @e1030> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1030> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1032>
    simulation.vpi_expression.decl @e1033 id 1033 type 42 in 1 owned_by <kind = symbol, symbol = @e1029> selector 97 iterate ordinal 1 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1029> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1033>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1033> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @e1034 id 1034 type 7 in 1 owned_by <kind = symbol, symbol = @e1033> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1033> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1034>
    simulation.vpi_expression.decl @e1035 id 1035 type 7 in 1 owned_by <kind = symbol, symbol = @e1033> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1033> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1035>
    simulation.statement.decl 108 scope 1 type 8 loc("loads.sv":108:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 8 modes 2 to 108
    simulation.vpi_relation.decl <kind = statement, id = 108 : i64> selector 77 handle ordinal 0 to <kind = net, id = 4 : i64>
    simulation.vpi_expression.decl @e1036 id 1036 type 39 in 1 owned_by <kind = statement, id = 108 : i64> selector 82 handle ordinal 0 : i1 {vpi_type = #u1, op_type = 5 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 108 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1036>
    simulation.vpi_expression.decl @e1037 id 1037 type 42 in 1 owned_by <kind = symbol, symbol = @e1036> selector 97 iterate ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1036> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1037>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1037> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1038 id 1038 type 7 in 1 owned_by <kind = symbol, symbol = @e1037> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1037> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1038>
    simulation.vpi_expression.decl @e1039 id 1039 type 7 in 1 owned_by <kind = symbol, symbol = @e1037> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1037> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1039>
    simulation.statement.decl 109 scope 1 type 8 loc("loads.sv":109:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 9 modes 2 to 109
    simulation.vpi_relation.decl <kind = statement, id = 109 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1040 id 1040 type 39 in 1 owned_by <kind = statement, id = 109 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 32 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 109 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1040>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1040> selector 97 iterate ordinal 0 to <kind = net, id = 4 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1040> selector 97 iterate ordinal 1 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1040> selector 97 iterate ordinal 2 to <kind = net, id = 1 : i64>
    simulation.statement.decl 110 scope 1 type 8 loc("loads.sv":110:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 10 modes 2 to 110
    simulation.vpi_relation.decl <kind = statement, id = 110 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1041 id 1041 type 39 in 1 owned_by <kind = statement, id = 110 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 110 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1041>
    simulation.vpi_expression.decl @e1042 id 1042 type 42 in 1 owned_by <kind = symbol, symbol = @e1041> selector 97 iterate ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1041> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1042>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1042> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1043 id 1043 type 7 in 1 owned_by <kind = symbol, symbol = @e1042> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1042> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1043>
    simulation.vpi_expression.decl @e1044 id 1044 type 7 in 1 owned_by <kind = symbol, symbol = @e1042> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1042> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1044>
    simulation.vpi_expression.decl @e1045 id 1045 type 39 in 1 owned_by <kind = symbol, symbol = @e1041> selector 97 iterate ordinal 1 : i4 {vpi_type = #u4, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1041> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1045>
    simulation.vpi_expression.decl @e1046 id 1046 type 42 in 1 owned_by <kind = symbol, symbol = @e1045> selector 97 iterate ordinal 0 : i2 {vpi_type = #u2}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1045> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1046>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1046> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1047 id 1047 type 7 in 1 owned_by <kind = symbol, symbol = @e1046> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1046> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1047>
    simulation.vpi_expression.decl @e1048 id 1048 type 7 in 1 owned_by <kind = symbol, symbol = @e1046> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1046> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1048>
    simulation.vpi_expression.decl @e1049 id 1049 type 42 in 1 owned_by <kind = symbol, symbol = @e1045> selector 97 iterate ordinal 1 : i2 {vpi_type = #u2}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1045> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1049>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1049> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @e1050 id 1050 type 7 in 1 owned_by <kind = symbol, symbol = @e1049> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1049> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1050>
    simulation.vpi_expression.decl @e1051 id 1051 type 7 in 1 owned_by <kind = symbol, symbol = @e1049> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1049> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1051>
    simulation.statement.decl 111 scope 1 type 8 loc("loads.sv":111:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 11 modes 2 to 111
    simulation.vpi_relation.decl <kind = statement, id = 111 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1052 id 1052 type 39 in 1 owned_by <kind = statement, id = 111 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 34 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 111 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1052>
    simulation.vpi_expression.decl @e1053 id 1053 type 7 in 1 owned_by <kind = symbol, symbol = @e1052> selector 97 iterate ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1052> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1053>
    simulation.vpi_expression.decl @e1054 id 1054 type 42 in 1 owned_by <kind = symbol, symbol = @e1052> selector 97 iterate ordinal 1 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1052> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1054>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1054> selector 81 handle ordinal 0 to <kind = net, id = 5 : i64>
    simulation.vpi_expression.decl @e1055 id 1055 type 7 in 1 owned_by <kind = symbol, symbol = @e1054> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1054> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1055>
    simulation.vpi_expression.decl @e1056 id 1056 type 7 in 1 owned_by <kind = symbol, symbol = @e1054> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1054> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1056>
    simulation.statement.decl 112 scope 2 type 8 loc("loads.sv":112:1)
    simulation.vpi_statement_relation.decl scope 2 type 32 selector 8 ordinal 0 modes 2 to 112
    simulation.vpi_relation.decl <kind = statement, id = 112 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1057 id 1057 type 39 in 2 owned_by <kind = statement, id = 112 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 24 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 112 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1057>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1057> selector 97 iterate ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1057> selector 97 iterate ordinal 1 to <kind = net, id = 1 : i64>
    simulation.statement.decl 113 scope 1 type 8 loc("loads.sv":113:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 12 modes 2 to 113
    simulation.vpi_relation.decl <kind = statement, id = 113 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1058 id 1058 type 39 in 1 owned_by <kind = statement, id = 113 : i64> selector 82 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 24 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 113 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1058>
    simulation.vpi_expression.decl @e1059 id 1059 type 37 in 1 owned_by <kind = symbol, symbol = @e1058> selector 97 iterate ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1058> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1059>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1059> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1059> selector 78 handle ordinal 0 to <kind = net, id = 6 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1058> selector 97 iterate ordinal 1 to <kind = net, id = 1 : i64>
    simulation.statement.decl 114 scope 1 type 8 loc("loads.sv":114:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 13 modes 2 to 114
    simulation.vpi_relation.decl <kind = statement, id = 114 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1060 id 1060 type 39 in 1 owned_by <kind = statement, id = 114 : i64> selector 82 handle ordinal 0 : i5 {vpi_type = #u5, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 114 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1060>
    simulation.vpi_expression.decl @e1061 id 1061 type 42 in 1 owned_by <kind = symbol, symbol = @e1060> selector 97 iterate ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1060> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1061>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1061> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1062 id 1062 type 7 in 1 owned_by <kind = symbol, symbol = @e1061> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1061> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1062>
    simulation.vpi_expression.decl @e1063 id 1063 type 7 in 1 owned_by <kind = symbol, symbol = @e1061> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1061> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1063>
    simulation.vpi_expression.decl @e1064 id 1064 type 37 in 1 owned_by <kind = symbol, symbol = @e1060> selector 97 iterate ordinal 1 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1060> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1064>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1064> selector 81 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_expression.decl @e1065 id 1065 type 42 in 1 owned_by <kind = symbol, symbol = @e1064> selector 78 handle ordinal 0 : i2 {vpi_type = #u2}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1064> selector 78 handle ordinal 0 to <kind = symbol, symbol = @e1065>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1065> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_expression.decl @e1066 id 1066 type 7 in 1 owned_by <kind = symbol, symbol = @e1065> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1065> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1066>
    simulation.vpi_expression.decl @e1067 id 1067 type 7 in 1 owned_by <kind = symbol, symbol = @e1065> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1065> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1067>
    simulation.statement.decl 115 scope 1 type 8 loc("loads.sv":115:1)
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 14 modes 2 to 115
    simulation.vpi_relation.decl <kind = statement, id = 115 : i64> selector 77 handle ordinal 0 to <kind = net, id = 2 : i64>
    simulation.vpi_expression.decl @e1068 id 1068 type 39 in 1 owned_by <kind = statement, id = 115 : i64> selector 82 handle ordinal 0 : i9 {vpi_type = #u9, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = statement, id = 115 : i64> selector 82 handle ordinal 0 to <kind = symbol, symbol = @e1068>
    simulation.vpi_expression.decl @e1069 id 1069 type 37 in 1 owned_by <kind = symbol, symbol = @e1068> selector 97 iterate ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1068> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1069>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1069> selector 81 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1069> selector 78 handle ordinal 0 to <kind = net, id = 6 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1068> selector 97 iterate ordinal 1 to <kind = net, id = 1 : i64>
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @and type 21 name "and" primitive_type 1
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 2 : i64}
    simulation.vpi_definition.bind @gate to @and
    simulation.vpi_object.anchor @term0 id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_expression.decl @e1070 id 1070 type 37 in 1 owned_by <kind = symbol, symbol = @term0> selector 102 handle ordinal 0 : i1 {vpi_type = #u1}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term0> selector 102 handle ordinal 0 to <kind = symbol, symbol = @e1070>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1070> selector 81 handle ordinal 0 to <kind = net, id = 10 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1070> selector 78 handle ordinal 0 to <kind = net, id = 11 : i64>
    simulation.vpi_object.anchor @term1 id 3 type 46 in 1 parent @gate ordinal 1 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_expression.decl @e1071 id 1071 type 39 in 1 owned_by <kind = symbol, symbol = @term1> selector 102 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 24 : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term1> selector 102 handle ordinal 0 to <kind = symbol, symbol = @e1071>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1071> selector 97 iterate ordinal 0 to <kind = net, id = 8 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1071> selector 97 iterate ordinal 1 to <kind = net, id = 9 : i64>
    simulation.vpi_object.anchor @term2 id 4 type 46 in 1 parent @gate ordinal 2 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 1 : i32>]>}
    simulation.vpi_expression.decl @e1072 id 1072 type 39 in 1 owned_by <kind = symbol, symbol = @term2> selector 102 handle ordinal 0 : i8 {vpi_type = #u8, op_type = 33 : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term2> selector 102 handle ordinal 0 to <kind = symbol, symbol = @e1072>
    simulation.vpi_expression.decl @e1073 id 1073 type 42 in 1 owned_by <kind = symbol, symbol = @e1072> selector 97 iterate ordinal 0 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1072> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @e1073>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1073> selector 81 handle ordinal 0 to <kind = net, id = 8 : i64>
    simulation.vpi_expression.decl @e1074 id 1074 type 7 in 1 owned_by <kind = symbol, symbol = @e1073> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1073> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1074>
    simulation.vpi_expression.decl @e1075 id 1075 type 7 in 1 owned_by <kind = symbol, symbol = @e1073> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1073> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1075>
    simulation.vpi_expression.decl @e1076 id 1076 type 42 in 1 owned_by <kind = symbol, symbol = @e1072> selector 97 iterate ordinal 1 : i4 {vpi_type = #u4}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1072> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @e1076>
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1076> selector 81 handle ordinal 0 to <kind = net, id = 9 : i64>
    simulation.vpi_expression.decl @e1077 id 1077 type 7 in 1 owned_by <kind = symbol, symbol = @e1076> selector 79 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [3 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1076> selector 79 handle ordinal 0 to <kind = symbol, symbol = @e1077>
    simulation.vpi_expression.decl @e1078 id 1078 type 7 in 1 owned_by <kind = symbol, symbol = @e1076> selector 83 handle ordinal 0 : i32 {vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [0 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl <kind = symbol, symbol = @e1076> selector 83 handle ordinal 0 to <kind = symbol, symbol = @e1078>
  }
}
