// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 10.9: a positional assignment pattern on the left-hand side
// assigns each member the corresponding element of the right-hand side. An
// unpacked value is assigned element by element, a packed one splits from
// its most significant bits like a concatenation, and every net member of a
// continuous assignment gets its own driver.
//
// module top;
//   int src[2], p, q;
//   struct packed { byte h, l; } ps;
//   byte h, l;
//   wire [7:0] w0, w1;
//   logic [7:0] ws[2];
//   assign '{w0, w1} = ws;
//   initial begin
//     '{p, q} = src;
//     '{h, l} = ps;
//   end
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.src attributes {hierarchical_name = "top.src", lifetime = 1 : i32, name = "src", node_id = 5 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.integral<32, true, false, 31 : 0, int>>} {
        }
        talos.sv.symbol.variable @s6.p attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 6 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s7.q attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 7 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s8.ps attributes {hierarchical_name = "top.ps", lifetime = 1 : i32, name = "ps", node_id = 8 : i64, semantic_type = !talos.source_aggregate<"top", true, false, false, false, false, false, 16, 16, 16, 0, [{name = "h", ordinal = 0 : i32, packed_offset = 8 : i64, rand_mode = 0 : i32, type = !talos.integral<8, true, false, 7 : 0, byte>}, {name = "l", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<8, true, false, 7 : 0, byte>}]>} {
        }
        talos.sv.symbol.variable @s9.h attributes {hierarchical_name = "top.h", lifetime = 1 : i32, name = "h", node_id = 9 : i64, semantic_type = !talos.integral<8, true, false, 7 : 0, byte>} {
        }
        talos.sv.symbol.variable @s10.l attributes {hierarchical_name = "top.l", lifetime = 1 : i32, name = "l", node_id = 10 : i64, semantic_type = !talos.integral<8, true, false, 7 : 0, byte>} {
        }
        talos.sv.symbol.net @s11.w0 attributes {hierarchical_name = "top.w0", is_implicit = false, name = "w0", net_kind = 1 : i32, node_id = 11 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.net @s12.w1 attributes {hierarchical_name = "top.w1", is_implicit = false, name = "w1", net_kind = 1 : i32, node_id = 12 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.variable @s13.ws attributes {hierarchical_name = "top.ws", lifetime = 1 : i32, name = "ws", node_id = 13 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
        }
        talos.sv.symbol.continuous_assign @s14 attributes {hierarchical_name = "top", node_id = 14 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
            talos.sv.expression.simple_assignment_pattern attributes {is_signed = false, node_id = 16 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 17 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.w0", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.w0, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                }
                talos.sv.expression.empty_argument attributes {is_signed = false, node_id = 19 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 20 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.w1", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.w1, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                }
                talos.sv.expression.empty_argument attributes {is_signed = false, node_id = 22 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.ws", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s13.ws, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
            }
          }
        }
        talos.sv.symbol.procedural_block @s15 attributes {hierarchical_name = "top", node_id = 24 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 25 : i64} {
            talos.sv.statement.list attributes {node_id = 26 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 27 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 28 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.integral<32, true, false, 31 : 0, int>>} {
                  talos.sv.expression.simple_assignment_pattern attributes {is_signed = false, node_id = 29 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.integral<32, true, false, 31 : 0, int>>} {
                    talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 30 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      talos.sv.expression.named_value attributes {is_signed = true, node_id = 31 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.p, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                      talos.sv.expression.empty_argument attributes {is_signed = true, node_id = 32 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 33 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      talos.sv.expression.named_value attributes {is_signed = true, node_id = 34 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                      talos.sv.expression.empty_argument attributes {is_signed = true, node_id = 35 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.src", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.src, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.integral<32, true, false, 31 : 0, int>>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 37 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 38 : i64, semantic_type = !talos.source_aggregate<"top", true, false, false, false, false, false, 16, 16, 16, 0, [{name = "h", ordinal = 0 : i32, packed_offset = 8 : i64, rand_mode = 0 : i32, type = !talos.integral<8, true, false, 7 : 0, byte>}, {name = "l", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<8, true, false, 7 : 0, byte>}]>} {
                  talos.sv.expression.simple_assignment_pattern attributes {is_signed = false, node_id = 39 : i64, semantic_type = !talos.source_aggregate<"top", true, false, false, false, false, false, 16, 16, 16, 0, [{name = "h", ordinal = 0 : i32, packed_offset = 8 : i64, rand_mode = 0 : i32, type = !talos.integral<8, true, false, 7 : 0, byte>}, {name = "l", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<8, true, false, 7 : 0, byte>}]>} {
                    talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 40 : i64, semantic_type = !talos.integral<8, true, false, 7 : 0, byte>} {
                      talos.sv.expression.named_value attributes {is_signed = true, node_id = 41 : i64, referenced_path = "top.h", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.h, semantic_type = !talos.integral<8, true, false, 7 : 0, byte>} {
                      }
                      talos.sv.expression.empty_argument attributes {is_signed = true, node_id = 42 : i64, semantic_type = !talos.integral<8, true, false, 7 : 0, byte>} {
                      }
                    }
                    talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 43 : i64, semantic_type = !talos.integral<8, true, false, 7 : 0, byte>} {
                      talos.sv.expression.named_value attributes {is_signed = true, node_id = 44 : i64, referenced_path = "top.l", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.l, semantic_type = !talos.integral<8, true, false, 7 : 0, byte>} {
                      }
                      talos.sv.expression.empty_argument attributes {is_signed = true, node_id = 45 : i64, semantic_type = !talos.integral<8, true, false, 7 : 0, byte>} {
                      }
                    }
                  }
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.ps", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.ps, semantic_type = !talos.source_aggregate<"top", true, false, false, false, false, false, 16, 16, 16, 0, [{name = "h", ordinal = 0 : i32, packed_offset = 8 : i64, rand_mode = 0 : i32, type = !talos.integral<8, true, false, 7 : 0, byte>}, {name = "l", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<8, true, false, 7 : 0, byte>}]>} {
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


// CHECK: simulation.driver.decl 0 in 1 drives 0 {{.*}} hierarchy "top.w0"
// CHECK: simulation.driver.decl 1 in 1 drives 1 {{.*}} hierarchy "top.w1"

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: %[[WS:[a-z0-9]+]]: !simulation.ref<!simulation.unpacked_array
// CHECK-SAME: %[[W0:[a-z0-9]+]]: !simulation.driver
// CHECK-SAME: %[[W1:[a-z0-9]+]]: !simulation.driver
// CHECK: %[[WS0_REF:.*]] = simulation.ref.subelement %[[WS]]{{\[\[}}0]]
// CHECK: %[[WS0:.*]] = simulation.ref.load %[[WS0_REF]]
// CHECK: %[[WS1_REF:.*]] = simulation.ref.subelement %[[WS]]{{\[\[}}1]]
// CHECK: %[[WS1:.*]] = simulation.ref.load %[[WS1_REF]]
// CHECK: simulation.driver.drive %[[W0]] = %[[WS0]]
// CHECK: simulation.driver.drive %[[W1]] = %[[WS1]]

// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: %[[SRC:[a-z0-9]+]]: !simulation.ref<!simulation.unpacked_array<0 : 1 x i32>>
// CHECK-SAME: %[[P:[a-z0-9]+]]: !simulation.ref<i32>
// CHECK-SAME: %[[Q:[a-z0-9]+]]: !simulation.ref<i32>
// CHECK-SAME: %[[PS:[a-z0-9]+]]: !simulation.ref<!simulation.packed_struct
// CHECK-SAME: %[[H:[a-z0-9]+]]: !simulation.ref<i8>
// CHECK-SAME: %[[L:[a-z0-9]+]]: !simulation.ref<i8>
// CHECK: %[[SRC0_REF:.*]] = simulation.ref.subelement %[[SRC]]{{\[\[}}0]]
// CHECK: %[[SRC0:.*]] = simulation.ref.load %[[SRC0_REF]]
// CHECK: %[[SRC1_REF:.*]] = simulation.ref.subelement %[[SRC]]{{\[\[}}1]]
// CHECK: %[[SRC1:.*]] = simulation.ref.load %[[SRC1_REF]]
// CHECK: simulation.ref.store %[[SRC0]] to %[[P]]
// CHECK: simulation.ref.store %[[SRC1]] to %[[Q]]
// CHECK: %[[PACKED:.*]] = simulation.packed.flatten
// CHECK: %[[HIGH:.*]] = arith.shrui %[[PACKED]], %{{.*}} : i16
// CHECK: %[[H_VALUE:.*]] = arith.trunci %[[HIGH]] : i16 to i8
// CHECK: simulation.ref.store %[[H_VALUE]] to %[[H]]
// CHECK: %[[L_VALUE:.*]] = arith.trunci %[[PACKED]] : i16 to i8
// CHECK: simulation.ref.store %[[L_VALUE]] to %[[L]]
