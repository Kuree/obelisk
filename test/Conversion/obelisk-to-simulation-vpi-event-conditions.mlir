// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read opt-level=0' -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CompoundEventConditions
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CompoundEventConditions
!logic = !obelisk.integral<1, false, true, 0 : 0, logic>
!word = !obelisk.integral<32, true, false, 31 : 0, int>
module attributes {llvm.data_layout = "e-p:64:64-i64:64", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
obelisk.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 49 : i64} {}
obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 48 : i64} {
obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_symbol = @def, referenced_path = "top", node_id = 47 : i64} {
obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 46 : i64} {
obelisk.sv.symbol.variable @clk attributes {hierarchical_name = "top.clk", name = "clk", lifetime = 1 : i32, semantic_type = !logic, node_id = 1 : i64} {}
obelisk.sv.symbol.variable @reset attributes {hierarchical_name = "top.reset", name = "reset", lifetime = 1 : i32, semantic_type = !logic, node_id = 2 : i64} {}
obelisk.sv.symbol.variable @enable attributes {hierarchical_name = "top.enable", name = "enable", lifetime = 1 : i32, semantic_type = !logic, node_id = 3 : i64} {}
obelisk.sv.symbol.variable @q attributes {hierarchical_name = "top.q", name = "q", lifetime = 1 : i32, semantic_type = !logic, node_id = 4 : i64} {}
obelisk.sv.symbol.variable @d attributes {hierarchical_name = "top.d", name = "d", lifetime = 1 : i32, semantic_type = !logic, node_id = 5 : i64} {}
obelisk.sv.symbol.procedural_block @level attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 10 : i64} {
obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 7 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.clk", referenced_symbol = @root::@top::@body::@clk, semantic_type = !logic, is_signed = false, node_id = 6 : i64} {}
}
obelisk.sv.statement.empty attributes {node_id = 8 : i64} {}
}
}
obelisk.sv.symbol.procedural_block @posedge attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 15 : i64} {
obelisk.sv.statement.timed attributes {node_id = 14 : i64} {
obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.clk", referenced_symbol = @root::@top::@body::@clk, semantic_type = !logic, is_signed = false, node_id = 11 : i64} {}
}
obelisk.sv.statement.empty attributes {node_id = 13 : i64} {}
}
}
obelisk.sv.symbol.procedural_block @edge_list attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 25 : i64} {
obelisk.sv.statement.timed attributes {node_id = 24 : i64} {
obelisk.sv.timing.event_list attributes {event_count = 3 : i64, node_id = 22 : i64} {
obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 17 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.clk", referenced_symbol = @root::@top::@body::@clk, semantic_type = !logic, is_signed = false, node_id = 16 : i64} {}
}
obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 19 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.reset", referenced_symbol = @root::@top::@body::@reset, semantic_type = !logic, is_signed = false, node_id = 18 : i64} {}
}
obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 21 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.clk", referenced_symbol = @root::@top::@body::@clk, semantic_type = !logic, is_signed = false, node_id = 20 : i64} {}
}
}
obelisk.sv.statement.empty attributes {node_id = 23 : i64} {}
}
}
obelisk.sv.symbol.procedural_block @qualified attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 31 : i64} {
obelisk.sv.statement.timed attributes {node_id = 30 : i64} {
obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 28 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.clk", referenced_symbol = @root::@top::@body::@clk, semantic_type = !logic, is_signed = false, node_id = 26 : i64} {}
obelisk.sv.expression.named_value attributes {referenced_path = "top.enable", referenced_symbol = @root::@top::@body::@enable, semantic_type = !logic, is_signed = false, node_id = 27 : i64} {}
}
obelisk.sv.statement.empty attributes {node_id = 29 : i64} {}
}
}
obelisk.sv.symbol.procedural_block @intra_assignment attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 38 : i64} {
obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, has_timing_control = true, semantic_type = !logic, is_signed = false, node_id = 36 : i64} {
obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.clk", referenced_symbol = @root::@top::@body::@clk, semantic_type = !logic, is_signed = false, node_id = 32 : i64} {}
}
obelisk.sv.expression.named_value attributes {referenced_path = "top.q", referenced_symbol = @root::@top::@body::@q, semantic_type = !logic, is_signed = false, node_id = 34 : i64} {}
obelisk.sv.expression.named_value attributes {referenced_path = "top.d", referenced_symbol = @root::@top::@body::@d, semantic_type = !logic, is_signed = false, node_id = 35 : i64} {}
}
}
}
obelisk.sv.symbol.procedural_block @repeat_event attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 45 : i64} {
obelisk.sv.statement.timed attributes {node_id = 44 : i64} {
obelisk.sv.timing.repeated_event attributes {node_id = 42 : i64} {
obelisk.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !word, is_signed = true, node_id = 39 : i64} {}
obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 41 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.clk", referenced_symbol = @root::@top::@body::@clk, semantic_type = !logic, is_signed = false, node_id = 40 : i64} {}
}
}
obelisk.sv.statement.empty attributes {node_id = 43 : i64} {}
}
}
}
}
}
}
