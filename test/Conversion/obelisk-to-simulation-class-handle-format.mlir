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
        this_variable_symbol = @s1.$root::@s2::@s3.C::@s4.this
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "C::this", is_compiler_generated, is_const,
          name = "this", node_id = 4 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
          sym_name = "s4.this"
        } {}
      }
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 5 : i64, referenced_path = "top",
      referenced_symbol = @s0.top, sym_name = "s5.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 6 : i64,
        sym_name = "s6.top", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.value", lifetime = 1 : i32,
          name = "value", node_id = 7 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
          sym_name = "s7.value"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.text", lifetime = 1 : i32,
          name = "text", node_id = 8 : i64,
          semantic_type = !obelisk.string, sym_name = "s8.text"
        } {}
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top", node_id = 9 : i64,
          procedure_kind = 0 : i32, sym_name = "s9",
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 10 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false,
              node_id = 11 : i64, semantic_type = !obelisk.string
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 12 : i64,
                referenced_path = "top.text",
                referenced_symbol = @s1.$root::@s5.top::@s6.top::@s8.text,
                semantic_type = !obelisk.string
              } {}
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64, callee_name = "$sformatf",
                constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>,
                has_inline_constraints = false, has_iterator_expression = false,
                has_output_arguments = false, has_this_class = false,
                is_signed = false, is_super_class = false,
                is_system_call = true, node_id = 13 : i64,
                semantic_type = !obelisk.string, subroutine_kind = 0 : i32,
                system_library_cell = "work.top", system_scope_path = "top",
                system_scope_symbol = @s1.$root::@s5.top::@s6.top
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "%p", is_signed = false,
                  node_id = 14 : i64,
                  semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {}
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 15 : i64,
                  referenced_path = "top.value",
                  referenced_symbol = @s1.$root::@s5.top::@s6.top::@s7.value,
                  semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
                } {}
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %[[VALUE:.*]] = obelisk_sim.ref.load {{.*}} :
// CHECK-SAME: -> !obelisk_sim.class_handle<@__obelisk_class_s3_C>
// CHECK: obelisk_sim.string.output_format
// CHECK-SAME: %[[VALUE]]
// CHECK-SAME: flags = [32, 64]
