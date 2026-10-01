// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.indexed_unpacked_slice attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "indexed_unpacked_slice",
    name = "indexed_unpacked_slice",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64
    } {
    }
    obelisk.sv.symbol.instance @s3.indexed_unpacked_slice attributes {
      hierarchical_name = "indexed_unpacked_slice",
      is_uninstantiated = false,
      name = "indexed_unpacked_slice",
      node_id = 3 : i64,
      referenced_path = "indexed_unpacked_slice",
      referenced_symbol = @s0.indexed_unpacked_slice
    } {
      obelisk.sv.symbol.instance_body @s4.indexed_unpacked_slice attributes {
        hierarchical_name = "indexed_unpacked_slice",
        name = "indexed_unpacked_slice",
        node_id = 4 : i64
      } {
        obelisk.sv.symbol.variable @s5.descending attributes {
          hierarchical_name = "indexed_unpacked_slice.descending",
          lifetime = 1 : i32,
          name = "descending",
          node_id = 5 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
        } {
        }
        obelisk.sv.symbol.variable @s6.ascending attributes {
          hierarchical_name = "indexed_unpacked_slice.ascending",
          lifetime = 1 : i32,
          name = "ascending",
          node_id = 6 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<0 : 7 x !obelisk.integral<1, false, false, 0 : 0, bit>>
        } {
        }
        obelisk.sv.symbol.variable @s7.falling attributes {
          hierarchical_name = "indexed_unpacked_slice.falling",
          lifetime = 1 : i32,
          name = "falling",
          node_id = 7 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
        } {
        }
        obelisk.sv.symbol.variable @s8.rising attributes {
          hierarchical_name = "indexed_unpacked_slice.rising",
          lifetime = 1 : i32,
          name = "rising",
          node_id = 8 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.integral<1, false, false, 0 : 0, bit>>
        } {
        }
        obelisk.sv.symbol.variable @s25.target attributes {
          hierarchical_name = "indexed_unpacked_slice.target",
          lifetime = 1 : i32,
          name = "target",
          node_id = 25 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
        } {
        }
        obelisk.sv.symbol.variable @s26.patch attributes {
          hierarchical_name = "indexed_unpacked_slice.patch",
          lifetime = 1 : i32,
          name = "patch",
          node_id = 26 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
        } {
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {
          hierarchical_name = "indexed_unpacked_slice",
          node_id = 9 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32,
                node_id = 12 : i64,
                semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 13 : i64,
                  referenced_path = "indexed_unpacked_slice.falling",
                  referenced_symbol = @s1.$root::@s3.indexed_unpacked_slice::@s4.indexed_unpacked_slice::@s7.falling,
                  semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                }
                obelisk.sv.expression.range_select attributes {
                  node_id = 14 : i64,
                  selection_kind = 1 : i32,
                  semantic_type = !obelisk.ranged_unpacked_array<7 : 4 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 15 : i64,
                    referenced_path = "indexed_unpacked_slice.descending",
                    referenced_symbol = @s1.$root::@s3.indexed_unpacked_slice::@s4.indexed_unpacked_slice::@s5.descending,
                    semantic_type = !obelisk.ranged_unpacked_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                  } {
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "4",
                    is_signed = true,
                    node_id = 16 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "4",
                    is_signed = true,
                    node_id = 17 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32,
                node_id = 19 : i64,
                semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.integral<1, false, false, 0 : 0, bit>>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 20 : i64,
                  referenced_path = "indexed_unpacked_slice.rising",
                  referenced_symbol = @s1.$root::@s3.indexed_unpacked_slice::@s4.indexed_unpacked_slice::@s8.rising,
                  semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                }
                obelisk.sv.expression.range_select attributes {
                  node_id = 21 : i64,
                  selection_kind = 2 : i32,
                  semantic_type = !obelisk.ranged_unpacked_array<4 : 7 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 22 : i64,
                    referenced_path = "indexed_unpacked_slice.ascending",
                    referenced_symbol = @s1.$root::@s3.indexed_unpacked_slice::@s4.indexed_unpacked_slice::@s6.ascending,
                    semantic_type = !obelisk.ranged_unpacked_array<0 : 7 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                  } {
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "7",
                    is_signed = true,
                    node_id = 23 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "4",
                    is_signed = true,
                    node_id = 24 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64} {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32,
                node_id = 28 : i64,
                semantic_type = !obelisk.ranged_unpacked_array<7 : 4 x !obelisk.integral<1, false, false, 0 : 0, bit>>
              } {
                obelisk.sv.expression.range_select attributes {
                  node_id = 29 : i64,
                  selection_kind = 1 : i32,
                  semantic_type = !obelisk.ranged_unpacked_array<7 : 4 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 30 : i64,
                    referenced_path = "indexed_unpacked_slice.target",
                    referenced_symbol = @s1.$root::@s3.indexed_unpacked_slice::@s4.indexed_unpacked_slice::@s25.target,
                    semantic_type = !obelisk.ranged_unpacked_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                  } {
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "4",
                    is_signed = true,
                    node_id = 31 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "4",
                    is_signed = true,
                    node_id = 32 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                }
                obelisk.sv.expression.named_value attributes {
                  node_id = 33 : i64,
                  referenced_path = "indexed_unpacked_slice.patch",
                  referenced_symbol = @s1.$root::@s3.indexed_unpacked_slice::@s4.indexed_unpacked_slice::@s26.patch,
                  semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
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

// IEEE 1800-2017 11.5.1: an indexed part-select names one end of its window and
// grows up (`+:`) or down (`-:`) from there, while "the msb/lsb ordering of the
// part-select is the same as the ordering of the vector being indexed" -- the
// rule 7.4.6 carries over to slicing an unpacked array. So the leftmost result
// element is the source element with the leftmost declared index, not the one
// the base names.

// CHECK-LABEL: simulation.func private @unit_0
// `descending[4+:4]` is `descending[7:4]`, so the result runs 7, 6, 5, 4 —
// storage ordinals 0, 1, 2, 3 of a `[7:0]` array.
// CHECK:      simulation.ref.subelement %arg1{{\[\[}}0]]
// CHECK:      simulation.ref.subelement %arg1{{\[\[}}1]]
// CHECK:      simulation.ref.subelement %arg1{{\[\[}}2]]
// CHECK:      simulation.ref.subelement %arg1{{\[\[}}3]]
// CHECK:      %[[FALLING:.*]] = simulation.aggregate.construct
// CHECK:      simulation.ref.store %[[FALLING]] to %arg3
// `ascending[7-:4]` is `ascending[4:7]`, so the result runs 4, 5, 6, 7 —
// storage ordinals 4, 5, 6, 7 of a `[0:7]` array.
// CHECK:      simulation.ref.subelement %arg2{{\[\[}}4]]
// CHECK:      simulation.ref.subelement %arg2{{\[\[}}5]]
// CHECK:      simulation.ref.subelement %arg2{{\[\[}}6]]
// CHECK:      simulation.ref.subelement %arg2{{\[\[}}7]]
// CHECK:      %[[RISING:.*]] = simulation.aggregate.construct
// CHECK:      simulation.ref.store %[[RISING]] to %arg4
// Writing through `target[4+:4]` selects the same window, so result element 0 —
// the value read from `patch` first — lands in `target`'s storage ordinal 0.
// CHECK:      %[[FIRST:.*]] = simulation.ref.load
// CHECK:      %[[SLOT:.*]] = simulation.ref.subelement %arg5{{\[\[}}0]]
// CHECK:      simulation.ref.store %[[FIRST]] to %[[SLOT]]
