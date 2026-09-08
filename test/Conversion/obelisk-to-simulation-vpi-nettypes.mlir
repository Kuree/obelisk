// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s

!logic = !obelisk.integral<1, false, true, 0 : 0, logic>
!net_array = !obelisk.ranged_unpacked_array<3 : 0 x !logic>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64,
      sym_name = "top_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 2 : i64,
        obelisk_sim.vpi_definition_name = "$unit", sym_name = "unit"} {
      obelisk.sv.type.net_type attributes {data_type = !logic,
          hierarchical_name = "base_nt", is_builtin = false,
          name = "base_nt", net_kind = 14 : i32, node_id = 3 : i64,
          semantic_type = !logic, sym_name = "base_nt"} {}
      obelisk.sv.type.net_type attributes {
          aliased_nettype_path = "base_nt",
          aliased_nettype_symbol = @root::@unit::@base_nt,
          data_type = !logic, hierarchical_name = "alias_nt",
          is_builtin = false, name = "alias_nt", net_kind = 14 : i32,
          node_id = 4 : i64, semantic_type = !logic,
          sym_name = "alias_nt"} {}
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 5 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top_instance"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 6 : i64,
          obelisk_sim.vpi_automatic = false,
          obelisk_sim.vpi_cell_instance = false,
          obelisk_sim.vpi_definition_name = "top",
          obelisk_sim.vpi_top = true, sym_name = "top_body",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "top.n",
            is_implicit = false, name = "n", net_kind = 14 : i32,
            nettype_path = "alias_nt",
            nettype_symbol = @root::@unit::@alias_nt, node_id = 7 : i64,
            semantic_type = !net_array, sym_name = "n"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "top.alias_n",
            is_implicit = false, name = "alias_n", net_kind = 14 : i32,
            nettype_path = "alias_nt",
            nettype_symbol = @root::@unit::@alias_nt, node_id = 8 : i64,
            semantic_type = !net_array, sym_name = "alias_n"} {}
        obelisk.sv.symbol.net_alias attributes {hierarchical_name = "top",
            node_id = 9 : i64, sym_name = "alias"} {
          obelisk.sv.expression.named_value attributes {is_signed = false,
              node_id = 10 : i64, referenced_path = "top.n",
              referenced_symbol = @root::@top_instance::@top_body::@n,
              semantic_type = !net_array} {}
          obelisk.sv.expression.named_value attributes {is_signed = false,
              node_id = 11 : i64, referenced_path = "top.alias_n",
              referenced_symbol = @root::@top_instance::@top_body::@alias_n,
              semantic_type = !net_array} {}
        }
      }
    }
  }
}

// CHECK-DAG: obelisk_sim.vpi_nettype.decl @[[BASE:[^ ]+]] id 0 {{.*}} hierarchy "base_nt" debug "base_nt"
// CHECK-DAG: obelisk_sim.vpi_nettype.decl @[[ALIAS:[^ ]+]] id 1 {{.*}} hierarchy "alias_nt" debug "alias_nt" {direct_alias = @[[BASE]]
// CHECK: obelisk_sim.net.decl 0 {{.*}} hierarchy "top.n" debug "n" {nettype = @[[ALIAS]], obelisk_sim.user_defined_net
// CHECK-SAME: #obelisk_sim.vpi_property<selector = 22 : i32, value = 14 : i32>
// CHECK: obelisk_sim.vpi_net_identity.decl {{[0-9]+}} backed_by 0 {{.*}} hierarchy "top.alias_n" debug "alias_n"
// CHECK-SAME: nettype = @[[ALIAS]]
// CHECK-SAME: #obelisk_sim.vpi_property<selector = 22 : i32, value = 14 : i32>
