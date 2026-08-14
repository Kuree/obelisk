// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = true, hierarchical_name = "node", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "node", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.node>, sym_name = "s3.node", this_variable_path = "node::this", this_variable_symbol = @s1.$root::@s2::@s3.node::@s7.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "node::next", name = "next", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.node>, sym_name = "s4.next"} {}
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "node::value", name = "value", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.value"} {}
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "node::bounded", name = "bounded", node_id = 6 : i64, sym_name = "s6.bounded", this_variable_path = "node::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.node::@s6.bounded::@s8.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 9 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "node::value", referenced_symbol = @s1.$root::@s2::@s3.node::@s5.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {}
                }
                obelisk.sv.expression.conversion attributes {folded_constant = "32'd8", is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "8", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "node::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.node>, sym_name = "s8.this"} {}
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "node::this", is_compiler_generated, is_const, name = "this", node_id = 15 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.node>, sym_name = "s7.this"} {}
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = true, hierarchical_name = "root", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "root", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.root>, sym_name = "s9.root", this_variable_path = "root::this", this_variable_symbol = @s1.$root::@s2::@s9.root::@s11.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "root::head", name = "head", node_id = 17 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.node>, sym_name = "s10.head"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "root::this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.root>, sym_name = "s11.this"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 19 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s12.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 20 : i64, sym_name = "s13.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.root>, sym_name = "s14.r"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 22 : i64, procedure_kind = 0 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s12.top::@s13.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s12.top::@s13.top::@s14.r, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.root>} {}
            }
          }
        }
      }
    }
  }
}

// A null recursive edge contributes no active object. If the edge is enabled
// and nonnull, identity decides whether it closes onto the already planned
// ancestor. Only a distinct object takes the explicit unsupported path.
// CHECK-LABEL: obelisk_sim.func private @{{unit_[0-9]+}}{{.*}}obelisk_sim.hierarchical_name = "top"
// CHECK: %[[HEAD_REF:.*]] = obelisk_sim.class.field_ref %{{.*}}[@__obelisk_class_s9_root_field_0]
// CHECK: %[[HEAD:.*]] = obelisk_sim.managed.load %[[HEAD_REF]]
// CHECK: %[[HEAD_NULL:.*]] = obelisk_sim.managed.is_null %[[HEAD]]
// CHECK: cf.cond_br {{.*}}, ^[[HEAD_ACTIVE:bb[0-9]+]], ^[[CONTINUE:bb[0-9]+]]
// CHECK: ^[[HEAD_ACTIVE]]:
// CHECK: %[[NODE_MODE_REF:.*]] = obelisk_sim.class.field_ref %[[HEAD]][@__obelisk_class_s3_node_field___obelisk_rand_mode]
// CHECK: %[[NEXT_REF:.*]] = obelisk_sim.class.field_ref %[[HEAD]][@__obelisk_class_s3_node_field_0]
// CHECK: %[[NEXT:.*]] = obelisk_sim.managed.load %[[NEXT_REF]]
// CHECK: obelisk_sim.managed.is_null %[[NEXT]]
// CHECK: cf.cond_br {{.*}}, ^[[NEXT_ACTIVE:bb[0-9]+]], ^[[CONTINUE]]
// CHECK: ^[[NEXT_ACTIVE]]:
// CHECK: %[[NEXT_ID:.*]] = obelisk_sim.class.id %[[NEXT]]
// CHECK: %[[HEAD_ID:.*]] = obelisk_sim.class.id %[[HEAD]]
// CHECK: %[[ALIASED:.*]] = arith.cmpi eq, %[[NEXT_ID]], %[[HEAD_ID]] : i64
// CHECK: cf.cond_br %[[ALIASED]], ^[[CONTINUE]], ^[[DISTINCT:bb[0-9]+]]
// CHECK: ^[[DISTINCT]]:
// CHECK: obelisk_sim.bytes.constant "{{.*}}distinct object beyond the static recursive graph plan"
// CHECK: obelisk_sim.fatal
// CHECK: obelisk_sim.random.solve
