// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 25.5.4: a modport expression port names an expression of
// the interface's items. A use through an interface port or a virtual
// interface denotes that expression in the selected instance, so writing a
// concatenation writes each item it spans.
//
//   interface Bus;
//     bit a, b;
//     modport source(output .word({a, b}));
//   endinterface
//   module drv(Bus.source p);
//     initial p.word = 2'b01;
//   endmodule
//   module top;
//     Bus bus();
//     drv d(bus);
//     virtual Bus.source v = bus;
//     initial v.word = 2'b10;
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: %[[A:arg[0-9]+]]: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}
// CHECK-SAME: %[[B:arg[0-9]+]]: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}
// CHECK: simulation.ref.store %false to %[[A]]
// CHECK: simulation.ref.store %true to %[[B]]
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-DAG: %[[VB:.*]] = simulation.context.storage %arg0[1] : !simulation.ref<i1>
// CHECK-DAG: %[[VA:.*]] = simulation.context.storage %arg0[0] : !simulation.ref<i1>
// CHECK: simulation.virtual_interface.scope
// CHECK: cf.cond_br %{{.*}}, ^[[SELA:bb[0-9]+]](%[[VA]] : !simulation.ref<i1>)
// CHECK: ^[[SELA]](%[[SA:.*]]: !simulation.ref<i1>):
// CHECK: simulation.virtual_interface.scope
// CHECK: cf.cond_br %{{.*}}, ^[[SELB:bb[0-9]+]](%[[VB]] : !simulation.ref<i1>)
// CHECK: ^[[SELB]](%[[SB:.*]]: !simulation.ref<i1>):
// CHECK: simulation.ref.store %true{{.*}} to %[[SA]]
// CHECK: simulation.ref.store %false{{.*}} to %[[SB]]
// CHECK-NOT: talos.sv.

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.Bus attributes {definition_kind = 1 : i32, hierarchical_name = "Bus", name = "Bus", node_id = 0 : i64} {
  }
  talos.sv.symbol.definition @s1.drv attributes {definition_kind = 0 : i32, hierarchical_name = "drv", name = "drv", node_id = 1 : i64} {
  }
  talos.sv.symbol.definition @s2.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 2 : i64} {
  }
  talos.sv.symbol.root @s3.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64} {
    talos.sv.symbol.compilation_unit @s4 attributes {hierarchical_name = "$unit", node_id = 4 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s5.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 5 : i64, referenced_path = "top", referenced_symbol = @s2.top} {
      talos.sv.symbol.instance_body @s6.top attributes {hierarchical_name = "top", name = "top", node_id = 6 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.instance @s7.bus attributes {hierarchical_name = "top.bus", is_uninstantiated = false, name = "bus", node_id = 7 : i64, referenced_path = "Bus", referenced_symbol = @s0.Bus} {
          talos.sv.symbol.instance_body @s8.Bus attributes {hierarchical_name = "top.bus", name = "Bus", node_id = 8 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "Bus", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.bus, vpi_scope_kind = 601 : i32} {
            talos.sv.symbol.variable @s9.a attributes {hierarchical_name = "top.bus.a", lifetime = 1 : i32, name = "a", node_id = 9 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
            talos.sv.symbol.variable @s10.b attributes {hierarchical_name = "top.bus.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
            talos.sv.symbol.modport @s11.source attributes {hierarchical_name = "top.bus.source", name = "source", node_id = 11 : i64} {
              talos.sv.symbol.modport_port @s12.word attributes {direction = 1 : i32, hierarchical_name = "top.bus.source.word", modport_explicit_connection, name = "word", node_id = 12 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                talos.sv.expression.concatenation attributes {is_signed = false, node_id = 13 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.bus.a", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s7.bus::@s8.Bus::@s9.a, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  }
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.bus.b", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s7.bus::@s8.Bus::@s10.b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
              }
            }
          }
        }
        talos.sv.symbol.instance @s13.d attributes {hierarchical_name = "top.d", is_uninstantiated = false, name = "d", node_id = 16 : i64, referenced_path = "drv", referenced_symbol = @s1.drv} {
          talos.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "p", formal_ordinal = 0 : i64, formal_path = "top.d.p", formal_symbol = @s3.$root::@s5.top::@s6.top::@s13.d::@s14.drv::@s15.p, formal_type = !talos.untyped, interface_instance_path = "top.bus", interface_instance_symbol = @s3.$root::@s5.top::@s6.top::@s7.bus, interface_shape = array<i64>, is_ansi = false, is_net = false, node_id = 17 : i64, provenance = 0 : i32, selected_modport = "source"} {
          } {
            talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.bus", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s7.bus, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.bus, "">} {
            }
          }
          talos.sv.symbol.instance_body @s14.drv attributes {hierarchical_name = "top.d", name = "drv", node_id = 19 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "drv", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
            talos.sv.symbol.interface_port @s15.p attributes {hierarchical_name = "top.d.p", name = "p", node_id = 20 : i64} {
            }
            talos.sv.symbol.procedural_block @s16 attributes {hierarchical_name = "top.d", node_id = 21 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 22 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 23 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  talos.sv.expression.hierarchical_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "top.bus.source.word", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s7.bus::@s8.Bus::@s11.source::@s12.word, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "2'b1", is_signed = false, node_id = 25 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
        talos.sv.symbol.variable @s17.v attributes {hierarchical_name = "top.v", lifetime = 1 : i32, name = "v", node_id = 26 : i64, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.bus, "source">} {
          talos.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 27 : i64, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.bus, "source">} {
            talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.bus", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s7.bus, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.bus, "">} {
            }
          }
        }
        talos.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "top", node_id = 29 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 30 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 31 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
              talos.sv.expression.member_access attributes {is_signed = false, member_name = "word", node_id = 32 : i64, referenced_path = "top.Bus.source.word", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s19.Bus::@s22.source::@s23.word, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, virtual_interface_access_direction = 1 : i32, virtual_interface_modport = "source"} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.v", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s17.v, semantic_type = !talos.virtual_interface<@s3.$root::@s6.top::@s7.bus, "source">} {
                }
              }
              talos.sv.expression.integer_literal attributes {constant_value = "2'b10", is_signed = false, node_id = 34 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
        }
        talos.sv.symbol.instance_body @s19.Bus attributes {hierarchical_name = "top.Bus", is_virtual_interface_type_instance = true, name = "Bus", node_id = 35 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "Bus", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.bus, vpi_scope_kind = 601 : i32} {
          talos.sv.symbol.variable @s20.a attributes {hierarchical_name = "top.Bus.a", lifetime = 1 : i32, name = "a", node_id = 36 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
          talos.sv.symbol.variable @s21.b attributes {hierarchical_name = "top.Bus.b", lifetime = 1 : i32, name = "b", node_id = 37 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
          talos.sv.symbol.modport @s22.source attributes {hierarchical_name = "top.Bus.source", name = "source", node_id = 38 : i64} {
            talos.sv.symbol.modport_port @s23.word attributes {direction = 1 : i32, hierarchical_name = "top.Bus.source.word", modport_explicit_connection, name = "word", node_id = 39 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
              talos.sv.expression.concatenation attributes {is_signed = false, node_id = 40 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.Bus.a", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s19.Bus::@s20.a, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                }
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, referenced_path = "top.Bus.b", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s19.Bus::@s21.b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

