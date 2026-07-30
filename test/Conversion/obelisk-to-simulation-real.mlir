// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "real_aggregate", name = "real_aggregate", node_id = 0 : i64, sym_name = "s0.real_aggregate"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "real_arithmetic", name = "real_arithmetic", node_id = 1 : i64, sym_name = "s1.real_arithmetic"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "real_literal", name = "real_literal", node_id = 2 : i64, sym_name = "s2.real_literal"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "real_port", name = "real_port", node_id = 3 : i64, sym_name = "s3.real_port"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "shortreal_value", name = "shortreal_value", node_id = 4 : i64, sym_name = "s4.shortreal_value"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 5 : i64, sym_name = "s5.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 6 : i64, sym_name = "s6"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "real_aggregate", is_uninstantiated = false, name = "real_aggregate", node_id = 7 : i64, referenced_path = "real_aggregate", referenced_symbol = @s0.real_aggregate, sym_name = "s7.real_aggregate"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "real_aggregate", name = "real_aggregate", node_id = 8 : i64, sym_name = "s8.real_aggregate"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_aggregate.values", lifetime = 1 : i32, name = "values", node_id = 9 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.real>, sym_name = "s9.values"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "real_arithmetic", is_uninstantiated = false, name = "real_arithmetic", node_id = 10 : i64, referenced_path = "real_arithmetic", referenced_symbol = @s1.real_arithmetic, sym_name = "s10.real_arithmetic"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "real_arithmetic", name = "real_arithmetic", node_id = 11 : i64, sym_name = "s11.real_arithmetic"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_arithmetic.lhs", lifetime = 1 : i32, name = "lhs", node_id = 12 : i64, semantic_type = !obelisk.real, sym_name = "s12.lhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_arithmetic.rhs", lifetime = 1 : i32, name = "rhs", node_id = 13 : i64, semantic_type = !obelisk.real, sym_name = "s13.rhs"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "real_arithmetic", node_id = 14 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 16 : i64, semantic_type = !obelisk.real} {
              obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "real_arithmetic.lhs", referenced_symbol = @s5.$root::@s10.real_arithmetic::@s11.real_arithmetic::@s12.lhs, semantic_type = !obelisk.real} {
              }
              obelisk.sv.expression.binary_op attributes {node_id = 18 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "real_arithmetic.lhs", referenced_symbol = @s5.$root::@s10.real_arithmetic::@s11.real_arithmetic::@s12.lhs, semantic_type = !obelisk.real} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "real_arithmetic.rhs", referenced_symbol = @s5.$root::@s10.real_arithmetic::@s11.real_arithmetic::@s13.rhs, semantic_type = !obelisk.real} {
                }
              }
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "real_literal", is_uninstantiated = false, name = "real_literal", node_id = 21 : i64, referenced_path = "real_literal", referenced_symbol = @s2.real_literal, sym_name = "s15.real_literal"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "real_literal", name = "real_literal", node_id = 22 : i64, sym_name = "s16.real_literal"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_literal.value", lifetime = 1 : i32, name = "value", node_id = 23 : i64, semantic_type = !obelisk.real, sym_name = "s17.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "real_literal", node_id = 24 : i64, procedure_kind = 0 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 26 : i64, semantic_type = !obelisk.real} {
              obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "real_literal.value", referenced_symbol = @s5.$root::@s15.real_literal::@s16.real_literal::@s17.value, semantic_type = !obelisk.real} {
              }
              obelisk.sv.expression.real_literal attributes {constant_value = "1.5", node_id = 28 : i64, semantic_type = !obelisk.real} {
              }
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "real_port", is_uninstantiated = false, name = "real_port", node_id = 29 : i64, referenced_path = "real_port", referenced_symbol = @s3.real_port, sym_name = "s19.real_port"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "real_port", name = "real_port", node_id = 30 : i64, sym_name = "s20.real_port"} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "real_port.value", name = "value", node_id = 31 : i64, semantic_type = !obelisk.real, sym_name = "s21.value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_port.value", lifetime = 1 : i32, name = "value", node_id = 32 : i64, semantic_type = !obelisk.real, sym_name = "s22.value"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "shortreal_value", is_uninstantiated = false, name = "shortreal_value", node_id = 33 : i64, referenced_path = "shortreal_value", referenced_symbol = @s4.shortreal_value, sym_name = "s23.shortreal_value"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "shortreal_value", name = "shortreal_value", node_id = 34 : i64, sym_name = "s24.shortreal_value"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "shortreal_value.value", lifetime = 1 : i32, name = "value", node_id = 35 : i64, semantic_type = !obelisk.shortreal, sym_name = "s25.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "shortreal_value", node_id = 36 : i64, procedure_kind = 0 : i32, sym_name = "s26", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 38 : i64, semantic_type = !obelisk.shortreal} {
              obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "shortreal_value.value", referenced_symbol = @s5.$root::@s23.shortreal_value::@s24.shortreal_value::@s25.value, semantic_type = !obelisk.shortreal} {
              }
              obelisk.sv.expression.conversion attributes {node_id = 40 : i64, semantic_type = !obelisk.shortreal} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: arith.constant 1.500000e+00 : f64
// CHECK-DAG: arith.addf
// CHECK-DAG: obelisk_sim.storage.decl {{.*}} : f32
// CHECK-DAG: obelisk_sim.storage.decl {{.*}} : !obelisk_sim.unpacked_array<0 : 1 x f64>
// CHECK-DAG: obelisk_sim.storage.decl {{.*}} : f64
// CHECK-NOT: obelisk.sv.
