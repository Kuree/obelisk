// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
        hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.I attributes {bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [],
          generic_parameter_symbols = [], has_base_constructor_call = false,
          has_cycles = false, hierarchical_name = "I",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = true, is_uninstantiated = false, name = "I",
          node_id = 3 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.I>,
          this_variable_path = "I::this",
          this_variable_symbol = @s1.$root::@s2::@s3.I::@s10.this} {
        obelisk.sv.symbol.method_prototype @s4.first attributes {
            extern_implementation_count = 0 : i64,
            extern_implementation_paths = [], extern_implementation_symbols = [],
            hierarchical_name = "I::first", is_declared_virtual, is_pure,
            is_virtual, name = "first", node_id = 4 : i64,
            semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
            subroutine_kind = 0 : i32, subroutine_path = "I::first",
            subroutine_symbol = @s1.$root::@s2::@s3.I::@s4.first::@s5.first
        } {
          obelisk.sv.symbol.subroutine @s5.first attributes {hierarchical_name = "I::first",
              is_declared_virtual, is_pure, is_virtual, name = "first",
              node_id = 5 : i64,
              prototype_path = "I::first",
              prototype_symbol = @s1.$root::@s2::@s3.I::@s4.first,
              semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
              subroutine_kind = 0 : i32,
              time_precision_fs = 1000000 : i64,
              time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 6 : i64} {
            }
          }
        }
        obelisk.sv.symbol.method_prototype @s7.second attributes {
            extern_implementation_count = 0 : i64,
            extern_implementation_paths = [], extern_implementation_symbols = [],
            hierarchical_name = "I::second", is_declared_virtual, is_pure,
            is_virtual, name = "second", node_id = 7 : i64,
            semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
            subroutine_kind = 0 : i32, subroutine_path = "I::second",
            subroutine_symbol = @s1.$root::@s2::@s3.I::@s7.second::@s8.second
        } {
          obelisk.sv.symbol.subroutine @s8.second attributes {hierarchical_name = "I::second",
              is_declared_virtual, is_pure, is_virtual, name = "second",
              node_id = 8 : i64,
              prototype_path = "I::second",
              prototype_symbol = @s1.$root::@s2::@s3.I::@s7.second,
              semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
              subroutine_kind = 0 : i32,
              time_precision_fs = 1000000 : i64,
              time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
            }
          }
        }
        obelisk.sv.symbol.variable @s10.this attributes {hierarchical_name = "I::this",
            is_compiler_generated, is_const, name = "this", node_id = 10 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.I>
        } {
        }
      }
      obelisk.sv.type.class_type @s11.J attributes {bitstream_width = 0 : i64,
          declared_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s3.I>],
          generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false,
          hierarchical_name = "J",
          implemented_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s3.I>],
          is_abstract = false, is_final = false, is_interface = true,
          is_uninstantiated = false, name = "J", node_id = 11 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s11.J>,
          this_variable_path = "J::this",
          this_variable_symbol = @s1.$root::@s2::@s11.J::@s16.this} {
        obelisk.sv.symbol.transparent_member @s12.first attributes {
            hierarchical_name = "J::first", name = "first", node_id = 12 : i64
        } {
        }
        obelisk.sv.symbol.transparent_member @s13.second attributes {
            hierarchical_name = "J::second", name = "second", node_id = 13 : i64
        } {
        }
        obelisk.sv.symbol.method_prototype @s14.third attributes {
            extern_implementation_count = 0 : i64,
            extern_implementation_paths = [], extern_implementation_symbols = [],
            hierarchical_name = "J::third", is_declared_virtual, is_pure,
            is_virtual, name = "third", node_id = 14 : i64,
            semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
            subroutine_kind = 0 : i32, subroutine_path = "J::third",
            subroutine_symbol = @s1.$root::@s2::@s11.J::@s14.third::@s15.third
        } {
          obelisk.sv.symbol.subroutine @s15.third attributes {hierarchical_name = "J::third",
              is_declared_virtual, is_pure, is_virtual, name = "third",
              node_id = 15 : i64, prototype_path = "J::third",
              prototype_symbol = @s1.$root::@s2::@s11.J::@s14.third,
              semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
              subroutine_kind = 0 : i32,
              time_precision_fs = 1000000 : i64,
              time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 16 : i64} {
            }
          }
        }
        obelisk.sv.symbol.variable @s16.this attributes {hierarchical_name = "J::this",
            is_compiler_generated, is_const, name = "this", node_id = 17 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s11.J>
        } {
        }
      }
      // Pure virtual methods commonly use a prototype wrapper. An extern
      // concrete override carries override_symbol on that wrapper rather than
      // on the nested executable subroutine.
      obelisk.sv.type.class_type @s20.Base attributes {bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [],
          generic_parameter_symbols = [], has_base_constructor_call = false,
          has_cycles = false, hierarchical_name = "Base",
          implemented_interfaces = [], is_abstract = true, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "Base",
          node_id = 20 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.Base>,
          this_variable_path = "Base::this",
          this_variable_symbol = @s1.$root::@s2::@s20.Base::@s24.this} {
        obelisk.sv.symbol.method_prototype @s21.f attributes {
            extern_implementation_count = 0 : i64,
            extern_implementation_paths = [], extern_implementation_symbols = [],
            hierarchical_name = "Base::f", is_declared_virtual, is_pure,
            is_virtual, name = "f", node_id = 21 : i64,
            semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
            subroutine_kind = 0 : i32, subroutine_path = "Base::f",
            subroutine_symbol = @s1.$root::@s2::@s20.Base::@s21.f::@s22.f
        } {
          obelisk.sv.symbol.subroutine @s22.f attributes {hierarchical_name = "Base::f",
              is_declared_virtual, is_pure, is_virtual, name = "f",
              node_id = 22 : i64, prototype_path = "Base::f",
              prototype_symbol = @s1.$root::@s2::@s20.Base::@s21.f,
              semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
              subroutine_kind = 0 : i32,
              time_precision_fs = 1000000 : i64,
              time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 23 : i64} {
            }
          }
        }
        obelisk.sv.symbol.variable @s24.this attributes {hierarchical_name = "Base::this",
            is_compiler_generated, is_const, name = "this", node_id = 24 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.Base>
        } {
        }
      }
      obelisk.sv.type.class_type @s25.Derived attributes {
          base_class = !obelisk.class_handle<@s1.$root::@s2::@s20.Base>,
          bitstream_width = 0 : i64, declared_interfaces = [],
          generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false,
          hierarchical_name = "Derived", implemented_interfaces = [],
          is_abstract = false, is_final = false, is_interface = false,
          is_uninstantiated = false, name = "Derived", node_id = 25 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s25.Derived>,
          this_variable_path = "Derived::this",
          this_variable_symbol = @s1.$root::@s2::@s25.Derived::@s29.this} {
        obelisk.sv.symbol.method_prototype @s26.f attributes {
            extern_implementation_count = 0 : i64,
            extern_implementation_paths = [], extern_implementation_symbols = [],
            hierarchical_name = "Derived::f", is_declared_virtual, is_virtual,
            name = "f", node_id = 26 : i64, override_path = "Base::f",
            override_symbol = @s1.$root::@s2::@s20.Base::@s21.f::@s22.f,
            semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
            subroutine_kind = 0 : i32, subroutine_path = "Derived::f",
            subroutine_symbol = @s1.$root::@s2::@s25.Derived::@s26.f::@s27.f
        } {
          obelisk.sv.symbol.subroutine @s27.f attributes {
              hierarchical_name = "Derived::f", is_declared_virtual,
              is_virtual, name = "f", node_id = 27 : i64,
              prototype_path = "Derived::f",
              prototype_symbol = @s1.$root::@s2::@s25.Derived::@s26.f,
              semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
              subroutine_kind = 0 : i32,
              this_variable_path = "Derived::f.this",
              this_variable_symbol = @s1.$root::@s2::@s25.Derived::@s26.f::@s27.f::@s30.this,
              time_precision_fs = 1000000 : i64,
              time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 28 : i64} {
            }
            obelisk.sv.symbol.variable @s30.this attributes {
                hierarchical_name = "Derived::f.this", is_compiler_generated,
                is_const, name = "this", node_id = 30 : i64,
                semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s25.Derived>
            } {
            }
          }
        }
        obelisk.sv.symbol.variable @s29.this attributes {hierarchical_name = "Derived::this",
            is_compiler_generated, is_const, name = "this", node_id = 29 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s25.Derived>
        } {
        }
      }
    }
  }
}

// CHECK: simulation.class.decl @__obelisk_class_s3_I
// CHECK: simulation.class.decl @__obelisk_class_s11_J
// CHECK-SAME: implements [@__obelisk_class_s3_I]
// CHECK: simulation.class.method @__obelisk_class_s20_Base_method_0
// CHECK-SAME: slot 0
// CHECK-SAME: is_pure = true
// CHECK: simulation.class.method @__obelisk_class_s25_Derived_method_0
// CHECK-SAME: slot 0
// CHECK-SAME: is_pure = false
// CHECK: simulation.class.method @__obelisk_class_s3_I_method_0
// CHECK-SAME: slot 4294967295
// CHECK-SAME: interface_ordinal 0
// CHECK: simulation.class.method @__obelisk_class_s3_I_method_1
// CHECK-SAME: slot 4294967295
// CHECK-SAME: interface_ordinal 1
// CHECK: simulation.class.method @__obelisk_class_s11_J_method_0
// CHECK-SAME: slot 4294967295
// CHECK-SAME: interface_ordinal 0
