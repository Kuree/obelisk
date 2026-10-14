// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | obelisk-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.exe --execution-tier=bytecode --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | obelisk-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.opt.o
// RUN: %llvm_dist/bin/clang++ %t.opt.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.opt.exe
// RUN: %t.opt.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.opt.exe --execution-tier=bytecode --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: z3

// Unconstrained 128-bit source requires direct Z3-verified definitions.
// RUNTIME: wide status 1
// RUNTIME-NEXT: wide functions match 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 199 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 203 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 200 : i64} {}
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 202 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 201 : i64} {
        obelisk.sv.symbol.variable @wide_x attributes {hierarchical_name = "top.wide_x", lifetime = 1 : i32, name = "wide_x", semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 189 : i64} {}
        obelisk.sv.symbol.variable @wide_y attributes {hierarchical_name = "top.wide_y", lifetime = 1 : i32, name = "wide_y", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 190 : i64} {}
        obelisk.sv.symbol.procedural_block @wide_initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 198 : i64} {
          obelisk.sv.statement.list attributes {node_id = 197 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 193 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 192 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "wide status %0d", semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 191 : i64} {}
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 172 : i64} {
                  obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 181 : i64} {
                    obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 180 : i64} {
                      obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 179 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "top.wide_y", referenced_symbol = @root::@instance::@body::@wide_y, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 178 : i64} {}
                        obelisk.sv.expression.binary_op attributes {operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 177 : i64} {
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$countones", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 174 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.wide_x", referenced_symbol = @root::@instance::@body::@wide_x, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 173 : i64} {}
                          }
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$clog2", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 176 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.wide_x", referenced_symbol = @root::@instance::@body::@wide_x, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 175 : i64} {}
                          }
                        }
                      }
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 168 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.wide_x", referenced_symbol = @root::@instance::@body::@wide_x, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 166 : i64} {}
                    obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 167 : i64} {}
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 171 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.wide_y", referenced_symbol = @root::@instance::@body::@wide_y, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 169 : i64} {}
                    obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 170 : i64} {}
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 196 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 195 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "wide functions match %0d", semantic_type = !obelisk.ranged_packed_array<199 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 194 : i64} {}
                obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 188 : i64} {
                  obelisk.sv.expression.named_value attributes {referenced_path = "top.wide_y", referenced_symbol = @root::@instance::@body::@wide_y, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 182 : i64} {}
                  obelisk.sv.expression.binary_op attributes {operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 187 : i64} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$countones", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 184 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.wide_x", referenced_symbol = @root::@instance::@body::@wide_x, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 183 : i64} {}
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$clog2", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 186 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.wide_x", referenced_symbol = @root::@instance::@body::@wide_x, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 185 : i64} {}
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
