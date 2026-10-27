// RUN: %split-file %s %t
// RUN: %talos --std=1800-2023 -emit-talos %S/../Runtime/talos-to-simulation-random-wide-enum-arrays-runtime.sv | talos-opt --talos-sim-prepare | FileCheck %s --check-prefix=ARRAY
// RUN: %talos --std=1800-2023 -emit-talos %S/../Runtime/talos-to-simulation-random-wide-enum-scope-runtime.sv | talos-opt --talos-sim-prepare | FileCheck %s --check-prefix=SCOPE
// RUN: talos-opt %t/finite.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=FINITE
// RUN: talos-opt %t/finite.mlir '--lower-talos-to-sim=opt-level=0' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: talos-opt %t/finite.mlir '--lower-talos-to-sim=opt-level=3' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: talos-opt %t/nonmember.mlir '--lower-talos-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNSAT
// REQUIRES: compiled-sampler

// Preserve complete enumerator representations in frozen class/scope schema.
// Independent 65-bit array subdomains use offsets beyond a machine word.
// Compiler Z3 must prove that even a 257-bit equality to a nonmember is empty.
// ARRAY-DAG: mask = -1 : i130, value = 18446744073709551616 : i130
// ARRAY-DAG: #simulation.random_domain<offset = 65, width = 65, patterns = [#simulation.random_pattern<mask = -1 : i65
// SCOPE-DAG: mask = -1 : i129, value = -340282366920938463463374607431768211456 : i129
// SCOPE-DAG: mask = -1 : i257, value = 340282366920938463463374607431768211456 : i257
// FINITE-DAG: arith.constant {{.*}} 340282366920938463463374607431768211456 : i257
// FINITE-DAG: simulation.random.solve_wide
// UNSAT: warning: randomize hard constraints are statically unsatisfiable (z3-4.13.4)

//--- finite.mlir

module attributes {talos.enum_domains = [{type = !talos.enum<"E257", !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, values = ["257'h0", "257'h100000000000000000000000000000000", "257'h10000000000000000000000000000000000000000000000000000000000000000", "257'h10000000000000000000000000000000000000000000000000000000000000009"]}], llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 200000 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 200001 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 200002 : i64} {
      talos.sv.type.class_type @C attributes {bitstream_width = 257 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", semantic_type = !talos.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 200003 : i64} {
        talos.sv.symbol.class_property @e attributes {hierarchical_name = "C::e", name = "e", rand_mode = 1 : i32, semantic_type = !talos.enum<"E257", !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 200004 : i64} {}
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 200005 : i64} {}
      }
    }
    talos.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 200006 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 200007 : i64} {
        talos.sv.symbol.variable @object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 200008 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 200009 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 200010 : i64} {
          talos.sv.statement.list attributes {node_id = 200011 : i64} {
            talos.sv.statement.expression_statement attributes {node_id = 200012 : i64} {
              talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 200013 : i64} {
                talos.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 200014 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}

//--- nonmember.mlir

module attributes {talos.enum_domains = [{type = !talos.enum<"E257", !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, values = ["257'h0", "257'h100000000000000000000000000000000", "257'h10000000000000000000000000000000000000000000000000000000000000000", "257'h10000000000000000000000000000000000000000000000000000000000000009"]}], llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 200000 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 200001 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 200002 : i64} {
      talos.sv.type.class_type @C attributes {bitstream_width = 257 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", semantic_type = !talos.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 200003 : i64} {
        talos.sv.symbol.class_property @e attributes {hierarchical_name = "C::e", name = "e", rand_mode = 1 : i32, semantic_type = !talos.enum<"E257", !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 200004 : i64} {}
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 200005 : i64} {}
      }
    }
    talos.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 200006 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 200007 : i64} {
        talos.sv.symbol.variable @object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 200008 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 200009 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 200010 : i64} {
          talos.sv.statement.list attributes {node_id = 200011 : i64} {
            talos.sv.statement.expression_statement attributes {node_id = 200012 : i64} {
              talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 200013 : i64} {
                talos.sv.constraint.list attributes {item_count = 1 : i64, node_id = 200014 : i64} {
                  talos.sv.constraint.expression attributes {is_soft = false, node_id = 200015 : i64} {
                    talos.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 200016 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "C::e", referenced_symbol = @root::@unit::@C::@e, semantic_type = !talos.enum<"E257", !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 200017 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "257'd7", semantic_type = !talos.enum<"E257", !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 200018 : i64} {}
                    }
                  }
                }
                talos.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 200019 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}
