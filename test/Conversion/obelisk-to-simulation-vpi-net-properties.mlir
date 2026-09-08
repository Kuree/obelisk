// RUN: %split-file %s %t
// RUN: obelisk-opt %t/topology.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s
// RUN: obelisk-opt %t/slang.mlir --convert-slang-to-obelisk | FileCheck %s --check-prefix=SLANG
// RUN: obelisk-opt %t/missing-definition.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s --check-prefix=MISSING

//--- topology.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "net_properties", name = "net_properties",
      node_id = 0 : i64, sym_name = "net_properties_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit",
        node_id = 2 : i64, sym_name = "cu"} {}
    obelisk.sv.symbol.instance attributes {
        hierarchical_name = "net_properties", is_uninstantiated = false,
        name = "net_properties", node_id = 3 : i64,
        referenced_path = "net_properties",
        referenced_symbol = @net_properties_def, sym_name = "instance"} {
      // A real typed port association drives Prepare's typed-interconnect
      // inference. The leaf metadata checked below is produced by that pass,
      // not forged test input.
      obelisk.sv.port.connection attributes {actual_is_constant = false,
          direction = 2 : i32, formal_name = "typed", formal_ordinal = 0 : i64,
          formal_path = "net_properties.typed",
          formal_symbol = @root::@instance::@body::@typed_port,
          formal_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
          internal_path = "net_properties.typed_internal",
          internal_symbol = @root::@instance::@body::@typed_internal,
          is_ansi = true, is_net = true, node_id = 23 : i64,
          provenance = 0 : i32} {
        obelisk.sv.expression.named_value attributes {node_id = 24 : i64,
            referenced_path = "net_properties.typed_internal",
            referenced_symbol = @root::@instance::@body::@typed_internal,
            semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
      } {
        obelisk.sv.expression.named_value attributes {node_id = 25 : i64,
            referenced_path = "net_properties.interconnect",
            referenced_symbol = @root::@instance::@body::@interconnect,
            semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.untyped>} {}
      }
      obelisk.sv.symbol.instance_body attributes {
          hierarchical_name = "net_properties", name = "net_properties",
          node_id = 4 : i64, sym_name = "body"} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.wire",
            is_implicit = false, name = "wire", net_kind = 1 : i32,
            node_id = 5 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "wire"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.wand",
            is_implicit = false, name = "wand", net_kind = 2 : i32,
            node_id = 6 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "wand"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.wor",
            is_implicit = false, name = "wor", net_kind = 3 : i32,
            node_id = 7 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "wor"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.tri",
            is_implicit = false, name = "tri", net_kind = 4 : i32,
            node_id = 8 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "tri"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.triand",
            is_implicit = false, name = "triand", net_kind = 5 : i32,
            node_id = 9 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "triand"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.trior",
            is_implicit = false, name = "trior", net_kind = 6 : i32,
            node_id = 10 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "trior"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.tri0",
            is_implicit = false, name = "tri0", net_kind = 7 : i32,
            node_id = 11 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "tri0"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.tri1",
            is_implicit = false, name = "tri1", net_kind = 8 : i32,
            node_id = 12 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "tri1"} {}
        obelisk.sv.symbol.net attributes {charge_strength = 0 : i32,
            hierarchical_name = "net_properties.trireg", is_implicit = false,
            name = "trireg", net_kind = 9 : i32, node_id = 13 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "trireg"} {}
        obelisk.sv.symbol.net attributes {
            hierarchical_name = "net_properties.trireg_default",
            is_implicit = false, name = "trireg_default", net_kind = 9 : i32,
            node_id = 21 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "trireg_default"} {}
        obelisk.sv.symbol.net attributes {charge_strength = 2 : i32,
            hierarchical_name = "net_properties.trireg_large",
            is_implicit = false, name = "trireg_large", net_kind = 9 : i32,
            node_id = 22 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "trireg_large"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.supply0",
            is_implicit = false, name = "supply0", net_kind = 10 : i32,
            node_id = 14 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "supply0"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.supply1",
            is_implicit = false, name = "supply1", net_kind = 11 : i32,
            node_id = 15 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "supply1"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_properties.uwire",
            is_implicit = false, name = "uwire", net_kind = 12 : i32,
            node_id = 16 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "uwire"} {}
        obelisk.sv.symbol.net attributes {drive_strength0 = 2 : i32,
            drive_strength1 = 3 : i32, expansion_hint = 2 : i32,
            hierarchical_name = "net_properties.scalared", is_implicit = true,
            name = "scalared", net_kind = 1 : i32, node_id = 17 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "scalared"} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "4'b0000", node_id = 18 : i64,
              semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {}
        }
        obelisk.sv.symbol.net attributes {expansion_hint = 1 : i32,
            hierarchical_name = "net_properties.vectored", is_implicit = false,
            name = "vectored", net_kind = 1 : i32, node_id = 19 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
            sym_name = "vectored"} {}
        obelisk.sv.symbol.port attributes {direction = 2 : i32,
            hierarchical_name = "net_properties.typed", name = "typed",
            node_id = 26 : i64,
            semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
            sym_name = "typed_port"} {}
        obelisk.sv.symbol.net attributes {
            hierarchical_name = "net_properties.typed_internal",
            is_implicit = false, name = "typed_internal", net_kind = 1 : i32,
            node_id = 27 : i64,
            semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
            sym_name = "typed_internal"} {}
        obelisk.sv.symbol.net attributes {
            hierarchical_name = "net_properties.interconnect",
            is_implicit = false, name = "interconnect", net_kind = 13 : i32,
            node_id = 20 : i64,
            semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.untyped>,
            sym_name = "interconnect"} {}
      }
    }
  }
}

// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.wire" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 1 : i32>, #obelisk_sim.vpi_property<selector = 27 : i32, value = 0 : i32>]>
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.wand" {{.*}}selector = 22 : i32, value = 2 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.wor" {{.*}}selector = 22 : i32, value = 3 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.tri" {{.*}}selector = 22 : i32, value = 4 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.tri0" {{.*}}selector = 22 : i32, value = 5 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.tri1" {{.*}}selector = 22 : i32, value = 6 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.trireg" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 7 : i32>, #obelisk_sim.vpi_property<selector = 27 : i32, value = 2 : i32>]>
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.trireg_default" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 7 : i32>, #obelisk_sim.vpi_property<selector = 27 : i32, value = 4 : i32>]>
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.trireg_large" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 7 : i32>, #obelisk_sim.vpi_property<selector = 27 : i32, value = 16 : i32>]>
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.triand" {{.*}}selector = 22 : i32, value = 8 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.trior" {{.*}}selector = 22 : i32, value = 9 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.supply1" {{.*}}selector = 22 : i32, value = 10 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.supply0" {{.*}}selector = 22 : i32, value = 11 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.uwire" {{.*}}selector = 22 : i32, value = 13 : i32
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.scalared" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 1 : i32>, #obelisk_sim.vpi_property<selector = 23 : i32, value = true>, #obelisk_sim.vpi_property<selector = 25 : i32, value = true>, #obelisk_sim.vpi_property<selector = 26 : i32, value = true>, #obelisk_sim.vpi_property<selector = 27 : i32, value = 0 : i32>, #obelisk_sim.vpi_property<selector = 31 : i32, value = 32 : i32>, #obelisk_sim.vpi_property<selector = 32 : i32, value = 8 : i32>, #obelisk_sim.vpi_property<selector = 43 : i32, value = true>]>
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.vectored" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 1 : i32>, #obelisk_sim.vpi_property<selector = 24 : i32, value = true>, #obelisk_sim.vpi_property<selector = 27 : i32, value = 0 : i32>]>
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.interconnect[0]" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 16 : i32>, #obelisk_sim.vpi_property<selector = 27 : i32, value = 0 : i32>]>
// CHECK-DAG: obelisk_sim.net.decl {{.*}} hierarchy "net_properties.interconnect[1]" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 16 : i32>, #obelisk_sim.vpi_property<selector = 27 : i32, value = 0 : i32>]>

//--- slang.mlir

module {
  slang.symbol.root attributes {hierarchical_name = "$root", node_id = 0 : i64,
      sym_name = "root"} {
    slang.symbol.net attributes {expansion_hint = 2 : i32,
        hierarchical_name = "top.scalared", is_implicit = false,
        name = "scalared", net_kind = 1 : i32, node_id = 1 : i64,
        semantic_type = !slang.integral<4, false, true, 3 : 0, generic>,
        sym_name = "scalared"} {}
    slang.symbol.net attributes {expansion_hint = 1 : i32,
        hierarchical_name = "top.vectored", is_implicit = false,
        name = "vectored", net_kind = 1 : i32, node_id = 2 : i64,
        semantic_type = !slang.integral<4, false, true, 3 : 0, generic>,
        sym_name = "vectored"} {}
    slang.symbol.net attributes {hierarchical_name = "top.default",
        is_implicit = false, name = "default", net_kind = 1 : i32,
        node_id = 3 : i64,
        semantic_type = !slang.integral<4, false, true, 3 : 0, generic>,
        sym_name = "default"} {}
  }
}

// SLANG-DAG: obelisk.sv.symbol.net attributes {expansion_hint = 2 : i32, hierarchical_name = "top.scalared"
// SLANG-DAG: obelisk.sv.symbol.net attributes {expansion_hint = 1 : i32, hierarchical_name = "top.vectored"
// SLANG-DAG: obelisk.sv.symbol.net attributes {hierarchical_name = "top.default", is_implicit = false

//--- missing-definition.mlir

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    // Hand-authored and partially lowered MLIR may not retain definition
    // provenance. The sparse VPI image must omit vpiDefName, not reject the
    // otherwise valid instance body or invent an instance-as-definition name.
    obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
        name = "top", node_id = 2 : i64, sym_name = "body"} {}
  }
}

// MISSING: obelisk_sim.vpi_object.anchor
// MISSING-SAME: type 32
// MISSING-SAME: hierarchy "top"
// MISSING-NOT: selector = 9
