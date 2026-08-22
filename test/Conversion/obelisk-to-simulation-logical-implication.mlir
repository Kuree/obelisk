// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.7: `a -> b` is logically equivalent to `!a || b`, and
// `a <-> b` to `(a -> b) && (b -> a)`.  Section 11.3.5 short-circuits `->`
// like `||`, so a left operand that is logically false skips the right one,
// while 11.4.7 evaluates each operand of `<->` exactly once.

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
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t,
      sym_name = "s3.t"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "t", name = "t", node_id = 4 : i64,
        sym_name = "s4.t", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.a", lifetime = 1 : i32, name = "a",
          node_id = 5 : i64,
          semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
          sym_name = "s5.a"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.b", lifetime = 1 : i32, name = "b",
          node_id = 6 : i64,
          semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
          sym_name = "s6.b"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.imp", lifetime = 1 : i32, name = "imp",
          node_id = 7 : i64,
          semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
          sym_name = "s7.imp"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.eqv", lifetime = 1 : i32, name = "eqv",
          node_id = 8 : i64,
          semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
          sym_name = "s8.eqv"
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
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 14 : i64,
                    referenced_path = "t.imp",
                    referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.imp,
                    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                  } {
                  }
                  obelisk.sv.expression.binary_op attributes {
                    is_signed = false, node_id = 15 : i64,
                    operator_kind = 21 : i32,
                    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 16 : i64,
                      referenced_path = "t.a",
                      referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.a,
                      semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                    } {
                    }
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 17 : i64,
                      referenced_path = "t.b",
                      referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.b,
                      semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
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
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 20 : i64,
                    referenced_path = "t.eqv",
                    referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.eqv,
                    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                  } {
                  }
                  obelisk.sv.expression.binary_op attributes {
                    is_signed = false, node_id = 21 : i64,
                    operator_kind = 22 : i32,
                    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 22 : i64,
                      referenced_path = "t.a",
                      referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.a,
                      semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                    } {
                    }
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 23 : i64,
                      referenced_path = "t.b",
                      referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.b,
                      semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
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



// CHECK-LABEL: obelisk_sim.func private @unit_0
// `a -> b` negates the left operand and branches on the negation, so the
// right operand is only loaded on the path that needs it.
// CHECK: %[[A:.*]] = obelisk_sim.logic.reduction or
// CHECK: %[[NOT_A:.*]] = obelisk_sim.logic.unary logical_not %[[A]]
// CHECK: %[[TAKEN:.*]] = obelisk_sim.logic.is_true %[[NOT_A]]
// CHECK: cf.cond_br %[[TAKEN]], ^bb2(%{{.*}} : !obelisk_sim.logic<1>), ^bb1
// CHECK: ^bb1:
// CHECK: %[[B:.*]] = obelisk_sim.logic.reduction or
// CHECK: %[[IMPLY:.*]] = obelisk_sim.logic.logical or %[[NOT_A]], %[[B]]
// CHECK: cf.br ^bb2(%[[IMPLY]] : !obelisk_sim.logic<1>)

// `a <-> b` evaluates both operands unconditionally and conjoins the two
// implications.
// CHECK: ^bb2(
// CHECK: %[[EA:.*]] = obelisk_sim.logic.reduction or
// CHECK: %[[EB:.*]] = obelisk_sim.logic.reduction or
// CHECK: %[[NOT_EB:.*]] = obelisk_sim.logic.unary logical_not %[[EB]]
// CHECK: %[[BACKWARD:.*]] = obelisk_sim.logic.logical or %[[NOT_EB]], %[[EA]]
// CHECK: %[[NOT_EA:.*]] = obelisk_sim.logic.unary logical_not %[[EA]]
// CHECK: %[[FORWARD:.*]] = obelisk_sim.logic.logical or %[[NOT_EA]], %[[EB]]
// CHECK: obelisk_sim.logic.logical and %[[FORWARD]], %[[BACKWARD]]
