// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 15.2: semaphore and mailbox are built-in classes usable as
// base classes. A derived class holds its built-in base object in a private
// `$base` field: super.new creates it (the implicit super.new() of a class
// without one uses the default key count), an inherited built-in method acts
// on it, and an upcast selects it, mapping a null handle to null.
//
// class S extends semaphore;
//   function new(int n = 0); super.new(n); endfunction
//   function void give(); put(2); endfunction
// endclass
// class E extends semaphore; endclass
// module top;
//   S s; E e; semaphore b;
//   initial begin s = new(1); s.give(); void'(s.try_get(1)); b = s; e = new; end
// endmodule
//
// The unused std::process, mailbox and weak_reference declarations and the
// built-in randomization methods are omitted from the input below.

// CHECK: simulation.class.decl @[[E:__obelisk_class_s[0-9]+_E]]
// CHECK-NEXT: simulation.class.field @[[E_BASE:.+]] of @[[E]] at 0 : !simulation.semaphore {debug_name = "$base"
// CHECK: simulation.class.decl @[[S:__obelisk_class_s[0-9]+_S]]
// CHECK-NEXT: simulation.class.field @[[S_BASE:.+]] of @[[S]] at 0 : !simulation.semaphore {debug_name = "$base"

// super.new(n)
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[CREATED:.+]] = simulation.semaphore.create %arg2
// CHECK: %[[FIELD:.+]] = simulation.class.field_ref %arg1[@[[S_BASE]]]
// CHECK: simulation.managed.store %[[CREATED]] to %[[FIELD]]

// put(2) inside a method of S
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: %[[FIELD:.+]] = simulation.class.field_ref %arg1[@[[S_BASE]]]
// CHECK: %[[BASE:.+]] = simulation.managed.load %[[FIELD]]
// CHECK: simulation.semaphore.put %[[BASE]]

// s.try_get(1), then b = s
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK: %[[FIELD:.+]] = simulation.class.field_ref %{{.+}}[@[[S_BASE]]]
// CHECK: %[[BASE:.+]] = simulation.managed.load %[[FIELD]]
// CHECK: simulation.semaphore.try_get %[[BASE]]
// CHECK: %[[HANDLE:.+]] = simulation.ref.load
// CHECK: %[[NULL_HANDLE:.+]] = simulation.managed.is_null %[[HANDLE]]
// CHECK: %[[NULL:.+]] = simulation.managed.null {{.*}}: !simulation.semaphore
// CHECK: cf.cond_br %[[NULL_HANDLE]], ^[[MERGE:bb[0-9]+]](%[[NULL]] : !simulation.semaphore), ^[[SELECT:bb[0-9]+]]
// CHECK: ^[[SELECT]]:
// CHECK: simulation.class.field_ref %[[HANDLE]][@[[S_BASE]]]
// CHECK: ^[[MERGE]](%[[UPCAST:.+]]: !simulation.semaphore):
// CHECK: simulation.ref.store %[[UPCAST]]

// E's implicit constructor creates a semaphore without keys.
// CHECK: simulation.func private @[[E]]_implicit_new(
// CHECK: %[[ZERO:.+]] = arith.constant 0 : i32
// CHECK: %[[CREATED:.+]] = simulation.semaphore.create %[[ZERO]]
// CHECK: %[[FIELD:.+]] = simulation.class.field_ref %arg1[@[[E_BASE]]]
// CHECK: simulation.managed.store %[[CREATED]] to %[[FIELD]]

