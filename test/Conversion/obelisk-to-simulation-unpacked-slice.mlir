// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "unpacked_slice",
    name = "unpacked_slice",
    node_id = 0 : i64,
    sym_name = "s0.unpacked_slice"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64,
      sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "unpacked_slice",
      is_uninstantiated = false,
      name = "unpacked_slice",
      node_id = 3 : i64,
      referenced_path = "unpacked_slice",
      referenced_symbol = @s0.unpacked_slice,
      sym_name = "s3.unpacked_slice"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "unpacked_slice",
        name = "unpacked_slice",
        node_id = 4 : i64,
        sym_name = "s4.unpacked_slice"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "unpacked_slice.source",
          lifetime = 1 : i32,
          name = "source",
          node_id = 5 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>,
          sym_name = "s5.source"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "unpacked_slice.result",
          lifetime = 1 : i32,
          name = "result",
          node_id = 6 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>,
          sym_name = "s6.result"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "unpacked_slice",
          node_id = 7 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s7",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 8 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32,
              node_id = 9 : i64,
              semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 10 : i64,
                referenced_path = "unpacked_slice.result",
                referenced_symbol = @s1.$root::@s3.unpacked_slice::@s4.unpacked_slice::@s6.result,
                semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
              } {
              }
              obelisk.sv.expression.range_select attributes {
                node_id = 11 : i64,
                selection_kind = 0 : i32,
                semantic_type = !obelisk.ranged_unpacked_array<7 : 4 x !obelisk.integral<1, false, false, 0 : 0, bit>>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 12 : i64,
                  referenced_path = "unpacked_slice.source",
                  referenced_symbol = @s1.$root::@s3.unpacked_slice::@s4.unpacked_slice::@s5.source,
                  semantic_type = !obelisk.ranged_unpacked_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "7",
                  node_id = 13 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "4",
                  node_id = 14 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-COUNT-4: obelisk_sim.ref.subelement %arg1
// CHECK: %[[SLICE:.*]] = obelisk_sim.aggregate.construct
// CHECK: obelisk_sim.ref.store %[[SLICE]]
