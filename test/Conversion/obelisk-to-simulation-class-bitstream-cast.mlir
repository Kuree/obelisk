// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' \
// RUN:   '--encode-obelisk-sim-to-bytecode=require-bytecode' \
// RUN:   | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' \
// RUN:   --encode-obelisk-sim-to-bytecode \
// RUN:   | FileCheck %s --check-prefix=AUTO
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' \
// RUN:   '--encode-obelisk-sim-to-bytecode=require-bytecode' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=HYBRID

!bits = !obelisk.ranged_packed_array<7 : 0 x
    !obelisk.integral<1, false, true, 0 : 0, logic>>
!nibbles = !obelisk.dynarray<!obelisk.integral<4, false, true, 3 : 0, logic>>
!class = !obelisk.class_handle<@root::@unit::@C>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  obelisk.feature.class_bitstream_source
} {
  obelisk.sv.symbol.definition @top_def attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 1 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {
    hierarchical_name = "$root", name = "$root", node_id = 2 : i64
  } {
    obelisk.sv.symbol.compilation_unit @unit attributes {
      hierarchical_name = "$unit", node_id = 3 : i64
    } {
      obelisk.sv.type.class_type @C attributes {
        bitstream_width = 8 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "C", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "C", node_id = 4 : i64,
        semantic_type = !class,
        this_variable_path = "C::this",
        this_variable_symbol = @root::@unit::@C::@class_this
      } {
        obelisk.sv.symbol.class_property @value attributes {
          hierarchical_name = "C::value", member_visibility = 0 : i32,
          name = "value", node_id = 5 : i64,
          semantic_type = !obelisk.integral<8, false, false, 7 : 0, byte>
        } {}
        obelisk.sv.symbol.subroutine @direct attributes {
          hierarchical_name = "C::direct", name = "direct", node_id = 6 : i64,
          return_variable_path = "C::direct.direct",
          return_variable_symbol = @root::@unit::@C::@direct::@direct_result,
          semantic_type = !obelisk.subroutine<() -> !bits, false>,
          subroutine_kind = 0 : i32,
          this_variable_path = "C::direct.this",
          this_variable_symbol = @root::@unit::@C::@direct::@direct_this,
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 123 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false,
              node_id = 124 : i64, semantic_type = !bits
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 125 : i64,
                referenced_path = "C::direct.streamed",
                referenced_symbol = @root::@unit::@C::@direct::@streamed,
                semantic_type = !bits
              } {}
              obelisk.sv.expression.streaming attributes {
                bitstream_width = 8 : i64, is_fixed_size = true,
                is_signed = false, node_id = 121 : i64,
                semantic_type = !obelisk.void, slice_size = 0 : i64,
                stream_count = 1 : i64, stream_with_flags = array<i64: 0>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 122 : i64,
                  referenced_path = "C::direct.this",
                  referenced_symbol = @root::@unit::@C::@direct::@direct_this,
                  semantic_type = !class
                } {}
              }
            }
          }
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
          obelisk.sv.symbol.variable @streamed attributes {
            hierarchical_name = "C::direct.streamed", lifetime = 0 : i32,
            name = "streamed", node_id = 120 : i64, semantic_type = !bits
          } {}
          obelisk.sv.symbol.variable @direct_result attributes {
            hierarchical_name = "C::direct.direct", is_compiler_generated,
            name = "direct", node_id = 10 : i64, semantic_type = !bits
          } {}
          obelisk.sv.symbol.variable @direct_this attributes {
            hierarchical_name = "C::direct.this", is_compiler_generated,
            is_const, name = "this", node_id = 11 : i64,
            semantic_type = !class
          } {}
        }
        obelisk.sv.symbol.subroutine @indirect attributes {
          hierarchical_name = "C::indirect", name = "indirect",
          node_id = 12 : i64,
          return_variable_path = "C::indirect.indirect",
          return_variable_symbol = @root::@unit::@C::@indirect::@indirect_result,
          semantic_type = !obelisk.subroutine<(!class) -> !bits, false>,
          subroutine_kind = 0 : i32,
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
          obelisk.sv.symbol.variable @indirect_result attributes {
            hierarchical_name = "C::indirect.indirect", is_compiler_generated,
            name = "indirect", node_id = 16 : i64, semantic_type = !bits
          } {}
          obelisk.sv.symbol.formal_argument @other attributes {
            direction = 0 : i32, hierarchical_name = "C::indirect.other",
            name = "other", node_id = 17 : i64, semantic_type = !class
          } {}
          obelisk.sv.symbol.variable @indirect_this attributes {
            hierarchical_name = "C::indirect.this", is_compiler_generated,
            is_const, name = "this", node_id = 18 : i64,
            semantic_type = !class
          } {}
        }
        obelisk.sv.symbol.variable @class_this attributes {
          hierarchical_name = "C::this", is_compiler_generated, is_const,
          name = "this", node_id = 19 : i64, semantic_type = !class
        } {}
      }
    }
    obelisk.sv.symbol.instance @top attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 20 : i64, referenced_path = "top",
      referenced_symbol = @top_def
    } {
      obelisk.sv.symbol.instance_body @top_body attributes {
        hierarchical_name = "top", name = "top", node_id = 21 : i64,
        time_precision_fs = 1 : i64,
        time_unit_fs = 1 : i64
      } {
        obelisk.sv.symbol.variable @object attributes {
          hierarchical_name = "top.object", lifetime = 1 : i32,
          name = "object", node_id = 129 : i64, semantic_type = !class
        } {}
        obelisk.sv.symbol.variable @nibbles attributes {
          hierarchical_name = "top.nibbles", lifetime = 1 : i32,
          name = "nibbles", node_id = 130 : i64, semantic_type = !nibbles
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 131 : i64,
            semantic_type = !nibbles
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 132 : i64,
              referenced_path = "top.object",
              referenced_symbol = @root::@top::@top_body::@object,
              semantic_type = !class
            } {}
          }
        }
      }
    }
  }
}

