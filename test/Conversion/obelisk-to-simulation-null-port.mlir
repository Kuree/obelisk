// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | \
// RUN:   FileCheck %s

// IEEE 1800-2017 23.2.2.1 permits an empty non-ANSI port expression. Slang
// represents it as a void formal with no internal path or actual expression;
// it contributes no topology and must not require a flattened descriptor.
// Clause 23.3.3.1 also permits a net port used in both directions to be
// coerced to inout. Its implicit width conversion is topology metadata: only
// the overlapping low bits merge and unmatched bits stay undriven.

// CHECK: simulation.design @design
// CHECK-SAME: edges = []
// CHECK: simulation.net.decl
// CHECK-COUNT-1: simulation.func @__obelisk_root
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "leaf", name = "leaf",
    node_id = 0 : i64, sym_name = "s0.leaf"
  } {}
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 1 : i64, sym_name = "s1.top"
  } {}
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64,
    sym_name = "s2.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"
    } {}
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 4 : i64, referenced_path = "top",
      referenced_symbol = @s1.top, sym_name = "s4.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 5 : i64,
        sym_name = "s5.top", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.net attributes {
          hierarchical_name = "top.a", is_implicit = false, name = "a",
          net_kind = 1 : i32, node_id = 6 : i64,
          semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
          sym_name = "s6.a"
        } {}
        obelisk.sv.symbol.instance attributes {
          hierarchical_name = "top.i", is_uninstantiated = false, name = "i",
          node_id = 7 : i64, referenced_path = "leaf",
          referenced_symbol = @s0.leaf, sym_name = "s7.i"
        } {
          obelisk.sv.port.connection attributes {
            actual_is_constant = false, direction = 2 : i32,
            formal_name = "value", formal_ordinal = 0 : i64,
            formal_path = "top.i.value",
            formal_symbol = @s2.$root::@s4.top::@s5.top::@s7.i::@s8.leaf::@s9.value,
            formal_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
            internal_path = "top.i.value",
            internal_symbol = @s2.$root::@s4.top::@s5.top::@s7.i::@s8.leaf::@s10.value,
            is_ansi = true, is_net = true, node_id = 8 : i64,
            provenance = 0 : i32
          } {} {
            obelisk.sv.expression.conversion attributes {
              is_implicit = true, node_id = 9 : i64,
              semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 10 : i64, referenced_path = "top.a",
                referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
              } {}
            }
          }
          obelisk.sv.port.connection attributes {
            actual_is_constant = false, direction = 0 : i32,
            formal_ordinal = 1 : i64, formal_path = "top.i",
            formal_symbol = @s2.$root::@s4.top::@s5.top::@s7.i::@s8.leaf::@s11,
            formal_type = !obelisk.void, is_ansi = false, is_net = false,
            node_id = 11 : i64, provenance = 4 : i32
          } {} {}
          obelisk.sv.symbol.instance_body attributes {
            hierarchical_name = "top.i", name = "leaf", node_id = 12 : i64,
            sym_name = "s8.leaf", time_precision_fs = 1000000 : i64,
            time_unit_fs = 1000000 : i64
          } {
            obelisk.sv.symbol.port attributes {
              direction = 0 : i32, hierarchical_name = "top.i.value",
              name = "value", node_id = 13 : i64,
              semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
              sym_name = "s9.value"
            } {}
            obelisk.sv.symbol.net attributes {
              hierarchical_name = "top.i.value", is_implicit = false,
              name = "value", net_kind = 1 : i32, node_id = 14 : i64,
              semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
              sym_name = "s10.value"
            } {}
            obelisk.sv.symbol.port attributes {
              direction = 0 : i32, hierarchical_name = "top.i",
              node_id = 15 : i64, semantic_type = !obelisk.void,
              sym_name = "s11"
            } {}
          }
        }
      }
    }
  }
}
