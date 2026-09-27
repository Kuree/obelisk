// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.12.1: a replication with a zero replication constant has
// a size of zero and is ignored, and may appear only within a concatenation in
// which another operand has a positive size.  The same clause requires the
// replication's operand to be evaluated exactly once even then, so `{{0{f()}},
// a}` still calls `f` and then yields `a` alone.

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
          semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
          sym_name = "s5.a"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.w", lifetime = 1 : i32, name = "w",
          node_id = 6 : i64,
          semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
          sym_name = "s6.w"
        } {
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "t.f", name = "f", node_id = 7 : i64,
          return_variable_path = "t.f.f",
          return_variable_symbol = @s1.$root::@s3.t::@s4.t::@s7.f::@s8.f,
          semantic_type = !obelisk.subroutine<() -> !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, false>,
          subroutine_kind = 0 : i32, sym_name = "s7.f",
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.return attributes {node_id = 8 : i64} {
            obelisk.sv.expression.conversion attributes {
              folded_constant = "2'b11", is_signed = false, node_id = 9 : i64,
              semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
            } {
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "2'b11", is_signed = false,
                node_id = 10 : i64,
                semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
              } {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "t.f.f", is_compiler_generated, name = "f",
            node_id = 11 : i64,
            semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
            sym_name = "s8.f"
          } {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "t", node_id = 12 : i64,
          procedure_kind = 0 : i32, sym_name = "s9",
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 13 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64,
              semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 15 : i64, referenced_path = "t.w",
                referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.w,
                semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
              } {
              }
              obelisk.sv.expression.concatenation attributes {
                is_signed = false, node_id = 16 : i64,
                semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
              } {
                obelisk.sv.expression.replication attributes {
                  is_signed = false, node_id = 17 : i64,
                  semantic_type = !obelisk.void
                } {
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "0", is_declared_unsized = true,
                    is_signed = true, node_id = 18 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                  obelisk.sv.expression.concatenation attributes {
                    is_signed = false, node_id = 19 : i64,
                    semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
                  } {
                    obelisk.sv.expression.call attributes {
                      argument_count = 0 : i64, callee_name = "f",
                      constraint_restrictions = [],
                      defaulted_arguments = array<i64>,
                      has_inline_constraints = false,
                      has_iterator_expression = false,
                      has_output_arguments = false, has_this_class = false,
                      is_signed = false, is_super_class = false,
                      is_system_call = false, node_id = 20 : i64,
                      referenced_path = "t.f",
                      referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.f,
                      semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
                      subroutine_kind = 0 : i32
                    } {
                    }
                  }
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 21 : i64,
                  referenced_path = "t.a",
                  referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.a,
                  semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
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



// CHECK-LABEL: simulation.func private @unit_0
// CHECK: simulation.return %{{.*}} : !simulation.packed_array<1 : 0 x !simulation.logic<1>>

// CHECK-LABEL: simulation.func private @unit_1
// The zero replication's operand is still called once, and its result feeds
// nothing.
// CHECK: simulation.call @unit_0
// CHECK-NOT: simulation.logic.replicate
// CHECK-NOT: simulation.logic.concat
// The concatenation is the surviving operand alone.
// CHECK: %[[A:.*]] = simulation.ref.load %arg1
// CHECK: simulation.ref.store %[[A]] to %arg2
