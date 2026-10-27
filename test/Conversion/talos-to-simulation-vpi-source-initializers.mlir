// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0 vpi=full instrument-vpi-frames=true' -o %t
// RUN: FileCheck %s < %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceInitializerFrames
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=3 vpi=full instrument-vpi-frames=true' -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceInitializerFrames
// RUN: talos-opt %t --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 2 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 3 : i64} {
talos.sv.symbol.subroutine @probe attributes {dpi_c_identifier = "initializer_probe", hierarchical_name = "top.probe", is_dpi_context, is_dpi_import, name = "probe", node_id = 5 : i64, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {}
talos.sv.symbol.subroutine @compute attributes {hierarchical_name = "top.compute", name = "compute", subroutine_kind = 0 : i32, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 8 : i64} {
talos.sv.statement.return attributes {node_id = 6 : i64} {
talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 7 : i64} {}
}
}
talos.sv.symbol.subroutine @make_event attributes {hierarchical_name = "top.make_event", name = "make_event", subroutine_kind = 0 : i32, semantic_type = !talos.subroutine<() -> !talos.event, false>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 14 : i64} {
talos.sv.statement.list attributes {node_id = 9 : i64} {
talos.sv.statement.expression_statement attributes {node_id = 10 : i64} {
talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 11 : i64} {}
}
talos.sv.statement.return attributes {node_id = 12 : i64} {
talos.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.seed", referenced_symbol = @root::@top::@body::@seed, semantic_type = !talos.event} {}
}
}
}
talos.sv.symbol.subroutine @owner attributes {hierarchical_name = "top.owner", name = "owner", subroutine_kind = 0 : i32, semantic_type = !talos.subroutine<() -> !talos.void, false>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 18 : i64} {
talos.sv.statement.variable_declaration attributes {node_id = 15 : i64, referenced_path = "top.owner.local", referenced_symbol = @root::@top::@body::@owner::@local} {}
talos.sv.symbol.variable @local attributes {hierarchical_name = "top.owner.local", name = "local", lifetime = 1 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 17 : i64} {
talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "compute", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.compute", referenced_symbol = @root::@top::@body::@compute, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 16 : i64} {}
}
}
talos.sv.symbol.variable @seed attributes {hierarchical_name = "top.seed", name = "seed", lifetime = 1 : i32, semantic_type = !talos.event, node_id = 22 : i64} {

}
talos.sv.symbol.variable @x attributes {hierarchical_name = "top.x", name = "x", lifetime = 1 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 24 : i64} {
talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "compute", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.compute", referenced_symbol = @root::@top::@body::@compute, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 23 : i64} {}
}
talos.sv.symbol.variable @alias attributes {hierarchical_name = "top.alias", name = "alias", lifetime = 1 : i32, semantic_type = !talos.event, node_id = 26 : i64} {
talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "make_event", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.make_event", referenced_symbol = @root::@top::@body::@make_event, semantic_type = !talos.event, subroutine_kind = 0 : i32, node_id = 25 : i64} {}
}
talos.sv.type.class_type @Node attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "top.Node", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Node", node_id = 19 : i64, semantic_type = !talos.class_handle<@root::@top::@body::@Node>} {
talos.sv.symbol.class_property @count attributes {hierarchical_name = "top.Node::count", name = "count", lifetime = 1 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 21 : i64} {
talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "compute", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.compute", referenced_symbol = @root::@top::@body::@compute, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 20 : i64} {}
}
}
      }
    }
  }
}
// CHECK-DAG: simulation.vpi_initializer_binding
// CHECK-DAG: simulation.code_unit.decl {{.*}}hierarchy "top.x.$static_initializer"{{.*}}internal
// CHECK-DAG: simulation.code_unit.decl {{.*}}hierarchy "top.alias.$static_initializer"{{.*}}internal
// CHECK-DAG: simulation.code_unit.decl {{.*}}hierarchy "top.Node::count.$static_initializer"{{.*}}internal
// CHECK-DAG: simulation.code_unit.decl {{.*}}hierarchy "top.owner.local.$static_initializer"{{.*}}internal
