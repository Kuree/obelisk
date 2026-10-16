// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read' | FileCheck %s

// Modports are declarations of each physical interface instance, including
// unreferenced modports and escaped identifiers. A representative body used
// only for a virtual-interface type has no physical modport inventory.
module {
  obelisk.sv.symbol.definition @iface_def attributes {definition_kind = 1 : i32,
      hierarchical_name = "iface_t", name = "iface_t", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @first attributes {
        hierarchical_name = "first", is_uninstantiated = false,
        name = "first", node_id = 2 : i64,
        referenced_path = "iface_t", referenced_symbol = @iface_def} {
      obelisk.sv.symbol.instance_body @first_body attributes {
          hierarchical_name = "first", name = "iface_t", node_id = 3 : i64} {
        obelisk.sv.symbol.modport @plain attributes {
            hierarchical_name = "first.m", name = "m", node_id = 4 : i64} {}
        obelisk.sv.symbol.modport @escaped attributes {
            hierarchical_name = "first.\\m.dot ", name = "m.dot",
            node_id = 5 : i64} {}
        obelisk.sv.symbol.modport @unused attributes {
            hierarchical_name = "first.spare", name = "spare",
            node_id = 6 : i64} {}
      }
    }
    obelisk.sv.symbol.instance @second attributes {
        hierarchical_name = "second", is_uninstantiated = false,
        name = "second", node_id = 7 : i64,
        referenced_path = "iface_t", referenced_symbol = @iface_def} {
      obelisk.sv.symbol.instance_body @second_body attributes {
          hierarchical_name = "second", name = "iface_t", node_id = 8 : i64} {
        obelisk.sv.symbol.modport @escaped attributes {
            hierarchical_name = "second.\\m.dot ", name = "m.dot",
            node_id = 9 : i64} {}
      }
    }
    obelisk.sv.symbol.instance @representative attributes {
        hierarchical_name = "representative", is_uninstantiated = false,
        is_virtual_interface_type_instance = true, name = "iface_t",
        node_id = 10 : i64, referenced_path = "iface_t",
        referenced_symbol = @iface_def} {
      obelisk.sv.symbol.instance_body @representative_body attributes {
          hierarchical_name = "representative",
          is_virtual_interface_type_instance = true, name = "iface_t",
          node_id = 11 : i64,
          virtual_interface_identity = @root::@representative} {
        obelisk.sv.symbol.modport @hidden attributes {
            hierarchical_name = "representative.hidden", name = "hidden",
            node_id = 12 : i64} {}
      }
    }
  }
}

// CHECK: simulation.vpi_object.anchor @[[FIRST:[^ ]+]] {{.*}} type 601
// CHECK: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}} type 606 {{.*}} parent @[[FIRST]] {{.*}} hierarchy "first.m" debug "m"
// CHECK: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}} type 606 {{.*}} parent @[[FIRST]] {{.*}} hierarchy "first.\\m.dot " debug "m.dot"
// CHECK: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}} type 606 {{.*}} parent @[[FIRST]] {{.*}} hierarchy "first.spare" debug "spare"
// CHECK: simulation.vpi_object.anchor @[[SECOND:[^ ]+]] {{.*}} type 601
// CHECK: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}} type 606 {{.*}} parent @[[SECOND]] {{.*}} hierarchy "second.\\m.dot " debug "m.dot"
// CHECK-NOT: type 606
// CHECK-NOT: representative.hidden
