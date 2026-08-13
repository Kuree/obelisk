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
        bitstream_width = 32 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "C", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "C", node_id = 3 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
        sym_name = "s3.C", this_variable_path = "C::this",
        this_variable_symbol = @s1.$root::@s2::@s3.C::@s7.this
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "C::value", name = "value", node_id = 4 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s4.value"
        } {}
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "C::spawn", name = "spawn", node_id = 5 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
          subroutine_kind = 0 : i32, sym_name = "s5.spawn",
          this_variable_path = "C::spawn.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.spawn::@s6.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {
            block_kind = 3 : i32, node_id = 6 : i64
          } {
            obelisk.sv.statement.block attributes {node_id = 7 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 8 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 9 : i64} {
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "1", is_declared_unsized = true,
                    is_signed = true, node_id = 10 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {}
                }
                obelisk.sv.statement.expression_statement attributes {
                  node_id = 11 : i64
                } {
                  obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = true,
                    node_id = 12 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true, node_id = 13 : i64,
                      referenced_path = "C::value",
                      referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {}
                    obelisk.sv.expression.integer_literal attributes {
                      constant_value = "7", is_declared_unsized = true,
                      is_signed = true, node_id = 14 : i64,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {}
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::spawn.this", is_compiler_generated,
            is_const, name = "this", node_id = 15 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
            sym_name = "s6.this"
          } {}
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "C::this", is_compiler_generated, is_const,
          name = "this", node_id = 16 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
          sym_name = "s7.this"
        } {}
      }
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 17 : i64, referenced_path = "top", referenced_symbol = @s0.top,
      sym_name = "s8.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 18 : i64,
        sym_name = "s9.top", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {}
    }
  }
}

// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} fork hierarchy "C::spawn.$fork.6.0"
// CHECK-LABEL: obelisk_sim.func private @{{.*}}.fork.6.0.0(
// CHECK-SAME: %[[THIS:arg[0-9]+]]: !obelisk_sim.class_handle<@__obelisk_class_s3_C>
// CHECK: obelisk_sim.suspend.delay
// CHECK: obelisk_sim.class.field_ref %[[THIS]][@{{.*}}]
// CHECK-NOT: obelisk.sv.
