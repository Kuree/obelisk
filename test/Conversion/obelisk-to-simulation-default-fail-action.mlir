// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 20.10 and 15.5.4: the default fail action of an assertion
// or of wait_order is a call to $error. It counts as an error and names the
// file and line, the enclosing scope, and the simulation time.
//
// module top;
//   event a, b;
//   initial begin : blk
//     wait_order (a, b);
//     assert (0);
//   end
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !obelisk.event} {
        }
        obelisk.sv.symbol.variable @s6.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 6 : i64, semantic_type = !obelisk.event} {
        }
        obelisk.sv.symbol.statement_block @s7.blk attributes {block_kind = 0 : i32, hierarchical_name = "top.blk", name = "blk", node_id = 7 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.blk", block_symbol = @s1.$root::@s3.top::@s4.top::@s7.blk, node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.wait_order attributes {event_count = 2 : i64, has_success_action = true, node_id = 11 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.event} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.b, semantic_type = !obelisk.event} {
                }
                obelisk.sv.statement.empty attributes {node_id = 14 : i64} {
                }
              }
              obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = false, is_final = false, node_id = 15 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.statement.empty attributes {node_id = 17 : i64} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[FAILED:.*]] = simulation.wait_order.failed
// CHECK: cf.cond_br %[[FAILED]], ^[[WAIT_ORDER_FAIL:bb[0-9]+]]
// CHECK: ^[[WAIT_ORDER_FAIL]](
// CHECK-NEXT: %[[WAIT_ORDER_TIME:.*]] = simulation.time.now %arg0
// CHECK-NEXT: simulation.error %arg0
// CHECK: %[[WAIT_ORDER_TEXT:.*]] = simulation.bytes.constant "ERROR: {{.*}}:{{[0-9]+}}: top.blk: simulation time %0t: wait_order failed."
// CHECK: simulation.display %arg0 to %{{.*}}(%[[WAIT_ORDER_TEXT]], %[[WAIT_ORDER_TIME]]) {{.*}}scope = "top.blk"
// CHECK: simulation.error %arg0
// CHECK: %[[ASSERT_TEXT:.*]] = simulation.bytes.constant "ERROR: {{.*}}:{{[0-9]+}}: top.blk: simulation time %0t: immediate assertion failed."
// CHECK: simulation.display %arg0 to %{{.*}}(%[[ASSERT_TEXT]], %{{.*}}) {{.*}}scope = "top.blk"
