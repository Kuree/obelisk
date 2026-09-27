// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 16.5.1 recursively evaluates a sampled expression from its
// sampled arguments. A dynamic packed select therefore applies its sampled
// index to the Preponed snapshot of the whole packed value; the dynamic
// subreference itself does not denote one statically addressable snapshot
// range.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!data = !obelisk.ranged_packed_array<7 : 0 x !logic1>
!index = !obelisk.ranged_packed_array<2 : 0 x !logic1>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64,
      sym_name = "top_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64, sym_name = "body",
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk",
            lifetime = 1 : i32, name = "clk", node_id = 4 : i64,
            semantic_type = !logic1, sym_name = "clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.data",
            lifetime = 1 : i32, name = "data", node_id = 5 : i64,
            semantic_type = !data, sym_name = "data"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.index",
            lifetime = 1 : i32, name = "index", node_id = 6 : i64,
            semantic_type = !index, sym_name = "index"} {}
        obelisk.sv.symbol.procedural_block attributes {
            hierarchical_name = "top", node_id = 7 : i64,
            procedure_kind = 2 : i32, sym_name = "assertion",
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
                obelisk.sv.expression.element_select attributes {
                    is_signed = false, node_id = 13 : i64,
                    semantic_type = !logic1} {
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

// CHECK: %[[INDEX:.*]] = simulation.assert.sampled_read
// CHECK: %[[DATA:.*]] = simulation.assert.sampled_read {{.*}} : {{.*}} -> !simulation.packed_array<7 : 0 x !simulation.logic<1>>
// CHECK-NOT: simulation.ref.array_element
// CHECK: simulation.array.extract_dynamic %[[DATA]]
