// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

// IEEE 1800-2017 10.4.2 and 11.5.1: capture the selected unpacked
// aggregate element and the dynamic packed index when the NBA is encountered,
// then clip the write to that four-bit element. The six-bit view must not be
// formed against the flattened eight-bit array storage, where an overhang
// could corrupt the neighboring element.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.aggregate_partial_nba attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "aggregate_partial_nba",
    name = "aggregate_partial_nba",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {
    }
    obelisk.sv.symbol.instance @s3.aggregate_partial_nba attributes {
      hierarchical_name = "aggregate_partial_nba", is_uninstantiated = false,
      name = "aggregate_partial_nba", node_id = 3 : i64,
      referenced_path = "aggregate_partial_nba",
      referenced_symbol = @s0.aggregate_partial_nba
    } {
      obelisk.sv.symbol.instance_body @s4.aggregate_partial_nba attributes {
        hierarchical_name = "aggregate_partial_nba",
        name = "aggregate_partial_nba", node_id = 4 : i64,
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable @s5.x attributes {
          hierarchical_name = "aggregate_partial_nba.x", lifetime = 1 : i32,
          name = "x", node_id = 5 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>
        } {
        }
        obelisk.sv.symbol.variable @s6.i attributes {
          hierarchical_name = "aggregate_partial_nba.i", lifetime = 1 : i32,
          name = "i", node_id = 6 : i64,
          semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
        } {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {
          hierarchical_name = "aggregate_partial_nba", node_id = 7 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 1 : i32, is_signed = false, node_id = 9 : i64,
              semantic_type = !obelisk.ranged_packed_array<5 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
            } {
              obelisk.sv.expression.range_select attributes {
                is_signed = false, node_id = 10 : i64,
                selection_kind = 1 : i32,
                semantic_type = !obelisk.ranged_packed_array<5 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
              } {
                obelisk.sv.expression.element_select attributes {
                  is_signed = false, node_id = 11 : i64,
                  semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 12 : i64,
                    referenced_path = "aggregate_partial_nba.x",
                    referenced_symbol = @s1.$root::@s3.aggregate_partial_nba::@s4.aggregate_partial_nba::@s5.x,
                    semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>
                  } {
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "0", is_declared_unsized = true,
                    is_signed = true, node_id = 13 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 14 : i64,
                  referenced_path = "aggregate_partial_nba.i",
                  referenced_symbol = @s1.$root::@s3.aggregate_partial_nba::@s4.aggregate_partial_nba::@s6.i,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "6", is_declared_unsized = true,
                  is_signed = true, node_id = 15 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
              }
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "6'b101010", is_signed = false,
                node_id = 16 : i64,
                semantic_type = !obelisk.ranged_packed_array<5 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
              } {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[ELEMENT:.*]] = simulation.ref.subelement %{{.*}}{{\[\[1\]\]}}
// CHECK-SAME: -> !simulation.ref<!simulation.packed_array<3 : 0 x i1>>
// CHECK-NOT: simulation.ref.load %[[ELEMENT]]
// CHECK: %[[SLICE:.*]] = simulation.ref.dyn_extract %[[ELEMENT]] from
// CHECK-SAME: -> !simulation.ref<!simulation.packed_array<5 : 0 x i1>>
// CHECK: simulation.nba.enqueue {{.*}} to %[[SLICE]]
// CHECK-NOT: simulation.ref.store
