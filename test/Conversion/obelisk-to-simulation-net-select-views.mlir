// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

// Constant selects of a built-in net, as a generate loop elaborates them:
//
//   wire [2:0] chain;
//   assign chain[0] = 1'b0;
//   for (genvar i = 0; i < 2; i++) begin : g
//     assign chain[i+1] = ~chain[i];
//   end
//   always_comb if (s) y = chain[1]; else y = chain[2];
//   always_comb z = chain[0] ^ (^chain);
//
// IEEE 1800-2023 10.3: each continuous assignment drives only the bits its
// lvalue names. Elaboration folds `i+1`, so every driver covers its own bit
// instead of the whole net. IEEE 1800-2023 9.2.2.2.1: implicit sensitivity
// watches the longest static prefix of a select, so a process reads and waits
// on only the selected bit. A view selected inside one branch is rebuilt at
// the wait, and a view of a net the process already watches whole is dropped.

module attributes {
  coverage.language_version = 2023 : i32,
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.net_views attributes {definition_kind = 0 : i32, hierarchical_name = "net_views", name = "net_views", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.net_views attributes {hierarchical_name = "net_views", is_uninstantiated = false, name = "net_views", node_id = 3 : i64, referenced_path = "net_views", referenced_symbol = @s0.net_views} {
      obelisk.sv.symbol.instance_body @s4.net_views attributes {hierarchical_name = "net_views", name = "net_views", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "net_views", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.net @s5.chain attributes {hierarchical_name = "net_views.chain", is_implicit = false, name = "chain", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.continuous_assign @s6 attributes {hierarchical_name = "net_views", node_id = 6 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
        }
        obelisk.sv.symbol.generate_block_array @s7.g attributes {array_indices = array<i64: 0, 1>, hierarchical_name = "net_views.g", name = "g", node_id = 13 : i64} {
          obelisk.sv.symbol.genvar @s8.i attributes {hierarchical_name = "net_views.g.i", name = "i", node_id = 14 : i64} {
          }
          obelisk.sv.symbol.generate_block @s9 attributes {hierarchical_name = "net_views.g[0]", is_uninstantiated = false, node_id = 15 : i64} {
            obelisk.sv.symbol.parameter @s10.i attributes {constant_value = "0", hierarchical_name = "net_views.g[0].i", is_local_param, name = "i", node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
            }
            obelisk.sv.symbol.continuous_assign @s11 attributes {hierarchical_name = "net_views.g[0]", node_id = 17 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.binary_op attributes {folded_constant = "1", is_signed = true, node_id = 21 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.named_value attributes {folded_constant = "0", is_signed = true, node_id = 22 : i64, referenced_path = "net_views.g[0].i", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s7.g::@s9::@s10.i, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1", is_implicit = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
                obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 25 : i64, operator_kind = 2 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {folded_constant = "0", is_signed = true, node_id = 28 : i64, referenced_path = "net_views.g[0].i", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s7.g::@s9::@s10.i, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.generate_block @s12 attributes {hierarchical_name = "net_views.g[1]", is_uninstantiated = false, node_id = 29 : i64} {
            obelisk.sv.symbol.parameter @s13.i attributes {constant_value = "1", hierarchical_name = "net_views.g[1].i", is_local_param, name = "i", node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
            }
            obelisk.sv.symbol.continuous_assign @s14 attributes {hierarchical_name = "net_views.g[1]", node_id = 31 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 32 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 34 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.binary_op attributes {folded_constant = "2", is_signed = true, node_id = 35 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.named_value attributes {folded_constant = "1", is_signed = true, node_id = 36 : i64, referenced_path = "net_views.g[1].i", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s7.g::@s12::@s13.i, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1", is_implicit = true, is_signed = true, node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
                obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 39 : i64, operator_kind = 2 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 40 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {folded_constant = "1", is_signed = true, node_id = 42 : i64, referenced_path = "net_views.g[1].i", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s7.g::@s12::@s13.i, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s15.s attributes {hierarchical_name = "net_views.s", lifetime = 1 : i32, name = "s", node_id = 43 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s16.y attributes {hierarchical_name = "net_views.y", lifetime = 1 : i32, name = "y", node_id = 44 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s17.z attributes {hierarchical_name = "net_views.z", lifetime = 1 : i32, name = "z", node_id = 45 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "net_views", node_id = 46 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = true, node_id = 47 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "net_views.s", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s15.s, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 49 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 50 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 51 : i64, referenced_path = "net_views.y", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s16.y, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 52 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 54 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 55 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 56 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "net_views.y", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s16.y, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 58 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s19 attributes {hierarchical_name = "net_views", node_id = 61 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 62 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 63 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 64 : i64, referenced_path = "net_views.z", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s17.z, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 65 : i64, operator_kind = 7 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 66 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 67 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 68 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 69 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 70 : i64, referenced_path = "net_views.chain", referenced_symbol = @s1.$root::@s3.net_views::@s4.net_views::@s5.chain, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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


// CHECK: simulation.driver.decl 0 in {{[0-9]+}} drives [[NET:[0-9]+]] {{.*}} {driven_low = 0 : i64, driven_width = 1 : i64}
// CHECK: simulation.driver.decl 1 in {{[0-9]+}} drives [[NET]] {{.*}} {driven_low = 1 : i64, driven_width = 1 : i64}
// CHECK: simulation.driver.decl 2 in {{[0-9]+}} drives [[NET]] {{.*}} {driven_low = 2 : i64, driven_width = 1 : i64}

// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: %[[CHAIN:[^:]*]]: !simulation.net<!simulation.packed_array<2 : 0 x !simulation.logic<1>>>
// CHECK: %[[BIT:.*]] = simulation.net.extract %[[CHAIN]] from 0
// CHECK: simulation.net.read %[[BIT]] : !simulation.net<!simulation.logic<1>>
// CHECK: simulation.suspend.change %[[BIT]] to

// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: %[[S:[^:]*]]: !simulation.ref<!simulation.logic<1>>
// CHECK-SAME: %[[CHAIN:[^:]*]]: !simulation.net<!simulation.packed_array<2 : 0 x !simulation.logic<1>>>
// CHECK: simulation.net.extract %[[CHAIN]] from 1
// CHECK: simulation.net.extract %[[CHAIN]] from 2
// CHECK: ^bb{{[0-9]+}}:
// CHECK-NEXT: %[[ONE:.*]] = simulation.net.extract %[[CHAIN]] from 1
// CHECK-NEXT: %[[TWO:.*]] = simulation.net.extract %[[CHAIN]] from 2
// CHECK-NEXT: simulation.suspend.any %[[S]], %[[ONE]], %[[TWO]] edges [0, 0, 0]

// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: %[[CHAIN:[^:]*]]: !simulation.net<!simulation.packed_array<2 : 0 x !simulation.logic<1>>>
// CHECK: simulation.net.extract %[[CHAIN]] from 0
// CHECK: simulation.net.read %[[CHAIN]]
// CHECK-NOT: simulation.net.extract
// CHECK: simulation.suspend.change %[[CHAIN]] to
