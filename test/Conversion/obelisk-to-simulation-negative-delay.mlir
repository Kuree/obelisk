// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 9.4.1: a negative delay is the unsigned value of its 64-bit
// two's complement. Such a time lies past the simulation time range, so it
// saturates and never matures; a signed delay sign-extends to 64 bits, and a
// negative real saturates as well.
//
// module top;
//   int n;
//   real r;
//   bit a;
//   initial begin
//     #(-1) a = 1;
//     #(n) a = 0;
//     #(r) a = 1;
//   end
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.n attributes {hierarchical_name = "top.n", lifetime = 1 : i32, name = "n", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s6.r attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 6 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.variable @s7.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 11 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 12 : i64} {
                  obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 13 : i64, operator_kind = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 20 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 21 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 22 : i64, referenced_path = "top.n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.n, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 28 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 29 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.r, semantic_type = !obelisk.real} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 32 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 34 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
}


// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[NEVER:.*]] = simulation.time.constant 9223372036854775807{{$}}
// CHECK: simulation.suspend.delay %[[NEVER]]
// CHECK: %[[N:.*]] = simulation.ref.load %arg1 : !simulation.ref<i32>
// CHECK: %[[N64:.*]] = arith.extsi %[[N]] : i32 to i64
// CHECK: %[[FITS:.*]] = arith.cmpi ule, %[[N64]]
// CHECK: %[[TICKS:.*]] = arith.select %[[FITS]], %[[N64]]
// CHECK: simulation.time.scale %[[TICKS]] by 1
// CHECK: %[[R:.*]] = simulation.ref.load %arg2 : !simulation.ref<f64>
// CHECK: %[[NEGATIVE:.*]] = arith.cmpf olt, %[[R]]
// CHECK: %[[REAL:.*]] = arith.select %[[NEGATIVE]], %{{.*}}, %[[R]] : f64
// CHECK: simulation.time.from_real %[[REAL]]
