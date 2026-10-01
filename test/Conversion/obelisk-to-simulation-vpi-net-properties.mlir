// RUN: %split-file %s %t
// RUN: obelisk-opt %t/topology.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s
// RUN: obelisk-opt %t/slang.mlir --convert-slang-to-obelisk | FileCheck %s --check-prefix=SLANG
// RUN: obelisk-opt %t/missing-definition.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s --check-prefix=MISSING

//--- topology.mlir

!anon_state = !obelisk.enum<"anon_state_t", !obelisk.integral<2, false, true, 1 : 0, logic>>

module {
  obelisk.sv.symbol.definition @net_properties_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "net_properties", name = "net_properties",
      node_id = 0 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit",
        node_id = 2 : i64} {
      obelisk.sv.type.type_alias @word_t attributes {
          hierarchical_name = "$unit::word_t", name = "word_t",
          node_id = 34 : i64,
          semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
          vpi_typedef_layers = [{aliases = [
            @root::@cu::@word_t], path = array<i64>}]} {}
      obelisk.sv.type.net_type @word_nt attributes {
          data_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
          hierarchical_name = "$unit::word_nt", is_builtin = false,
          name = "word_nt", net_kind = 14 : i32, node_id = 35 : i64,
          semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
      } {}
      obelisk.sv.symbol.enum_value @anon_idle attributes {constant_value = "2'b00",
          hierarchical_name = "$unit::anon_state_t.IDLE", name = "IDLE",
          node_id = 42 : i64, semantic_type = !anon_state,
          vpi_source_type_identity = 77 : i64} {}
    }
    obelisk.sv.symbol.instance @instance attributes {
        hierarchical_name = "net_properties", is_uninstantiated = false,
        name = "net_properties", node_id = 3 : i64,
        referenced_path = "net_properties",
        referenced_symbol = @net_properties_def} {
      // A real typed port association drives Prepare's typed-interconnect
      // inference. The leaf metadata checked below is produced by that pass,
      // not forged test input.
      obelisk.sv.port.connection attributes {actual_is_constant = false,
          direction = 2 : i32, formal_name = "typed", formal_ordinal = 0 : i64,
          formal_path = "net_properties.typed",
          formal_symbol = @root::@instance::@body::@typed_port,
          formal_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<-1 : 0 x !obelisk.integral<8, false, true, 7 : 0, logic>>>,
          internal_path = "net_properties.typed_internal",
          internal_symbol = @root::@instance::@body::@typed_internal,
          is_ansi = true, is_net = true, node_id = 23 : i64,
          provenance = 0 : i32} {
        obelisk.sv.expression.named_value attributes {node_id = 24 : i64,
            referenced_path = "net_properties.typed_internal",
            referenced_symbol = @root::@instance::@body::@typed_internal,
            semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<-1 : 0 x !obelisk.integral<8, false, true, 7 : 0, logic>>>} {}
      } {
        obelisk.sv.expression.named_value attributes {node_id = 25 : i64,
            referenced_path = "net_properties.interconnect",
            referenced_symbol = @root::@instance::@body::@interconnect,
            semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<-1 : 0 x !obelisk.untyped>>} {}
      }
      obelisk.sv.port.connection attributes {actual_is_constant = false,
          direction = 2 : i32, formal_name = "scalar_typed",
          formal_ordinal = 1 : i64,
          formal_path = "net_properties.scalar_typed",
          formal_symbol = @root::@instance::@body::@scalar_port,
          formal_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
          internal_path = "net_properties.scalar_internal",
          internal_symbol = @root::@instance::@body::@scalar_internal,
          is_ansi = true, is_net = true, node_id = 28 : i64,
          provenance = 0 : i32} {
        obelisk.sv.expression.named_value attributes {node_id = 29 : i64,
            referenced_path = "net_properties.scalar_internal",
            referenced_symbol = @root::@instance::@body::@scalar_internal,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {}
      } {
        obelisk.sv.expression.named_value attributes {node_id = 30 : i64,
            referenced_path = "net_properties.scalar_interconnect",
            referenced_symbol = @root::@instance::@body::@scalar_interconnect,
            semantic_type = !obelisk.untyped} {}
      }
      obelisk.sv.port.connection attributes {actual_is_constant = false,
          direction = 2 : i32, formal_name = "enum_typed",
          formal_ordinal = 3 : i64,
          formal_path = "net_properties.enum_typed",
          formal_symbol = @root::@instance::@body::@enum_port,
          formal_type = !anon_state,
          internal_path = "net_properties.enum_internal",
          internal_symbol = @root::@instance::@body::@enum_internal,
          is_ansi = true, is_net = true, node_id = 43 : i64,
          provenance = 0 : i32, vpi_source_type_identity = 77 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 44 : i64,
            referenced_path = "net_properties.enum_internal",
            referenced_symbol = @root::@instance::@body::@enum_internal,
            semantic_type = !anon_state} {}
      } {
        obelisk.sv.expression.named_value attributes {node_id = 45 : i64,
            referenced_path = "net_properties.enum_interconnect",
            referenced_symbol = @root::@instance::@body::@enum_interconnect,
            semantic_type = !obelisk.untyped} {}
      }
      obelisk.sv.port.connection attributes {actual_is_constant = false,
          direction = 2 : i32, formal_name = "alias_typed",
          formal_ordinal = 2 : i64,
          formal_path = "net_properties.alias_typed",
          formal_symbol = @root::@instance::@body::@alias_port,
          formal_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
          internal_path = "net_properties.alias_internal",
          internal_symbol = @root::@instance::@body::@alias_internal,
          is_ansi = true, is_net = true, node_id = 36 : i64,
          provenance = 0 : i32, vpi_typedef_layers = [{aliases = [
            @root::@cu::@word_t], path = array<i64>}]} {
        obelisk.sv.expression.named_value attributes {node_id = 37 : i64,
            referenced_path = "net_properties.alias_internal",
            referenced_symbol = @root::@instance::@body::@alias_internal,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {}
      } {
        obelisk.sv.expression.named_value attributes {node_id = 38 : i64,
            referenced_path = "net_properties.alias_interconnect",
            referenced_symbol = @root::@instance::@body::@alias_interconnect,
            semantic_type = !obelisk.untyped} {}
      }
      obelisk.sv.symbol.instance_body @body attributes {
          hierarchical_name = "net_properties", name = "net_properties",
          node_id = 4 : i64} {
        obelisk.sv.symbol.net @wire attributes {hierarchical_name = "net_properties.wire",
            is_implicit = false, name = "wire", net_kind = 1 : i32,
            node_id = 5 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @wand attributes {hierarchical_name = "net_properties.wand",
            is_implicit = false, name = "wand", net_kind = 2 : i32,
            node_id = 6 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @wor attributes {hierarchical_name = "net_properties.wor",
            is_implicit = false, name = "wor", net_kind = 3 : i32,
            node_id = 7 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @tri attributes {hierarchical_name = "net_properties.tri",
            is_implicit = false, name = "tri", net_kind = 4 : i32,
            node_id = 8 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @triand attributes {hierarchical_name = "net_properties.triand",
            is_implicit = false, name = "triand", net_kind = 5 : i32,
            node_id = 9 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @trior attributes {hierarchical_name = "net_properties.trior",
            is_implicit = false, name = "trior", net_kind = 6 : i32,
            node_id = 10 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @tri0 attributes {hierarchical_name = "net_properties.tri0",
            is_implicit = false, name = "tri0", net_kind = 7 : i32,
            node_id = 11 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @tri1 attributes {hierarchical_name = "net_properties.tri1",
            is_implicit = false, name = "tri1", net_kind = 8 : i32,
            node_id = 12 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @trireg attributes {charge_strength = 0 : i32,
            hierarchical_name = "net_properties.trireg", is_implicit = false,
            name = "trireg", net_kind = 9 : i32, node_id = 13 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @trireg_default attributes {
            hierarchical_name = "net_properties.trireg_default",
            is_implicit = false, name = "trireg_default", net_kind = 9 : i32,
            node_id = 21 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @trireg_large attributes {charge_strength = 2 : i32,
            hierarchical_name = "net_properties.trireg_large",
            is_implicit = false, name = "trireg_large", net_kind = 9 : i32,
            node_id = 22 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @supply0 attributes {hierarchical_name = "net_properties.supply0",
            is_implicit = false, name = "supply0", net_kind = 10 : i32,
            node_id = 14 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @supply1 attributes {hierarchical_name = "net_properties.supply1",
            is_implicit = false, name = "supply1", net_kind = 11 : i32,
            node_id = 15 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @uwire attributes {hierarchical_name = "net_properties.uwire",
            is_implicit = false, name = "uwire", net_kind = 12 : i32,
            node_id = 16 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @scalared attributes {drive_strength0 = 2 : i32,
            drive_strength1 = 3 : i32, expansion_hint = 2 : i32,
            hierarchical_name = "net_properties.scalared", is_implicit = true,
            name = "scalared", net_kind = 1 : i32, node_id = 17 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "4'b0000", node_id = 18 : i64,
              semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {}
        }
        obelisk.sv.symbol.net @vectored attributes {expansion_hint = 1 : i32,
            hierarchical_name = "net_properties.vectored", is_implicit = false,
            name = "vectored", net_kind = 1 : i32, node_id = 19 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.port @typed_port attributes {direction = 2 : i32,
            hierarchical_name = "net_properties.typed", name = "typed",
            node_id = 26 : i64,
            semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<-1 : 0 x !obelisk.integral<8, false, true, 7 : 0, logic>>>
        } {}
        obelisk.sv.symbol.net @typed_internal attributes {
            hierarchical_name = "net_properties.typed_internal",
            is_implicit = false, name = "typed_internal", net_kind = 1 : i32,
            node_id = 27 : i64,
            semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<-1 : 0 x !obelisk.integral<8, false, true, 7 : 0, logic>>>
        } {}
        obelisk.sv.symbol.net @interconnect attributes {
            hierarchical_name = "net_properties.interconnect",
            is_implicit = false, name = "interconnect", net_kind = 13 : i32,
            node_id = 20 : i64,
            semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<-1 : 0 x !obelisk.untyped>>
        } {}
        obelisk.sv.symbol.port @scalar_port attributes {direction = 2 : i32,
            hierarchical_name = "net_properties.scalar_typed",
            name = "scalar_typed", node_id = 31 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @scalar_internal attributes {
            hierarchical_name = "net_properties.scalar_internal",
            is_implicit = false, name = "scalar_internal", net_kind = 14 : i32,
            nettype_path = "$unit::word_nt",
            nettype_symbol = @root::@cu::@word_nt,
            node_id = 32 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @scalar_interconnect attributes {
            hierarchical_name = "net_properties.scalar_interconnect",
            is_implicit = false, name = "scalar_interconnect",
            net_kind = 13 : i32, node_id = 33 : i64,
            semantic_type = !obelisk.untyped
        } {}
        obelisk.sv.symbol.port @alias_port attributes {direction = 2 : i32,
            hierarchical_name = "net_properties.alias_typed",
            name = "alias_typed", node_id = 39 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @alias_internal attributes {
            hierarchical_name = "net_properties.alias_internal",
            is_implicit = false, name = "alias_internal", net_kind = 1 : i32,
            node_id = 40 : i64,
            semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>
        } {}
        obelisk.sv.symbol.net @alias_interconnect attributes {
            hierarchical_name = "net_properties.alias_interconnect",
            is_implicit = false, name = "alias_interconnect",
            net_kind = 13 : i32, node_id = 41 : i64,
            semantic_type = !obelisk.untyped
        } {}
        obelisk.sv.symbol.port @enum_port attributes {direction = 2 : i32,
            hierarchical_name = "net_properties.enum_typed",
            name = "enum_typed", node_id = 46 : i64,
            semantic_type = !anon_state,
            vpi_source_type_identity = 77 : i64} {}
        obelisk.sv.symbol.net @enum_internal attributes {
            hierarchical_name = "net_properties.enum_internal",
            is_implicit = false, name = "enum_internal", net_kind = 1 : i32,
            node_id = 47 : i64, semantic_type = !anon_state,
            vpi_source_type_identity = 77 : i64} {}
        obelisk.sv.symbol.net @enum_interconnect attributes {
            hierarchical_name = "net_properties.enum_interconnect",
            is_implicit = false, name = "enum_interconnect",
            net_kind = 13 : i32, node_id = 48 : i64,
            semantic_type = !obelisk.untyped
        } {}
      }
    }
  }
}

// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.wire" {{.*}}vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 1 : i32>, #simulation.vpi_property<selector = 27 : i32, value = 0 : i32>]>
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.wand" {{.*}}selector = 22 : i32, value = 2 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.wor" {{.*}}selector = 22 : i32, value = 3 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.tri" {{.*}}selector = 22 : i32, value = 4 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.tri0" {{.*}}selector = 22 : i32, value = 5 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.tri1" {{.*}}selector = 22 : i32, value = 6 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.trireg" {{.*}}vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 7 : i32>, #simulation.vpi_property<selector = 27 : i32, value = 2 : i32>]>
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.trireg_default" {{.*}}vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 7 : i32>, #simulation.vpi_property<selector = 27 : i32, value = 4 : i32>]>
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.trireg_large" {{.*}}vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 7 : i32>, #simulation.vpi_property<selector = 27 : i32, value = 16 : i32>]>
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.triand" {{.*}}selector = 22 : i32, value = 8 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.trior" {{.*}}selector = 22 : i32, value = 9 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.supply1" {{.*}}selector = 22 : i32, value = 10 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.supply0" {{.*}}selector = 22 : i32, value = 11 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.uwire" {{.*}}selector = 22 : i32, value = 13 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.scalared" {{.*}}vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 1 : i32>, #simulation.vpi_property<selector = 23 : i32, value = true>, #simulation.vpi_property<selector = 25 : i32, value = true>, #simulation.vpi_property<selector = 26 : i32, value = true>, #simulation.vpi_property<selector = 27 : i32, value = 0 : i32>, #simulation.vpi_property<selector = 31 : i32, value = 32 : i32>, #simulation.vpi_property<selector = 32 : i32, value = 8 : i32>, #simulation.vpi_property<selector = 43 : i32, value = true>]>
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.vectored" {{.*}}vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 1 : i32>, #simulation.vpi_property<selector = 24 : i32, value = true>, #simulation.vpi_property<selector = 27 : i32, value = 0 : i32>]>
// CHECK-DAG: simulation.vpi_object.anchor @[[INTERCONNECT:[^ ]+]] {{.*}} type 534 {{.*}} hierarchy "net_properties.interconnect" {{.*}}index_dimension_flags = array<i64: 0, 1>, index_ranges = array<i64: 1, 0, -1, 0>
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.interconnect[1][-1]" {{.*}}vpi_type = #simulation.vpi_type<kind = logic{{.*}}range = [7, 0]
// CHECK-DAG: simulation.vpi_object.anchor {{.*}} type 533 {{.*}} parent @[[INTERCONNECT]] {{.*}} hierarchy "net_properties.interconnect[1][-1]" {{.*}}backing = #simulation.vpi_backing<kind = net{{.*}}member_indices = array<i64: 1, -1>
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.interconnect[1][0]" {{.*}}selector = 22 : i32, value = 16 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.interconnect[0][-1]" {{.*}}selector = 22 : i32, value = 16 : i32
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.interconnect[0][0]" {{.*}}selector = 22 : i32, value = 16 : i32
// CHECK-DAG: simulation.vpi_object.anchor {{.*}} type 533 {{.*}} parent @[[INTERCONNECT]] {{.*}} hierarchy "net_properties.interconnect[0][0]" {{.*}}backing = #simulation.vpi_backing<kind = net{{.*}}member_indices = array<i64: 0, 0>
// CHECK-DAG: simulation.vpi_nettype.decl @[[WORDNT:[^ ]+]] {{.*}} hierarchy "$unit::word_nt"
// CHECK-DAG: simulation.vpi_typespec.decl @[[WORDT:[^ ]+]] {{.*}} hierarchy "$unit::word_t"
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.scalar_interconnect" {{.*}}nettype = @[[WORDNT]]{{.*}}vpi_type = #simulation.vpi_type<kind = logic{{.*}}range = [3, 0]
// CHECK-DAG: simulation.vpi_object.anchor {{.*}} type 533 {{.*}} hierarchy "net_properties.scalar_interconnect" {{.*}}backing = #simulation.vpi_backing<kind = net
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.alias_interconnect" {{.*}}vpi_type = #simulation.vpi_type<kind = logic{{.*}}typedefAliases = [@[[WORDT]]]
// CHECK-DAG: simulation.vpi_object.anchor {{.*}} type 533 {{.*}} hierarchy "net_properties.alias_interconnect" {{.*}}backing = #simulation.vpi_backing<kind = net
// CHECK-DAG: simulation.vpi_typespec.decl @[[ANON:[^ ]+]] {{.*}} hierarchy "$unit::anon_state_t" {{.*}}source_type_identity = 77 : i64
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "net_properties.enum_interconnect" {{.*}}simulation.vpi_source_type_identity = 77 : i64{{.*}}vpi_type = #simulation.vpi_type<kind = enum{{.*}}name = "anon_state_t"
// CHECK-DAG: simulation.vpi_object.anchor {{.*}} type 533 {{.*}} hierarchy "net_properties.enum_interconnect" {{.*}}backing = #simulation.vpi_backing<kind = net

//--- slang.mlir

module {
  slang.symbol.root @root attributes {hierarchical_name = "$root", node_id = 0 : i64
  } {
    slang.symbol.net @scalared attributes {expansion_hint = 2 : i32,
        hierarchical_name = "top.scalared", is_implicit = false,
        name = "scalared", net_kind = 1 : i32, node_id = 1 : i64,
        semantic_type = !slang.integral<4, false, true, 3 : 0, generic>
    } {}
    slang.symbol.net @vectored attributes {expansion_hint = 1 : i32,
        hierarchical_name = "top.vectored", is_implicit = false,
        name = "vectored", net_kind = 1 : i32, node_id = 2 : i64,
        semantic_type = !slang.integral<4, false, true, 3 : 0, generic>
    } {}
    slang.symbol.net @default attributes {hierarchical_name = "top.default",
        is_implicit = false, name = "default", net_kind = 1 : i32,
        node_id = 3 : i64,
        semantic_type = !slang.integral<4, false, true, 3 : 0, generic>
    } {}
  }
}

// SLANG-DAG: obelisk.sv.symbol.net @{{[^ ]+}} attributes {expansion_hint = 2 : i32, hierarchical_name = "top.scalared"
// SLANG-DAG: obelisk.sv.symbol.net @{{[^ ]+}} attributes {expansion_hint = 1 : i32, hierarchical_name = "top.vectored"
// SLANG-DAG: obelisk.sv.symbol.net @{{[^ ]+}} attributes {hierarchical_name = "top.default", is_implicit = false

//--- missing-definition.mlir

module {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    // Hand-authored and partially lowered MLIR may not retain definition
    // provenance. The sparse VPI image must omit vpiDefName, not reject the
    // otherwise valid instance body or invent an instance-as-definition name.
    obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top",
        name = "top", node_id = 2 : i64} {}
  }
}

// MISSING: simulation.vpi_object.anchor
// MISSING-SAME: type 32
// MISSING-SAME: hierarchy "top"
// MISSING-NOT: selector = 9
