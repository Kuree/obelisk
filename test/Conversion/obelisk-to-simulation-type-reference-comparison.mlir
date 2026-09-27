// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.23: a type reference shall only be compared with another
// type reference, and two are equal exactly when the types they refer to match
// (6.22.1); such comparisons are constant expressions.  The importer numbers
// each distinct matching type, so `type(logic [12:0]) == type(logic12_t)` is
// true through the typedef of 6.22.1(b) while `type(real)` differs, and the
// case of 12.5 selects its item here rather than at run time.

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "t", name = "t",
    node_id = 0 : i64, sym_name = "s0.t"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {
      obelisk.sv.type.type_alias attributes {
        hierarchical_name = "logic12_t", name = "logic12_t", node_id = 3 : i64,
        semantic_type = !obelisk.ranged_packed_array<12 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
        sym_name = "s3.logic12_t"
      } {
      }
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 4 : i64, referenced_path = "t", referenced_symbol = @s0.t,
      sym_name = "s4.t"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "t", name = "t", node_id = 5 : i64,
        sym_name = "s5.t", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.same", lifetime = 1 : i32, name = "same",
          node_id = 6 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "s6.same"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.differ", lifetime = 1 : i32, name = "differ",
          node_id = 7 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "s7.differ"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.picked", lifetime = 1 : i32, name = "picked",
          node_id = 8 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s8.picked"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "t", node_id = 9 : i64, procedure_kind = 0 : i32,
          sym_name = "s9", time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.expression_statement attributes {
                node_id = 12 : i64
              } {
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = false,
                  node_id = 13 : i64,
                  semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 14 : i64,
                    referenced_path = "t.same",
                    referenced_symbol = @s1.$root::@s4.t::@s5.t::@s6.same,
                    semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                  } {
                  }
                  obelisk.sv.expression.binary_op attributes {
                    is_signed = false, node_id = 15 : i64,
                    operator_kind = 9 : i32,
                    semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                  } {
                    obelisk.sv.expression.type_reference attributes {
                      is_signed = false, node_id = 16 : i64,
                      semantic_type = !obelisk.type_reference,
                      type_reference_identity = 0 : i64
                    } {
                    }
                    obelisk.sv.expression.type_reference attributes {
                      is_signed = false, node_id = 17 : i64,
                      semantic_type = !obelisk.type_reference,
                      type_reference_identity = 0 : i64
                    } {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {
                node_id = 18 : i64
              } {
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = false,
                  node_id = 19 : i64,
                  semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 20 : i64,
                    referenced_path = "t.differ",
                    referenced_symbol = @s1.$root::@s4.t::@s5.t::@s7.differ,
                    semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                  } {
                  }
                  obelisk.sv.expression.binary_op attributes {
                    is_signed = false, node_id = 21 : i64,
                    operator_kind = 9 : i32,
                    semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                  } {
                    obelisk.sv.expression.type_reference attributes {
                      is_signed = false, node_id = 22 : i64,
                      semantic_type = !obelisk.type_reference,
                      type_reference_identity = 1 : i64
                    } {
                    }
                    obelisk.sv.expression.type_reference attributes {
                      is_signed = false, node_id = 23 : i64,
                      semantic_type = !obelisk.type_reference,
                      type_reference_identity = 0 : i64
                    } {
                    }
                  }
                }
              }
              obelisk.sv.statement.case attributes {
                check_kind = 0 : i32, condition_kind = 0 : i32,
                has_default = true, item_count = 2 : i64,
                item_label_counts = array<i64: 1, 1>, node_id = 24 : i64
              } {
                obelisk.sv.expression.type_reference attributes {
                  is_signed = false, node_id = 25 : i64,
                  semantic_type = !obelisk.type_reference,
                  type_reference_identity = 1 : i64
                } {
                }
                obelisk.sv.expression.type_reference attributes {
                  is_signed = false, node_id = 26 : i64,
                  semantic_type = !obelisk.type_reference,
                  type_reference_identity = 0 : i64
                } {
                }
                obelisk.sv.expression.type_reference attributes {
                  is_signed = false, node_id = 27 : i64,
                  semantic_type = !obelisk.type_reference,
                  type_reference_identity = 1 : i64
                } {
                }
                obelisk.sv.statement.expression_statement attributes {
                  node_id = 28 : i64
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 29 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 30 : i64,
                      referenced_path = "t.picked",
                      referenced_symbol = @s1.$root::@s4.t::@s5.t::@s8.picked,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {
                    }
                    obelisk.sv.expression.integer_literal attributes {
                      constant_value = "1", is_declared_unsized = true,
                      is_signed = true, node_id = 31 : i64,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {
                  node_id = 32 : i64
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 33 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 34 : i64,
                      referenced_path = "t.picked",
                      referenced_symbol = @s1.$root::@s4.t::@s5.t::@s8.picked,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {
                    }
                    obelisk.sv.expression.integer_literal attributes {
                      constant_value = "2", is_declared_unsized = true,
                      is_signed = true, node_id = 35 : i64,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {
                  node_id = 36 : i64
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 37 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 38 : i64,
                      referenced_path = "t.picked",
                      referenced_symbol = @s1.$root::@s4.t::@s5.t::@s8.picked,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {
                    }
                    obelisk.sv.expression.integer_literal attributes {
                      constant_value = "3", is_declared_unsized = true,
                      is_signed = true, node_id = 39 : i64,
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
  }
}



// CHECK-LABEL: simulation.func private @unit_0
// CHECK-DAG: %[[TWO:.*]] = arith.constant 2 : i32
// CHECK-DAG: %[[FALSE:.*]] = arith.constant false
// CHECK-DAG: %[[TRUE:.*]] = arith.constant true
// A typedef matches the type it renames, and `real` matches neither.
// CHECK: simulation.ref.store %[[TRUE]] to %arg1
// CHECK: simulation.ref.store %[[FALSE]] to %arg2
// Only the matching case item survives; no comparison is left to run.
// CHECK-NOT: cf.cond_br
// CHECK: simulation.ref.store %[[TWO]] to %arg3
