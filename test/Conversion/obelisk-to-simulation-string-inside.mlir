// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
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
        sym_name = "s4.top", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.value", lifetime = 1 : i32,
          name = "value", node_id = 5 : i64,
          semantic_type = !obelisk.string, sym_name = "s5.value"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.found", lifetime = 1 : i32,
          name = "found", node_id = 6 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "s6.found"
        } {}
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top", node_id = 7 : i64,
          procedure_kind = 0 : i32, sym_name = "s7",
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 8 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false,
              node_id = 9 : i64,
              semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 10 : i64,
                referenced_path = "top.found",
                referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.found,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {}
              obelisk.sv.expression.conversion attributes {
                is_signed = false, node_id = 11 : i64,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.inside attributes {
                  is_signed = false, item_count = 3 : i64, node_id = 12 : i64,
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 13 : i64,
                    referenced_path = "top.value",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value,
                    semantic_type = !obelisk.string
                  } {}
                  obelisk.sv.expression.conversion attributes {
                    is_signed = false, node_id = 14 : i64,
                    semantic_type = !obelisk.string
                  } {
                    obelisk.sv.expression.string_literal attributes {
                      constant_value = "RO", is_signed = false,
                      node_id = 15 : i64,
                      semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                    } {}
                  }
                  obelisk.sv.expression.conversion attributes {
                    is_signed = false, node_id = 16 : i64,
                    semantic_type = !obelisk.string
                  } {
                    obelisk.sv.expression.string_literal attributes {
                      constant_value = "RC", is_signed = false,
                      node_id = 17 : i64,
                      semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                    } {}
                  }
                  obelisk.sv.expression.value_range attributes {
                    is_signed = false, node_id = 18 : i64,
                    range_kind = 0 : i32, semantic_type = !obelisk.void
                  } {
                    obelisk.sv.expression.conversion attributes {
                      is_signed = false, node_id = 19 : i64,
                      semantic_type = !obelisk.string
                    } {
                      obelisk.sv.expression.string_literal attributes {
                        constant_value = "A", folded_constant = "8'd65",
                        is_signed = false, node_id = 20 : i64,
                        semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                      } {}
                    }
                    obelisk.sv.expression.conversion attributes {
                      is_signed = false, node_id = 21 : i64,
                      semantic_type = !obelisk.string
                    } {
                      obelisk.sv.expression.string_literal attributes {
                        constant_value = "Z", folded_constant = "8'd90",
                        is_signed = false, node_id = 22 : i64,
                        semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                      } {}
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 23 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false,
              node_id = 24 : i64,
              semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 25 : i64,
                referenced_path = "top.found",
                referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.found,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {}
              obelisk.sv.expression.conversion attributes {
                is_signed = false, node_id = 26 : i64,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.inside attributes {
                  is_signed = false, item_count = 2 : i64, node_id = 27 : i64,
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                } {
                  obelisk.sv.expression.conversion attributes {
                    is_signed = false, node_id = 28 : i64,
                    semantic_type = !obelisk.string
                  } {
                    obelisk.sv.expression.string_literal attributes {
                      constant_value = "RC", is_signed = false,
                      node_id = 29 : i64,
                      semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                    } {}
                  }
                  obelisk.sv.expression.conversion attributes {
                    is_signed = false, node_id = 30 : i64,
                    semantic_type = !obelisk.string
                  } {
                    obelisk.sv.expression.string_literal attributes {
                      constant_value = "RO", is_signed = false,
                      node_id = 31 : i64,
                      semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                    } {}
                  }
                  obelisk.sv.expression.conversion attributes {
                    is_signed = false, node_id = 32 : i64,
                    semantic_type = !obelisk.string
                  } {
                    obelisk.sv.expression.string_literal attributes {
                      constant_value = "RC", is_signed = false,
                      node_id = 33 : i64,
                      semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                    } {}
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

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-DAG: %[[TRUE:.*]] = arith.constant true
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK: %[[VALUE:.*]] = obelisk_sim.ref.load %{{.*}} : !obelisk_sim.ref<!obelisk_sim.string> -> !obelisk_sim.string
// CHECK: %[[RO:.*]] = obelisk_sim.string.literal "RO"
// CHECK: %[[RO_CMP:.*]] = obelisk_sim.string.compare %[[VALUE]], %[[RO]] case_insensitive = false
// CHECK: %[[RO_EQ:.*]] = arith.cmpi eq, %[[RO_CMP]], %[[ZERO]] : i32
// CHECK: %[[RC:.*]] = obelisk_sim.string.literal "RC"
// CHECK: %[[RC_CMP:.*]] = obelisk_sim.string.compare %[[VALUE]], %[[RC]] case_insensitive = false
// CHECK: %[[RC_EQ:.*]] = arith.cmpi eq, %[[RC_CMP]], %[[ZERO]] : i32
// CHECK: %[[A:.*]] = obelisk_sim.string.literal "A"
// CHECK: %[[LOWER_CMP:.*]] = obelisk_sim.string.compare %[[VALUE]], %[[A]] case_insensitive = false
// CHECK: %[[ABOVE:.*]] = arith.cmpi sge, %[[LOWER_CMP]], %[[ZERO]] : i32
// CHECK: %[[Z:.*]] = obelisk_sim.string.literal "Z"
// CHECK: %[[UPPER_CMP:.*]] = obelisk_sim.string.compare %[[VALUE]], %[[Z]] case_insensitive = false
// CHECK: %[[BELOW:.*]] = arith.cmpi sle, %[[UPPER_CMP]], %[[ZERO]] : i32
// CHECK: arith.andi %[[ABOVE]], %[[BELOW]] : i1
// CHECK: obelisk_sim.ref.store %{{.*}} to %{{.*}} : i1, !obelisk_sim.ref<i1>
// CHECK: obelisk_sim.string.literal "RC"
// CHECK: obelisk_sim.string.literal "RO"
// CHECK: obelisk_sim.string.literal "RC"
// CHECK-NOT: obelisk_sim.string.compare
// CHECK: obelisk_sim.ref.store %[[TRUE]] to %{{.*}} : i1, !obelisk_sim.ref<i1>
// CHECK-NOT: obelisk.sv.