module attributes {coverage.language_version = 2023 : i32, obelisk.enum_domains = [{type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, values = ["0", "1", "2", "3", "4"]}]} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.S attributes {base_class = !obelisk.class_handle<@s5.std::@s4.semaphore>, bitstream_width = 0 : i64, constructor_path = "S::new", constructor_symbol = @s1.$root::@s2::@s3.S::@s9.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = true, has_cycles = false, hierarchical_name = "S", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "S", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>, this_variable_path = "S::this", this_variable_symbol = @s1.$root::@s2::@s3.S::@s49.this} {
        obelisk.sv.symbol.transparent_member @s6.try_get attributes {hierarchical_name = "S::try_get", name = "try_get", node_id = 4 : i64} {
        }
        obelisk.sv.symbol.transparent_member @s7.get attributes {hierarchical_name = "S::get", name = "get", node_id = 5 : i64} {
        }
        obelisk.sv.symbol.transparent_member @s8.put attributes {hierarchical_name = "S::put", name = "put", node_id = 6 : i64} {
        }
        obelisk.sv.symbol.subroutine @s9.new attributes {hierarchical_name = "S::new", is_constructor, name = "new", node_id = 7 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "S::new.this", this_variable_symbol = @s1.$root::@s2::@s3.S::@s9.new::@s11.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = true, node_id = 9 : i64, semantic_type = !obelisk.void} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = true, is_system_call = false, node_id = 10 : i64, referenced_path = "std::semaphore::new", referenced_symbol = @s5.std::@s4.semaphore::@s68.new, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "S::new.n", referenced_symbol = @s1.$root::@s2::@s3.S::@s9.new::@s10.n, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument @s10.n attributes {direction = 0 : i32, hierarchical_name = "S::new.n", name = "n", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.variable @s11.this attributes {hierarchical_name = "S::new.this", is_compiler_generated, is_const, name = "this", node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
          }
        }
        obelisk.sv.symbol.subroutine @s12.give attributes {hierarchical_name = "S::give", name = "give", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "S::give.this", this_variable_symbol = @s1.$root::@s2::@s3.S::@s12.give::@s13.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "put", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 17 : i64, referenced_path = "std::semaphore::put", referenced_symbol = @s5.std::@s4.semaphore::@s70.put, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s13.this attributes {hierarchical_name = "S::give.this", is_compiler_generated, is_const, name = "this", node_id = 19 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
          }
        }
        obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = true, node_id = 40 : i64, semantic_type = !obelisk.void} {
          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = true, is_system_call = false, node_id = 41 : i64, referenced_path = "std::semaphore::new", referenced_symbol = @s5.std::@s4.semaphore::@s68.new, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
            obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 42 : i64, referenced_path = "S::new.n", referenced_symbol = @s1.$root::@s2::@s3.S::@s9.new::@s10.n, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.variable @s49.this attributes {hierarchical_name = "S::this", is_compiler_generated, is_const, name = "this", node_id = 98 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
        }
      }
      obelisk.sv.type.class_type @s26.E attributes {base_class = !obelisk.class_handle<@s5.std::@s4.semaphore>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "E", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "E", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s26.E>, this_variable_path = "E::this", this_variable_symbol = @s1.$root::@s2::@s26.E::@s48.this} {
        obelisk.sv.symbol.transparent_member @s27.try_get attributes {hierarchical_name = "E::try_get", name = "try_get", node_id = 44 : i64} {
        }
        obelisk.sv.symbol.transparent_member @s28.get attributes {hierarchical_name = "E::get", name = "get", node_id = 45 : i64} {
        }
        obelisk.sv.symbol.transparent_member @s29.put attributes {hierarchical_name = "E::put", name = "put", node_id = 46 : i64} {
        }
        obelisk.sv.symbol.variable @s48.this attributes {hierarchical_name = "E::this", is_compiler_generated, is_const, name = "this", node_id = 97 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s26.E>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s42.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 67 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s43.top attributes {hierarchical_name = "top", name = "top", node_id = 68 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s44.s attributes {hierarchical_name = "top.s", lifetime = 1 : i32, name = "s", node_id = 69 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
        }
        obelisk.sv.symbol.variable @s45.e attributes {hierarchical_name = "top.e", lifetime = 1 : i32, name = "e", node_id = 70 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s26.E>} {
        }
        obelisk.sv.symbol.variable @s46.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 71 : i64, semantic_type = !obelisk.class_handle<@s5.std::@s4.semaphore>} {
        }
        obelisk.sv.symbol.procedural_block @s47 attributes {hierarchical_name = "top", node_id = 72 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 73 : i64} {
            obelisk.sv.statement.list attributes {node_id = 74 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 75 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 76 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 77 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s42.top::@s43.top::@s44.s, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 78 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 79 : i64, referenced_path = "S::new", referenced_symbol = @s1.$root::@s2::@s3.S::@s9.new, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 80 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 81 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "give", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 82 : i64, referenced_path = "S::give", referenced_symbol = @s1.$root::@s2::@s3.S::@s12.give, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 83 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s42.top::@s43.top::@s44.s, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 84 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "try_get", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = true, is_super_class = false, is_system_call = false, is_void_casted, node_id = 85 : i64, referenced_path = "std::semaphore::try_get", referenced_symbol = @s5.std::@s4.semaphore::@s74.try_get, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 86 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s42.top::@s43.top::@s44.s, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 87 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 88 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 89 : i64, semantic_type = !obelisk.class_handle<@s5.std::@s4.semaphore>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 90 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s42.top::@s43.top::@s46.b, semantic_type = !obelisk.class_handle<@s5.std::@s4.semaphore>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 91 : i64, semantic_type = !obelisk.class_handle<@s5.std::@s4.semaphore>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 92 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s42.top::@s43.top::@s44.s, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.S>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 93 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 94 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s26.E>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s42.top::@s43.top::@s45.e, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s26.E>} {
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 96 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s26.E>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  obelisk.sv.symbol.package @s5.std attributes {hierarchical_name = "std", name = "std", node_id = 99 : i64, simulation.vpi_automatic = false, simulation.vpi_definition_name = "std"} {
    obelisk.sv.type.class_type @s4.semaphore attributes {bitstream_width = 0 : i64, constructor_path = "std::semaphore::new", constructor_symbol = @s5.std::@s4.semaphore::@s68.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "std::semaphore", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "semaphore", node_id = 127 : i64, semantic_type = !obelisk.class_handle<@s5.std::@s4.semaphore>} {
      obelisk.sv.symbol.subroutine @s68.new attributes {hierarchical_name = "std::semaphore::new", is_builtin, is_constructor, name = "new", node_id = 128 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 129 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s69.keyCount attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::new.keyCount", name = "keyCount", node_id = 130 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 131 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine @s70.put attributes {hierarchical_name = "std::semaphore::put", is_builtin, name = "put", node_id = 132 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 133 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s71.keyCount attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::put.keyCount", name = "keyCount", node_id = 134 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 135 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine @s74.try_get attributes {hierarchical_name = "std::semaphore::try_get", is_builtin, name = "try_get", node_id = 140 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 141 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s75.keyCount attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::try_get.keyCount", name = "keyCount", node_id = 142 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 143 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
  }
}

