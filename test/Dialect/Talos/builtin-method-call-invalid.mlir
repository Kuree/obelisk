// RUN: talos-opt %s -verify-diagnostics -o /dev/null
module {
  // expected-error@+1 {{failed to verify that builtin methods are system calls}}
  talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "f", constraint_restrictions = [], has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_builtin_method, is_super_class = false, is_system_call = false, node_id = 0 : i64, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
}
