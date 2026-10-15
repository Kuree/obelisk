// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 39.3.1: a named concurrent assertion is a VPI object of the
// scope that declares it, named by its label, so vpi_handle_by_name and the
// scope's vpiAssertion iteration find it.
//
//   module top;
//     bit clk, a;
//     check_one: assert property (@(posedge clk) a);
//     cov: cover property (@(posedge clk) a);
//   endmodule

// CHECK: simulation.vpi_object.anchor @[[TOP:.*]] id {{[0-9]+}} type 32 in 1 ordinal {{[0-9]+}} hierarchy "top" debug "top"
// CHECK-NEXT: simulation.vpi_object.anchor @{{.*}} id {{[0-9]+}} type 686 in 1 parent @[[TOP]] ordinal 0 hierarchy "top.check_one" debug "check_one"
// CHECK-NEXT: simulation.vpi_object.anchor @{{.*}} id {{[0-9]+}} type 688 in 1 parent @[[TOP]] ordinal 1 hierarchy "top.cov" debug "cov"

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s6.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.statement_block @s7.check_one attributes {block_kind = 0 : i32, hierarchical_name = "top.check_one", name = "check_one", node_id = 7 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.check_one", block_symbol = @s1.$root::@s3.top::@s4.top::@s7.check_one, node_id = 9 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 10 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 11 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 14 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 16 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s9.cov attributes {block_kind = 0 : i32, hierarchical_name = "top.cov", name = "cov", node_id = 17 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 18 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.cov", block_symbol = @s1.$root::@s3.top::@s4.top::@s9.cov, node_id = 19 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 20 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 21 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 22 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 26 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

