// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "top",
    name = "top",
    node_id = 0 : i64,
    sym_name = "s0.top"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64,
      sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top",
      is_uninstantiated = false,
      name = "top",
      node_id = 3 : i64,
      referenced_path = "top",
      referenced_symbol = @s0.top,
      sym_name = "s3.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top",
        name = "top",
        node_id = 4 : i64,
        sym_name = "s4.top"
      } {
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top",
          node_id = 5 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s5",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 6 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 7 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$ceil",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 8 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 9 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 10 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 11 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$floor",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 12 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 13 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 14 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 15 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$sqrt",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 16 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 17 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 18 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 19 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$exp",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 20 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 21 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 22 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 23 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$ln",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 24 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 25 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 26 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 27 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$log10",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 28 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 29 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 30 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 31 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64,
                callee_name = "$pow",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 32 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 33 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 34 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 35 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 36 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64,
                callee_name = "$atan2",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 37 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 38 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 39 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 40 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 41 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64,
                callee_name = "$hypot",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 42 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 43 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$urandom",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 44 : i64,
                  semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 45 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              defaulted_arguments = array<i64>,
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 46 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_library_cell = "work.top",
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$itor",
                constraint_restrictions = [],
                defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 47 : i64,
                semantic_type = !obelisk.real,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "$random",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 48 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, integer>,
                  subroutine_kind = 0 : i32
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

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[RANDOM:.*]] = obelisk_sim.random.next
// CHECK: %[[INPUT:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[RESULT:.*]] = math.ceil %[[INPUT]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[RESULT]])
// CHECK: %[[FLOOR_INPUT:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[FLOOR_RESULT:.*]] = math.floor %[[FLOOR_INPUT]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[FLOOR_RESULT]])
// CHECK: %[[SQRT_INPUT:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[SQRT_RESULT:.*]] = math.sqrt %[[SQRT_INPUT]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[SQRT_RESULT]])
// CHECK: %[[EXP_INPUT:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[EXP_RESULT:.*]] = math.exp %[[EXP_INPUT]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[EXP_RESULT]])
// CHECK: %[[LN_INPUT:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[LN_RESULT:.*]] = math.log %[[LN_INPUT]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[LN_RESULT]])
// CHECK: %[[LOG10_INPUT:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[LOG10_RESULT:.*]] = math.log10 %[[LOG10_INPUT]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[LOG10_RESULT]])
// CHECK: %[[POW_BASE:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[POW_EXPONENT:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[POW_RESULT:.*]] = math.powf %[[POW_BASE]], %[[POW_EXPONENT]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[POW_RESULT]])
// CHECK: %[[ATAN2_Y:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[ATAN2_X:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[ATAN2_RESULT:.*]] = math.atan2 %[[ATAN2_Y]], %[[ATAN2_X]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[ATAN2_RESULT]])
// CHECK: %[[HYPOT_X:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[HYPOT_Y:.*]] = obelisk_sim.real.from_integer
// CHECK: %[[HYPOT_X_SQUARED:.*]] = arith.mulf %[[HYPOT_X]], %[[HYPOT_X]] : f64
// CHECK: %[[HYPOT_Y_SQUARED:.*]] = arith.mulf %[[HYPOT_Y]], %[[HYPOT_Y]] : f64
// CHECK: %[[HYPOT_SUM:.*]] = arith.addf %[[HYPOT_X_SQUARED]], %[[HYPOT_Y_SQUARED]] : f64
// CHECK: %[[HYPOT_RESULT:.*]] = math.sqrt %[[HYPOT_SUM]] : f64
// CHECK: obelisk_sim.display {{.*}}(%[[HYPOT_RESULT]])
// CHECK: %[[ITOR_INPUT:.*]] = obelisk_sim.random.next
// CHECK: %[[ITOR_RESULT:.*]] = obelisk_sim.real.from_integer
// CHECK: obelisk_sim.display {{.*}}(%[[ITOR_RESULT]])