// CHECK: simulation.class.field
// CHECK-SAME: simulation.class_bitstream_member
// CHECK-SAME: simulation.class_bitstream_visibility = #simulation.member_visibility<public>
// CHECK-LABEL: simulation.func private @{{.*direct}}
// CHECK-COUNT-2: simulation.recursive.export_bitstream
// CHECK-SAME: class_allow_hidden_root
// CHECK-LABEL: simulation.func private @{{.*indirect}}
// CHECK: simulation.recursive.export_bitstream
// CHECK-NOT: class_allow_hidden_root
// CHECK-LABEL: simulation.func private @{{[^ (]+}}
// CHECK-SAME: simulation.hierarchical_name = "top.nibbles"
// CHECK: simulation.recursive.export_bitstream
// CHECK-SAME: -> (!simulation.logic<8>, i1, !simulation.managed_watch)
// CHECK: arith.constant {{.*}} 2 : i64
// CHECK: simulation.container.create
// CHECK-SAME: -> !simulation.dynamic_array<!simulation.logic<4>>

// BYTECODE: obelisk.execution.class_bitstream_blob = array<i8: 66, 83, 66, 67
// BYTECODE: obelisk.feature.class_bitstream
// BYTECODE: simulation.recursive.export_bitstream
// BYTECODE-SAME: class_site_id = 1 : i64
// BYTECODE-SAME: simulation.class_bitstream_bytecode_function
// BYTECODE-SAME: simulation.class_bitstream_bytecode_site

// AUTO: obelisk.execution.class_bitstream_blob
// AUTO-NOT: obelisk.feature.class_bitstream_bytecode
// AUTO-NOT: obelisk.feature.container_bitstream
// AUTO-NOT: obelisk.feature.recursive_bitstream
// AUTO: simulation.recursive.export_bitstream
// AUTO-SAME: class_site_id = 1 : i64
// AUTO-NOT: simulation.class_bitstream_bytecode_function
// AUTO-NOT: simulation.class_bitstream_bytecode_site

// The native lowering must preserve the bytecode bindings already patched into
// the class plan by the preceding encoder.  The first site is bound to bytecode
// function/site 3; rematerializing the plan here would replace both with -1.
// HYBRID: obelisk.execution.class_bitstream_blob = array<i8: 66, 83, 66, 67
// HYBRID-SAME: {{.*}}1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0
// HYBRID: obelisk.feature.class_bitstream_bytecode
// HYBRID: obelisk.feature.container_bitstream
// HYBRID-NOT: obelisk.feature.recursive_bitstream
// HYBRID: llvm.call @obelisk_rt_v1_container_bitstream_link_anchor
// HYBRID: llvm.call @obelisk_rt_v1_class_bitstream_link_anchor
// HYBRID: llvm.call @obelisk_rt_v1_class_bitstream_bytecode_link_anchor
