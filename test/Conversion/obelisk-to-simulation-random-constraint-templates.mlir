// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s

// IEEE 1800-2017 18.5.11: a static constraint block has one mode state shared
// by every instance.  Section 18.5.14.1: surviving base constraints have
// lower soft priority than derived constraints.  A derived override replaces
// the base body while retaining the effective block's stable mode identity.

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
    } {
      obelisk.sv.type.class_type attributes {
        bitstream_width = 32 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "B", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "B", node_id = 3 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.B>,
        sym_name = "s3.B"
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "B::x", name = "x", node_id = 4 : i64,
          rand_mode = 1 : i32,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s4.x"
        } {}
        obelisk.sv.symbol.constraint_block attributes {
          hierarchical_name = "B::a", name = "a", node_id = 5 : i64,
          sym_name = "s5.a"
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 6 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = true, node_id = 7 : i64
            } {
              obelisk.sv.expression.binary_op attributes {
                node_id = 8 : i64, operator_kind = 14 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 9 : i64, referenced_path = "B::x",
                  referenced_symbol = @s1.$root::@s2::@s3.B::@s4.x,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "0", node_id = 10 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
              }
            }
          }
        }
        obelisk.sv.symbol.constraint_block attributes {
          hierarchical_name = "B::shared", is_static, name = "shared",
          node_id = 11 : i64, sym_name = "s6.shared"
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 12 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = true, node_id = 13 : i64
            } {
              obelisk.sv.expression.binary_op attributes {
                node_id = 14 : i64, operator_kind = 16 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 15 : i64, referenced_path = "B::x",
                  referenced_symbol = @s1.$root::@s2::@s3.B::@s4.x,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "10", node_id = 16 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
              }
            }
          }
        }
      }
      obelisk.sv.type.class_type attributes {
        base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.B>,
        bitstream_width = 64 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "D", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "D", node_id = 17 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s7.D>,
        sym_name = "s7.D"
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "D::y", name = "y", node_id = 18 : i64,
          rand_mode = 1 : i32,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s8.y"
        } {}
        obelisk.sv.symbol.constraint_block attributes {
          hierarchical_name = "D::a", name = "a", node_id = 19 : i64,
          sym_name = "s9.a"
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 20 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = true, node_id = 21 : i64
            } {
              obelisk.sv.expression.binary_op attributes {
                node_id = 22 : i64, operator_kind = 16 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 23 : i64, referenced_path = "B::x",
                  referenced_symbol = @s1.$root::@s2::@s3.B::@s4.x,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "9", node_id = 24 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
              }
            }
          }
        }
        obelisk.sv.symbol.constraint_block attributes {
          hierarchical_name = "D::z", name = "z", node_id = 25 : i64,
          sym_name = "s10.z"
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 26 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = true, node_id = 27 : i64
            } {
              obelisk.sv.expression.binary_op attributes {
                node_id = 28 : i64, operator_kind = 14 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 29 : i64, referenced_path = "D::y",
                  referenced_symbol = @s1.$root::@s2::@s7.D::@s8.y,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.named_value attributes {
                  node_id = 30 : i64, referenced_path = "B::x",
                  referenced_symbol = @s1.$root::@s2::@s3.B::@s4.x,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
              }
            }
          }
        }
      }
      // Four-state symbolic values are deliberately not represented by the
      // current one-plane template IR.  The whole class must remain on the
      // legacy path without emitting a diagnostic or a partial template.
      obelisk.sv.type.class_type attributes {
        bitstream_width = 1 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "F", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "F", node_id = 31 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s11.F>,
        sym_name = "s11.F"
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "F::flag", name = "flag", node_id = 32 : i64,
          rand_mode = 1 : i32,
          semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
          sym_name = "s12.flag"
        } {}
        obelisk.sv.symbol.constraint_block attributes {
          hierarchical_name = "F::known", name = "known", node_id = 33 : i64,
          sym_name = "s13.known"
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 34 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = false, node_id = 35 : i64
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 36 : i64, referenced_path = "F::flag",
                referenced_symbol = @s1.$root::@s2::@s11.F::@s12.flag,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
              } {}
            }
          }
        }
      }
      // IEEE 1800-2017 11.8.1/11.8.2: an unsigned operand makes the
      // relational common type unsigned, including extension of its peer.
      obelisk.sv.type.class_type attributes {
        bitstream_width = 40 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "M", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "M", node_id = 37 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s14.M>,
        sym_name = "s14.M"
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "M::a", name = "a", node_id = 38 : i64,
          rand_mode = 1 : i32,
          semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>,
          sym_name = "s15.a"
        } {}
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "M::b", name = "b", node_id = 39 : i64,
          rand_mode = 1 : i32,
          semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>,
          sym_name = "s16.b"
        } {}
        obelisk.sv.symbol.constraint_block attributes {
          hierarchical_name = "M::mixed", name = "mixed", node_id = 40 : i64,
          sym_name = "s17.mixed"
        } {
          obelisk.sv.constraint.list attributes {
            item_count = 1 : i64, node_id = 41 : i64
          } {
            obelisk.sv.constraint.expression attributes {
              is_soft = false, node_id = 42 : i64
            } {
              obelisk.sv.expression.binary_op attributes {
                node_id = 43 : i64, operator_kind = 16 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.conversion attributes {
                  node_id = 44 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 45 : i64, referenced_path = "M::a",
                    referenced_symbol = @s1.$root::@s2::@s14.M::@s15.a,
                    semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>
                  } {}
                }
                obelisk.sv.expression.named_value attributes {
                  node_id = 46 : i64, referenced_path = "M::b",
                  referenced_symbol = @s1.$root::@s2::@s14.M::@s16.b,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>
                } {}
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.class.decl @[[BASE:[A-Za-z0-9_.$]+]] id 1
// CHECK-SAME: random_constraint_template = @[[BASE_TEMPLATE:[A-Za-z0-9_.$]+]]
// CHECK: obelisk_sim.class.decl @[[DERIVED:[A-Za-z0-9_.$]+]] id 2 extends @[[BASE]]
// CHECK-SAME: random_constraint_template = @[[DERIVED_TEMPLATE:[A-Za-z0-9_.$]+]]
// CHECK: obelisk_sim.class.decl @[[FOUR_STATE:[A-Za-z0-9_.$]+]] id 3 {
// CHECK-NOT: random_constraint_template
// CHECK: obelisk_sim.class.field {{.*}} of @[[FOUR_STATE]]
// CHECK: obelisk_sim.class.decl @[[MIXED:[A-Za-z0-9_.$]+]] id 4 {
// CHECK-SAME: random_constraint_template = @[[MIXED_TEMPLATE:[A-Za-z0-9_.$]+]]
// CHECK: obelisk_sim.storage.decl [[STATIC_MODE:[0-9]+]] {{.*}} : i64 design hierarchy "B::shared.__obelisk_constraint_mode"

// CHECK: obelisk_sim.random.constraint_template @[[BASE_TEMPLATE]] of @[[BASE]]
// CHECK-SAME: constraint_blocks = [#obelisk_sim.random_constraint_block_reference<kind = object_block, index = 0 : i32>, #obelisk_sim.random_constraint_block_reference<kind = storage, storage = [[STATIC_MODE]] : i64>]
// CHECK: obelisk_sim.random.soft_constraint %{{.*}} block 0 priority 0
// CHECK: obelisk_sim.random.soft_constraint %{{.*}} block 1 priority 1

// CHECK: obelisk_sim.random.constraint_template @[[DERIVED_TEMPLATE]] of @[[DERIVED]]
// CHECK-SAME: constraint_blocks = [#obelisk_sim.random_constraint_block_reference<kind = object_block, index = 0 : i32>, #obelisk_sim.random_constraint_block_reference<kind = storage, storage = [[STATIC_MODE]] : i64>, #obelisk_sim.random_constraint_block_reference<kind = object_block, index = 2 : i32>]
// CHECK-SAME: references = [#obelisk_sim.random_value_reference<kind = object_field, target = @{{[^,]+}}, low = 0, width = 32>, #obelisk_sim.random_value_reference<kind = object_field, target = @{{[^,]+}}, low = 0, width = 32>]
// CHECK: %[[BASE_X:.*]] = obelisk_sim.random.constraint_value 0 : i32
// CHECK: %[[TEN:.*]] = arith.constant 10 : i32
// CHECK: %[[BASE_STATIC:.*]] = arith.cmpi slt, %[[BASE_X]], %[[TEN]] : i32
// CHECK: obelisk_sim.random.soft_constraint %[[BASE_STATIC]] block 1 priority 0
// CHECK: %[[OVERRIDE_X:.*]] = obelisk_sim.random.constraint_value 0 : i32
// CHECK: %[[NINE:.*]] = arith.constant 9 : i32
// CHECK: %[[DERIVED_OVERRIDE:.*]] = arith.cmpi slt, %[[OVERRIDE_X]], %[[NINE]] : i32
// CHECK: obelisk_sim.random.soft_constraint %[[DERIVED_OVERRIDE]] block 0 priority 1
// CHECK: %[[Y:.*]] = obelisk_sim.random.constraint_value 1 : i32
// CHECK: %[[X:.*]] = obelisk_sim.random.constraint_value 0 : i32
// CHECK: %[[DERIVED_LOCAL:.*]] = arith.cmpi sgt, %[[Y]], %[[X]] : i32
// CHECK: obelisk_sim.random.soft_constraint %[[DERIVED_LOCAL]] block 2 priority 2
// CHECK-NOT: obelisk_sim.random.soft_constraint {{.*}} priority 3
// CHECK: obelisk_sim.random.constraint_template @[[MIXED_TEMPLATE]] of @[[MIXED]]
// CHECK: %[[SIGNED_BYTE:.*]] = obelisk_sim.random.constraint_value 0 : i8
// CHECK: %[[UNSIGNED_WIDE:.*]] = arith.extui %[[SIGNED_BYTE]] : i8 to i32
// CHECK: %[[UNSIGNED_INT:.*]] = obelisk_sim.random.constraint_value 1 : i32
// CHECK: %[[MIXED_LESS:.*]] = arith.cmpi ult, %[[UNSIGNED_WIDE]], %[[UNSIGNED_INT]] : i32
// CHECK: obelisk_sim.random.hard_constraint %[[MIXED_LESS]] block 0
// CHECK: obelisk_sim.code_unit.decl
