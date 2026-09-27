// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A signed value boxed as a wildcard associative-array key must not reuse the
// ordinary container descriptor ID for the same lowered storage type.

!int = !obelisk.integral<32, true, false, 31 : 0, int>
!queue = !obelisk.queue<!int, 0>
!wild = !obelisk.assoc<!obelisk.untyped, !obelisk.string, true>

module {
  obelisk.sv.symbol.definition attributes {
      definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
      node_id = 0 : i64, sym_name = "top_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.values",
            lifetime = 1 : i32, name = "values", node_id = 4 : i64,
            semantic_type = !queue, sym_name = "values"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.items",
            lifetime = 1 : i32, name = "items", node_id = 5 : i64,
            semantic_type = !wild, sym_name = "items"} {}
        obelisk.sv.symbol.statement_block attributes {
            block_kind = 0 : i32, hierarchical_name = "top", node_id = 6 : i64,
            sym_name = "block"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.key",
              lifetime = 1 : i32, name = "key", node_id = 7 : i64,
              semantic_type = !int, sym_name = "key"} {}
        }
        obelisk.sv.symbol.procedural_block attributes {
            hierarchical_name = "top", node_id = 8 : i64,
            procedure_kind = 0 : i32, sym_name = "initial"} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.variable_declaration attributes {
                  node_id = 11 : i64, referenced_path = "top.key",
                  referenced_symbol = @root::@top::@body::@block::@key} {}
              obelisk.sv.statement.expression_statement attributes {
                  node_id = 12 : i64} {
                obelisk.sv.expression.call attributes {
                    argument_count = 2 : i64, callee_name = "push_back",
                    constraint_restrictions = [],
                    defaulted_arguments = array<i64: 0, 0>,
                    has_inline_constraints = false,
                    has_iterator_expression = false,
                    has_output_arguments = false, has_this_class = false,
                    is_signed = false, is_super_class = false,
                    is_system_call = true, node_id = 13 : i64,
                    semantic_type = !obelisk.void, subroutine_kind = 0 : i32,
                    system_library_cell = "work.top", system_scope_path = "top",
                    system_scope_symbol = @root::@top::@body::@block} {
                  obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 14 : i64,
                      referenced_path = "top.values",
                      referenced_symbol = @root::@top::@body::@values,
                      semantic_type = !queue} {}
                  obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 15 : i64,
                      referenced_path = "top.key",
                      referenced_symbol = @root::@top::@body::@block::@key,
                      semantic_type = !int} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {
                  node_id = 16 : i64} {
                obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = false,
                    node_id = 17 : i64, semantic_type = !obelisk.string} {
                  obelisk.sv.expression.element_select attributes {
                      is_signed = false, node_id = 18 : i64,
                      semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {
                        is_signed = false, node_id = 19 : i64,
                        referenced_path = "top.items",
                        referenced_symbol = @root::@top::@body::@items,
                        semantic_type = !wild} {}
                    obelisk.sv.expression.named_value attributes {
                        is_signed = true, node_id = 20 : i64,
                        referenced_path = "top.key",
                        referenced_symbol = @root::@top::@body::@block::@key,
                        semantic_type = !int} {}
                  }
                  obelisk.sv.expression.string_literal attributes {
                      constant_value = "x", is_signed = false,
                      node_id = 21 : i64, semantic_type = !obelisk.string} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.container.create {{.*}}element_flags = #simulation.element_flags<none>{{.*}}type_id = [[PLAIN:[0-9]+]] : i64
// CHECK-NOT: simulation.container.create {{.*}}element_flags = #simulation.element_flags<signed>{{.*}}type_id = [[PLAIN]] : i64
// CHECK: simulation.container.create {{.*}}element_flags = #simulation.element_flags<signed>
