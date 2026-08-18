// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 8.3 makes a class property a variable of the object, so one
// member of a structure property (7.2) or one element of an array property
// (7.4.6) is an assignment target like any other variable's part. The object's
// storage is managed and exposes no stable interior reference, so writing one
// part of an aggregate property reads the whole property, replaces the named
// element, and stores it back — the treatment a queue or dynamic-array element
// already gets.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "class_property_subwrite", name = "class_property_subwrite", node_id = 0 : i64, sym_name = "s0.class_property_subwrite"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "class_property_subwrite", is_uninstantiated = false, name = "class_property_subwrite", node_id = 3 : i64, referenced_path = "class_property_subwrite", referenced_symbol = @s0.class_property_subwrite, sym_name = "s3.class_property_subwrite"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "class_property_subwrite", name = "class_property_subwrite", node_id = 4 : i64, sym_name = "s4.class_property_subwrite", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.type.type_alias attributes {hierarchical_name = "class_property_subwrite.pair_t", name = "pair_t", node_id = 5 : i64, semantic_type = !obelisk.source_aggregate<"class_property_subwrite", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fst", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "snd", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s5.pair_t"} {
        }
        obelisk.sv.type.class_type attributes {bitstream_width = 128 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "class_property_subwrite.Cls", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Cls", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_subwrite::@s6.Cls>, sym_name = "s6.Cls", this_variable_path = "class_property_subwrite.Cls::this", this_variable_symbol = @s1.$root::@s3.class_property_subwrite::@s4.class_property_subwrite::@s6.Cls::@s23.this} {
          obelisk.sv.symbol.class_property attributes {hierarchical_name = "class_property_subwrite.Cls::p", name = "p", node_id = 7 : i64, semantic_type = !obelisk.source_aggregate<"class_property_subwrite", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fst", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "snd", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s7.p"} {
          }
          obelisk.sv.symbol.class_property attributes {hierarchical_name = "class_property_subwrite.Cls::arr", name = "arr", node_id = 8 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s8.arr"} {
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "class_property_subwrite.Cls::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 9 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s9.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "class_property_subwrite.Cls::pre_randomize", is_builtin, name = "pre_randomize", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 12 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "class_property_subwrite.Cls::post_randomize", is_builtin, name = "post_randomize", node_id = 13 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 14 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "class_property_subwrite.Cls::get_randstate", is_builtin, name = "get_randstate", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s12.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 16 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "class_property_subwrite.Cls::set_randstate", is_builtin, name = "set_randstate", node_id = 17 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 18 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "class_property_subwrite.Cls::set_randstate.state", name = "state", node_id = 19 : i64, semantic_type = !obelisk.string, sym_name = "s14.state"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "class_property_subwrite.Cls::srandom", is_builtin, name = "srandom", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 21 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "class_property_subwrite.Cls::srandom.seed", name = "seed", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.seed"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "class_property_subwrite.Cls::rand_mode", is_builtin, name = "rand_mode", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 24 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "class_property_subwrite.Cls::rand_mode.on_ff", name = "on_ff", node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
            }
          }
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "class_property_subwrite.Cls::constraint_mode", is_builtin, name = "constraint_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s19.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 27 : i64} {
            }
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "class_property_subwrite.Cls::constraint_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s20.on_ff"} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "class_property_subwrite.Cls::this", is_compiler_generated, is_const, name = "this", node_id = 47 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_subwrite::@s6.Cls>, sym_name = "s23.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "class_property_subwrite.c", lifetime = 1 : i32, name = "c", node_id = 29 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_subwrite::@s6.Cls>, sym_name = "s21.c"} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 30 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_subwrite::@s6.Cls>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "class_property_subwrite", node_id = 31 : i64, procedure_kind = 0 : i32, sym_name = "s22", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 32 : i64} {
            obelisk.sv.statement.list attributes {node_id = 33 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, is_signed = true, member_name = "fst", node_id = 36 : i64, packed_offset = 0 : i64, referenced_path = "class_property_subwrite.fst", referenced_symbol = @s1.$root::@s3.class_property_subwrite::@s4.class_property_subwrite::@s24::@s25.fst, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "p", node_id = 37 : i64, referenced_path = "class_property_subwrite.Cls::p", referenced_symbol = @s1.$root::@s3.class_property_subwrite::@s4.class_property_subwrite::@s6.Cls::@s7.p, semantic_type = !obelisk.source_aggregate<"class_property_subwrite", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fst", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "snd", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "class_property_subwrite.c", referenced_symbol = @s1.$root::@s3.class_property_subwrite::@s4.class_property_subwrite::@s21.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_subwrite::@s6.Cls>} {
                      }
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 40 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.element_select attributes {is_signed = true, node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "arr", node_id = 43 : i64, referenced_path = "class_property_subwrite.Cls::arr", referenced_symbol = @s1.$root::@s3.class_property_subwrite::@s4.class_property_subwrite::@s6.Cls::@s8.arr, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "class_property_subwrite.c", referenced_symbol = @s1.$root::@s3.class_property_subwrite::@s4.class_property_subwrite::@s21.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_subwrite::@s6.Cls>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.type.unpacked_struct_type attributes {hierarchical_name = "class_property_subwrite", node_id = 48 : i64, semantic_type = !obelisk.source_aggregate<"class_property_subwrite", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fst", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "snd", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s24"} {
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 0 : i64, hierarchical_name = "class_property_subwrite.fst", name = "fst", node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s25.fst"} {
          }
          obelisk.sv.symbol.field attributes {bit_offset = 32 : i64, field_index = 1 : i64, hierarchical_name = "class_property_subwrite.snd", name = "snd", node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s26.snd"} {
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// `c.p.fst = 5` rebuilds the struct property around its first member.
// CHECK:      %[[FIELD:.*]] = obelisk_sim.class.field_ref {{.*}}@{{.*}}p
// CHECK:      %[[STRUCT:.*]] = obelisk_sim.managed.load %[[FIELD]]
// CHECK:      %[[UPDATED:.*]] = obelisk_sim.aggregate.insert {{.*}} into %[[STRUCT]][0]
// CHECK:      obelisk_sim.managed.store %[[UPDATED]] to %[[FIELD]]
// `c.arr[1] = 7` rebuilds the array property around its second element.
// CHECK:      %[[ELEMENT:.*]] = obelisk_sim.class.field_ref {{.*}}@{{.*}}arr
// CHECK:      %[[ARRAY:.*]] = obelisk_sim.managed.load %[[ELEMENT]]
// CHECK:      %[[WRITTEN:.*]] = obelisk_sim.aggregate.insert {{.*}} into %[[ARRAY]][1]
// CHECK:      obelisk_sim.managed.store %[[WRITTEN]] to %[[ELEMENT]]
