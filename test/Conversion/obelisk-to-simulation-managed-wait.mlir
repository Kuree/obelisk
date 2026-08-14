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
    } {
      obelisk.sv.type.class_type attributes {
        bitstream_width = 0 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "C", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "C", node_id = 3 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
        sym_name = "s3.C", this_variable_path = "C::this",
        this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "C::flag", name = "flag", node_id = 4 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s4.flag"
        } {}
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "C::flag2", name = "flag2", node_id = 25 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s14.flag2"
        } {}
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "C::q", name = "q", node_id = 5 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s5.q"
        } {}
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "C::wait_flag", name = "wait_flag",
          node_id = 6 : i64, semantic_type = !obelisk.subroutine<() -> (), true>,
          subroutine_kind = 1 : i32, sym_name = "s6.wait_flag",
          this_variable_path = "C::wait_flag.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.wait_flag::@s7.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.wait attributes {node_id = 7 : i64} {
            obelisk.sv.expression.binary_op attributes {
              is_signed = false, node_id = 8 : i64, operator_kind = 9 : i32,
              semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = true, node_id = 9 : i64,
                referenced_path = "C::flag",
                referenced_symbol = @s1.$root::@s2::@s3.C::@s4.flag,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {}
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "1", is_declared_unsized = true,
                is_signed = true, node_id = 10 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {}
            }
            obelisk.sv.statement.empty attributes {node_id = 11 : i64} {}
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::wait_flag.this", is_compiler_generated,
            is_const, name = "this", node_id = 12 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
            sym_name = "s7.this"
          } {}
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "C::wait_q", name = "wait_q", node_id = 13 : i64,
          semantic_type = !obelisk.subroutine<() -> (), true>,
          subroutine_kind = 1 : i32, sym_name = "s8.wait_q",
          this_variable_path = "C::wait_q.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s8.wait_q::@s9.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.wait attributes {node_id = 14 : i64} {
            obelisk.sv.expression.binary_op attributes {
              is_signed = false, node_id = 15 : i64, operator_kind = 14 : i32,
              semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64, callee_name = "size",
                constraint_restrictions = [], defaulted_arguments = array<i64: 0>,
                has_inline_constraints = false, has_iterator_expression = false,
                has_output_arguments = false, has_this_class = false,
                is_signed = true, is_super_class = false, is_system_call = true,
                node_id = 16 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                subroutine_kind = 0 : i32, system_library_cell = "work.$unit",
                system_scope_path = "C::wait_q",
                system_scope_symbol = @s1.$root::@s2::@s3.C::@s8.wait_q
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 17 : i64,
                  referenced_path = "C::q",
                  referenced_symbol = @s1.$root::@s2::@s3.C::@s5.q,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {}
              }
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "0", is_declared_unsized = true,
                is_signed = true, node_id = 18 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {}
            }
            obelisk.sv.statement.empty attributes {node_id = 19 : i64} {}
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::wait_q.this", is_compiler_generated,
            is_const, name = "this", node_id = 20 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
            sym_name = "s9.this"
          } {}
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "C::wait_both", name = "wait_both",
          node_id = 26 : i64, semantic_type = !obelisk.subroutine<() -> (), true>,
          subroutine_kind = 1 : i32, sym_name = "s15.wait_both",
          this_variable_path = "C::wait_both.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s15.wait_both::@s16.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.wait attributes {node_id = 27 : i64} {
            obelisk.sv.expression.binary_op attributes {
              is_signed = false, node_id = 28 : i64, operator_kind = 19 : i32,
              semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
            } {
              obelisk.sv.expression.binary_op attributes {
                is_signed = false, node_id = 29 : i64, operator_kind = 9 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 30 : i64,
                  referenced_path = "C::flag",
                  referenced_symbol = @s1.$root::@s2::@s3.C::@s4.flag,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1", is_declared_unsized = true,
                  is_signed = true, node_id = 31 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
              }
              obelisk.sv.expression.binary_op attributes {
                is_signed = false, node_id = 32 : i64, operator_kind = 9 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 33 : i64,
                  referenced_path = "C::flag2",
                  referenced_symbol = @s1.$root::@s2::@s3.C::@s14.flag2,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1", is_declared_unsized = true,
                  is_signed = true, node_id = 34 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {}
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 35 : i64} {}
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::wait_both.this", is_compiler_generated,
            is_const, name = "this", node_id = 36 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
            sym_name = "s16.this"
          } {}
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "C::wait_event", name = "wait_event",
          node_id = 37 : i64, semantic_type = !obelisk.subroutine<() -> (), true>,
          subroutine_kind = 1 : i32, sym_name = "s17.wait_event",
          this_variable_path = "C::wait_event.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s17.wait_event::@s18.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.timed attributes {node_id = 38 : i64} {
            obelisk.sv.timing.signal_event attributes {
              edge_kind = 0 : i32, has_iff = false, node_id = 39 : i64
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = true, node_id = 40 : i64,
                referenced_path = "C::flag",
                referenced_symbol = @s1.$root::@s2::@s3.C::@s4.flag,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {}
            }
            obelisk.sv.statement.empty attributes {node_id = 41 : i64} {}
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::wait_event.this", is_compiler_generated,
            is_const, name = "this", node_id = 42 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
            sym_name = "s18.this"
          } {}
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "C::this", is_compiler_generated, is_const,
          name = "this", node_id = 21 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
          sym_name = "s10.this"
        } {}
      }
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 22 : i64, referenced_path = "top",
      referenced_symbol = @s0.top, sym_name = "s11.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 23 : i64,
        sym_name = "s12.top", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.c", lifetime = 1 : i32, name = "c",
          node_id = 24 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
          sym_name = "s13.c"
        } {}
      }
    }
  }
}

