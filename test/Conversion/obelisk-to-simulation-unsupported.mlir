// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_assertion", name = "unsupported_assertion", node_id = 0 : i64, sym_name = "s0.unsupported_assertion"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_assertion", is_uninstantiated = false, name = "unsupported_assertion", node_id = 3 : i64, referenced_path = "unsupported_assertion", referenced_symbol = @s0.unsupported_assertion, sym_name = "s3.unsupported_assertion"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_assertion", name = "unsupported_assertion", node_id = 4 : i64, sym_name = "s4.unsupported_assertion"} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "unsupported_assertion.enable", name = "enable", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.enable"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_assertion.enable", lifetime = 1 : i32, name = "enable", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.enable"} {
        }
        obelisk.sv.symbol.property attributes {has_default_instance = true, hierarchical_name = "unsupported_assertion.named_property", name = "named_property", node_id = 7 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "s7.named_property"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_assertion", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 9 : i64} {
            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 10 : i64, repetition_is_unbounded = false} {
              obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 11 : i64, referenced_path = "unsupported_assertion.named_property", referenced_symbol = @s1.$root::@s3.unsupported_assertion::@s4.unsupported_assertion::@s7.named_property, semantic_type = !obelisk.property} {
                obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "unsupported_assertion.enable", referenced_symbol = @s1.$root::@s3.unsupported_assertion::@s4.unsupported_assertion::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 15 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "unsupported_assertion.enable", referenced_symbol = @s1.$root::@s3.unsupported_assertion::@s4.unsupported_assertion::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 17 : i64} {
            }
          }
        }
      }
    }
  }
}

// CHECK: expect statements are not executable by the bounded concurrent monitor
