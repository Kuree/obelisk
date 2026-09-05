// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=COUNT
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=read' \
// RUN:   | %python %S/Inputs/dump-design-database.py \
// RUN:   | FileCheck %s --check-prefix=IMAGE

// Exercise recursive source geometry independently of executable array values.
// The dimensions deliberately alternate direction and the singleton innermost
// dimension makes this a rank-three array without making the fixture large.
// Intermediate semantic array nodes must not become VPI anchors: each terminal
// instance body is a direct member of the one aggregate array anchor.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64,
      sym_name = "leaf_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 1 : i64,
      sym_name = "top_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32,
      hierarchical_name = "iface", name = "iface", node_id = 27 : i64,
      sym_name = "iface_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32,
      hierarchical_name = "program", name = "program", node_id = 28 : i64,
      sym_name = "program_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "cu"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 4 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 5 : i64, sym_name = "top_b"} {
        obelisk.sv.symbol.instance_array attributes {
            array_range = array<i64: 2, 1>, hierarchical_name = "top.cube",
            name = "cube", node_id = 6 : i64, sym_name = "cube"} {
          obelisk.sv.symbol.instance_array attributes {
              array_range = array<i64: -1, 0>,
              hierarchical_name = "top.cube", node_id = 7 : i64,
              sym_name = "outer_0"} {
            obelisk.sv.symbol.instance_array attributes {
                array_range = array<i64: 7, 7>,
                hierarchical_name = "top.cube", node_id = 8 : i64,
                sym_name = "middle_0_0"} {
              obelisk.sv.symbol.instance attributes {
                  hierarchical_name = "top.cube[1][-1][7]", is_uninstantiated = false,
                  node_id = 9 : i64, referenced_path = "leaf",
                  referenced_symbol = @leaf_def, sym_name = "leaf_0_0"} {
                obelisk.sv.symbol.instance_body attributes {
                    hierarchical_name = "top.cube[1][-1][7]", name = "leaf",
                    node_id = 10 : i64, sym_name = "leaf_body_0_0"} {
                  // This nested anchor is bounded by the terminal instance-body
                  // anchor and must not inherit the array member coordinates.
                  obelisk.sv.symbol.variable attributes {
                      hierarchical_name = "top.cube[1][-1][7].ready", lifetime = 1 : i32,
                      name = "ready", node_id = 11 : i64,
                      semantic_type = !obelisk.event, sym_name = "ready"} {}
                }
              }
            }
            obelisk.sv.symbol.instance_array attributes {
                array_range = array<i64: 7, 7>,
                hierarchical_name = "top.cube", node_id = 12 : i64,
                sym_name = "middle_0_1"} {
              obelisk.sv.symbol.instance attributes {
                  hierarchical_name = "top.cube[1][0][7]", is_uninstantiated = false,
                  node_id = 13 : i64, referenced_path = "leaf",
                  referenced_symbol = @leaf_def, sym_name = "leaf_0_1"} {
                obelisk.sv.symbol.instance_body attributes {
                    hierarchical_name = "top.cube[1][0][7]", name = "leaf",
                    node_id = 14 : i64, sym_name = "leaf_body_0_1"} {}
              }
            }
          }
          obelisk.sv.symbol.instance_array attributes {
              array_range = array<i64: -1, 0>,
              hierarchical_name = "top.cube", node_id = 15 : i64,
              sym_name = "outer_1"} {
            obelisk.sv.symbol.instance_array attributes {
                array_range = array<i64: 7, 7>,
                hierarchical_name = "top.cube", node_id = 16 : i64,
                sym_name = "middle_1_0"} {
              obelisk.sv.symbol.instance attributes {
                  hierarchical_name = "top.cube[2][-1][7]", is_uninstantiated = false,
                  node_id = 17 : i64, referenced_path = "leaf",
                  referenced_symbol = @leaf_def, sym_name = "leaf_1_0"} {
                obelisk.sv.symbol.instance_body attributes {
                    hierarchical_name = "top.cube[2][-1][7]", name = "leaf",
                    node_id = 18 : i64, sym_name = "leaf_body_1_0"} {}
              }
            }
            obelisk.sv.symbol.instance_array attributes {
                array_range = array<i64: 7, 7>,
                hierarchical_name = "top.cube", node_id = 19 : i64,
                sym_name = "middle_1_1"} {
              obelisk.sv.symbol.instance attributes {
                  hierarchical_name = "top.cube[2][0][7]", is_uninstantiated = false,
                  node_id = 20 : i64, referenced_path = "leaf",
                  referenced_symbol = @leaf_def, sym_name = "leaf_1_1"} {
                obelisk.sv.symbol.instance_body attributes {
                    hierarchical_name = "top.cube[2][0][7]", name = "leaf",
                    node_id = 21 : i64, sym_name = "leaf_body_1_1"} {}
              }
            }
          }
        }
        obelisk.sv.symbol.generate_block_array attributes {
            array_indices = array<i64: -3, 5>, hierarchical_name = "top.g",
            name = "g", node_id = 22 : i64, sym_name = "g"} {
          obelisk.sv.symbol.generate_block attributes {
              hierarchical_name = "top.g[-3]", node_id = 23 : i64,
              sym_name = "g_neg3"} {
            obelisk.sv.symbol.variable attributes {
                hierarchical_name = "top.g[-3].done", lifetime = 1 : i32,
                name = "done", node_id = 24 : i64,
                semantic_type = !obelisk.event, sym_name = "done"} {}
          }
          obelisk.sv.symbol.generate_block attributes {
              hierarchical_name = "top.g[5]", node_id = 25 : i64,
              sym_name = "g_5"} {}
        }
        obelisk.sv.symbol.variable attributes {
            hierarchical_name = "top.events", lifetime = 1 : i32,
            name = "events", node_id = 26 : i64,
            semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_unpacked_array<-1 : 0 x !obelisk.event>>,
            sym_name = "events"} {}
        obelisk.sv.symbol.instance_array attributes {
            array_range = array<i64: 2, 2>, hierarchical_name = "top.i",
            name = "i", node_id = 29 : i64, sym_name = "interfaces"} {
          obelisk.sv.symbol.instance attributes {
              hierarchical_name = "top.i[2]", is_uninstantiated = false,
              node_id = 30 : i64, referenced_path = "iface",
              referenced_symbol = @iface_def, sym_name = "interface_2"} {
            obelisk.sv.symbol.instance_body attributes {
                hierarchical_name = "top.i[2]", name = "iface",
                node_id = 31 : i64, sym_name = "interface_body_2"} {}
          }
        }
        obelisk.sv.symbol.instance_array attributes {
            array_range = array<i64: -2, -2>, hierarchical_name = "top.p",
            name = "p", node_id = 32 : i64, sym_name = "programs"} {
          obelisk.sv.symbol.instance attributes {
              hierarchical_name = "top.p[-2]", is_uninstantiated = false,
              node_id = 33 : i64, referenced_path = "program",
              referenced_symbol = @program_def, sym_name = "program_neg2"} {
            obelisk.sv.symbol.instance_body attributes {
                hierarchical_name = "top.p[-2]", name = "program",
                node_id = 34 : i64, sym_name = "program_body_neg2"} {}
          }
        }
      }
    }
  }
}

// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[TOP:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 32{{.*}} hierarchy "top" debug "top"
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[ARRAY:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 112{{.*}}parent @[[TOP]]{{.*}}hierarchy "top.cube" debug "cube" {index_ranges = array<i64: 2, 1, -1, 0, 7, 7>}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[FIRST:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 32{{.*}}parent @[[ARRAY]]{{.*}}hierarchy "top.cube[1][-1][7]"{{.*}}member_indices = array<i64: 1, -1, 7>
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[SECOND:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 32{{.*}}parent @[[ARRAY]]{{.*}}hierarchy "top.cube[1][0][7]"{{.*}}member_indices = array<i64: 1, 0, 7>
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[THIRD:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 32{{.*}}parent @[[ARRAY]]{{.*}}hierarchy "top.cube[2][-1][7]"{{.*}}member_indices = array<i64: 2, -1, 7>
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[FOURTH:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 32{{.*}}parent @[[ARRAY]]{{.*}}hierarchy "top.cube[2][0][7]"{{.*}}member_indices = array<i64: 2, 0, 7>
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[READY:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 34{{.*}}parent @[[FIRST]]{{.*}}hierarchy "top.cube[1][-1][7].ready" debug "ready"{{ *$}}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[GEN_ARRAY:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 133{{.*}}parent @[[TOP]]{{.*}}hierarchy "top.g" debug "g" {sparse_indices = array<i64: -3, 5>}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[GEN_NEG3:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 134{{.*}}parent @[[GEN_ARRAY]]{{.*}}hierarchy "top.g[-3]"{{.*}}member_indices = array<i64: -3>
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[GEN_5:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 134{{.*}}parent @[[GEN_ARRAY]]{{.*}}hierarchy "top.g[5]"{{.*}}member_indices = array<i64: 5>
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[DONE:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 34{{.*}}parent @[[GEN_NEG3]]{{.*}}hierarchy "top.g[-3].done" debug "done"{{ *$}}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[EVENTS:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 129{{.*}}parent @[[TOP]]{{.*}}hierarchy "top.events" debug "events" {index_ranges = array<i64: 1, 0, -1, 0>}
// CHECK-DAG: obelisk_sim.storage.decl {{.*}} hierarchy "top.events" debug "events" {{.*}}obelisk_sim.vpi_identity_delegated = @[[EVENTS]]
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}}type 34{{.*}}parent @[[EVENTS]]{{.*}}hierarchy "top.events[1][-1]"{{.*}}member_indices = array<i64: 1, -1>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}}type 34{{.*}}parent @[[EVENTS]]{{.*}}hierarchy "top.events[1][0]"{{.*}}member_indices = array<i64: 1, 0>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}}type 34{{.*}}parent @[[EVENTS]]{{.*}}hierarchy "top.events[0][-1]"{{.*}}member_indices = array<i64: 0, -1>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}}type 34{{.*}}parent @[[EVENTS]]{{.*}}hierarchy "top.events[0][0]"{{.*}}member_indices = array<i64: 0, 0>
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[INTERFACES:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 603{{.*}}parent @[[TOP]]{{.*}}hierarchy "top.i" debug "i" {index_ranges = array<i64: 2, 2>}
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}}type 601{{.*}}parent @[[INTERFACES]]{{.*}}hierarchy "top.i[2]"{{.*}}member_indices = array<i64: 2>
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[PROGRAMS:__obelisk_vpi_anchor_[0-9]+]] {{.*}}type 604{{.*}}parent @[[TOP]]{{.*}}hierarchy "top.p" debug "p" {index_ranges = array<i64: -2, -2>}
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}}type 602{{.*}}parent @[[PROGRAMS]]{{.*}}hierarchy "top.p[-2]"{{.*}}member_indices = array<i64: -2>
// CHECK-NOT: obelisk.sv.
// COUNT-COUNT-1: obelisk_sim.vpi_object.anchor {{.*}} type 112
// IMAGE-COUNT-1: object name=top.events kind=9 vpi_kind=129
// IMAGE-COUNT-4: object name=top.events[
// IMAGE: relation_index {{.*}} object_name=top.cube
// IMAGE: relation_index_dimension index={{[0-9]+}} range=[2:1]
// IMAGE-NEXT: relation_index_dimension index={{[0-9]+}} range=[-1:0]
// IMAGE-NEXT: relation_index_dimension index={{[0-9]+}} range=[7:7]
// IMAGE: relation_index_dimension index={{[0-9]+}} range=[1:0]
// IMAGE-NEXT: relation_index_dimension index={{[0-9]+}} range=[-1:0]
// IMAGE: relation {{.*}} source_name=top.cube target_name=top.cube[2][-1][7]
// IMAGE-NEXT: relation {{.*}} source_name=top.cube target_name=top.cube[2][0][7]
// IMAGE-NEXT: relation {{.*}} source_name=top.cube target_name=top.cube[1][-1][7]
// IMAGE-NEXT: relation {{.*}} source_name=top.cube target_name=top.cube[1][0][7]
// IMAGE: relation {{.*}} source_name=top.events target_name=top.events[1][-1]
// IMAGE-NEXT: relation {{.*}} source_name=top.events target_name=top.events[1][0]
// IMAGE-NEXT: relation {{.*}} source_name=top.events target_name=top.events[0][-1]
// IMAGE-NEXT: relation {{.*}} source_name=top.events target_name=top.events[0][0]