// CHECK: obelisk_sim.func private @unit_
// CHECK-SAME: %[[THIS:arg[0-9]+]]: !obelisk_sim.class_handle
// CHECK: %[[FIELD:.*]] = obelisk_sim.class.field_ref %[[THIS]]
// CHECK: %[[FIELD_WATCH:.*]] = obelisk_sim.managed.watch field %[[FIELD]]
// CHECK: obelisk_sim.observer.bind
// CHECK-SAME: values(%[[THIS]], %[[FIELD_WATCH]]
// CHECK-SAME: captures 1 : <i1>
// CHECK: %[[QUEUE_FIELD:.*]] = obelisk_sim.class.field_ref
// CHECK: %[[QUEUE_WATCH:.*]] = obelisk_sim.managed.watch field %[[QUEUE_FIELD]]
// CHECK: %[[SIZE_WATCH:.*]] = obelisk_sim.managed.watch container_size
// CHECK: obelisk_sim.observer.bind
// CHECK-SAME: %[[QUEUE_WATCH]], %[[SIZE_WATCH]]
// CHECK-SAME: captures 1 : <i1>
// CHECK: obelisk_sim.func private @unit_
// CHECK-SAME: obelisk_sim.hierarchical_name = "C::wait_both"
// CHECK: obelisk_sim.observer.bind
// CHECK-SAME: !obelisk_sim.managed_watch, !obelisk_sim.managed_watch
// CHECK: obelisk_sim.func private @unit_
// CHECK-SAME: obelisk_sim.hierarchical_name = "C::wait_event"
// CHECK: %[[EVENT_FIELD:.*]] = obelisk_sim.class.field_ref
// CHECK: %[[EVENT_WATCH:.*]] = obelisk_sim.managed.watch field %[[EVENT_FIELD]]
// CHECK: %[[EVENT_OBSERVER:.*]] = obelisk_sim.observer.bind
// CHECK-SAME: values({{.*}}, %[[EVENT_WATCH]]
// CHECK: obelisk_sim.suspend.observe %[[EVENT_OBSERVER]],
// CHECK-NOT: obelisk_sim.suspend.change {{.*}}!obelisk_sim.managed_ref
// CHECK: obelisk_sim.func private @observer_
// CHECK-SAME: %{{.*}}: !obelisk_sim.class_handle
// CHECK: obelisk_sim.class.field_ref %{{.*}}
// CHECK-NOT: instance property reference has no this object
