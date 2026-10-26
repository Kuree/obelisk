// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read opt-level=0' -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.StaticDriverStrengths:GeneratedDesignDatabase.MissingGateStrengthDefault
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.StaticDriverStrengths:GeneratedDesignDatabase.MissingGateStrengthDefault
!logic = !obelisk.integral<1, false, true, 0 : 0, logic>
module attributes {llvm.data_layout = "e-p:64:64-i64:64", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
obelisk.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 49 : i64} {}
obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 52 : i64} {
obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_symbol = @def, referenced_path = "top", node_id = 51 : i64} {
obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 50 : i64} {
obelisk.sv.symbol.net @a attributes {hierarchical_name = "top.a", name = "a", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 1 : i64} {}
obelisk.sv.symbol.net @b attributes {hierarchical_name = "top.b", name = "b", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 2 : i64} {}
obelisk.sv.symbol.net @out attributes {hierarchical_name = "top.out", name = "out", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 3 : i64} {}
obelisk.sv.symbol.primitive_instance @g attributes {hierarchical_name = "top.g", name = "g", primitive_name = "and", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 9 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 6 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 4 : i64} {}
obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 5 : i64} {}
}
obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !logic, is_signed = false, node_id = 7 : i64} {}
obelisk.sv.expression.named_value attributes {referenced_path = "top.b", referenced_symbol = @root::@top::@body::@b, semantic_type = !logic, is_signed = false, node_id = 8 : i64} {}
}
obelisk.sv.symbol.primitive_instance @override attributes {hierarchical_name = "top.override", name = "override", primitive_name = "and", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, drive_strength0 = 4 : i32, drive_strength1 = 3 : i32, node_id = 15 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 12 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 10 : i64} {}
obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 11 : i64} {}
}
obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !logic, is_signed = false, node_id = 13 : i64} {}
obelisk.sv.expression.named_value attributes {referenced_path = "top.b", referenced_symbol = @root::@top::@body::@b, semantic_type = !logic, is_signed = false, node_id = 14 : i64} {}
}
obelisk.sv.symbol.primitive_instance @pu attributes {hierarchical_name = "top.pu", name = "pu", primitive_name = "pullup", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 19 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 18 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 16 : i64} {}
obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 17 : i64} {}
}
}
obelisk.sv.symbol.primitive_instance @pd attributes {hierarchical_name = "top.pd", name = "pd", primitive_name = "pulldown", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 23 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 22 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 20 : i64} {}
obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 21 : i64} {}
}
}
obelisk.sv.symbol.primitive_instance @pu_override attributes {hierarchical_name = "top.pu_override", name = "pu_override", primitive_name = "pullup", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, drive_strength0 = 3 : i32, drive_strength1 = 0 : i32, node_id = 27 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 26 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 24 : i64} {}
obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 25 : i64} {}
}
}
obelisk.sv.symbol.primitive_instance @pd_override attributes {hierarchical_name = "top.pd_override", name = "pd_override", primitive_name = "pulldown", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, drive_strength0 = 0 : i32, drive_strength1 = 3 : i32, node_id = 31 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 30 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 28 : i64} {}
obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 29 : i64} {}
}
}
obelisk.sv.symbol.primitive_instance @explicit_default attributes {hierarchical_name = "top.explicit_default", name = "explicit_default", primitive_name = "and", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, drive_strength0 = 1 : i32, drive_strength1 = 1 : i32, node_id = 37 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 34 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 32 : i64} {}
obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 33 : i64} {}
}
obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !logic, is_signed = false, node_id = 35 : i64} {}
obelisk.sv.expression.named_value attributes {referenced_path = "top.b", referenced_symbol = @root::@top::@body::@b, semantic_type = !logic, is_signed = false, node_id = 36 : i64} {}
}
obelisk.sv.symbol.primitive_instance @sw attributes {hierarchical_name = "top.sw", name = "sw", primitive_name = "tran", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, node_id = 40 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !logic, is_signed = false, node_id = 38 : i64} {}
obelisk.sv.expression.named_value attributes {referenced_path = "top.b", referenced_symbol = @root::@top::@body::@b, semantic_type = !logic, is_signed = false, node_id = 39 : i64} {}
}
obelisk.sv.symbol.continuous_assign @default_assign attributes {hierarchical_name = "top", node_id = 44 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 43 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 41 : i64} {}
obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !logic, is_signed = false, node_id = 42 : i64} {}
}
}
obelisk.sv.symbol.continuous_assign @override_assign attributes {hierarchical_name = "top", drive_strength0 = 2 : i32, drive_strength1 = 3 : i32, node_id = 48 : i64} {
obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 47 : i64} {
obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic, is_signed = false, node_id = 45 : i64} {}
obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !logic, is_signed = false, node_id = 46 : i64} {}
}
}
}
}
}
}
