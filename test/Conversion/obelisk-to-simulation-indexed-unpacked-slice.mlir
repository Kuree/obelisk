// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "indexed_unpacked_slice",
    name = "indexed_unpacked_slice",
    node_id = 0 : i64,
    sym_name = "s0.indexed_unpacked_slice"
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
      hierarchical_name = "indexed_unpacked_slice",
      is_uninstantiated = false,
      name = "indexed_unpacked_slice",
      node_id = 3 : i64,
      referenced_path = "indexed_unpacked_slice",
      referenced_symbol = @s0.indexed_unpacked_slice,
      sym_name = "s3.indexed_unpacked_slice"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "indexed_unpacked_slice",
        name = "indexed_unpacked_slice",
        node_id = 4 : i64,
        sym_name = "s4.indexed_unpacked_slice"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "indexed_unpacked_slice.descending",
          lifetime = 1 : i32,
          name = "descending",
          node_id = 5 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>,
          sym_name = "s5.descending"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "indexed_unpacked_slice.ascending",
          lifetime = 1 : i32,
          name = "ascending",
          node_id = 6 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<0 : 7 x !obelisk.integral<1, false, false, 0 : 0, bit>>,
          sym_name = "s6.ascending"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "indexed_unpacked_slice.falling",
          lifetime = 1 : i32,
          name = "falling",
          node_id = 7 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>,
          sym_name = "s7.falling"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "indexed_unpacked_slice.rising",
          lifetime = 1 : i32,
          name = "rising",
          node_id = 8 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.integral<1, false, false, 0 : 0, bit>>,
          sym_name = "s8.rising"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "indexed_unpacked_slice.target",
          lifetime = 1 : i32,
          name = "target",
          node_id = 25 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>,
          sym_name = "s25.target"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "indexed_unpacked_slice.patch",
          lifetime = 1 : i32,
          name = "patch",
          node_id = 26 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>,
          sym_name = "s26.patch"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "indexed_unpacked_slice",
          node_id = 9 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s9",
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

// CHECK-LABEL: obelisk_sim.func private @unit_0
// `descending[4+:4]` is `descending[7:4]`, so the result runs 7, 6, 5, 4 —
// storage ordinals 0, 1, 2, 3 of a `[7:0]` array.
// CHECK:      obelisk_sim.ref.subelement %arg1{{\[\[}}0]]
// CHECK:      obelisk_sim.ref.subelement %arg1{{\[\[}}1]]
// CHECK:      obelisk_sim.ref.subelement %arg1{{\[\[}}2]]
// CHECK:      obelisk_sim.ref.subelement %arg1{{\[\[}}3]]
// CHECK:      %[[FALLING:.*]] = obelisk_sim.aggregate.construct
// CHECK:      obelisk_sim.ref.store %[[FALLING]] to %arg3
// `ascending[7-:4]` is `ascending[4:7]`, so the result runs 4, 5, 6, 7 —
// storage ordinals 4, 5, 6, 7 of a `[0:7]` array.
// CHECK:      obelisk_sim.ref.subelement %arg2{{\[\[}}4]]
// CHECK:      obelisk_sim.ref.subelement %arg2{{\[\[}}5]]
// CHECK:      obelisk_sim.ref.subelement %arg2{{\[\[}}6]]
// CHECK:      obelisk_sim.ref.subelement %arg2{{\[\[}}7]]
// CHECK:      %[[RISING:.*]] = obelisk_sim.aggregate.construct
// CHECK:      obelisk_sim.ref.store %[[RISING]] to %arg4
// Writing through `target[4+:4]` selects the same window, so result element 0 —
// the value read from `patch` first — lands in `target`'s storage ordinal 0.
// CHECK:      %[[FIRST:.*]] = obelisk_sim.ref.load
// CHECK:      %[[SLOT:.*]] = obelisk_sim.ref.subelement %arg5{{\[\[}}0]]
// CHECK:      obelisk_sim.ref.store %[[FIRST]] to %[[SLOT]]
