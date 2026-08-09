// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_narrow_index", name = "simulation_narrow_index", node_id = 0 : i64, sym_name = "s0.simulation_narrow_index"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_narrow_index", is_uninstantiated = false, name = "simulation_narrow_index", node_id = 3 : i64, referenced_path = "simulation_narrow_index", referenced_symbol = @s0.simulation_narrow_index, sym_name = "s3.simulation_narrow_index"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_narrow_index", name = "simulation_narrow_index", node_id = 4 : i64, sym_name = "s4.simulation_narrow_index"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_narrow_index.declared_range", lifetime = 1 : i32, name = "declared_range", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.declared_range"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_narrow_index.zero_based", lifetime = 1 : i32, name = "zero_based", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.zero_based"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_narrow_index.unsigned_index", lifetime = 1 : i32, name = "unsigned_index", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.unsigned_index"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_narrow_index.signed_index", lifetime = 1 : i32, name = "signed_index", node_id = 8 : i64, semantic_type = !obelisk.integral<1, true, true, 0 : 0, logic>, sym_name = "s8.signed_index"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_narrow_index.bit_index", lifetime = 1 : i32, name = "bit_index", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s9.bit_index"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_narrow_index.result", lifetime = 1 : i32, name = "result", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.result"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_narrow_index.part", lifetime = 1 : i32, name = "part", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s11.part"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_narrow_index", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 13 : i64} {
            obelisk.sv.statement.list attributes {node_id = 14 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "simulation_narrow_index.result", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s10.result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "simulation_narrow_index.declared_range", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s5.declared_range, semantic_type = !obelisk.ranged_packed_array<15 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "simulation_narrow_index.unsigned_index", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s7.unsigned_index, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 22 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "simulation_narrow_index.part", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s11.part, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 24 : i64, selection_kind = 2 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "simulation_narrow_index.zero_based", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s6.zero_based, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "simulation_narrow_index.unsigned_index", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s7.unsigned_index, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "simulation_narrow_index.result", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s10.result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "simulation_narrow_index.zero_based", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s6.zero_based, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "simulation_narrow_index.signed_index", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s8.signed_index, semantic_type = !obelisk.integral<1, true, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "simulation_narrow_index.result", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s10.result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 37 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "simulation_narrow_index.declared_range", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s5.declared_range, semantic_type = !obelisk.ranged_packed_array<15 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "simulation_narrow_index.bit_index", referenced_symbol = @s1.$root::@s3.simulation_narrow_index::@s4.simulation_narrow_index::@s9.bit_index, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: %[[THREE_LOGIC:.*]] = obelisk_sim.logic.constant 3 : i66, 0 : i66 : !obelisk_sim.logic<66>

// CHECK: %[[UNSIGNED_RAW:.*]] = obelisk_sim.ref.load {{.*}} -> !obelisk_sim.logic<1>
// CHECK: %[[UNSIGNED:.*]] = obelisk_sim.logic.resize %[[UNSIGNED_RAW]] signed = false : !obelisk_sim.logic<1> -> !obelisk_sim.logic<65>
// CHECK: %[[UNSIGNED_ELEMENT:.*]] = obelisk_sim.ref.array_element {{.*}}[%[[UNSIGNED]]]
// CHECK: obelisk_sim.ref.load %[[UNSIGNED_ELEMENT]]

// CHECK: %[[PART_RAW:.*]] = obelisk_sim.ref.load {{.*}} -> !obelisk_sim.logic<1>
// CHECK: %[[PART_INDEX:.*]] = obelisk_sim.logic.resize %[[PART_RAW]] signed = false : !obelisk_sim.logic<1> -> !obelisk_sim.logic<66>
// CHECK: %[[PART_LOW:.*]] = obelisk_sim.logic.binary sub %[[PART_INDEX]], %[[THREE_LOGIC]] : !obelisk_sim.logic<66>
// CHECK: obelisk_sim.logic.dyn_extract {{.*}} from %[[PART_LOW]]

// CHECK: %[[SIGNED_RAW:.*]] = obelisk_sim.ref.load {{.*}} -> !obelisk_sim.logic<1>
// CHECK: %[[SIGNED:.*]] = obelisk_sim.logic.resize %[[SIGNED_RAW]] signed = true : !obelisk_sim.logic<1> -> !obelisk_sim.logic<65>
// CHECK: %[[SIGNED_ELEMENT:.*]] = obelisk_sim.ref.array_element {{.*}}[%[[SIGNED]]]
// CHECK: obelisk_sim.ref.load %[[SIGNED_ELEMENT]]

// CHECK: %[[BIT_RAW:.*]] = obelisk_sim.ref.load {{.*}} -> i1
// CHECK: %[[BIT_INDEX:.*]] = arith.extui %[[BIT_RAW]] : i1 to i65
// CHECK: %[[BIT_INDEX_ELEMENT:.*]] = obelisk_sim.ref.array_element {{.*}}[%[[BIT_INDEX]]]
// CHECK: obelisk_sim.ref.load %[[BIT_INDEX_ELEMENT]]
// CHECK-NOT: obelisk.sv.
