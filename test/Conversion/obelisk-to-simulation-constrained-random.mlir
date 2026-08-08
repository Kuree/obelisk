// RUN: %split-file %s %t
// RUN: obelisk-opt %t/success.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: not obelisk-opt %t/unsupported.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=UNSUPPORTED
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/unsat.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:     | FileCheck %s --check-prefix=UNSAT \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/domain.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=DOMAIN \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/domain-bounded.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=DOMAIN-BOUNDED \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/domain-residual.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=DOMAIN-RESIDUAL \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/capture-domain.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=CAPTURE-DOMAIN \
// RUN: %}
// RUN: %if !z3 %{ \
// RUN:   obelisk-opt %t/capture-domain.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=CAPTURE-DOMAIN-FALLBACK \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/capture-domain-64.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=CAPTURE-DOMAIN-64 \
// RUN: %}
// RUN: %if !z3 %{ \
// RUN:   obelisk-opt %t/capture-domain-64.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=CAPTURE-DOMAIN-64-FALLBACK \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/capture-domain-signed.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=CAPTURE-DOMAIN-SIGNED \
// RUN: %}
// RUN: %if !z3 %{ \
// RUN:   obelisk-opt %t/capture-domain-signed.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=CAPTURE-DOMAIN-SIGNED-FALLBACK \
// RUN: %}
// RUN: %if !z3 %{ \
// RUN:   obelisk-opt %t/domain-bounded.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=DOMAIN-BOUNDED-FALLBACK \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/table.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=TABLE \
// RUN: %}
// RUN: %if !z3 %{ \
// RUN:   obelisk-opt %t/table.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=TABLE-FALLBACK \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/table-bounded.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=TABLE-BOUNDED \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/components.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=COMPONENTS \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/components-correlated.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=COMPONENT-CORRELATED \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/components-partial.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=COMPONENT-PARTIAL \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/components-capture.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=COMPONENT-CAPTURE \
// RUN: %}
// RUN: %if !z3 %{ \
// RUN:   obelisk-opt %t/components.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=COMPONENTS-FALLBACK \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/table-residual.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=TABLE-RESIDUAL \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/alias.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=ALIAS \
// RUN: %}
// RUN: %if !z3 %{ \
// RUN:   obelisk-opt %t/alias.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=ALIAS-FALLBACK \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/definition.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=DEFINITION \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/definition.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=COMPOSE-FALLBACK \
// RUN: %}
// RUN: %if !z3 %{ \
// RUN:   obelisk-opt %t/definition.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=DEFINITION-FALLBACK \
// RUN: %}

