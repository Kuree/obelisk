// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_wired_resolution", name = "unsupported_wired_resolution", node_id = 0 : i64, sym_name = "s0.unsupported_wired_resolution"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_wired_resolution", is_uninstantiated = false, name = "unsupported_wired_resolution", node_id = 3 : i64, referenced_path = "unsupported_wired_resolution", referenced_symbol = @s0.unsupported_wired_resolution, sym_name = "s3.unsupported_wired_resolution"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_wired_resolution", name = "unsupported_wired_resolution", node_id = 4 : i64, sym_name = "s4.unsupported_wired_resolution"} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "unsupported_wired_resolution.value", is_implicit = false, name = "value", net_kind = 2 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "unsupported_wired_resolution.or_value", is_implicit = false, name = "or_value", net_kind = 3 : i32, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.or_value"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "unsupported_wired_resolution.triand_value", is_implicit = false, name = "triand_value", net_kind = 5 : i32, node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.triand_value"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "unsupported_wired_resolution.trior_value", is_implicit = false, name = "trior_value", net_kind = 6 : i32, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.trior_value"} {
        }
      }
    }
  }
}

// IEEE 1800-2017 6.6.3: triand and trior are exact aliases of wand and wor.
// CHECK-DAG: obelisk_sim.net.decl {{[0-9]+}} {{.*}} hierarchy "unsupported_wired_resolution.value" {{.*}}resolution_kind = 3 : i32
// CHECK-DAG: obelisk_sim.net.decl {{[0-9]+}} {{.*}} hierarchy "unsupported_wired_resolution.or_value" {{.*}}resolution_kind = 4 : i32
// CHECK-DAG: obelisk_sim.net.decl {{[0-9]+}} {{.*}} hierarchy "unsupported_wired_resolution.triand_value" {{.*}}resolution_kind = 3 : i32
// CHECK-DAG: obelisk_sim.net.decl {{[0-9]+}} {{.*}} hierarchy "unsupported_wired_resolution.trior_value" {{.*}}resolution_kind = 4 : i32
