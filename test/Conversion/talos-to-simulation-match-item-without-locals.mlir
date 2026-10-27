// RUN: talos-opt %s --verify-diagnostics '--lower-talos-to-sim=opt-level=0' \
// RUN:   | FileCheck %s

// IEEE 1800-2023 16.11: a subroutine attached to a sequence match runs once
// per matching attempt, whether or not the property declares a local. The
// call is outlined and spawned at the attempt's endpoint.
//
// module top; bit clk;
// function void f(); endfunction
// ap: assert property (@(posedge clk) (1,f()));
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.subroutine @s6.f attributes {default_lifetime = 1 : i32, hierarchical_name = "top.f", name = "f", node_id = 6 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 7 : i64} {
          }
        }
        talos.sv.symbol.statement_block @s7.ap attributes {block_kind = 0 : i32, hierarchical_name = "top.ap", name = "ap", node_id = 8 : i64} {
        }
        talos.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {block_path = "top.ap", block_symbol = @s1.$root::@s3.top::@s4.top::@s7.ap, node_id = 10 : i64} {
            talos.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 11 : i64} {
              talos.sv.assertion.clocking attributes {node_id = 12 : i64} {
                talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                talos.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 15 : i64, repetition_is_unbounded = false} {
                  talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                    talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "f", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 18 : i64, referenced_path = "top.f", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.f, semantic_type = !talos.void, subroutine_kind = 0 : i32} {
                  }
                }
              }
              talos.sv.statement.empty attributes {node_id = 19 : i64} {
              }
            }
          }
        }
      }
    }
  }
}


// CHECK: simulation.func private @[[F:unit_0]](
// CHECK: simulation.func private @[[CALL:unit_1.fork.[0-9.]+]](
// CHECK: simulation.call @[[F]](%arg0)
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: simulation.suspend.edge posedge %arg1
// CHECK: simulation.spawn @[[CALL]](%arg0)
