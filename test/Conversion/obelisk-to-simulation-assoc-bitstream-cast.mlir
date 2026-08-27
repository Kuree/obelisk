// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

!values = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<8, false, true, 7 : 0, logic>, false>
!packed = !obelisk.integral<24, false, true, 23 : 0, logic>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64,
      sym_name = "top_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.source",
            lifetime = 1 : i32, name = "source", node_id = 4 : i64,
            semantic_type = !values, sym_name = "source"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result",
            lifetime = 1 : i32, name = "result", node_id = 5 : i64,
            semantic_type = !packed, sym_name = "result"} {
          obelisk.sv.expression.conversion attributes {is_implicit = false,
              is_signed = false, node_id = 6 : i64,
              semantic_type = !packed} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 7 : i64, referenced_path = "top.source",
                referenced_symbol = @root::@top::@body::@source,
                semantic_type = !values} {}
          }
        }
      }
    }
  }
}

// IEEE 1800-2023 6.24.3: a dynamic source requires an exact runtime size.
// CHECK-DAG: %[[SOURCE:.*]] = obelisk_sim.ref.load
// CHECK-DAG: %[[THREE:.*]] = arith.constant 3 : i64
// CHECK: %[[SIZE:.*]] = obelisk_sim.container.size %[[SOURCE]]
// CHECK: %[[MATCH:.*]] = arith.cmpi eq, %[[SIZE]], %[[THREE]] : i64
// CHECK: cf.cond_br %[[MATCH]], ^[[ACCEPT:.*]], ^[[REJECT:.*]]
// CHECK: ^[[ACCEPT]]:
// CHECK: obelisk_sim.container.export_bitstream %[[SOURCE]]
// CHECK: ^[[REJECT]]:
// CHECK: bit-stream cast source and destination widths differ

// -----

!values = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<8, false, true, 7 : 0, logic>, false>
!packed = !obelisk.integral<24, false, true, 23 : 0, logic>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 10 : i64,
      sym_name = "top_implicit_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 11 : i64, sym_name = "implicit_root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 12 : i64,
        referenced_path = "top", referenced_symbol = @top_implicit_def,
        sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 13 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.source",
            lifetime = 1 : i32, name = "source", node_id = 14 : i64,
            semantic_type = !values, sym_name = "source"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result",
            lifetime = 1 : i32, name = "result", node_id = 15 : i64,
            semantic_type = !packed, sym_name = "result"} {
          // IEEE 1800-2023 6.24.3 requires an explicit bit-stream cast.
          // expected-error@+1 {{unsupported normalized conversion}}
          obelisk.sv.expression.conversion attributes {is_implicit = true,
              is_signed = false, node_id = 16 : i64,
              semantic_type = !packed} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 17 : i64, referenced_path = "top.source",
                referenced_symbol = @implicit_root::@top::@body::@source,
                semantic_type = !values} {}
          }
        }
      }
    }
  }
}

// CHECK-NOT: obelisk_sim.container.export_bitstream
