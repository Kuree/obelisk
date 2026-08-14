// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64,
    sym_name = "s0.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 1 : i64, sym_name = "s1"
    } {
      obelisk.sv.type.class_type attributes {
        bitstream_width = 32 : i64, constructor_path = "callee::new",
        constructor_symbol = @s0.$root::@s1::@s2.callee::@s18.new,
        declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "callee", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "callee", node_id = 2 : i64,
        semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>,
        sym_name = "s2.callee", this_variable_path = "callee::this",
        this_variable_symbol = @s0.$root::@s1::@s2.callee::@s11.this
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "callee::value", name = "value",
          node_id = 3 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s3.value"
        } {}
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "callee::default_value", name = "default_value",
          node_id = 4 : i64,
          return_variable_path = "callee::default_value.default_value",
          return_variable_symbol = @s0.$root::@s1::@s2.callee::@s4.default_value::@s5.default_value,
          semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
          subroutine_kind = 0 : i32, sym_name = "s4.default_value",
          this_variable_path = "callee::default_value.this",
          this_variable_symbol = @s0.$root::@s1::@s2.callee::@s4.default_value::@s6.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.return attributes {node_id = 5 : i64} {
            obelisk.sv.expression.named_value attributes {
              is_signed = true, node_id = 6 : i64,
              referenced_path = "callee::value",
              referenced_symbol = @s0.$root::@s1::@s2.callee::@s3.value,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {}
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "callee::default_value.default_value",
            is_compiler_generated, name = "default_value", node_id = 7 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s5.default_value"
          } {}
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "callee::default_value.this",
            is_compiler_generated, is_const, name = "this", node_id = 8 : i64,
            semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>,
            sym_name = "s6.this"
          } {}
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "callee::read", name = "read", node_id = 9 : i64,
          return_variable_path = "callee::read.read",
          return_variable_symbol = @s0.$root::@s1::@s2.callee::@s7.read::@s9.read,
          semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
          subroutine_kind = 0 : i32, sym_name = "s7.read",
          this_variable_path = "callee::read.this",
          this_variable_symbol = @s0.$root::@s1::@s2.callee::@s7.read::@s10.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.return attributes {node_id = 10 : i64} {
            obelisk.sv.expression.named_value attributes {
              is_signed = true, node_id = 11 : i64,
              referenced_path = "callee::read.argument",
              referenced_symbol = @s0.$root::@s1::@s2.callee::@s7.read::@s8.argument,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {}
          }
          obelisk.sv.symbol.formal_argument attributes {
            direction = 0 : i32, hierarchical_name = "callee::read.argument",
            name = "argument", node_id = 12 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s8.argument"
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 0 : i64, callee_name = "default_value",
              constraint_restrictions = [], defaulted_arguments = array<i64>,
              has_inline_constraints = false, has_iterator_expression = false,
              has_output_arguments = false, has_this_class = false,
              is_signed = true, is_super_class = false, is_system_call = false,
              node_id = 13 : i64,
              referenced_path = "callee::default_value",
              referenced_symbol = @s0.$root::@s1::@s2.callee::@s4.default_value,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
              subroutine_kind = 0 : i32
            } {}
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "callee::read.read", is_compiler_generated,
            name = "read", node_id = 14 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s9.read"
          } {}
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "callee::read.this", is_compiler_generated,
            is_const, name = "this", node_id = 15 : i64,
            semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>,
            sym_name = "s10.this"
          } {}
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "callee::new", is_constructor, name = "new",
          node_id = 27 : i64,
          semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>,
          subroutine_kind = 0 : i32, sym_name = "s18.new",
          this_variable_path = "callee::new.this",
          this_variable_symbol = @s0.$root::@s1::@s2.callee::@s18.new::@s20.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.symbol.formal_argument attributes {
            direction = 0 : i32, hierarchical_name = "callee::new.argument",
            name = "argument", node_id = 28 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s19.argument"
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 0 : i64, callee_name = "default_value",
              constraint_restrictions = [], defaulted_arguments = array<i64>,
              has_inline_constraints = false, has_iterator_expression = false,
              has_output_arguments = false, has_this_class = false,
              is_signed = true, is_super_class = false, is_system_call = false,
              node_id = 29 : i64,
              referenced_path = "callee::default_value",
              referenced_symbol = @s0.$root::@s1::@s2.callee::@s4.default_value,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
              subroutine_kind = 0 : i32
            } {}
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "callee::new.this", is_compiler_generated,
            is_const, name = "this", node_id = 30 : i64,
            semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>,
            sym_name = "s20.this"
          } {}
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "callee::this", is_compiler_generated,
          is_const, name = "this", node_id = 16 : i64,
          semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>,
          sym_name = "s11.this"
        } {}
      }
      obelisk.sv.type.class_type attributes {
        bitstream_width = 64 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "caller", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "caller", node_id = 17 : i64,
        semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s12.caller>,
        sym_name = "s12.caller", this_variable_path = "caller::this",
        this_variable_symbol = @s0.$root::@s1::@s12.caller::@s17.this
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "caller::target", name = "target",
          node_id = 18 : i64,
          semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>,
          sym_name = "s13.target"
        } {}
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "caller::invoke", name = "invoke",
          node_id = 19 : i64, return_variable_path = "caller::invoke.invoke",
          return_variable_symbol = @s0.$root::@s1::@s12.caller::@s14.invoke::@s15.invoke,
          semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
          subroutine_kind = 0 : i32, sym_name = "s14.invoke",
          this_variable_path = "caller::invoke.this",
          this_variable_symbol = @s0.$root::@s1::@s12.caller::@s14.invoke::@s16.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.return attributes {node_id = 20 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64, callee_name = "read",
              constraint_restrictions = [],
              defaulted_arguments = array<i64: 1>,
              has_inline_constraints = false, has_iterator_expression = false,
              has_output_arguments = false, has_this_class = true,
              is_signed = true, is_super_class = false, is_system_call = false,
              node_id = 21 : i64, referenced_path = "callee::read",
              referenced_symbol = @s0.$root::@s1::@s2.callee::@s7.read,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
              subroutine_kind = 0 : i32
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 22 : i64,
                referenced_path = "caller::target",
                referenced_symbol = @s0.$root::@s1::@s12.caller::@s13.target,
                semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>
              } {}
              obelisk.sv.expression.call attributes {
                argument_count = 0 : i64, callee_name = "default_value",
                constraint_restrictions = [], defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false, has_this_class = false,
                is_signed = true, is_super_class = false,
                is_system_call = false, node_id = 23 : i64,
                referenced_path = "callee::default_value",
                referenced_symbol = @s0.$root::@s1::@s2.callee::@s4.default_value,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                subroutine_kind = 0 : i32
              } {}
            }
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "caller::invoke.invoke", is_compiler_generated,
            name = "invoke", node_id = 24 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s15.invoke"
          } {}
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "caller::invoke.this", is_compiler_generated,
            is_const, name = "this", node_id = 25 : i64,
            semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s12.caller>,
            sym_name = "s16.this"
          } {}
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "caller::make", name = "make", node_id = 31 : i64,
          return_variable_path = "caller::make.make",
          return_variable_symbol = @s0.$root::@s1::@s12.caller::@s21.make::@s22.make,
          semantic_type = !obelisk.subroutine<() -> !obelisk.class_handle<@s0.$root::@s1::@s2.callee>, false>,
          subroutine_kind = 0 : i32, sym_name = "s21.make",
          this_variable_path = "caller::make.this",
          this_variable_symbol = @s0.$root::@s1::@s12.caller::@s21.make::@s23.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.return attributes {node_id = 32 : i64} {
            obelisk.sv.expression.new_class attributes {
              is_signed = false, is_super_class = false, node_id = 33 : i64,
              semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64, callee_name = "new",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 1>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false, has_this_class = false,
                is_signed = false, is_super_class = false,
                is_system_call = false, node_id = 34 : i64,
                referenced_path = "callee::new",
                referenced_symbol = @s0.$root::@s1::@s2.callee::@s18.new,
                semantic_type = !obelisk.void, subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64, callee_name = "default_value",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_signed = true, is_super_class = false,
                  is_system_call = false, node_id = 35 : i64,
                  referenced_path = "callee::default_value",
                  referenced_symbol = @s0.$root::@s1::@s2.callee::@s4.default_value,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 0 : i32
                } {}
              }
            }
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "caller::make.make", is_compiler_generated,
            name = "make", node_id = 36 : i64,
            semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.callee>,
            sym_name = "s22.make"
          } {}
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "caller::make.this", is_compiler_generated,
            is_const, name = "this", node_id = 37 : i64,
            semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s12.caller>,
            sym_name = "s23.this"
          } {}
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "caller::this", is_compiler_generated,
          is_const, name = "this", node_id = 26 : i64,
          semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s12.caller>,
          sym_name = "s17.this"
        } {}
      }
    }
  }
}

