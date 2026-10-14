// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s --check-prefix=DEFAULT < %t.mlir
// RUN: obelisk-opt %t.mlir --canonicalize --cse | FileCheck %s --check-prefix=DEFAULT
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// Resolved semantic IR for sample(ref int live = value), IEEE 19.8.1 and
// 13.5.3. The enclosing-scope source lookup remains separately XFAIL in
// functional-sample-ref-default-slang-xfail.sv. A default contributes a scalar
// FormalDefault result to the sample batch while retaining its live reference.
// DEFAULT: %[[REF:[0-9]+]] = simulation.argument_ref.from_ref
// DEFAULT: %[[VALUE:[0-9]+]] = simulation.argument_ref.load %[[REF]] : !simulation.argument_ref<i32> -> i32
// DEFAULT: simulation.covergroup.sample {{.*}} values[%[[VALUE]], {{%[0-9]+}}] ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}]
// SCHEMA-DAG: functional_formal {{.*}} name=live kind=2 direction=2 result_kind=2 flags=1 {{.*}} default_expression=[[EXPR:[1-9][0-9]*]]
// SCHEMA-DAG: functional_expression id=[[EXPR]] {{.*}} owner_kind=6 role=17 result_kind=2 {{.*}} phase=2

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.ref_default attributes {definition_kind = 0 : i32, hierarchical_name = "ref_default", name = "ref_default", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.ref_default attributes {hierarchical_name = "ref_default", is_uninstantiated = false, name = "ref_default", node_id = 3 : i64, referenced_path = "ref_default", referenced_symbol = @s0.ref_default} {
      obelisk.sv.symbol.instance_body @s4.ref_default attributes {hierarchical_name = "ref_default", name = "ref_default", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "ref_default", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.value attributes {hierarchical_name = "ref_default.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.type.covergroup_type @s6.cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 2 : i32, has_coverage_event = false, hierarchical_name = "ref_default.cg", name = "cg", node_id = 7 : i64, sample_formal_count = 1 : i64, sample_formals = [@s1.$root::@s3.ref_default::@s4.ref_default::@s6.cg::@s7.live], semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.ref_default::@s6.cg>} {
          obelisk.sv.symbol.formal_argument @s7.live attributes {direction = 3 : i32, hierarchical_name = "ref_default.cg::live", is_coverage_sample_formal, name = "live", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 67 : i64, referenced_path = "ref_default.value", referenced_symbol = @s1.$root::@s3.ref_default::@s4.ref_default::@s5.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.covergroup_body @s8 attributes {hierarchical_name = "ref_default.cg", node_id = 9 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.subroutine @s21.sample attributes {hierarchical_name = "ref_default.cg.sample", is_builtin, name = "sample", node_id = 31 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 32 : i64} {
              }
              obelisk.sv.symbol.formal_argument @s22.live attributes {direction = 3 : i32, hierarchical_name = "ref_default.cg.sample.live", is_coverage_sample_formal, name = "live", node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 68 : i64, referenced_path = "ref_default.value", referenced_symbol = @s1.$root::@s3.ref_default::@s4.ref_default::@s5.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.coverpoint @s23.cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "ref_default.cg.cp", name = "cp", node_id = 34 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = true, node_id = 35 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 36 : i64, referenced_path = "ref_default.cg::live", referenced_symbol = @s1.$root::@s3.ref_default::@s4.ref_default::@s6.cg::@s7.live, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s34.cov attributes {hierarchical_name = "ref_default.cov", lifetime = 1 : i32, name = "cov", node_id = 55 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.ref_default::@s6.cg>} {
        }
        obelisk.sv.symbol.procedural_block @s35 attributes {hierarchical_name = "ref_default", node_id = 56 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 57 : i64} {
            obelisk.sv.statement.list attributes {node_id = 58 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 59 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 60 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.ref_default::@s6.cg>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 61 : i64, referenced_path = "ref_default.cov", referenced_symbol = @s1.$root::@s3.ref_default::@s4.ref_default::@s34.cov, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.ref_default::@s6.cg>} {
                  }
                  obelisk.sv.expression.new_covergroup attributes {argument_count = 0 : i64, defaulted_arguments = array<i64>, is_signed = false, node_id = 62 : i64, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.ref_default::@s6.cg>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sample", constraint_restrictions = [], defaulted_arguments = array<i64: 1>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 64 : i64, referenced_path = "ref_default.cg.sample", referenced_symbol = @s1.$root::@s3.ref_default::@s4.ref_default::@s6.cg::@s8::@s21.sample, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "ref_default.cov", referenced_symbol = @s1.$root::@s3.ref_default::@s4.ref_default::@s34.cov, semantic_type = !obelisk.covergroup_handle<@s1.$root::@s4.ref_default::@s6.cg>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 66 : i64, referenced_path = "ref_default.value", referenced_symbol = @s1.$root::@s3.ref_default::@s4.ref_default::@s5.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