//--- success.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_constraint", name = "unsupported_constraint", node_id = 0 : i64, sym_name = "s0.unsupported_constraint"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "limit", lifetime = 1 : i32, name = "limit", node_id = 75 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s24.limit"} {
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "constrained", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "constrained", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s3.constrained", this_variable_path = "constrained::this", this_variable_symbol = @s1.$root::@s2::@s3.constrained::@s21.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "constrained::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "constrained::bounds", name = "bounds", node_id = 5 : i64, sym_name = "s5.bounds", this_variable_path = "constrained::bounds.this", this_variable_symbol = @s1.$root::@s2::@s3.constrained::@s5.bounds::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s24.limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "constrained::bounds.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "constrained::this", is_compiler_generated, is_const, name = "this", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s21.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_constraint", is_uninstantiated = false, name = "unsupported_constraint", node_id = 32 : i64, referenced_path = "unsupported_constraint", referenced_symbol = @s0.unsupported_constraint, sym_name = "s19.unsupported_constraint"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_constraint", name = "unsupported_constraint", node_id = 33 : i64, sym_name = "s20.unsupported_constraint"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_constraint.object", lifetime = 1 : i32, name = "object", node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s22.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 36 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_constraint", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.unsupported_constraint", system_scope_path = "unsupported_constraint", system_scope_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint} {
              obelisk.sv.constraint.list attributes {item_count = 5 : i64, node_id = 41 : i64} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 42 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 43 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.constraint.implication attributes {node_id = 46 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 47 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 50 : i64} {
                    obelisk.sv.expression.binary_op attributes {node_id = 51 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {node_id = 52 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
                obelisk.sv.constraint.conditional attributes {has_else = true, node_id = 54 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 55 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 56 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 58 : i64} {
                    obelisk.sv.expression.binary_op attributes {node_id = 59 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 62 : i64} {
                    obelisk.sv.expression.binary_op attributes {node_id = 63 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {node_id = 64 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 65 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
                obelisk.sv.constraint.uniqueness attributes {item_count = 2 : i64, node_id = 66 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 67 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 68 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 69 : i64} {
                  obelisk.sv.expression.inside attributes {item_count = 1 : i64, node_id = 70 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 71 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.value_range attributes {node_id = 72 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 73 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 74 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "unsupported_constraint.object", referenced_symbol = @s1.$root::@s19.unsupported_constraint::@s20.unsupported_constraint::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>} {
              }
            }
          }
        }
      }
    }
  }
}

// The object stream chooses the first assignment for a bounded generated
// sampler. Exhaustion invokes the compiler-serialized runtime program, and the
// rand property is stored only on a successful commit edge.
// CHECK: obelisk_sim.class.decl
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "value"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_state"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_increment"
// CHECK: obelisk_sim.func private @unit_1({{.*}}%[[LIMIT_ARG:arg[0-9]+]]: !obelisk_sim.ref<i32>
// CHECK: obelisk_sim.class.field_ref
// CHECK: obelisk_sim.managed.load
// CHECK: arith.muli
// CHECK: obelisk_sim.managed.store
// CHECK: cf.br ^[[SEARCH:[a-zA-Z0-9_]+]]
// CHECK: ^[[SEARCH]]({{.*}}: i64, {{.*}}: i64):
// CHECK: arith.cmpi slt
// CHECK: arith.ori
// CHECK: arith.cmpi sgt
// CHECK: arith.select
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi sge
// CHECK: arith.cmpi sle
// CHECK: obelisk_sim.ref.load %[[LIMIT_ARG]]
// CHECK: cf.cond_br {{.*}}, ^[[COMMIT:[a-zA-Z0-9_]+]]({{.*}}), ^[[ADVANCE:[a-zA-Z0-9_]+]]
// CHECK: ^[[ADVANCE]]:
// CHECK: arith.cmpi uge
// CHECK: cf.cond_br {{.*}}, ^[[EXHAUSTED:[a-zA-Z0-9_]+]], ^[[SEARCH]]
// CHECK: ^[[EXHAUSTED]]:
// CHECK: obelisk_sim.random.solve
// CHECK: cf.cond_br {{.*}}, ^[[COMMIT]]({{.*}}), ^[[DONE:[a-zA-Z0-9_]+]]
// CHECK: ^[[COMMIT]]({{.*}}: i64):
// CHECK: obelisk_sim.managed.store
// CHECK: cf.br ^[[DONE]]
// CHECK: ^[[DONE]]:
// CHECK-NOT: obelisk_sim.managed.store
// CHECK-NOT: obelisk.sv.

// UNSUPPORTED-DAG: error: randc properties are not executable yet
// UNSUPPORTED-DAG: error: rand enum and tagged-union domains are not executable yet
// UNSUPPORTED-DAG: error: at most one soft constraint is executable per randomization plan
// UNSUPPORTED-DAG: error: soft constraints must be direct items of a top-level constraint list
// UNSUPPORTED-DAG: error: constraint form is outside the executable hard-expression boundary: obelisk.sv.constraint.solve_before
// UNSUPPORTED-DAG: error: user pre_randomize and post_randomize hooks are not executable yet
// UNSUPPORTED-DAG: error: constraint expression is outside the total side-effect-free executable boundary: obelisk.sv.expression.assignment
// UNSUPPORTED-DAG: error: constraint expression is outside the total side-effect-free executable boundary: obelisk.sv.expression.unary_op

// UNSAT: warning: randomize hard constraints are statically unsatisfiable (z3-4.13.4)
// UNSAT-NOT: obelisk_sim.random.solve

// A Z3-proven power-of-two domain is folded without modulo bias into the
// generated proposal. Z3 also proves that the proposal implies the hard
// formula, so generated code commits directly without a checker or fallback.
// DOMAIN-LABEL: obelisk_sim.func private @unit_1
// DOMAIN-DAG: %[[SEVEN:.*]] = arith.constant 7 : i64
// DOMAIN-DAG: %[[EIGHT:.*]] = arith.constant 8 : i64
// DOMAIN-DAG: %[[CLEAR:.*]] = arith.constant -16 : i64
// DOMAIN-DAG: %[[FIFTEEN:.*]] = arith.constant 15 : i64
// DOMAIN: %[[RAW:.*]] = arith.andi {{.*}}, %[[FIFTEEN]] : i64
// DOMAIN: %[[LOW:.*]] = arith.andi %[[RAW]], %[[SEVEN]] : i64
// DOMAIN: %[[VALUE:.*]] = arith.addi %[[LOW]], %[[EIGHT]] : i64
// DOMAIN: %[[REST:.*]] = arith.andi %[[RAW]], %[[CLEAR]] : i64
// DOMAIN: %[[ASSIGNMENT:.*]] = arith.ori %[[REST]], %[[VALUE]] : i64
// DOMAIN-NOT: arith.cmpi
// DOMAIN-NOT: obelisk_sim.random.solve
// DOMAIN: arith.trunci %[[ASSIGNMENT]] : i64 to i4
// DOMAIN: obelisk_sim.managed.store

// A ten-value interval uses an unbiased 64-bit bounded draw. Retry proposals
// advance cyclically with overflow-safe modular addition, and the exact Z3
// domain commits without the checker or runtime solver.
// DOMAIN-BOUNDED-LABEL: obelisk_sim.func private @unit_1
// DOMAIN-BOUNDED: arith.constant 10 : i64
// DOMAIN-BOUNDED: arith.remui {{.*}}, {{.*}} : i64
// DOMAIN-BOUNDED: arith.cmpi ult
// DOMAIN-BOUNDED: %[[WRAPS:.*]] = arith.cmpi uge
// DOMAIN-BOUNDED: arith.select %[[WRAPS]]
// DOMAIN-BOUNDED-NOT: obelisk_sim.random.solve
// DOMAIN-BOUNDED: arith.trunci {{.*}} : i64 to i4
// DOMAIN-BOUNDED: obelisk_sim.managed.store

// DOMAIN-BOUNDED-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// DOMAIN-BOUNDED-FALLBACK: obelisk_sim.random.solve

// Two bounded domains coupled by inequality are not an exact product. Their
// unbiased starts still feed the checker, and the retry attempt advances both
// intervals cyclically before preserving the runtime fallback.
// DOMAIN-RESIDUAL-LABEL: obelisk_sim.func private @unit_1
// DOMAIN-RESIDUAL-COUNT-2: arith.cmpi ult
// DOMAIN-RESIDUAL: arith.remui
// DOMAIN-RESIDUAL-COUNT-2: arith.cmpi uge
// DOMAIN-RESIDUAL: arith.cmpi ne
// DOMAIN-RESIDUAL: obelisk_sim.random.solve

// Z3 proves the direct runtime bounds equivalent to the hard formula. The
// generated samplers compute cardinalities limit + 1, 16 - limit,
// high - low + 1, and high - low - 1. Strict endpoints are normalized only
// after checking their individual overflow edges. Any empty intersected range
// fails before modulo; valid ranges use unbiased dynamic sampling without
// checker/solver calls.
// CAPTURE-DOMAIN-LABEL: obelisk_sim.func private @unit_1
// CAPTURE-DOMAIN: obelisk_sim.ref.load
// CAPTURE-DOMAIN: arith.andi {{.*}}, {{.*}} : i64
// CAPTURE-DOMAIN: %[[CARDINALITY:.*]] = arith.addi {{.*}}, {{.*}} : i64
// CAPTURE-DOMAIN: arith.subi %{{c16_i64.*}}, {{.*}} : i64
// CAPTURE-DOMAIN: %[[INCLUSIVE_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN: arith.subi
// CAPTURE-DOMAIN: arith.addi
// CAPTURE-DOMAIN: %[[LOW_EDGE:.*]] = arith.cmpi ne, {{.*}}, %{{c15_i64.*}} : i64
// CAPTURE-DOMAIN: arith.andi %[[INCLUSIVE_VALID]], %[[LOW_EDGE]] : i1
// CAPTURE-DOMAIN: arith.addi
// CAPTURE-DOMAIN: arith.maxui
// CAPTURE-DOMAIN: %[[HIGH_EDGE:.*]] = arith.cmpi ne, {{.*}}, %{{c0_i64.*}} : i64
// CAPTURE-DOMAIN: arith.andi {{.*}}, %[[HIGH_EDGE]] : i1
// CAPTURE-DOMAIN: arith.subi
// CAPTURE-DOMAIN: arith.minui
// CAPTURE-DOMAIN: %[[STRICT_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN: %[[ALL_VALID:.*]] = arith.andi {{.*}}, %[[STRICT_VALID]] : i1
// CAPTURE-DOMAIN: arith.subi
// CAPTURE-DOMAIN: arith.addi
// CAPTURE-DOMAIN: cf.cond_br %[[ALL_VALID]], ^[[RANGE_SAMPLE:bb[0-9]+]], ^[[RANGE_EMPTY:bb[0-9]+]]
// CAPTURE-DOMAIN: ^[[RANGE_SAMPLE]]:
// CAPTURE-DOMAIN: %[[FULL_CARDINALITY:.*]] = arith.cmpi eq, %[[CARDINALITY]], %{{c0_i64.*}} : i64
// CAPTURE-DOMAIN: %[[SAFE_CARDINALITY:.*]] = arith.select %[[FULL_CARDINALITY]], %{{c1_i64.*}}, %[[CARDINALITY]] : i64
// CAPTURE-DOMAIN: arith.remui {{.*}}, %[[SAFE_CARDINALITY]] : i64
// CAPTURE-DOMAIN: cf.br
// CAPTURE-DOMAIN: ^[[RANGE_EMPTY]]:
// CAPTURE-DOMAIN: obelisk_sim.managed.store {{.*}} : i64
// CAPTURE-DOMAIN: cf.br ^[[RANGE_DONE:bb[0-9]+]]
// CAPTURE-DOMAIN: arith.remui {{.*}}, %[[SAFE_CARDINALITY]] : i64
// CAPTURE-DOMAIN: arith.cmpi ult
// CAPTURE-DOMAIN-NOT: arith.cmpi ule
// CAPTURE-DOMAIN-NOT: obelisk_sim.random.solve
// CAPTURE-DOMAIN: obelisk_sim.managed.store
// CAPTURE-DOMAIN: ^[[RANGE_DONE]]:

// CAPTURE-DOMAIN-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// CAPTURE-DOMAIN-FALLBACK: arith.cmpi ule
// CAPTURE-DOMAIN-FALLBACK: arith.cmpi uge
// CAPTURE-DOMAIN-FALLBACK: arith.cmpi ugt
// CAPTURE-DOMAIN-FALLBACK: arith.cmpi ult
// CAPTURE-DOMAIN-FALLBACK: obelisk_sim.random.solve

// A zero i64 cardinality denotes all 2^64 values. Generated MLIR substitutes
// one only as the modulo divisor and selects the unmodified draw as the full-
// domain index. The same sentinel makes cyclic retry advancement wrap in i64.
// CAPTURE-DOMAIN-64-LABEL: obelisk_sim.func private @unit_1
// CAPTURE-DOMAIN-64: %[[RANGE_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN-64: %[[CARDINALITY:.*]] = arith.addi
// CAPTURE-DOMAIN-64: cf.cond_br %[[RANGE_VALID]], ^[[SAMPLE:bb[0-9]+]], ^[[EMPTY:bb[0-9]+]]
// CAPTURE-DOMAIN-64: ^[[SAMPLE]]:
// CAPTURE-DOMAIN-64: %[[FULL:.*]] = arith.cmpi eq, %[[CARDINALITY]], %{{c0_i64.*}} : i64
// CAPTURE-DOMAIN-64: %[[SAFE:.*]] = arith.select %[[FULL]], %{{c1_i64.*}}, %[[CARDINALITY]] : i64
// CAPTURE-DOMAIN-64: arith.remui {{.*}}, %[[SAFE]] : i64
// CAPTURE-DOMAIN-64: cf.br
// CAPTURE-DOMAIN-64: ^[[EMPTY]]:
// CAPTURE-DOMAIN-64: obelisk_sim.managed.store {{.*}} : i64
// CAPTURE-DOMAIN-64: %[[REDUCED:.*]] = arith.remui {{.*}}, %[[SAFE]] : i64
// CAPTURE-DOMAIN-64: arith.select %[[FULL]], {{.*}}, %[[REDUCED]] : i64
// CAPTURE-DOMAIN-64: obelisk_sim.managed.store
// CAPTURE-DOMAIN-64: arith.remui {{.*}}, %[[SAFE]] : i64
// CAPTURE-DOMAIN-64: arith.select %[[FULL]]
// CAPTURE-DOMAIN-64-NOT: obelisk_sim.random.solve

// CAPTURE-DOMAIN-64-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// CAPTURE-DOMAIN-64-FALLBACK: arith.cmpi uge
// CAPTURE-DOMAIN-64-FALLBACK: arith.cmpi ule
// CAPTURE-DOMAIN-64-FALLBACK: obelisk_sim.random.solve

// Signed captures are biased with the sign bit before interval arithmetic and
// sampled values are un-biased before insertion into the aggregate assignment.
// Strict signed extrema take the same direct-failure edge as unsigned extrema.
// CAPTURE-DOMAIN-SIGNED-LABEL: obelisk_sim.func private @unit_1
// CAPTURE-DOMAIN-SIGNED-COUNT-2: arith.xori {{.*}}, %{{c8_i64.*}} : i64
// CAPTURE-DOMAIN-SIGNED: %[[INCLUSIVE_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN-SIGNED: %[[LOW_EDGE:.*]] = arith.cmpi ne, {{.*}}, %{{c15_i64.*}} : i64
// CAPTURE-DOMAIN-SIGNED: arith.addi
// CAPTURE-DOMAIN-SIGNED: %[[HIGH_EDGE:.*]] = arith.cmpi ne, {{.*}}, %{{c0_i64.*}} : i64
// CAPTURE-DOMAIN-SIGNED: arith.subi
// CAPTURE-DOMAIN-SIGNED: %[[STRICT_VALID:.*]] = arith.cmpi ule
// CAPTURE-DOMAIN-SIGNED: arith.andi
// CAPTURE-DOMAIN-SIGNED: cf.cond_br {{.*}}, ^[[SAMPLE:bb[0-9]+]], ^[[EMPTY:bb[0-9]+]]
// CAPTURE-DOMAIN-SIGNED: ^[[SAMPLE]]:
// CAPTURE-DOMAIN-SIGNED-NOT: obelisk_sim.random.solve
// CAPTURE-DOMAIN-SIGNED: ^[[EMPTY]]:
// CAPTURE-DOMAIN-SIGNED: obelisk_sim.managed.store {{.*}} : i64
// CAPTURE-DOMAIN-SIGNED-COUNT-2: arith.xori {{.*}}, %{{c8_i64.*}} : i64

// CAPTURE-DOMAIN-SIGNED-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// CAPTURE-DOMAIN-SIGNED-FALLBACK: arith.cmpi sge
// CAPTURE-DOMAIN-SIGNED-FALLBACK: arith.cmpi sle
// CAPTURE-DOMAIN-SIGNED-FALLBACK: arith.cmpi sgt
// CAPTURE-DOMAIN-SIGNED-FALLBACK: arith.cmpi slt
// CAPTURE-DOMAIN-SIGNED-FALLBACK: obelisk_sim.random.solve

// The even-value constraint has eight correlated solutions. Z3 enumerates
// the complete sorted table, and generated MLIR indexes it with three random
// bits, producing every legal assignment uniformly without checking or
// runtime solving.
// TABLE-LABEL: obelisk_sim.func private @unit_1
// TABLE-DAG: %[[ZERO:.*]] = arith.constant 0 : i64
// TABLE-DAG: %[[ONE:.*]] = arith.constant 1 : i64
// TABLE-DAG: %[[TWO:.*]] = arith.constant 2 : i64
// TABLE-DAG: %[[FOUR:.*]] = arith.constant 4 : i64
// TABLE-DAG: %[[SIX:.*]] = arith.constant 6 : i64
// TABLE-DAG: %[[SEVEN:.*]] = arith.constant 7 : i64
// TABLE-DAG: %[[EIGHT:.*]] = arith.constant 8 : i64
// TABLE-DAG: %[[TEN:.*]] = arith.constant 10 : i64
// TABLE-DAG: %[[TWELVE:.*]] = arith.constant 12 : i64
// TABLE-DAG: %[[FOURTEEN:.*]] = arith.constant 14 : i64
// TABLE-DAG: %[[FIFTEEN:.*]] = arith.constant 15 : i64
// TABLE: %[[RAW:.*]] = arith.andi {{.*}}, %[[FIFTEEN]] : i64
// TABLE: %[[INDEX:.*]] = arith.andi %[[RAW]], %[[SEVEN]] : i64
// TABLE: %[[IS_ONE:.*]] = arith.cmpi eq, %[[INDEX]], %[[ONE]] : i64
// TABLE: %[[SELECT_ONE:.*]] = arith.select %[[IS_ONE]], %[[TWO]], %[[ZERO]] : i64
// TABLE: arith.select {{.*}}, %[[FOUR]], %[[SELECT_ONE]] : i64
// TABLE: arith.select {{.*}}, %[[SIX]], {{.*}} : i64
// TABLE: arith.select {{.*}}, %[[EIGHT]], {{.*}} : i64
// TABLE: arith.select {{.*}}, %[[TEN]], {{.*}} : i64
// TABLE: arith.select {{.*}}, %[[TWELVE]], {{.*}} : i64
// TABLE: %[[ASSIGNMENT:.*]] = arith.select {{.*}}, %[[FOURTEEN]], {{.*}} : i64
// TABLE-NOT: obelisk_sim.random.solve
// TABLE: arith.trunci %[[ASSIGNMENT]] : i64 to i4
// TABLE: obelisk_sim.managed.store

// TABLE-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// TABLE-FALLBACK: obelisk_sim.random.solve

// A three-value solution table uses generated rejection sampling over the full
// 64-bit object draw. Only UINT64_MAX is rejected, and retry PCG state is
// committed only on that edge. The accepted uniform index selects 0, 2, or 3.
// TABLE-BOUNDED-LABEL: obelisk_sim.func private @unit_1
// TABLE-BOUNDED: cf.br ^[[BOUNDED:[a-zA-Z0-9_]+]]({{.*}}, {{.*}} : i64, i64)
// TABLE-BOUNDED: ^[[BOUNDED]](%[[STATE:.*]]: i64, %[[DRAW:.*]]: i64):
// TABLE-BOUNDED: %[[THREE:.*]] = arith.constant {{.*}}3 : i64
// TABLE-BOUNDED: %[[INDEX:.*]] = arith.remui %[[DRAW]], %[[THREE]] : i64
// TABLE-BOUNDED: %[[MAX:.*]] = arith.constant {{.*}}-1 : i64
// TABLE-BOUNDED: %[[ACCEPTED:.*]] = arith.cmpi ult, %[[DRAW]], %[[MAX]] : i64
// TABLE-BOUNDED: cf.cond_br %[[ACCEPTED]], ^[[SELECT:[a-zA-Z0-9_]+]](%[[STATE]], %[[INDEX]] : i64, i64), ^[[BOUNDED]]
// TABLE-BOUNDED: ^[[SELECT]](%[[FINAL_STATE:.*]]: i64, %[[BOUNDED_INDEX:.*]]: i64):
// TABLE-BOUNDED: obelisk_sim.managed.store %[[FINAL_STATE]]
// TABLE-BOUNDED: arith.remui %[[BOUNDED_INDEX]], {{.*}} : i64
// TABLE-BOUNDED: arith.select
// TABLE-BOUNDED: arith.select
// TABLE-BOUNDED-NOT: obelisk_sim.random.solve
// TABLE-BOUNDED: obelisk_sim.managed.store

// Two independent four-bit constraints have 225 aggregate solutions, beyond
// the global table cap. Component planning emits two independent 15-entry
// tables and two unbiased object-stream draws, then commits without checking.
// COMPONENTS-LABEL: obelisk_sim.func private @unit_1
// COMPONENTS-COUNT-2: arith.cmpi ult
// COMPONENTS-COUNT-14: arith.select
// COMPONENTS-NOT: obelisk_sim.random.solve
// COMPONENTS: arith.trunci {{.*}} : i64 to i4
// COMPONENTS: arith.trunci {{.*}} : i64 to i4
// COMPONENTS: obelisk_sim.managed.store
// COMPONENTS: obelisk_sim.managed.store

// COMPONENTS-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// COMPONENTS-FALLBACK: obelisk_sim.random.solve

// A relation merges x and y into one component while an unconstrained
// eight-bit property remains outside it. The aggregate is too wide for global
// enumeration, but the connected component still becomes one exact table.
// COMPONENT-CORRELATED-LABEL: obelisk_sim.func private @unit_1
// COMPONENT-CORRELATED-COUNT-1: arith.cmpi ult
// COMPONENT-CORRELATED-COUNT-14: arith.select
// COMPONENT-CORRELATED-NOT: obelisk_sim.random.solve
// COMPONENT-CORRELATED-COUNT-2: arith.trunci {{.*}} : i64 to i4
// COMPONENT-CORRELATED: arith.trunci {{.*}} : i64 to i8
// COMPONENT-CORRELATED-COUNT-3: obelisk_sim.managed.store

// The first component has 31 solutions and exceeds the table cap. Planning
// continues to the later independent three-solution component, materializes
// that table, and retains checker/runtime solving for the oversized component.
// COMPONENT-PARTIAL-LABEL: obelisk_sim.func private @unit_1
// COMPONENT-PARTIAL: arith.cmpi ult
// COMPONENT-PARTIAL-COUNT-2: arith.select
// COMPONENT-PARTIAL-COUNT-2: arith.cmpi ne
// COMPONENT-PARTIAL: obelisk_sim.random.solve

// The capture-dependent y component becomes a parameterized interval, while
// the independent three-solution x component becomes a compile-time table.
// Their whole-formula proof is exact, so the composed sampler commits directly.
// COMPONENT-CAPTURE-LABEL: obelisk_sim.func private @unit_1
// COMPONENT-CAPTURE: arith.cmpi ult
// COMPONENT-CAPTURE-COUNT-2: arith.select
// COMPONENT-CAPTURE: arith.remui
// COMPONENT-CAPTURE: arith.cmpi ult
// COMPONENT-CAPTURE: arith.select
// COMPONENT-CAPTURE-NOT: arith.cmpi ne
// COMPONENT-CAPTURE-NOT: arith.cmpi ule
// COMPONENT-CAPTURE-NOT: obelisk_sim.random.solve
// COMPONENT-CAPTURE: obelisk_sim.managed.store

// A 31-value solution set exceeds the bounded compile-time table cap. It stays
// on the checker/runtime path and guards the structural reverse implication.
// TABLE-RESIDUAL-LABEL: obelisk_sim.func private @unit_1
// TABLE-RESIDUAL: arith.cmpi ne
// TABLE-RESIDUAL: obelisk_sim.random.solve

// ALIAS-LABEL: obelisk_sim.func private @unit_1
// ALIAS-DAG: %[[THREE:.*]] = arith.constant 3 : i64
// ALIAS-DAG: %[[TWO:.*]] = arith.constant 2 : i64
// ALIAS-DAG: %[[FOUR:.*]] = arith.constant 4 : i64
// ALIAS-DAG: %[[CLEAR_Y:.*]] = arith.constant -13 : i64
// ALIAS-DAG: %[[CLEAR_Z:.*]] = arith.constant -49 : i64
// ALIAS-DAG: %[[SIXTY_THREE:.*]] = arith.constant 63 : i64
// ALIAS: %[[RAW:.*]] = arith.andi {{.*}}, %[[SIXTY_THREE]] : i64
// ALIAS: %[[X:.*]] = arith.andi %[[RAW]], %[[THREE]] : i64
// ALIAS: %[[Y:.*]] = arith.shli %[[X]], %[[TWO]] : i64
// ALIAS: %[[REST:.*]] = arith.andi %[[RAW]], %[[CLEAR_Y]] : i64
// ALIAS: %[[XY:.*]] = arith.ori %[[REST]], %[[Y]] : i64
// ALIAS: %[[Z:.*]] = arith.shli %[[X]], %[[FOUR]] : i64
// ALIAS: %[[REST_Z:.*]] = arith.andi %[[XY]], %[[CLEAR_Z]] : i64
// ALIAS: %[[ASSIGNMENT:.*]] = arith.ori %[[REST_Z]], %[[Z]] : i64
// ALIAS-NOT: arith.cmpi
// ALIAS-NOT: obelisk_sim.random.solve
// ALIAS: obelisk_sim.managed.store
// ALIAS: obelisk_sim.managed.store
// ALIAS: obelisk_sim.managed.store

// ALIAS-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// ALIAS-FALLBACK: arith.cmpi eq
// ALIAS-FALLBACK: arith.cmpi eq
// ALIAS-FALLBACK: obelisk_sim.random.solve

// DEFINITION-LABEL: obelisk_sim.func private @unit_1
// DEFINITION-DAG: %[[ONE:.*]] = arith.constant 1 : i64
// DEFINITION-DAG: %[[TWO:.*]] = arith.constant 2 : i64
// DEFINITION-DAG: %[[THREE:.*]] = arith.constant 3 : i64
// DEFINITION-DAG: %[[FOUR:.*]] = arith.constant 4 : i64
// DEFINITION-DAG: %[[TEN:.*]] = arith.constant 10 : i64
// DEFINITION-DAG: %[[CLEAR_X:.*]] = arith.constant -4 : i64
// DEFINITION-DAG: %[[CLEAR_Y:.*]] = arith.constant -13 : i64
// DEFINITION-DAG: %[[FOUR_NINETY_FIVE:.*]] = arith.constant 4095 : i64
// DEFINITION: %[[RAW:.*]] = arith.andi {{.*}}, %[[FOUR_NINETY_FIVE]] : i64
// DEFINITION: %[[Z_SHIFTED:.*]] = arith.shrui %[[RAW]], %[[FOUR]] : i64
// DEFINITION: %[[Z:.*]] = arith.andi %[[Z_SHIFTED]], %[[THREE]] : i64
// DEFINITION: %[[Y_SUM:.*]] = arith.addi %[[Z]], %[[ONE]] : i64
// DEFINITION: %[[Y_VALUE:.*]] = arith.andi %[[Y_SUM]], %[[THREE]] : i64
// DEFINITION: %[[Y_PLACED:.*]] = arith.shli %[[Y_VALUE]], %[[TWO]] : i64
// DEFINITION: %[[NO_Y:.*]] = arith.andi %[[RAW]], %[[CLEAR_Y]] : i64
// DEFINITION: %[[WITH_Y:.*]] = arith.ori %[[NO_Y]], %[[Y_PLACED]] : i64
// DEFINITION: %[[Y_SHIFTED:.*]] = arith.shrui %[[WITH_Y]], %[[TWO]] : i64
// DEFINITION: %[[Y:.*]] = arith.andi %[[Y_SHIFTED]], %[[THREE]] : i64
// DEFINITION: %[[X_SUM:.*]] = arith.addi %[[Y]], %[[ONE]] : i64
// DEFINITION: %[[X_VALUE:.*]] = arith.andi %[[X_SUM]], %[[THREE]] : i64
// DEFINITION: %[[NO_X:.*]] = arith.andi %[[WITH_Y]], %[[CLEAR_X]] : i64
// DEFINITION: %[[WITH_X:.*]] = arith.ori %[[NO_X]], %[[X_VALUE]] : i64
// DEFINITION: %[[X_READ:.*]] = arith.andi %[[WITH_X]], %[[THREE]] : i64
// DEFINITION: %[[Z_READ_SHIFTED:.*]] = arith.shrui %[[WITH_X]], %[[FOUR]] : i64
// DEFINITION: %[[Z_READ:.*]] = arith.andi %[[Z_READ_SHIFTED]], %[[THREE]] : i64
// DEFINITION: arith.cmpi ult, %[[X_READ]], %[[Z_READ]] : i64
// DEFINITION: arith.extui
// DEFINITION: arith.shli
// DEFINITION: arith.cmpi ne
// DEFINITION: arith.select
// Definition expressions use total fixed-width arithmetic in the generated
// proposal. The guarded shift amount avoids arith shift poison, and power is
// expanded into modular multiplication rather than left for the runtime.
// DEFINITION: arith.divui
// DEFINITION: arith.cmpi uge
// DEFINITION: arith.select
// DEFINITION: arith.shli
// DEFINITION: arith.select
// DEFINITION: arith.remui
// DEFINITION: arith.muli
// DEFINITION: arith.muli
// DEFINITION: arith.shrui
// DEFINITION: arith.select
// The copy alias is fed from the already materialized x definition. The flag
// definition above reads that same canonical alias representative.
// DEFINITION: arith.shli {{.*}}, %[[TEN]] : i64
// DEFINITION-NOT: obelisk_sim.random.solve
// DEFINITION: obelisk_sim.managed.store
// DEFINITION: obelisk_sim.managed.store
// DEFINITION: obelisk_sim.managed.store
// DEFINITION: obelisk_sim.managed.store
// DEFINITION: obelisk_sim.managed.store
// DEFINITION: obelisk_sim.managed.store
// DEFINITION: obelisk_sim.managed.store

// A definition target whose inferred domain is narrower than its bit width must
// retain the checker and runtime fallback: overwriting the sampled domain with
// the definition alone does not prove that their conjunction holds.
// COMPOSE-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// COMPOSE-FALLBACK: arith.xori
// COMPOSE-FALLBACK: arith.cmpi ne
// COMPOSE-FALLBACK: arith.cmpi ne
// COMPOSE-FALLBACK: arith.andi
// COMPOSE-FALLBACK: arith.extui
// COMPOSE-FALLBACK: arith.cmpi eq
// COMPOSE-FALLBACK: obelisk_sim.random.solve

// DEFINITION-FALLBACK-LABEL: obelisk_sim.func private @unit_1
// DEFINITION-FALLBACK: arith.addi
// DEFINITION-FALLBACK: arith.cmpi eq
// DEFINITION-FALLBACK: obelisk_sim.random.solve

//--- unsupported.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 100 : i64, sym_name = "s100.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 101 : i64, sym_name = "s101.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 102 : i64, sym_name = "s102"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "unsupported", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "unsupported", node_id = 103 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>, sym_name = "s103.unsupported", this_variable_path = "unsupported::this", this_variable_symbol = @s101.$root::@s102::@s103.unsupported::@s108.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "unsupported::value", name = "value", node_id = 104 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s104.value"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "unsupported::enum_value", name = "enum_value", node_id = 132 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s132.enum_value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "unsupported::rules", name = "rules", node_id = 105 : i64, sym_name = "s105.rules", this_variable_path = "unsupported::rules.this", this_variable_symbol = @s101.$root::@s102::@s103.unsupported::@s105.rules::@s106.this} {
          obelisk.sv.constraint.list attributes {item_count = 5 : i64, node_id = 110 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 111 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 112 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
            obelisk.sv.constraint.implication attributes {node_id = 136 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 137 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 138 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 139 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 113 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 114 : i64, referenced_path = "unsupported::value", referenced_symbol = @s101.$root::@s102::@s103.unsupported::@s104.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 115 : i64, referenced_path = "unsupported::value", referenced_symbol = @s101.$root::@s102::@s103.unsupported::@s104.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 116 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 117 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 118 : i64, referenced_path = "unsupported::value", referenced_symbol = @s101.$root::@s102::@s103.unsupported::@s104.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 133 : i64} {
              obelisk.sv.expression.unary_op attributes {node_id = 134 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 135 : i64, referenced_path = "unsupported::value", referenced_symbol = @s101.$root::@s102::@s103.unsupported::@s104.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported::rules.this", is_compiler_generated, is_const, name = "this", node_id = 120 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>, sym_name = "s106.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "unsupported::pre_randomize", is_pre_post_randomize, name = "pre_randomize", node_id = 121 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s107.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 122 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported::this", is_compiler_generated, is_const, name = "this", node_id = 123 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>, sym_name = "s108.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 124 : i64, referenced_path = "top", referenced_symbol = @s100.top, sym_name = "s109.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 125 : i64, sym_name = "s110.top"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 126 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>, sym_name = "s111.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 127 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 128 : i64, procedure_kind = 0 : i32, sym_name = "s112", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 129 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 130 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s101.$root::@s109.top::@s110.top} {
              obelisk.sv.expression.named_value attributes {node_id = 131 : i64, referenced_path = "top.object", referenced_symbol = @s101.$root::@s109.top::@s110.top::@s111.object, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- domain.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::fixed", name = "fixed", node_id = 5 : i64, sym_name = "s5.fixed", this_variable_path = "C::fixed.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.fixed::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "8", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::fixed.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 32 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 33 : i64, sym_name = "s20.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s21.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s22.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

//--- domain-bounded.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::bounded", name = "bounded", node_id = 5 : i64, sym_name = "s5.bounded", this_variable_path = "C::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.bounded::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "9", node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 13 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 18 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s11.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 19 : i64, sym_name = "s12.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 22 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- domain-residual.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 8 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s11.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.y"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::residual", name = "residual", node_id = 6 : i64, sym_name = "s6.residual", this_variable_path = "C::residual.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.residual::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 3 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 9 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "9", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 12 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 13 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "9", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 16 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 17 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::residual.this", is_compiler_generated, is_const, name = "this", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s8.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 27 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s11.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 28 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s12.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 29 : i64, sym_name = "s13.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 30 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s14.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 31 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 32 : i64, procedure_kind = 0 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 33 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s12.top::@s13.top} {
              obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s12.top::@s13.top::@s14.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- table.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::even", name = "even", node_id = 5 : i64, sym_name = "s5.even", this_variable_path = "C::even.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.even::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.binary_op attributes {node_id = 9 : i64, operator_kind = 4 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 27 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 28 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::even.this", is_compiler_generated, is_const, name = "this", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 13 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 18 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s11.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 19 : i64, sym_name = "s12.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 22 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- table-bounded.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::not_one", name = "not_one", node_id = 5 : i64, sym_name = "s5.not_one", this_variable_path = "C::not_one.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.not_one::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::not_one.this", is_compiler_generated, is_const, name = "this", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 13 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 18 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s11.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 19 : i64, sym_name = "s12.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 22 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- components.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 8 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s11.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.y"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::independent", name = "independent", node_id = 6 : i64, sym_name = "s6.independent", this_variable_path = "C::independent.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.independent::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 9 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 12 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 13 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::independent.this", is_compiler_generated, is_const, name = "this", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 17 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s8.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 18 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 23 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s11.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 24 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s12.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 25 : i64, sym_name = "s13.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s14.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 27 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 28 : i64, procedure_kind = 0 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s12.top::@s13.top} {
              obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s12.top::@s13.top::@s14.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- components-capture.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "limit", lifetime = 1 : i32, name = "limit", node_id = 32 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s16.limit"} {
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 6 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s11.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.y"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::mixed", name = "mixed", node_id = 6 : i64, sym_name = "s6.mixed", this_variable_path = "C::mixed.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.mixed::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 9 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 12 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 13 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s16.limit, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::mixed.this", is_compiler_generated, is_const, name = "this", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 17 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s8.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 18 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 23 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s11.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 24 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s12.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 25 : i64, sym_name = "s13.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s14.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 27 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 28 : i64, procedure_kind = 0 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s12.top::@s13.top} {
              obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s12.top::@s13.top::@s14.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- components-correlated.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 16 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s12.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.y"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::z", name = "z", node_id = 6 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s6.z"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::correlated", name = "correlated", node_id = 7 : i64, sym_name = "s7.correlated", this_variable_path = "C::correlated.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s7.correlated::@s8.this} {
          obelisk.sv.constraint.list attributes {item_count = 3 : i64, node_id = 8 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 9 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 10 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 13 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 14 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 17 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 18 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::correlated.this", is_compiler_generated, is_const, name = "this", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s8.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 22 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s9.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 23 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 24 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 25 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 26 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 28 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s12.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 29 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s13.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 30 : i64, sym_name = "s14.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 31 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s15.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 32 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 33 : i64, procedure_kind = 0 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s13.top::@s14.top} {
              obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s13.top::@s14.top::@s15.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- components-partial.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 7 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s11.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<4 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.y"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::partial", name = "partial", node_id = 6 : i64, sym_name = "s6.partial", this_variable_path = "C::partial.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.partial::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 9 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<4 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<4 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 12 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 13 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::partial.this", is_compiler_generated, is_const, name = "this", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 17 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s8.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 18 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 23 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s11.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 24 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s12.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 25 : i64, sym_name = "s13.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s14.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 27 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 28 : i64, procedure_kind = 0 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s12.top::@s13.top} {
              obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s12.top::@s13.top::@s14.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- capture-domain.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "limit", lifetime = 1 : i32, name = "limit", node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s15.limit"} {
      }
      obelisk.sv.symbol.variable attributes {hierarchical_name = "low", lifetime = 1 : i32, name = "low", node_id = 33 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s17.low"} {
      }
      obelisk.sv.symbol.variable attributes {hierarchical_name = "high", lifetime = 1 : i32, name = "high", node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s18.high"} {
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 16 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::high", name = "high", node_id = 28 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s16.high"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::range", name = "range", node_id = 35 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s19.range"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::strict_range", name = "strict_range", node_id = 44 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s20.strict_range"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::bounded", name = "bounded", node_id = 5 : i64, sym_name = "s5.bounded", this_variable_path = "C::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.bounded::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 6 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s15.limit, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 29 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 30 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "C::high", referenced_symbol = @s1.$root::@s2::@s3.C::@s16.high, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "limit", referenced_symbol = @s1.$root::@s2::@s15.limit, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 36 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 37 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "C::range", referenced_symbol = @s1.$root::@s2::@s3.C::@s19.range, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s17.low, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 40 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 41 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "C::range", referenced_symbol = @s1.$root::@s2::@s3.C::@s19.range, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s18.high, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 45 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 46 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "C::strict_range", referenced_symbol = @s1.$root::@s2::@s3.C::@s20.strict_range, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s17.low, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 49 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 50 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "C::strict_range", referenced_symbol = @s1.$root::@s2::@s3.C::@s20.strict_range, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 52 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s18.high, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 19 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s11.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 20 : i64, sym_name = "s12.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 23 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- capture-domain-64.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "low", lifetime = 1 : i32, name = "low", node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s15.low"} {
      }
      obelisk.sv.symbol.variable attributes {hierarchical_name = "high", lifetime = 1 : i32, name = "high", node_id = 27 : i64, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s16.high"} {
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::bounded", name = "bounded", node_id = 5 : i64, sym_name = "s5.bounded", this_variable_path = "C::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.bounded::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s15.low, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 28 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 29 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s16.high, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 19 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s11.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 20 : i64, sym_name = "s12.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 23 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- capture-domain-signed.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "low", lifetime = 1 : i32, name = "low", node_id = 26 : i64, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>, sym_name = "s15.low"} {
      }
      obelisk.sv.symbol.variable attributes {hierarchical_name = "high", lifetime = 1 : i32, name = "high", node_id = 27 : i64, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>, sym_name = "s16.high"} {
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 8 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::inclusive", name = "inclusive", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>, sym_name = "s4.inclusive"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::strict", name = "strict", node_id = 28 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>, sym_name = "s17.strict"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::bounded", name = "bounded", node_id = 5 : i64, sym_name = "s5.bounded", this_variable_path = "C::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.bounded::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 4 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::inclusive", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.inclusive, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s15.low, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 29 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 30 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "C::inclusive", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.inclusive, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s16.high, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 33 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 34 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "C::strict", referenced_symbol = @s1.$root::@s2::@s3.C::@s17.strict, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "low", referenced_symbol = @s1.$root::@s2::@s15.low, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 37 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 38 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "C::strict", referenced_symbol = @s1.$root::@s2::@s3.C::@s17.strict, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "high", referenced_symbol = @s1.$root::@s2::@s16.high, semantic_type = !obelisk.integral<4, true, false, 3 : 0, bit>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 19 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s11.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 20 : i64, sym_name = "s12.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 23 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- table-residual.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 5 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<4 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::not_one", name = "not_one", node_id = 5 : i64, sym_name = "s5.not_one", this_variable_path = "C::not_one.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.not_one::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<4 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<4 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::not_one.this", is_compiler_generated, is_const, name = "this", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 13 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 18 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s11.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 19 : i64, sym_name = "s12.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s13.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 22 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- alias.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 6 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 44 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s25.y"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::z", name = "z", node_id = 45 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s26.z"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::same", name = "same", node_id = 5 : i64, sym_name = "s5.same", this_variable_path = "C::same.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.same::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s25.y, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 46 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 47 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s25.y, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "C::z", referenced_symbol = @s1.$root::@s2::@s3.C::@s26.z, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::same.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 32 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 33 : i64, sym_name = "s20.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s21.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s22.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

//--- definition.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 12 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 44 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s25.y"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::z", name = "z", node_id = 48 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s26.z"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::flag", name = "flag", node_id = 55 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s27.flag"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::pick", name = "pick", node_id = 56 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s28.pick"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::op", name = "op", node_id = 94 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s29.op"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::copy", name = "copy", node_id = 122 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s30.copy"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::defined", name = "defined", node_id = 5 : i64, sym_name = "s5.defined", this_variable_path = "C::defined.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.defined::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 6 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.binary_op attributes {node_id = 45 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 46 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s25.y, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 47 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 49 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 50 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s25.y, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.binary_op attributes {node_id = 52 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_path = "C::z", referenced_symbol = @s1.$root::@s2::@s3.C::@s26.z, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 54 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 57 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 58 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "C::flag", referenced_symbol = @s1.$root::@s2::@s3.C::@s27.flag, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
                obelisk.sv.expression.binary_op attributes {node_id = 60 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {node_id = 61 : i64, referenced_path = "C::copy", referenced_symbol = @s1.$root::@s2::@s3.C::@s30.copy, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "C::z", referenced_symbol = @s1.$root::@s2::@s3.C::@s26.z, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 63 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 64 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_path = "C::pick", referenced_symbol = @s1.$root::@s2::@s3.C::@s28.pick, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 66 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 67 : i64, referenced_path = "C::flag", referenced_symbol = @s1.$root::@s2::@s3.C::@s27.flag, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 68 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s25.y, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 69 : i64, referenced_path = "C::z", referenced_symbol = @s1.$root::@s2::@s3.C::@s26.z, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 104 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 105 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.conversion attributes {node_id = 106 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 121 : i64, referenced_path = "C::op", referenced_symbol = @s1.$root::@s2::@s3.C::@s29.op, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                obelisk.sv.expression.binary_op attributes {node_id = 107 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.binary_op attributes {node_id = 108 : i64, operator_kind = 4 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.binary_op attributes {node_id = 109 : i64, operator_kind = 23 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.binary_op attributes {node_id = 110 : i64, operator_kind = 3 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.conversion attributes {node_id = 111 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.named_value attributes {node_id = 120 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                          }
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 112 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 113 : i64, referenced_path = "C::z", referenced_symbol = @s1.$root::@s2::@s3.C::@s26.z, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 114 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                  obelisk.sv.expression.binary_op attributes {node_id = 115 : i64, operator_kind = 24 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.binary_op attributes {node_id = 116 : i64, operator_kind = 27 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.named_value attributes {node_id = 117 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s25.y, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 118 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 119 : i64, referenced_path = "C::z", referenced_symbol = @s1.$root::@s2::@s3.C::@s26.z, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 123 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 124 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 125 : i64, referenced_path = "C::copy", referenced_symbol = @s1.$root::@s2::@s3.C::@s30.copy, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 126 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::defined.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 32 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 33 : i64, sym_name = "s20.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s21.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s22.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 70 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 71 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
              obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 72 : i64} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 73 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 74 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 75 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 76 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 95 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 96 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 97 : i64, referenced_path = "C::op", referenced_symbol = @s1.$root::@s2::@s3.C::@s29.op, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                    obelisk.sv.expression.binary_op attributes {node_id = 98 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.binary_op attributes {node_id = 99 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        obelisk.sv.expression.named_value attributes {node_id = 100 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                        obelisk.sv.expression.named_value attributes {node_id = 101 : i64, referenced_path = "C::z", referenced_symbol = @s1.$root::@s2::@s3.C::@s26.z, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                      obelisk.sv.expression.unary_op attributes {node_id = 102 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 103 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.expression.named_value attributes {node_id = 77 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

//--- unsat.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::impossible", name = "impossible", node_id = 5 : i64, sym_name = "s5.impossible", this_variable_path = "C::impossible.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.impossible::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::impossible.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 32 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 33 : i64, sym_name = "s20.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s21.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s22.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
