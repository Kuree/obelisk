// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=LOWER \
// RUN:     --implicit-check-not=obelisk.sv.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "inventory_bus", name = "inventory_bus", node_id = 0 : i64, sym_name = "s0.inventory_bus"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inventory_generic_interface_child", name = "inventory_generic_interface_child", node_id = 1 : i64, sym_name = "s1.inventory_generic_interface_child"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inventory_interface_array_child", name = "inventory_interface_array_child", node_id = 2 : i64, sym_name = "s2.inventory_interface_array_child"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inventory_interface_child", name = "inventory_interface_child", node_id = 3 : i64, sym_name = "s3.inventory_interface_child"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inventory_leaf", name = "inventory_leaf", node_id = 4 : i64, sym_name = "s4.inventory_leaf"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inventory_multiport", name = "inventory_multiport", node_id = 5 : i64, sym_name = "s5.inventory_multiport"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inventory_nonansi", name = "inventory_nonansi", node_id = 6 : i64, sym_name = "s6.inventory_nonansi"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32, hierarchical_name = "inventory_program", name = "inventory_program", node_id = 7 : i64, sym_name = "s7.inventory_program"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "port_connections_interface_inventory", name = "port_connections_interface_inventory", node_id = 8 : i64, sym_name = "s8.port_connections_interface_inventory"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "port_connections_inventory", name = "port_connections_inventory", node_id = 9 : i64, sym_name = "s9.port_connections_inventory"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 10 : i64, sym_name = "s10.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 11 : i64, sym_name = "s11"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_interface_inventory", is_uninstantiated = false, name = "port_connections_interface_inventory", node_id = 12 : i64, referenced_path = "port_connections_interface_inventory", referenced_symbol = @s8.port_connections_interface_inventory, sym_name = "s12.port_connections_interface_inventory"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_interface_inventory", name = "port_connections_interface_inventory", node_id = 13 : i64, sym_name = "s13.port_connections_interface_inventory"} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_interface_inventory.bus", is_uninstantiated = false, name = "bus", node_id = 14 : i64, referenced_path = "inventory_bus", referenced_symbol = @s0.inventory_bus, sym_name = "s14.bus"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_interface_inventory.bus", name = "inventory_bus", node_id = 15 : i64, sym_name = "s15.inventory_bus"} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_interface_inventory.bus.value", lifetime = 1 : i32, name = "value", node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s16.value"} {
            }
            obelisk.sv.symbol.modport attributes {hierarchical_name = "port_connections_interface_inventory.bus.consumer", name = "consumer", node_id = 17 : i64, sym_name = "s17.consumer"} {
              obelisk.sv.symbol.modport_port attributes {hierarchical_name = "port_connections_interface_inventory.bus.consumer.value", name = "value", node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s18.value"} {
              }
            }
          }
        }
        obelisk.sv.symbol.instance_array attributes {hierarchical_name = "port_connections_interface_inventory.buses", name = "buses", node_id = 19 : i64, sym_name = "s19.buses"} {
          obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_interface_inventory.buses[0]", is_uninstantiated = false, node_id = 20 : i64, referenced_path = "inventory_bus", referenced_symbol = @s0.inventory_bus, sym_name = "s20"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_interface_inventory.buses[0]", name = "inventory_bus", node_id = 21 : i64, sym_name = "s21.inventory_bus"} {
              obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_interface_inventory.buses[0].value", lifetime = 1 : i32, name = "value", node_id = 22 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s22.value"} {
              }
              obelisk.sv.symbol.modport attributes {hierarchical_name = "port_connections_interface_inventory.buses[0].consumer", name = "consumer", node_id = 23 : i64, sym_name = "s23.consumer"} {
                obelisk.sv.symbol.modport_port attributes {hierarchical_name = "port_connections_interface_inventory.buses[0].consumer.value", name = "value", node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s24.value"} {
                }
              }
            }
          }
          obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_interface_inventory.buses[1]", is_uninstantiated = false, node_id = 25 : i64, referenced_path = "inventory_bus", referenced_symbol = @s0.inventory_bus, sym_name = "s25"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_interface_inventory.buses[1]", name = "inventory_bus", node_id = 26 : i64, sym_name = "s26.inventory_bus"} {
              obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_interface_inventory.buses[1].value", lifetime = 1 : i32, name = "value", node_id = 27 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s27.value"} {
              }
              obelisk.sv.symbol.modport attributes {hierarchical_name = "port_connections_interface_inventory.buses[1].consumer", name = "consumer", node_id = 28 : i64, sym_name = "s28.consumer"} {
                obelisk.sv.symbol.modport_port attributes {hierarchical_name = "port_connections_interface_inventory.buses[1].consumer.value", name = "value", node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s29.value"} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_interface_inventory.child", is_uninstantiated = false, name = "child", node_id = 30 : i64, referenced_path = "inventory_interface_child", referenced_symbol = @s3.inventory_interface_child, sym_name = "s30.child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "bus", formal_ordinal = 0 : i64, formal_path = "port_connections_interface_inventory.child.bus", formal_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s30.child::@s31.inventory_interface_child::@s32.bus, formal_type = !obelisk.untyped, interface_instance_path = "port_connections_interface_inventory.bus", interface_instance_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s14.bus, interface_shape = array<i64>, is_ansi = false, is_net = false, node_id = 31 : i64, provenance = 1 : i32, selected_modport = "consumer"} {
          } {
            obelisk.sv.expression.arbitrary_symbol attributes {node_id = 32 : i64, referenced_path = "port_connections_interface_inventory.bus", referenced_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s14.bus, semantic_type = !obelisk.virtual_interface<@s10.$root::@s13.port_connections_interface_inventory::@s14.bus, "">} {
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_interface_inventory.child", name = "inventory_interface_child", node_id = 33 : i64, sym_name = "s31.inventory_interface_child"} {
            obelisk.sv.symbol.interface_port attributes {hierarchical_name = "port_connections_interface_inventory.child.bus", name = "bus", node_id = 34 : i64, sym_name = "s32.bus"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_interface_inventory.array_child", is_uninstantiated = false, name = "array_child", node_id = 35 : i64, referenced_path = "inventory_interface_array_child", referenced_symbol = @s2.inventory_interface_array_child, sym_name = "s33.array_child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "bus", formal_ordinal = 0 : i64, formal_path = "port_connections_interface_inventory.array_child.bus", formal_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s33.array_child::@s34.inventory_interface_array_child::@s35.bus, formal_type = !obelisk.untyped, interface_instance_path = "port_connections_interface_inventory.buses", interface_instance_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s19.buses, interface_shape = array<i64: 1, 0>, is_ansi = false, is_net = false, node_id = 36 : i64, provenance = 1 : i32, selected_modport = "consumer"} {
          } {
            obelisk.sv.expression.arbitrary_symbol attributes {node_id = 37 : i64, referenced_path = "port_connections_interface_inventory.buses", referenced_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s19.buses, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.virtual_interface<@s10.$root::@s13.port_connections_interface_inventory::@s19.buses::@s20, "">>} {
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_interface_inventory.array_child", name = "inventory_interface_array_child", node_id = 38 : i64, sym_name = "s34.inventory_interface_array_child"} {
            obelisk.sv.symbol.interface_port attributes {hierarchical_name = "port_connections_interface_inventory.array_child.bus", name = "bus", node_id = 39 : i64, sym_name = "s35.bus"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_interface_inventory.generic_child", is_uninstantiated = false, name = "generic_child", node_id = 40 : i64, referenced_path = "inventory_generic_interface_child", referenced_symbol = @s1.inventory_generic_interface_child, sym_name = "s36.generic_child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "bus", formal_ordinal = 0 : i64, formal_path = "port_connections_interface_inventory.generic_child.bus", formal_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s36.generic_child::@s37.inventory_generic_interface_child::@s38.bus, formal_type = !obelisk.untyped, interface_instance_path = "port_connections_interface_inventory.bus", interface_instance_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s14.bus, interface_shape = array<i64>, is_ansi = false, is_net = false, node_id = 41 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.arbitrary_symbol attributes {node_id = 42 : i64, referenced_path = "port_connections_interface_inventory.bus", referenced_symbol = @s10.$root::@s12.port_connections_interface_inventory::@s13.port_connections_interface_inventory::@s14.bus, semantic_type = !obelisk.virtual_interface<@s10.$root::@s13.port_connections_interface_inventory::@s14.bus, "">} {
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_interface_inventory.generic_child", name = "inventory_generic_interface_child", node_id = 43 : i64, sym_name = "s37.inventory_generic_interface_child"} {
            obelisk.sv.symbol.interface_port attributes {hierarchical_name = "port_connections_interface_inventory.generic_child.bus", name = "bus", node_id = 44 : i64, sym_name = "s38.bus"} {
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory", is_uninstantiated = false, name = "port_connections_inventory", node_id = 45 : i64, referenced_path = "port_connections_inventory", referenced_symbol = @s9.port_connections_inventory, sym_name = "s39.port_connections_inventory"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory", name = "port_connections_inventory", node_id = 46 : i64, sym_name = "s40.port_connections_inventory"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 47 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s41.defaulted"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.input_value", lifetime = 1 : i32, name = "input_value", node_id = 48 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s42.input_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.output_value", lifetime = 1 : i32, name = "output_value", node_id = 49 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s43.output_value"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 50 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s44.net_value"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.ordered", is_uninstantiated = false, name = "ordered", node_id = 51 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s45.ordered"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.ordered.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s45.ordered::@s46.inventory_leaf::@s47.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.ordered.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s45.ordered::@s46.inventory_leaf::@s48.defaulted, is_ansi = true, is_net = false, node_id = 52 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.ordered.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s45.ordered::@s46.inventory_leaf::@s49.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.ordered.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s45.ordered::@s46.inventory_leaf::@s50.input_value, is_ansi = true, is_net = false, node_id = 54 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 55 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.ordered.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s45.ordered::@s46.inventory_leaf::@s51.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.ordered.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s45.ordered::@s46.inventory_leaf::@s52.output_value, is_ansi = true, is_net = false, node_id = 56 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 57 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 59 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.ordered.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s45.ordered::@s46.inventory_leaf::@s53.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.ordered.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s45.ordered::@s46.inventory_leaf::@s54.net_value, is_ansi = true, is_net = true, node_id = 60 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 61 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 63 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.ordered", name = "inventory_leaf", node_id = 64 : i64, sym_name = "s46.inventory_leaf"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.ordered.defaulted", name = "defaulted", node_id = 65 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s47.defaulted"} {
              obelisk.sv.expression.conversion attributes {node_id = 66 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 67 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.ordered.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 68 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s48.defaulted"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.ordered.input_value", name = "input_value", node_id = 69 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s49.input_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.ordered.input_value", lifetime = 1 : i32, name = "input_value", node_id = 70 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s50.input_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.ordered.output_value", name = "output_value", node_id = 71 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s51.output_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.ordered.output_value", lifetime = 1 : i32, name = "output_value", node_id = 72 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s52.output_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.ordered.net_value", name = "net_value", node_id = 73 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s53.net_value"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.ordered.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 74 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s54.net_value"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.named", is_uninstantiated = false, name = "named", node_id = 75 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s55.named"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.named.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s55.named::@s56.inventory_leaf::@s57.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.named.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s55.named::@s56.inventory_leaf::@s58.defaulted, is_ansi = true, is_net = false, node_id = 76 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 77 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.named.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s55.named::@s56.inventory_leaf::@s59.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.named.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s55.named::@s56.inventory_leaf::@s60.input_value, is_ansi = true, is_net = false, node_id = 78 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.named.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s55.named::@s56.inventory_leaf::@s61.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.named.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s55.named::@s56.inventory_leaf::@s62.output_value, is_ansi = true, is_net = false, node_id = 80 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 81 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 83 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.named.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s55.named::@s56.inventory_leaf::@s63.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.named.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s55.named::@s56.inventory_leaf::@s64.net_value, is_ansi = true, is_net = true, node_id = 84 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 85 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 86 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 87 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.named", name = "inventory_leaf", node_id = 88 : i64, sym_name = "s56.inventory_leaf"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.named.defaulted", name = "defaulted", node_id = 89 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s57.defaulted"} {
              obelisk.sv.expression.conversion attributes {node_id = 90 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 91 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.named.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 92 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s58.defaulted"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.named.input_value", name = "input_value", node_id = 93 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s59.input_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.named.input_value", lifetime = 1 : i32, name = "input_value", node_id = 94 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s60.input_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.named.output_value", name = "output_value", node_id = 95 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s61.output_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.named.output_value", lifetime = 1 : i32, name = "output_value", node_id = 96 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s62.output_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.named.net_value", name = "net_value", node_id = 97 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s63.net_value"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.named.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 98 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s64.net_value"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.implicit", is_uninstantiated = false, name = "implicit", node_id = 99 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s65.implicit"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.implicit.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s65.implicit::@s66.inventory_leaf::@s67.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.implicit.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s65.implicit::@s66.inventory_leaf::@s68.defaulted, is_ansi = true, is_net = false, node_id = 100 : i64, provenance = 2 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 101 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.implicit.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s65.implicit::@s66.inventory_leaf::@s69.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.implicit.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s65.implicit::@s66.inventory_leaf::@s70.input_value, is_ansi = true, is_net = false, node_id = 102 : i64, provenance = 2 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 103 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.implicit.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s65.implicit::@s66.inventory_leaf::@s71.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.implicit.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s65.implicit::@s66.inventory_leaf::@s72.output_value, is_ansi = true, is_net = false, node_id = 104 : i64, provenance = 2 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 105 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 106 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 107 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.implicit.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s65.implicit::@s66.inventory_leaf::@s73.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.implicit.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s65.implicit::@s66.inventory_leaf::@s74.net_value, is_ansi = true, is_net = true, node_id = 108 : i64, provenance = 2 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 109 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 110 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 111 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.implicit", name = "inventory_leaf", node_id = 112 : i64, sym_name = "s66.inventory_leaf"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.implicit.defaulted", name = "defaulted", node_id = 113 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s67.defaulted"} {
              obelisk.sv.expression.conversion attributes {node_id = 114 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 115 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.implicit.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 116 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s68.defaulted"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.implicit.input_value", name = "input_value", node_id = 117 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s69.input_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.implicit.input_value", lifetime = 1 : i32, name = "input_value", node_id = 118 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s70.input_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.implicit.output_value", name = "output_value", node_id = 119 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s71.output_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.implicit.output_value", lifetime = 1 : i32, name = "output_value", node_id = 120 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s72.output_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.implicit.net_value", name = "net_value", node_id = 121 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s73.net_value"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.implicit.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 122 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s74.net_value"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.wildcard_instance", is_uninstantiated = false, name = "wildcard_instance", node_id = 123 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s75.wildcard_instance"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.wildcard_instance.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s75.wildcard_instance::@s76.inventory_leaf::@s77.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.wildcard_instance.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s75.wildcard_instance::@s76.inventory_leaf::@s78.defaulted, is_ansi = true, is_net = false, node_id = 124 : i64, provenance = 3 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 125 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.wildcard_instance.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s75.wildcard_instance::@s76.inventory_leaf::@s79.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.wildcard_instance.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s75.wildcard_instance::@s76.inventory_leaf::@s80.input_value, is_ansi = true, is_net = false, node_id = 126 : i64, provenance = 3 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 127 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.wildcard_instance.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s75.wildcard_instance::@s76.inventory_leaf::@s81.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.wildcard_instance.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s75.wildcard_instance::@s76.inventory_leaf::@s82.output_value, is_ansi = true, is_net = false, node_id = 128 : i64, provenance = 3 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 129 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 130 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 131 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.wildcard_instance.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s75.wildcard_instance::@s76.inventory_leaf::@s83.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.wildcard_instance.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s75.wildcard_instance::@s76.inventory_leaf::@s84.net_value, is_ansi = true, is_net = true, node_id = 132 : i64, provenance = 3 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 133 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 134 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 135 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.wildcard_instance", name = "inventory_leaf", node_id = 136 : i64, sym_name = "s76.inventory_leaf"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.wildcard_instance.defaulted", name = "defaulted", node_id = 137 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s77.defaulted"} {
              obelisk.sv.expression.conversion attributes {node_id = 138 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 139 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.wildcard_instance.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 140 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s78.defaulted"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.wildcard_instance.input_value", name = "input_value", node_id = 141 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s79.input_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.wildcard_instance.input_value", lifetime = 1 : i32, name = "input_value", node_id = 142 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s80.input_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.wildcard_instance.output_value", name = "output_value", node_id = 143 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s81.output_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.wildcard_instance.output_value", lifetime = 1 : i32, name = "output_value", node_id = 144 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s82.output_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.wildcard_instance.net_value", name = "net_value", node_id = 145 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s83.net_value"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.wildcard_instance.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 146 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s84.net_value"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.opened", is_uninstantiated = false, name = "opened", node_id = 147 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s85.opened"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.opened.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s85.opened::@s86.inventory_leaf::@s87.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.opened.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s85.opened::@s86.inventory_leaf::@s88.defaulted, is_ansi = true, is_net = false, node_id = 148 : i64, provenance = 5 : i32} {
          } {
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.opened.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s85.opened::@s86.inventory_leaf::@s89.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.opened.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s85.opened::@s86.inventory_leaf::@s90.input_value, is_ansi = true, is_net = false, node_id = 149 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 150 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.opened.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s85.opened::@s86.inventory_leaf::@s91.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.opened.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s85.opened::@s86.inventory_leaf::@s92.output_value, is_ansi = true, is_net = false, node_id = 151 : i64, provenance = 5 : i32} {
          } {
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.opened.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s85.opened::@s86.inventory_leaf::@s93.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.opened.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s85.opened::@s86.inventory_leaf::@s94.net_value, is_ansi = true, is_net = true, node_id = 152 : i64, provenance = 5 : i32} {
          } {
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.opened", name = "inventory_leaf", node_id = 153 : i64, sym_name = "s86.inventory_leaf"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.opened.defaulted", name = "defaulted", node_id = 154 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s87.defaulted"} {
              obelisk.sv.expression.conversion attributes {node_id = 155 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 156 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.opened.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 157 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s88.defaulted"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.opened.input_value", name = "input_value", node_id = 158 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s89.input_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.opened.input_value", lifetime = 1 : i32, name = "input_value", node_id = 159 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s90.input_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.opened.output_value", name = "output_value", node_id = 160 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s91.output_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.opened.output_value", lifetime = 1 : i32, name = "output_value", node_id = 161 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s92.output_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.opened.net_value", name = "net_value", node_id = 162 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s93.net_value"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.opened.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 163 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s94.net_value"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.omitted", is_uninstantiated = false, name = "omitted", node_id = 164 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s95.omitted"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.omitted.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s95.omitted::@s96.inventory_leaf::@s97.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.omitted.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s95.omitted::@s96.inventory_leaf::@s98.defaulted, is_ansi = true, is_net = false, node_id = 165 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 166 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.omitted.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s95.omitted::@s96.inventory_leaf::@s99.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.omitted.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s95.omitted::@s96.inventory_leaf::@s100.input_value, is_ansi = true, is_net = false, node_id = 167 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 168 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.omitted.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s95.omitted::@s96.inventory_leaf::@s101.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.omitted.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s95.omitted::@s96.inventory_leaf::@s102.output_value, is_ansi = true, is_net = false, node_id = 169 : i64, provenance = 4 : i32} {
          } {
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.omitted.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s95.omitted::@s96.inventory_leaf::@s103.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.omitted.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s95.omitted::@s96.inventory_leaf::@s104.net_value, is_ansi = true, is_net = true, node_id = 170 : i64, provenance = 4 : i32} {
          } {
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.omitted", name = "inventory_leaf", node_id = 171 : i64, sym_name = "s96.inventory_leaf"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.omitted.defaulted", name = "defaulted", node_id = 172 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s97.defaulted"} {
              obelisk.sv.expression.conversion attributes {node_id = 173 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 174 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.omitted.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 175 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s98.defaulted"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.omitted.input_value", name = "input_value", node_id = 176 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s99.input_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.omitted.input_value", lifetime = 1 : i32, name = "input_value", node_id = 177 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s100.input_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.omitted.output_value", name = "output_value", node_id = 178 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s101.output_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.omitted.output_value", lifetime = 1 : i32, name = "output_value", node_id = 179 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s102.output_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.omitted.net_value", name = "net_value", node_id = 180 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s103.net_value"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.omitted.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 181 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s104.net_value"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.uses_default", is_uninstantiated = false, name = "uses_default", node_id = 182 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s105.uses_default"} {
          obelisk.sv.port.connection attributes {actual_is_constant = true, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.uses_default.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s105.uses_default::@s106.inventory_leaf::@s107.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.uses_default.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s105.uses_default::@s106.inventory_leaf::@s108.defaulted, is_ansi = true, is_net = false, node_id = 183 : i64, provenance = 6 : i32} {
          } {
            obelisk.sv.expression.conversion attributes {node_id = 184 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 185 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.uses_default.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s105.uses_default::@s106.inventory_leaf::@s109.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.uses_default.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s105.uses_default::@s106.inventory_leaf::@s110.input_value, is_ansi = true, is_net = false, node_id = 186 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 187 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.uses_default.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s105.uses_default::@s106.inventory_leaf::@s111.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.uses_default.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s105.uses_default::@s106.inventory_leaf::@s112.output_value, is_ansi = true, is_net = false, node_id = 188 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 189 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 190 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 191 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.uses_default.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s105.uses_default::@s106.inventory_leaf::@s113.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.uses_default.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s105.uses_default::@s106.inventory_leaf::@s114.net_value, is_ansi = true, is_net = true, node_id = 192 : i64, provenance = 1 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 193 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 194 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 195 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.uses_default", name = "inventory_leaf", node_id = 196 : i64, sym_name = "s106.inventory_leaf"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.uses_default.defaulted", name = "defaulted", node_id = 197 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s107.defaulted"} {
              obelisk.sv.expression.conversion attributes {node_id = 198 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 199 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.uses_default.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 200 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s108.defaulted"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.uses_default.input_value", name = "input_value", node_id = 201 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s109.input_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.uses_default.input_value", lifetime = 1 : i32, name = "input_value", node_id = 202 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s110.input_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.uses_default.output_value", name = "output_value", node_id = 203 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s111.output_value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.uses_default.output_value", lifetime = 1 : i32, name = "output_value", node_id = 204 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s112.output_value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.uses_default.net_value", name = "net_value", node_id = 205 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s113.net_value"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.uses_default.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 206 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s114.net_value"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.nonansi", is_uninstantiated = false, name = "nonansi", node_id = 207 : i64, referenced_path = "inventory_nonansi", referenced_symbol = @s6.inventory_nonansi, sym_name = "s115.nonansi"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "a", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.nonansi.a", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s115.nonansi::@s116.inventory_nonansi::@s117.a, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.nonansi.a", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s115.nonansi::@s116.inventory_nonansi::@s121.a, is_ansi = false, is_net = true, node_id = 208 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 209 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "b", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.nonansi.b", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s115.nonansi::@s116.inventory_nonansi::@s118.b, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.nonansi.b", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s115.nonansi::@s116.inventory_nonansi::@s120.b, is_ansi = false, is_net = true, node_id = 210 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 211 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "c", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.nonansi.c", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s115.nonansi::@s116.inventory_nonansi::@s119.c, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.nonansi.c", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s115.nonansi::@s116.inventory_nonansi::@s122.c, is_ansi = false, is_net = true, node_id = 212 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 213 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 214 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 215 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.nonansi", name = "inventory_nonansi", node_id = 216 : i64, sym_name = "s116.inventory_nonansi"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.nonansi.a", name = "a", node_id = 217 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s117.a"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.nonansi.b", name = "b", node_id = 218 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s118.b"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.nonansi.c", name = "c", node_id = 219 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s119.c"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.nonansi.b", is_implicit = false, name = "b", net_kind = 1 : i32, node_id = 220 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s120.b"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.nonansi.a", is_implicit = false, name = "a", net_kind = 1 : i32, node_id = 221 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s121.a"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.nonansi.c", is_implicit = false, name = "c", net_kind = 1 : i32, node_id = 222 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s122.c"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.multiport", is_uninstantiated = false, name = "multiport", node_id = 223 : i64, referenced_path = "inventory_multiport", referenced_symbol = @s5.inventory_multiport, sym_name = "s123.multiport"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "right", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.multiport.right", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s123.multiport::@s124.inventory_multiport::@s180.right, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.multiport.right", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s123.multiport::@s124.inventory_multiport::@s126.right, is_ansi = false, is_net = true, node_id = 224 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 225 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "left", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.multiport.left", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s123.multiport::@s124.inventory_multiport::@s179.left, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.multiport.left", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s123.multiport::@s124.inventory_multiport::@s127.left, is_ansi = false, is_net = true, node_id = 226 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 227 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.multiport", name = "inventory_multiport", node_id = 228 : i64, sym_name = "s124.inventory_multiport"} {
            obelisk.sv.symbol.multi_port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.multiport.pair", name = "pair", node_id = 229 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s125.pair"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.multiport.right", is_implicit = false, name = "right", net_kind = 1 : i32, node_id = 230 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s126.right"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.multiport.left", is_implicit = false, name = "left", net_kind = 1 : i32, node_id = 231 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s127.left"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.multiport.left", name = "left", node_id = 341 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s179.left"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.multiport.right", name = "right", node_id = 342 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s180.right"} {
            }
          }
        }
        obelisk.sv.symbol.instance_array attributes {hierarchical_name = "port_connections_inventory.arrayed", name = "arrayed", node_id = 232 : i64, sym_name = "s128.arrayed"} {
          obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.arrayed[0]", is_uninstantiated = false, node_id = 233 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s129"} {
            obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.arrayed[0].defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s129::@s130.inventory_leaf::@s131.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.arrayed[0].defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s129::@s130.inventory_leaf::@s132.defaulted, is_ansi = true, is_net = false, node_id = 234 : i64, provenance = 0 : i32} {
            } {
              obelisk.sv.expression.named_value attributes {node_id = 235 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.arrayed[0].input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s129::@s130.inventory_leaf::@s133.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.arrayed[0].input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s129::@s130.inventory_leaf::@s134.input_value, is_ansi = true, is_net = false, node_id = 236 : i64, provenance = 0 : i32} {
            } {
              obelisk.sv.expression.named_value attributes {node_id = 237 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.arrayed[0].output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s129::@s130.inventory_leaf::@s135.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.arrayed[0].output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s129::@s130.inventory_leaf::@s136.output_value, is_ansi = true, is_net = false, node_id = 238 : i64, provenance = 0 : i32} {
            } {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 239 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 240 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.empty_argument attributes {node_id = 241 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.arrayed[0].net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s129::@s130.inventory_leaf::@s137.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.arrayed[0].net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s129::@s130.inventory_leaf::@s138.net_value, is_ansi = true, is_net = true, node_id = 242 : i64, provenance = 0 : i32} {
            } {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 243 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 244 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.empty_argument attributes {node_id = 245 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.arrayed[0]", name = "inventory_leaf", node_id = 246 : i64, sym_name = "s130.inventory_leaf"} {
              obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.arrayed[0].defaulted", name = "defaulted", node_id = 247 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s131.defaulted"} {
                obelisk.sv.expression.conversion attributes {node_id = 248 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 249 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.arrayed[0].defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 250 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s132.defaulted"} {
              }
              obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.arrayed[0].input_value", name = "input_value", node_id = 251 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s133.input_value"} {
              }
              obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.arrayed[0].input_value", lifetime = 1 : i32, name = "input_value", node_id = 252 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s134.input_value"} {
              }
              obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.arrayed[0].output_value", name = "output_value", node_id = 253 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s135.output_value"} {
              }
              obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.arrayed[0].output_value", lifetime = 1 : i32, name = "output_value", node_id = 254 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s136.output_value"} {
              }
              obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.arrayed[0].net_value", name = "net_value", node_id = 255 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s137.net_value"} {
              }
              obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.arrayed[0].net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 256 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s138.net_value"} {
              }
            }
          }
          obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.arrayed[1]", is_uninstantiated = false, node_id = 257 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s139"} {
            obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.arrayed[1].defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s139::@s140.inventory_leaf::@s141.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.arrayed[1].defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s139::@s140.inventory_leaf::@s142.defaulted, is_ansi = true, is_net = false, node_id = 258 : i64, provenance = 0 : i32} {
            } {
              obelisk.sv.expression.named_value attributes {node_id = 259 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.arrayed[1].input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s139::@s140.inventory_leaf::@s143.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.arrayed[1].input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s139::@s140.inventory_leaf::@s144.input_value, is_ansi = true, is_net = false, node_id = 260 : i64, provenance = 0 : i32} {
            } {
              obelisk.sv.expression.named_value attributes {node_id = 261 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.arrayed[1].output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s139::@s140.inventory_leaf::@s145.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.arrayed[1].output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s139::@s140.inventory_leaf::@s146.output_value, is_ansi = true, is_net = false, node_id = 262 : i64, provenance = 0 : i32} {
            } {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 263 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 264 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.empty_argument attributes {node_id = 265 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.arrayed[1].net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s139::@s140.inventory_leaf::@s147.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.arrayed[1].net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s128.arrayed::@s139::@s140.inventory_leaf::@s148.net_value, is_ansi = true, is_net = true, node_id = 266 : i64, provenance = 0 : i32} {
            } {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 267 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 268 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.empty_argument attributes {node_id = 269 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.arrayed[1]", name = "inventory_leaf", node_id = 270 : i64, sym_name = "s140.inventory_leaf"} {
              obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.arrayed[1].defaulted", name = "defaulted", node_id = 271 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s141.defaulted"} {
                obelisk.sv.expression.conversion attributes {node_id = 272 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 273 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.arrayed[1].defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 274 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s142.defaulted"} {
              }
              obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.arrayed[1].input_value", name = "input_value", node_id = 275 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s143.input_value"} {
              }
              obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.arrayed[1].input_value", lifetime = 1 : i32, name = "input_value", node_id = 276 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s144.input_value"} {
              }
              obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.arrayed[1].output_value", name = "output_value", node_id = 277 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s145.output_value"} {
              }
              obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.arrayed[1].output_value", lifetime = 1 : i32, name = "output_value", node_id = 278 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s146.output_value"} {
              }
              obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.arrayed[1].net_value", name = "net_value", node_id = 279 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s147.net_value"} {
              }
              obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.arrayed[1].net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 280 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s148.net_value"} {
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.program_instance", is_uninstantiated = false, name = "program_instance", node_id = 281 : i64, referenced_path = "inventory_program", referenced_symbol = @s7.inventory_program, sym_name = "s149.program_instance"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "value", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.program_instance.value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s149.program_instance::@s150.inventory_program::@s151.value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.program_instance.value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s149.program_instance::@s150.inventory_program::@s152.value, is_ansi = true, is_net = false, node_id = 282 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 283 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.program_instance", name = "inventory_program", node_id = 284 : i64, sym_name = "s150.inventory_program"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.program_instance.value", name = "value", node_id = 285 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s151.value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.program_instance.value", lifetime = 1 : i32, name = "value", node_id = 286 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s152.value"} {
            }
          }
        }
        obelisk.sv.symbol.generate_block_array attributes {hierarchical_name = "port_connections_inventory.generated", name = "generated", node_id = 287 : i64, sym_name = "s153.generated"} {
          obelisk.sv.symbol.genvar attributes {hierarchical_name = "port_connections_inventory.generated.i", name = "i", node_id = 288 : i64, sym_name = "s154.i"} {
          }
          obelisk.sv.symbol.generate_block attributes {hierarchical_name = "port_connections_inventory.generated[0]", node_id = 289 : i64, sym_name = "s155"} {
            obelisk.sv.symbol.parameter attributes {constant_value = "0", hierarchical_name = "port_connections_inventory.generated[0].i", name = "i", node_id = 290 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s156.i"} {
            }
            obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.generated[0].element", is_uninstantiated = false, name = "element", node_id = 291 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s157.element"} {
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.generated[0].element.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s155::@s157.element::@s158.inventory_leaf::@s159.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.generated[0].element.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s155::@s157.element::@s158.inventory_leaf::@s160.defaulted, is_ansi = true, is_net = false, node_id = 292 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.named_value attributes {node_id = 293 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.generated[0].element.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s155::@s157.element::@s158.inventory_leaf::@s161.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.generated[0].element.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s155::@s157.element::@s158.inventory_leaf::@s162.input_value, is_ansi = true, is_net = false, node_id = 294 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.named_value attributes {node_id = 295 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.generated[0].element.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s155::@s157.element::@s158.inventory_leaf::@s163.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.generated[0].element.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s155::@s157.element::@s158.inventory_leaf::@s164.output_value, is_ansi = true, is_net = false, node_id = 296 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 297 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 298 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {node_id = 299 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.generated[0].element.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s155::@s157.element::@s158.inventory_leaf::@s165.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.generated[0].element.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s155::@s157.element::@s158.inventory_leaf::@s166.net_value, is_ansi = true, is_net = true, node_id = 300 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 301 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 302 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {node_id = 303 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.generated[0].element", name = "inventory_leaf", node_id = 304 : i64, sym_name = "s158.inventory_leaf"} {
                obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.generated[0].element.defaulted", name = "defaulted", node_id = 305 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s159.defaulted"} {
                  obelisk.sv.expression.conversion attributes {node_id = 306 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 307 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.generated[0].element.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 308 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s160.defaulted"} {
                }
                obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.generated[0].element.input_value", name = "input_value", node_id = 309 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s161.input_value"} {
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.generated[0].element.input_value", lifetime = 1 : i32, name = "input_value", node_id = 310 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s162.input_value"} {
                }
                obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.generated[0].element.output_value", name = "output_value", node_id = 311 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s163.output_value"} {
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.generated[0].element.output_value", lifetime = 1 : i32, name = "output_value", node_id = 312 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s164.output_value"} {
                }
                obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.generated[0].element.net_value", name = "net_value", node_id = 313 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s165.net_value"} {
                }
                obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.generated[0].element.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 314 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s166.net_value"} {
                }
              }
            }
          }
          obelisk.sv.symbol.generate_block attributes {hierarchical_name = "port_connections_inventory.generated[1]", node_id = 315 : i64, sym_name = "s167"} {
            obelisk.sv.symbol.parameter attributes {constant_value = "1", hierarchical_name = "port_connections_inventory.generated[1].i", name = "i", node_id = 316 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s168.i"} {
            }
            obelisk.sv.symbol.instance attributes {hierarchical_name = "port_connections_inventory.generated[1].element", is_uninstantiated = false, name = "element", node_id = 317 : i64, referenced_path = "inventory_leaf", referenced_symbol = @s4.inventory_leaf, sym_name = "s169.element"} {
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "defaulted", formal_ordinal = 0 : i64, formal_path = "port_connections_inventory.generated[1].element.defaulted", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s167::@s169.element::@s170.inventory_leaf::@s171.defaulted, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.generated[1].element.defaulted", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s167::@s169.element::@s170.inventory_leaf::@s172.defaulted, is_ansi = true, is_net = false, node_id = 318 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.named_value attributes {node_id = 319 : i64, referenced_path = "port_connections_inventory.defaulted", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s41.defaulted, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "input_value", formal_ordinal = 1 : i64, formal_path = "port_connections_inventory.generated[1].element.input_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s167::@s169.element::@s170.inventory_leaf::@s173.input_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.generated[1].element.input_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s167::@s169.element::@s170.inventory_leaf::@s174.input_value, is_ansi = true, is_net = false, node_id = 320 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.named_value attributes {node_id = 321 : i64, referenced_path = "port_connections_inventory.input_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s42.input_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "output_value", formal_ordinal = 2 : i64, formal_path = "port_connections_inventory.generated[1].element.output_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s167::@s169.element::@s170.inventory_leaf::@s175.output_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.generated[1].element.output_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s167::@s169.element::@s170.inventory_leaf::@s176.output_value, is_ansi = true, is_net = false, node_id = 322 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 323 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 324 : i64, referenced_path = "port_connections_inventory.output_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s43.output_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {node_id = 325 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "net_value", formal_ordinal = 3 : i64, formal_path = "port_connections_inventory.generated[1].element.net_value", formal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s167::@s169.element::@s170.inventory_leaf::@s177.net_value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "port_connections_inventory.generated[1].element.net_value", internal_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s153.generated::@s167::@s169.element::@s170.inventory_leaf::@s178.net_value, is_ansi = true, is_net = true, node_id = 326 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 327 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 328 : i64, referenced_path = "port_connections_inventory.net_value", referenced_symbol = @s10.$root::@s39.port_connections_inventory::@s40.port_connections_inventory::@s44.net_value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {node_id = 329 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_connections_inventory.generated[1].element", name = "inventory_leaf", node_id = 330 : i64, sym_name = "s170.inventory_leaf"} {
                obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.generated[1].element.defaulted", name = "defaulted", node_id = 331 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s171.defaulted"} {
                  obelisk.sv.expression.conversion attributes {node_id = 332 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 333 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.generated[1].element.defaulted", lifetime = 1 : i32, name = "defaulted", node_id = 334 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s172.defaulted"} {
                }
                obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_connections_inventory.generated[1].element.input_value", name = "input_value", node_id = 335 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s173.input_value"} {
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.generated[1].element.input_value", lifetime = 1 : i32, name = "input_value", node_id = 336 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s174.input_value"} {
                }
                obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_connections_inventory.generated[1].element.output_value", name = "output_value", node_id = 337 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s175.output_value"} {
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "port_connections_inventory.generated[1].element.output_value", lifetime = 1 : i32, name = "output_value", node_id = 338 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s176.output_value"} {
                }
                obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "port_connections_inventory.generated[1].element.net_value", name = "net_value", node_id = 339 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s177.net_value"} {
                }
                obelisk.sv.symbol.net attributes {hierarchical_name = "port_connections_inventory.generated[1].element.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 340 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s178.net_value"} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// A complete elaborated design serializes through the unified runtime
// artifact version 1.
// CHECK: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0, 1, 0, 0, 0, 0, 0, 0, 0
// CHECK: obelisk.execution.state_bits = {{[1-9][0-9]*}} : i64

// Port connection lowering owns the semantic inventory independently of the
// driver: static net topology remains declarative, empty defaulted inputs get
// initialization code units, and generated/arrayed instances retain distinct
// hidden connection identities.
// LOWER-DAG: obelisk_sim.net.connect.decl {{[0-9]+}} in {{[0-9]+}} 0[0] to 1[0] width 1 reversed = false provenance "ordered"
// LOWER-DAG: obelisk_sim.net.connect.decl {{[0-9]+}} in {{[0-9]+}} 0[0] to 2[0] width 1 reversed = false provenance "named"
// LOWER-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_initialize hierarchy "port_connections_inventory.uses_default.$port_connection_0"{{.*}}{internal}
// LOWER-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_output hierarchy "port_connections_inventory.arrayed[0].$port_connection_2"{{.*}}{internal}
// LOWER-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_output hierarchy "port_connections_inventory.arrayed[1].$port_connection_2"{{.*}}{internal}
// LOWER-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_input hierarchy "port_connections_inventory.generated[0].element.$port_connection_0"{{.*}}{internal}
// LOWER-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_input hierarchy "port_connections_inventory.generated[1].element.$port_connection_0"{{.*}}{internal}