// CHECK: obelisk_sim.func private @[[DEFAULT:unit_[0-9]+]]({{.*}}!obelisk_sim.class_handle<@[[CALLEE:__obelisk_class_[^>]+]]>
// CHECK-SAME: obelisk_sim.hierarchical_name = "callee::default_value"
// CHECK: obelisk_sim.func private @[[READ:unit_[0-9]+]]({{.*}}!obelisk_sim.class_handle<@[[CALLEE]]>{{.*}}i32
// CHECK-SAME: obelisk_sim.hierarchical_name = "callee::read"
// CHECK: obelisk_sim.func private @[[NEW:unit_[0-9]+]]({{.*}}!obelisk_sim.class_handle<@[[CALLEE]]>{{.*}}i32
// CHECK-SAME: obelisk_sim.hierarchical_name = "callee::new"
// CHECK: obelisk_sim.func private @{{unit_[0-9]+}}({{.*}}%[[CALLER_THIS:arg[0-9]+]]: !obelisk_sim.class_handle<@[[CALLER:__obelisk_class_[^>]+]]>
// CHECK-SAME: obelisk_sim.hierarchical_name = "caller::invoke"
// CHECK: %[[TARGET_REF:.*]] = obelisk_sim.class.field_ref %[[CALLER_THIS]]{{.*}} -> !obelisk_sim.managed_ref<!obelisk_sim.class_handle<@[[CALLEE]]>, @[[CALLER]]>
// CHECK: %[[TARGET:.*]] = obelisk_sim.managed.load %[[TARGET_REF]]{{.*}} -> !obelisk_sim.class_handle<@[[CALLEE]]>
// CHECK-NOT: obelisk_sim.class.cast
// CHECK: %[[DEFAULT_VALUE:.*]] = obelisk_sim.class.direct_call @[[DEFAULT]] %[[TARGET]]()
// CHECK-NEXT: %[[RESULT:.*]] = obelisk_sim.class.direct_call @[[READ]] %[[TARGET]](%[[DEFAULT_VALUE]])
// CHECK-NEXT: obelisk_sim.return %[[RESULT]] : i32
// CHECK: obelisk_sim.func private @{{unit_[0-9]+}}(%[[CONTEXT:arg[0-9]+]]: !obelisk_sim.context{{.*}}) -> !obelisk_sim.class_handle<@[[CALLEE]]>
// CHECK-SAME: obelisk_sim.hierarchical_name = "caller::make"
// CHECK: %[[OBJECT:.*]] = obelisk_sim.class.alloc %[[CONTEXT]] : !obelisk_sim.context -> !obelisk_sim.class_handle<@[[CALLEE]]>
// CHECK: %[[CONSTRUCTOR_DEFAULT:.*]] = obelisk_sim.class.direct_call @[[DEFAULT]] %[[OBJECT]]()
// CHECK-NEXT: obelisk_sim.class.direct_call @[[NEW]] %[[OBJECT]](%[[CONSTRUCTOR_DEFAULT]])
// CHECK: obelisk_sim.return %[[OBJECT]] : !obelisk_sim.class_handle<@[[CALLEE]]>
