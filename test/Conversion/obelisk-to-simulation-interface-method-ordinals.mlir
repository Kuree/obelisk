// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64,
      sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [],
          generic_parameter_symbols = [], has_base_constructor_call = false,
          has_cycles = false, hierarchical_name = "I",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = true, is_uninstantiated = false, name = "I",
          node_id = 3 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.I>,
          sym_name = "s3.I", this_variable_path = "I::this",
          this_variable_symbol = @s1.$root::@s2::@s3.I::@s10.this} {
        obelisk.sv.symbol.method_prototype attributes {
            extern_implementation_count = 0 : i64,
            extern_implementation_paths = [], extern_implementation_symbols = [],
            hierarchical_name = "I::first", is_declared_virtual, is_pure,
            is_virtual, name = "first", node_id = 4 : i64,
            semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
            subroutine_kind = 0 : i32, subroutine_path = "I::first",
            subroutine_symbol = @s1.$root::@s2::@s3.I::@s4.first::@s5.first,
            sym_name = "s4.first"} {
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "I::first",
              is_declared_virtual, is_pure, is_virtual, name = "first",
              node_id = 5 : i64,
              prototype_path = "I::first",
              prototype_symbol = @s1.$root::@s2::@s3.I::@s4.first,
              semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
              subroutine_kind = 0 : i32, sym_name = "s5.first",
              time_precision_fs = 1000000 : i64,
              time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 6 : i64} {
            }
          }
        }
        obelisk.sv.symbol.method_prototype attributes {
            extern_implementation_count = 0 : i64,
            extern_implementation_paths = [], extern_implementation_symbols = [],
            hierarchical_name = "I::second", is_declared_virtual, is_pure,
            is_virtual, name = "second", node_id = 7 : i64,
            semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
            subroutine_kind = 0 : i32, subroutine_path = "I::second",
            subroutine_symbol = @s1.$root::@s2::@s3.I::@s7.second::@s8.second,
            sym_name = "s7.second"} {
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "I::second",
              is_declared_virtual, is_pure, is_virtual, name = "second",
              node_id = 8 : i64,
              prototype_path = "I::second",
              prototype_symbol = @s1.$root::@s2::@s3.I::@s7.second,
              semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
              subroutine_kind = 0 : i32, sym_name = "s8.second",
              time_precision_fs = 1000000 : i64,
              time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "I::this",
            is_compiler_generated, is_const, name = "this", node_id = 10 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.I>,
            sym_name = "s10.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64,
          declared_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s3.I>],
          generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false,
          hierarchical_name = "J",
          implemented_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s3.I>],
          is_abstract = false, is_final = false, is_interface = true,
          is_uninstantiated = false, name = "J", node_id = 11 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s11.J>,
          sym_name = "s11.J", this_variable_path = "J::this",
          this_variable_symbol = @s1.$root::@s2::@s11.J::@s16.this} {
        obelisk.sv.symbol.transparent_member attributes {
            hierarchical_name = "J::first", name = "first", node_id = 12 : i64,
            sym_name = "s12.first"} {
        }
        obelisk.sv.symbol.transparent_member attributes {
            hierarchical_name = "J::second", name = "second", node_id = 13 : i64,
            sym_name = "s13.second"} {
        }
        obelisk.sv.symbol.method_prototype attributes {
            extern_implementation_count = 0 : i64,
            extern_implementation_paths = [], extern_implementation_symbols = [],
            hierarchical_name = "J::third", is_declared_virtual, is_pure,
            is_virtual, name = "third", node_id = 14 : i64,
            semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
            subroutine_kind = 0 : i32, subroutine_path = "J::third",
            subroutine_symbol = @s1.$root::@s2::@s11.J::@s14.third::@s15.third,
            sym_name = "s14.third"} {
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "J::third",
              is_declared_virtual, is_pure, is_virtual, name = "third",
              node_id = 15 : i64, prototype_path = "J::third",
              prototype_symbol = @s1.$root::@s2::@s11.J::@s14.third,
              semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
              subroutine_kind = 0 : i32, sym_name = "s15.third",
              time_precision_fs = 1000000 : i64,
              time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 16 : i64} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "J::this",
            is_compiler_generated, is_const, name = "this", node_id = 17 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s11.J>,
            sym_name = "s16.this"} {
        }
      }
    }
  }
}

// CHECK: obelisk_sim.class.decl @__obelisk_class_s3_I
// CHECK: obelisk_sim.class.decl @__obelisk_class_s11_J
// CHECK-SAME: implements [@__obelisk_class_s3_I]
// CHECK: obelisk_sim.class.method @__obelisk_class_s3_I_method_0
// CHECK-SAME: slot 4294967295
// CHECK-SAME: interface_ordinal 0
// CHECK: obelisk_sim.class.method @__obelisk_class_s3_I_method_1
// CHECK-SAME: slot 4294967295
// CHECK-SAME: interface_ordinal 1
// CHECK: obelisk_sim.class.method @__obelisk_class_s11_J_method_0
// CHECK-SAME: slot 4294967295
// CHECK-SAME: interface_ordinal 0
