// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.bus_if attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @s1.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {
  }
  obelisk.sv.symbol.root @s2.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.compilation_unit @s3 attributes {hierarchical_name = "$unit", node_id = 3 : i64} {
    }
    obelisk.sv.symbol.instance @s4.top attributes {hierarchical_name = "top", is_uninstantiated = false, is_virtual_interface_type_instance = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top} {
      obelisk.sv.symbol.instance_body @s5.top attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s6.sentinel attributes {hierarchical_name = "top.sentinel", lifetime = 1 : i32, name = "sentinel", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        // Slang creates these compile-time-only instances to resolve the two
        // parameterized virtual-interface types. They are not design scopes.
        obelisk.sv.symbol.instance @s7.bus_if attributes {hierarchical_name = "top.bus_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "bus_if", node_id = 7 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if} {
          obelisk.sv.symbol.instance_body @s8.bus_if attributes {hierarchical_name = "top.bus_if", name = "bus_if", node_id = 8 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s7.bus_if} {
            obelisk.sv.symbol.parameter @s18.WIDTH attributes {constant_value = "32'd8",
                hierarchical_name = "top.bus_if.WIDTH", name = "WIDTH",
                node_id = 18 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {}
            obelisk.sv.symbol.variable @s9.synthetic_a attributes {hierarchical_name = "top.bus_if.synthetic_a", lifetime = 1 : i32, name = "synthetic_a", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.instance @s10.bus_if attributes {hierarchical_name = "top.bus_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "bus_if", node_id = 10 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if} {
          obelisk.sv.symbol.instance_body @s11.bus_if attributes {hierarchical_name = "top.bus_if", name = "bus_if", node_id = 11 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s10.bus_if} {
            obelisk.sv.symbol.variable @s12.synthetic_b attributes {hierarchical_name = "top.bus_if.synthetic_b", lifetime = 1 : i32, name = "synthetic_b", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        // Typedef aliases retain the canonical specialization and only add a
        // modport view; they do not create a third interface type.
        obelisk.sv.type.type_alias @s13.vif_a_t attributes {hierarchical_name = "top.vif_a_t", name = "vif_a_t", node_id = 13 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.bus_if, "">} {}
        obelisk.sv.type.type_alias @s14.vif_a_m_t attributes {hierarchical_name = "top.vif_a_m_t", name = "vif_a_m_t", node_id = 14 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.bus_if, "m">} {}
        obelisk.sv.symbol.variable @s15.vif_a attributes {hierarchical_name = "top.vif_a", lifetime = 1 : i32, name = "vif_a", node_id = 15 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.bus_if, "">} {}
        obelisk.sv.symbol.variable @s16.vif_a_m attributes {hierarchical_name = "top.vif_a_m", lifetime = 1 : i32, name = "vif_a_m", node_id = 16 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.bus_if, "m">} {}
        obelisk.sv.symbol.variable @s17.vif_b attributes {hierarchical_name = "top.vif_b", lifetime = 1 : i32, name = "vif_b", node_id = 17 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s10.bus_if, "">} {}
      }
    }
  }
}

// CHECK: simulation.scope.decl 0 hierarchy "\\$root "
// CHECK: simulation.scope.decl 1 parent 0 hierarchy "top"
// CHECK-DAG: simulation.vpi_object.anchor @[[TOP_ANCHOR:__obelisk_vpi_anchor_[0-9]+]] id {{[0-9]+}} type 32 in 1 {{.*}}hierarchy "top" debug "top"
// CHECK: simulation.vpi_typespec.decl @__obelisk_vpi_typespec_0 {{.*}}symbol = @[[VIF_A:__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_]], modport = ""
// CHECK: simulation.vpi_typespec.decl @__obelisk_vpi_typespec_1 {{.*}}symbol = @[[VIF_A_M:__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_[0-9A-F]+]], modport = "m"
// CHECK-DAG: simulation.vpi_typespec.decl @[[VIF_B:__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_]] {{.*}}owner @[[TOP_ANCHOR]]{{.*}}origin = 1 : i32{{.*}}symbol = @[[VIF_B]], modport = ""
// CHECK-DAG: simulation.vpi_typespec.decl @[[VIF_A]] {{.*}}owner @[[TOP_ANCHOR]]{{.*}}origin = 1 : i32{{.*}}symbol = @[[VIF_A]], modport = ""
// CHECK-DAG: simulation.vpi_typespec.decl @[[VIF_A_M]] {{.*}}owner @[[TOP_ANCHOR]]{{.*}}origin = 1 : i32{{.*}}symbol = @[[VIF_A_M]], modport = "m"
// CHECK: simulation.storage.decl 0 in 1 {{.*}} hierarchy "top.sentinel"
// CHECK-DAG: simulation.storage.decl {{[0-9]+}} in 1 : !simulation.virtual_interface<"@s2.$root::@s5.top::@s7.bus_if", ""> {{.*}} hierarchy "top.vif_a" {{.*}}vpi_type = #simulation.vpi_type<kind = virtual_interface{{.*}}name = "@s2.$root::@s5.top::@s7.bus_if"{{.*}}symbol = @[[VIF_A]], modport = ""
// CHECK-DAG: simulation.storage.decl {{[0-9]+}} in 1 : !simulation.virtual_interface<"@s2.$root::@s5.top::@s7.bus_if", "m"> {{.*}} hierarchy "top.vif_a_m" {{.*}}vpi_type = #simulation.vpi_type<kind = virtual_interface{{.*}}name = "@s2.$root::@s5.top::@s7.bus_if"{{.*}}symbol = @[[VIF_A_M]], modport = "m"
// CHECK-DAG: simulation.storage.decl {{[0-9]+}} in 1 : !simulation.virtual_interface<"@s2.$root::@s5.top::@s10.bus_if", ""> {{.*}} hierarchy "top.vif_b" {{.*}}vpi_type = #simulation.vpi_type<kind = virtual_interface{{.*}}name = "@s2.$root::@s5.top::@s10.bus_if"{{.*}}symbol = @[[VIF_B]], modport = ""
// CHECK-NOT: hierarchy "top.bus_if
// CHECK-NOT: synthetic_a
// CHECK-NOT: synthetic_b
// CHECK-NOT: obelisk.sv.
