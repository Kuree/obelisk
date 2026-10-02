// RUN: %split-file %s %t
// RUN: obelisk-opt %t/up.mlir --obelisk-sim-prepare -o %t.up.prepared
// RUN: obelisk-opt %t.up.prepared --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s
// RUN: obelisk-opt %t/down.mlir --obelisk-sim-prepare -o %t.down.prepared
// RUN: obelisk-opt %t.down.prepared --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s
// Both indexed-up and indexed-down assertion slices sample the whole base
// and index before extracting a value, never a dynamically addressed reference.

// CHECK: simulation.assert.sampled_read
// CHECK: %[[DATA:.*]] = simulation.assert.sampled_read {{.*}} : {{.*}} -> !simulation.packed_array<7 : 0 x !simulation.logic<1>>
// CHECK-NOT: simulation.ref.dyn_extract
// CHECK: simulation.packed.flatten %[[DATA]]
// CHECK: simulation.logic.dyn_extract

//--- up.mlir
!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!data = !obelisk.ranged_packed_array<7 : 0 x !logic1>
!index = !obelisk.ranged_packed_array<2 : 0 x !logic1>

module {
  obelisk.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def
    } {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @clk attributes {hierarchical_name = "top.clk",
            lifetime = 1 : i32, name = "clk", node_id = 4 : i64,
            semantic_type = !logic1} {}
        obelisk.sv.symbol.variable @data attributes {hierarchical_name = "top.data",
            lifetime = 1 : i32, name = "data", node_id = 5 : i64,
            semantic_type = !data} {}
        obelisk.sv.symbol.variable @index attributes {hierarchical_name = "top.index",
            lifetime = 1 : i32, name = "index", node_id = 6 : i64,
            semantic_type = !index} {}
        obelisk.sv.symbol.procedural_block @assertion attributes {
            hierarchical_name = "top", node_id = 7 : i64,
            procedure_kind = 2 : i32,
            time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {
              assertion_kind = 0 : i32, has_default_disable = false,
              has_fail_action = false, has_pass_action = true,
              node_id = 8 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 9 : i64} {
              obelisk.sv.timing.signal_event attributes {
                  edge_kind = 1 : i32, has_iff = false, node_id = 10 : i64} {
                obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 11 : i64,
                    referenced_path = "top.clk",
                    referenced_symbol = @root::@top::@body::@clk,
                    semantic_type = !logic1} {}
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false,
                  is_null = false, node_id = 12 : i64,
                  repetition_is_unbounded = false} {
                obelisk.sv.expression.range_select attributes {
                    is_signed = false, node_id = 13 : i64,
                    selection_kind = 1 : i32,
                    semantic_type = !obelisk.ranged_packed_array<3 : 0 x !logic1>} {
                  obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 14 : i64,
                      referenced_path = "top.data",
                      referenced_symbol = @root::@top::@body::@data,
                      semantic_type = !data} {}
                  obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 15 : i64,
                      referenced_path = "top.index",
                      referenced_symbol = @root::@top::@body::@index,
                      semantic_type = !index} {}
                  obelisk.sv.expression.integer_literal attributes {
                      constant_value = "4", is_signed = true, node_id = 17 : i64,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 16 : i64} {}
          }
        }
      }
    }
  }
}

//--- down.mlir
!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!data = !obelisk.ranged_packed_array<7 : 0 x !logic1>
!index = !obelisk.ranged_packed_array<2 : 0 x !logic1>

module {
  obelisk.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def
    } {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @clk attributes {hierarchical_name = "top.clk",
            lifetime = 1 : i32, name = "clk", node_id = 4 : i64,
            semantic_type = !logic1} {}
        obelisk.sv.symbol.variable @data attributes {hierarchical_name = "top.data",
            lifetime = 1 : i32, name = "data", node_id = 5 : i64,
            semantic_type = !data} {}
        obelisk.sv.symbol.variable @index attributes {hierarchical_name = "top.index",
            lifetime = 1 : i32, name = "index", node_id = 6 : i64,
            semantic_type = !index} {}
        obelisk.sv.symbol.procedural_block @assertion attributes {
            hierarchical_name = "top", node_id = 7 : i64,
            procedure_kind = 2 : i32,
            time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {
              assertion_kind = 0 : i32, has_default_disable = false,
              has_fail_action = false, has_pass_action = true,
              node_id = 8 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 9 : i64} {
              obelisk.sv.timing.signal_event attributes {
                  edge_kind = 1 : i32, has_iff = false, node_id = 10 : i64} {
                obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 11 : i64,
                    referenced_path = "top.clk",
                    referenced_symbol = @root::@top::@body::@clk,
                    semantic_type = !logic1} {}
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false,
                  is_null = false, node_id = 12 : i64,
                  repetition_is_unbounded = false} {
                obelisk.sv.expression.range_select attributes {
                    is_signed = false, node_id = 13 : i64,
                    selection_kind = 2 : i32,
                    semantic_type = !obelisk.ranged_packed_array<3 : 0 x !logic1>} {
                  obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 14 : i64,
                      referenced_path = "top.data",
                      referenced_symbol = @root::@top::@body::@data,
                      semantic_type = !data} {}
                  obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 15 : i64,
                      referenced_path = "top.index",
                      referenced_symbol = @root::@top::@body::@index,
                      semantic_type = !index} {}
                  obelisk.sv.expression.integer_literal attributes {
                      constant_value = "4", is_signed = true, node_id = 17 : i64,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 16 : i64} {}
          }
        }
      }
    }
  }
}
