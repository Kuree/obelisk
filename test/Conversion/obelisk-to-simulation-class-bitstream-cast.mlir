// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=require-bytecode' \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

!bits = !obelisk.ranged_packed_array<7 : 0 x
    !obelisk.integral<1, false, true, 0 : 0, logic>>
!class = !obelisk.class_handle<@root::@unit::@C>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  obelisk.feature.class_bitstream_source
} {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 1 : i64, sym_name = "top_def"
  } {}
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "$root", name = "$root", node_id = 2 : i64,
    sym_name = "root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "unit"
    } {
      obelisk.sv.type.class_type attributes {
        bitstream_width = 8 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "C", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "C", node_id = 4 : i64,
        semantic_type = !class, sym_name = "C",
        this_variable_path = "C::this",
        this_variable_symbol = @root::@unit::@C::@class_this
      } {
        obelisk.sv.symbol.class_property attributes {
          hierarchical_name = "C::value", member_visibility = 0 : i32,
          name = "value", node_id = 5 : i64,
          semantic_type = !obelisk.integral<8, false, false, 7 : 0, byte>,
          sym_name = "value"
        } {}
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "C::direct", name = "direct", node_id = 6 : i64,
          return_variable_path = "C::direct.direct",
          return_variable_symbol = @root::@unit::@C::@direct::@direct_result,
          semantic_type = !obelisk.subroutine<() -> !bits, false>,
          subroutine_kind = 0 : i32, sym_name = "direct",
          this_variable_path = "C::direct.this",
          this_variable_symbol = @root::@unit::@C::@direct::@direct_this,
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64
        } {
          obelisk.sv.statement.return attributes {node_id = 7 : i64} {
            obelisk.sv.expression.conversion attributes {
              is_implicit = false, is_signed = false, node_id = 8 : i64,
              semantic_type = !bits
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 9 : i64,
                referenced_path = "C::direct.this",
                referenced_symbol = @root::@unit::@C::@direct::@direct_this,
                semantic_type = !class
              } {}
            }
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::direct.direct", is_compiler_generated,
            name = "direct", node_id = 10 : i64, semantic_type = !bits,
            sym_name = "direct_result"
          } {}
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::direct.this", is_compiler_generated,
            is_const, name = "this", node_id = 11 : i64,
            semantic_type = !class, sym_name = "direct_this"
          } {}
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "C::indirect", name = "indirect",
          node_id = 12 : i64,
          return_variable_path = "C::indirect.indirect",
          return_variable_symbol = @root::@unit::@C::@indirect::@indirect_result,
          semantic_type = !obelisk.subroutine<(!class) -> !bits, false>,
          subroutine_kind = 0 : i32, sym_name = "indirect",
          this_variable_path = "C::indirect.this",
          this_variable_symbol = @root::@unit::@C::@indirect::@indirect_this,
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64
        } {
          obelisk.sv.statement.return attributes {node_id = 13 : i64} {
            obelisk.sv.expression.conversion attributes {
              is_implicit = false, is_signed = false, node_id = 14 : i64,
              semantic_type = !bits
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 15 : i64,
                referenced_path = "C::indirect.other",
                referenced_symbol = @root::@unit::@C::@indirect::@other,
                semantic_type = !class
              } {}
            }
          }
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::indirect.indirect", is_compiler_generated,
            name = "indirect", node_id = 16 : i64, semantic_type = !bits,
            sym_name = "indirect_result"
          } {}
          obelisk.sv.symbol.formal_argument attributes {
            direction = 0 : i32, hierarchical_name = "C::indirect.other",
            name = "other", node_id = 17 : i64, semantic_type = !class,
            sym_name = "other"
          } {}
          obelisk.sv.symbol.variable attributes {
            hierarchical_name = "C::indirect.this", is_compiler_generated,
            is_const, name = "this", node_id = 18 : i64,
            semantic_type = !class, sym_name = "indirect_this"
          } {}
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "C::this", is_compiler_generated, is_const,
          name = "this", node_id = 19 : i64, semantic_type = !class,
          sym_name = "class_this"
        } {}
      }
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 20 : i64, referenced_path = "top",
      referenced_symbol = @top_def, sym_name = "top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 21 : i64,
        sym_name = "top_body", time_precision_fs = 1 : i64,
        time_unit_fs = 1 : i64
      } {}
    }
  }
}

// CHECK: obelisk_sim.class.field
// CHECK-SAME: obelisk_sim.class_bitstream_member
// CHECK-SAME: obelisk_sim.class_bitstream_visibility = 0 : i32
// CHECK-LABEL: obelisk_sim.func private @{{.*direct}}
// CHECK: obelisk_sim.recursive.export_bitstream
// CHECK-SAME: class_allow_hidden_root
// CHECK-LABEL: obelisk_sim.func private @{{.*indirect}}
// CHECK: obelisk_sim.recursive.export_bitstream
// CHECK-NOT: class_allow_hidden_root

// BYTECODE: obelisk.execution.class_bitstream_blob = array<i8: 66, 83, 66, 67
// BYTECODE: obelisk.feature.class_bitstream
// BYTECODE: obelisk_sim.recursive.export_bitstream
// BYTECODE-SAME: class_site_id = 1 : i64
// BYTECODE-SAME: obelisk_sim.class_bitstream_bytecode_function
// BYTECODE-SAME: obelisk_sim.class_bitstream_bytecode_site
