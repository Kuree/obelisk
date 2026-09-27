// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64, sym_name = "s0.top"
  } {}
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {}
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 3 : i64, referenced_path = "top",
      referenced_symbol = @s0.top, sym_name = "s3.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 4 : i64,
        sym_name = "s4.top"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.source", lifetime = 1 : i32,
          name = "source", node_id = 5 : i64,
          semantic_type = !obelisk.dynarray<!obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, bit>>>,
          sym_name = "s5.source"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.result", lifetime = 1 : i32,
          name = "result", node_id = 6 : i64,
          semantic_type = !obelisk.integral<32, false, false, 31 : 0, bit>,
          sym_name = "s6.result"
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = true, is_signed = false, node_id = 7 : i64,
            semantic_type = !obelisk.integral<32, false, false, 31 : 0, bit>
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 8 : i64,
              referenced_path = "top.source",
              referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.source,
              semantic_type = !obelisk.dynarray<!obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, bit>>>
            } {}
          }
        }
      }
    }
  }
}

// CHECK: unsupported bit-stream cast from '!simulation.dynamic_array<!simulation.dynamic_array<i8>>' to 'i32'
