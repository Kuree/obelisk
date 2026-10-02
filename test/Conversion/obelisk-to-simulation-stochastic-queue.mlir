// RUN: obelisk-opt %s --lower-obelisk-to-sim='vpi=read' | FileCheck %s

// IEEE 1800-2017 20.16 semantic-IR lowering. Keep source-call ownership,
// output-argument copyback, four-state payload conversion, and frozen scope
// time scaling above the executable Simulation-IR runtime test.

module {
  obelisk.sv.symbol.definition @s0.top attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {
    }
    obelisk.sv.symbol.instance @s3.top attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 3 : i64, referenced_path = "top",
      referenced_symbol = @s0.top
    } {
      obelisk.sv.symbol.instance_body @s4.top attributes {
        hierarchical_name = "top", name = "top", node_id = 4 : i64,
        time_precision_fs = 1 : i64,
        time_unit_fs = 1000 : i64
      } {
        obelisk.sv.symbol.variable @s5.status attributes {
          hierarchical_name = "top.status", lifetime = 1 : i32,
          name = "status", node_id = 5 : i64,
          semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
        } {
        }
        obelisk.sv.symbol.variable @s6.job attributes {
          hierarchical_name = "top.job", lifetime = 1 : i32,
          name = "job", node_id = 6 : i64,
          semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
        } {
        }
        obelisk.sv.symbol.variable @s7.inform attributes {
          hierarchical_name = "top.inform", lifetime = 1 : i32,
          name = "inform", node_id = 7 : i64,
          semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
        } {
        }
        obelisk.sv.symbol.variable @s8.stat attributes {
          hierarchical_name = "top.stat", lifetime = 1 : i32,
          name = "stat", node_id = 8 : i64,
          semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
        } {
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {
          hierarchical_name = "top", node_id = 9 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1 : i64, time_unit_fs = 1000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
            obelisk.sv.statement.expression_statement attributes {
              node_id = 11 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 4 : i64, callee_name = "$q_initialize",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0, 0, 0>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = true, has_this_class = false,
                is_signed = false, is_super_class = false,
                is_system_call = true, node_id = 12 : i64,
                semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
                system_scope_path = "top",
                system_scope_symbol = @s1.$root::@s3.top::@s4.top
              } {
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "7", is_declared_unsized = true,
                  is_signed = true, node_id = 13 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1", is_declared_unsized = true,
                  is_signed = true, node_id = 14 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "4", is_declared_unsized = true,
                  is_signed = true, node_id = 15 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = true,
                  node_id = 16 : i64,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 17 : i64,
                    referenced_path = "top.status",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.status,
                    semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                  } {
                  }
                  obelisk.sv.expression.empty_argument attributes {
                    is_signed = true, node_id = 18 : i64,
                    semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                  } {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 19 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 4 : i64, callee_name = "$q_add",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0, 0, 0>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = true, has_this_class = false,
                is_signed = false, is_super_class = false,
                is_system_call = true, node_id = 20 : i64,
                semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
                system_scope_path = "top",
                system_scope_symbol = @s1.$root::@s3.top::@s4.top
              } {
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "7", is_declared_unsized = true,
                  is_signed = true, node_id = 21 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "32'bxxxxxxxxxxxxxxxxxxxxxxxxxxxx0001",
                  is_signed = false, node_id = 22 : i64,
                  semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "32'bzzzzzzzzzzzzzzzzzzzzzzzzzzzz0010",
                  is_signed = false, node_id = 23 : i64,
                  semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
                } {
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 24 : i64,
                  referenced_path = "top.status",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.status,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 25 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 4 : i64, callee_name = "$q_remove",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0, 0, 0>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = true, has_this_class = false,
                is_signed = false, is_super_class = false,
                is_system_call = true, node_id = 26 : i64,
                semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
                system_scope_path = "top",
                system_scope_symbol = @s1.$root::@s3.top::@s4.top
              } {
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "7", is_declared_unsized = true,
                  is_signed = true, node_id = 27 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 28 : i64,
                  referenced_path = "top.job",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.job,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                }
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = true,
                  node_id = 29 : i64,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 30 : i64,
                    referenced_path = "top.inform",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.inform,
                    semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                  } {
                  }
                  obelisk.sv.expression.empty_argument attributes {
                    is_signed = true, node_id = 31 : i64,
                    semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                  } {
                  }
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 32 : i64,
                  referenced_path = "top.status",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.status,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 33 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64, callee_name = "$q_full",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = true, has_this_class = false,
                is_signed = true, is_super_class = false,
                is_system_call = true, node_id = 34 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                subroutine_kind = 0 : i32, system_scope_path = "top",
                system_scope_symbol = @s1.$root::@s3.top::@s4.top
              } {
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "7", is_declared_unsized = true,
                  is_signed = true, node_id = 35 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 36 : i64,
                  referenced_path = "top.status",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.status,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 37 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 4 : i64, callee_name = "$q_exam",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0, 0, 0>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = true, has_this_class = false,
                is_signed = false, is_super_class = false,
                is_system_call = true, node_id = 38 : i64,
                semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
                system_scope_path = "top",
                system_scope_symbol = @s1.$root::@s3.top::@s4.top
              } {
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "7", is_declared_unsized = true,
                  is_signed = true, node_id = 39 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "6", is_declared_unsized = true,
                  is_signed = true, node_id = 40 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 41 : i64,
                  referenced_path = "top.stat",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.stat,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = true, node_id = 42 : i64,
                  referenced_path = "top.status",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.status,
                  semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
                } {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.stochastic_queue {{.*}} {action = #simulation.stochastic_queue_action<initialize>, unit_scale = 1000 : i64}
// CHECK: simulation.ref.store {{.*}} : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
// CHECK: simulation.stochastic_queue {{.*}} {action = #simulation.stochastic_queue_action<add>, unit_scale = 1000 : i64}
// CHECK: simulation.ref.store
// CHECK: simulation.stochastic_queue {{.*}} {action = #simulation.stochastic_queue_action<remove>, unit_scale = 1000 : i64}
// CHECK: simulation.ref.store
// CHECK: simulation.ref.store
// CHECK: simulation.ref.store
// CHECK: simulation.stochastic_queue {{.*}} {action = #simulation.stochastic_queue_action<full>, unit_scale = 1000 : i64}
// CHECK: simulation.ref.store
// CHECK: simulation.stochastic_queue {{.*}} {action = #simulation.stochastic_queue_action<exam>, unit_scale = 1000 : i64}
// CHECK: simulation.ref.store
// CHECK: simulation.ref.store
