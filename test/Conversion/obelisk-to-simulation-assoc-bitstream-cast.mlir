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
        obelisk.sv.symbol.variable attributes {
            hierarchical_name = "top.streamed", lifetime = 1 : i32,
            name = "streamed", node_id = 8 : i64,
            semantic_type = !packed, sym_name = "streamed"} {
          obelisk.sv.expression.streaming attributes {
              bitstream_width = 0 : i64, is_fixed_size = false,
              is_signed = false, node_id = 18 : i64,
              semantic_type = !obelisk.void, slice_size = 0 : i64,
              stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 19 : i64, referenced_path = "top.source",
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
// IEEE 1800-2023 11.4.14 streams typed associative values in sorted-key order.
// CHECK: obelisk_sim.assoc.traverse
// CHECK: obelisk_sim.assoc.read

// -----

!wild = !obelisk.assoc<!obelisk.untyped, !obelisk.integral<8, true, false, 7 : 0, byte>, true>
!packed = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "wild", name = "wild", node_id = 20 : i64,
      sym_name = "wild_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 21 : i64, sym_name = "wild_root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "wild",
        is_uninstantiated = false, name = "wild", node_id = 22 : i64,
        referenced_path = "wild", referenced_symbol = @wild_def,
        sym_name = "wild"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "wild",
          name = "wild", node_id = 23 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wild.source",
            lifetime = 1 : i32, name = "source", node_id = 24 : i64,
            semantic_type = !wild, sym_name = "source"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wild.result",
            lifetime = 1 : i32, name = "result", node_id = 25 : i64,
            semantic_type = !packed, sym_name = "result"} {}
        obelisk.sv.symbol.procedural_block attributes {
            hierarchical_name = "wild", node_id = 26 : i64,
            procedure_kind = 0 : i32, sym_name = "initial",
            time_precision_fs = 1000000 : i64,
            time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 27 : i64} {
            obelisk.sv.statement.list attributes {node_id = 28 : i64} {
              obelisk.sv.statement.expression_statement attributes {
                  node_id = 29 : i64} {
                obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 30 : i64,
                    semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
                  obelisk.sv.expression.element_select attributes {
                      is_signed = true, node_id = 31 : i64,
                      semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
                    obelisk.sv.expression.named_value attributes {
                        is_signed = false, node_id = 32 : i64,
                        referenced_path = "wild.source",
                        referenced_symbol = @wild_root::@wild::@body::@source,
                        semantic_type = !wild} {}
                    obelisk.sv.expression.integer_literal attributes {
                        constant_value = "16'sd256", is_signed = true,
                        node_id = 33 : i64,
                        semantic_type = !obelisk.integral<16, true, false, 15 : 0, shortint>} {}
                  }
                  obelisk.sv.expression.integer_literal attributes {
                      constant_value = "8'sd17", is_signed = true,
                      node_id = 34 : i64,
                      semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {
                  node_id = 35 : i64} {
                obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = false,
                    node_id = 36 : i64, semantic_type = !packed} {
                  obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 37 : i64,
                      referenced_path = "wild.result",
                      referenced_symbol = @wild_root::@wild::@body::@result,
                      semantic_type = !packed} {}
                  obelisk.sv.expression.conversion attributes {
                      is_implicit = false, is_signed = false,
                      node_id = 38 : i64, semantic_type = !packed} {
                    obelisk.sv.expression.named_value attributes {
                        is_signed = false, node_id = 39 : i64,
                        referenced_path = "wild.source",
                        referenced_symbol = @wild_root::@wild::@body::@source,
                        semantic_type = !wild} {}
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// Wildcard indices lower to a boxed integral key; bit-stream export reuses the
// runtime's deterministic key ordering and remains one bulk operation.
// CHECK: obelisk_sim.container.create
// CHECK-SAME: bit_width = 16 : i64{{.*}}element_flags = 2 : i32
// CHECK: obelisk_sim.box.pack
// CHECK: obelisk_sim.assoc.create {{.*}}key_kind = 6 : i32
// CHECK: obelisk_sim.assoc.write
// CHECK: obelisk_sim.container.export_bitstream

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
