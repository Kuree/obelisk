// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Runtime behavior is checked in ../Runtime/obelisk-to-simulation-container-bitstream-cast.test.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
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
          semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, true, 7 : 0, logic>>,
          sym_name = "s5.source"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.result", lifetime = 1 : i32,
          name = "result", node_id = 6 : i64,
          semantic_type = !obelisk.integral<24, false, true, 23 : 0, logic>,
          sym_name = "s6.result"
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 7 : i64,
            semantic_type = !obelisk.integral<24, false, true, 23 : 0, logic>
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 8 : i64,
              referenced_path = "top.source",
              referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.source,
              semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, true, 7 : 0, logic>>
            } {}
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[THREE:.*]] = arith.constant 3 : i64
// CHECK: %[[SOURCE:.*]] = simulation.ref.load
// CHECK: %[[SIZE:.*]] = simulation.container.size %[[SOURCE]]
// CHECK: %[[MATCHES:.*]] = arith.cmpi eq, %[[SIZE]], %[[THREE]] : i64
// CHECK: cf.cond_br %[[MATCHES]], ^[[ACCEPTED:.*]], ^[[REJECTED:.*]]
// CHECK: ^[[ACCEPTED]]:
// CHECK: simulation.container.export_bitstream %[[SOURCE]]
// CHECK-NOT: simulation.logic.dyn_insert
// CHECK: ^[[REJECTED]]:
// CHECK: bit-stream cast source and destination widths differ
