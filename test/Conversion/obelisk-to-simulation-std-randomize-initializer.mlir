// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native --seed=1 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.exe --execution-tier=bytecode --seed=1 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: z3

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "unit"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "instance"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "body", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.x", lifetime = 1 : i32, name = "x", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "result"} {
          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@instance::@body} {
            obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 8 : i64} {
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 9 : i64} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 10 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 16 : i64, procedure_kind = 0 : i32, sym_name = "initial", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 18 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@instance::@body} {
              obelisk.sv.expression.string_literal attributes {constant_value = "init %0d %0d", node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<95 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 20 : i64, referenced_path = "top.result", referenced_symbol = @root::@instance::@body::@result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 21 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 10.5 and 18.12: static initialization precedes every
// process, so std::randomize must fall back to the initialization stream.
// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 function hierarchy "top.result.$static_initializer"
// CHECK-LABEL: obelisk_sim.func @__obelisk_root
// CHECK: obelisk_sim.call
// CHECK: obelisk_sim.spawn
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: obelisk_sim.random.state
// CHECK: obelisk_sim.random.set_state

// RUNTIME: init 1 7
