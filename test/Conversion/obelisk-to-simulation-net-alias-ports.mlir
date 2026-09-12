// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --implicit-check-not='hierarchy "top.y"' \
// RUN:     --implicit-check-not='hierarchy "top.c.c"'
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// IEEE 1800-2017 10.10 aliases are transitive topology. Two whole ref-port
// aliases that target distinct flattened objects must merge those targets as
// well. The closing c=a repetition must not form a directed alias cycle.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "leaf", name = "leaf", node_id = 27 : i64, sym_name = "leaf_def"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "child", name = "child", node_id = 0 : i64, sym_name = "child_def"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "top_def"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top_def, sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "top_body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.x", lifetime = 1 : i32, name = "x", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.y", lifetime = 1 : i32, name = "y", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "y"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.c", is_uninstantiated = false, name = "c", node_id = 7 : i64, referenced_path = "child", referenced_symbol = @child_def, sym_name = "child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 3 : i32, formal_name = "a", formal_ordinal = 0 : i64, formal_path = "top.c.a", formal_symbol = @root::@top::@top_body::@child::@child_body::@a_port, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "top.c.a", internal_symbol = @root::@top::@top_body::@child::@child_body::@a, is_ansi = true, is_net = false, node_id = 8 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "top.x", referenced_symbol = @root::@top::@top_body::@x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 3 : i32, formal_name = "b", formal_ordinal = 1 : i64, formal_path = "top.c.b", formal_symbol = @root::@top::@top_body::@child::@child_body::@b_port, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "top.c.b", internal_symbol = @root::@top::@top_body::@child::@child_body::@b, is_ansi = true, is_net = false, node_id = 10 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.y", referenced_symbol = @root::@top::@top_body::@y, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.c", name = "child", node_id = 12 : i64, sym_name = "child_body"} {
            obelisk.sv.symbol.port attributes {direction = 3 : i32, hierarchical_name = "top.c.a", name = "a", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "a_port"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c.a", lifetime = 1 : i32, name = "a", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "a"} {
            }
            obelisk.sv.symbol.port attributes {direction = 3 : i32, hierarchical_name = "top.c.b", name = "b", node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "b_port"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c.b", lifetime = 1 : i32, name = "b", node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "b"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c.c", lifetime = 1 : i32, name = "c", node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "c"} {
            }
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.c.g", is_uninstantiated = false, name = "g", node_id = 28 : i64, referenced_path = "leaf", referenced_symbol = @leaf_def, sym_name = "g"} {
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 3 : i32, formal_name = "x", formal_ordinal = 0 : i64, formal_path = "top.c.g.x", formal_symbol = @root::@top::@top_body::@child::@child_body::@g::@leaf_body::@x_port, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "top.c.g.x", internal_symbol = @root::@top::@top_body::@child::@child_body::@g::@leaf_body::@x, is_ansi = true, is_net = false, node_id = 29 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "top.c.a", referenced_symbol = @root::@top::@top_body::@child::@child_body::@a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.c.g", name = "leaf", node_id = 31 : i64, sym_name = "leaf_body"} {
                obelisk.sv.symbol.port attributes {direction = 3 : i32, hierarchical_name = "top.c.g.x", name = "x", node_id = 32 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "x_port"} {
                }
                obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c.g.x", lifetime = 1 : i32, name = "x", node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "x"} {
                }
              }
            }
            obelisk.sv.symbol.net_alias attributes {hierarchical_name = "top.c", node_id = 18 : i64, sym_name = "alias_ab"} {
              obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.c.a", referenced_symbol = @root::@top::@top_body::@child::@child_body::@a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "top.c.b", referenced_symbol = @root::@top::@top_body::@child::@child_body::@b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.symbol.net_alias attributes {hierarchical_name = "top.c", node_id = 21 : i64, sym_name = "alias_bc"} {
              obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.c.b", referenced_symbol = @root::@top::@top_body::@child::@child_body::@b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.c.c", referenced_symbol = @root::@top::@top_body::@child::@child_body::@c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.symbol.net_alias attributes {hierarchical_name = "top.c", node_id = 24 : i64, sym_name = "alias_ca"} {
              obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.c.c", referenced_symbol = @root::@top::@top_body::@child::@child_body::@c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "top.c.a", referenced_symbol = @root::@top::@top_body::@child::@child_body::@a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: obelisk_sim.vpi_definition_member.decl @[[REF_A:[^ ]+]] {{.*}} name "a" direction ref
// CHECK-DAG: obelisk_sim.vpi_definition_member.decl @[[REF_B:[^ ]+]] {{.*}} name "b" direction ref
// CHECK-DAG: obelisk_sim.vpi_definition_member.decl @[[REF_X:[^ ]+]] {{.*}} name "x" direction ref
// CHECK-DAG: obelisk_sim.vpi_definition_member.bind scope {{[0-9]+}} member @[[REF_A]] expr <kind = storage, id = 0 : i64>
// CHECK-DAG: obelisk_sim.vpi_definition_member.bind scope {{[0-9]+}} member @[[REF_B]] expr <kind = storage, id = 0 : i64>
// CHECK-DAG: obelisk_sim.storage.decl 0 {{.*}} hierarchy "top.x"
// CHECK-DAG: obelisk_sim.port.decl 0 {{.*}} source 0 net = false
// CHECK-DAG: obelisk_sim.port.decl 1 {{.*}} source 0 net = false
// CHECK-DAG: obelisk_sim.port.decl 2 {{.*}} source 0 net = false
// CHECK-DAG: obelisk_sim.vpi_definition_member.relation scope {{[0-9]+}} member @[[REF_A]] selector 44 iterate ordinal 0 to <kind = port, id = 0 : i64>
// CHECK-DAG: obelisk_sim.vpi_definition_member.relation scope {{[0-9]+}} member @[[REF_B]] selector 44 iterate ordinal 0 to <kind = port, id = 1 : i64>
// CHECK-DAG: obelisk_sim.vpi_definition_member.relation scope {{[0-9]+}} member @[[REF_A]] selector 98 iterate ordinal 0 to <kind = port, id = 2 : i64>
// CHECK-DAG: obelisk_sim.vpi_definition_member.relation scope {{[0-9]+}} member @[[REF_X]] selector 44 iterate ordinal 0 to <kind = port, id = 2 : i64>
