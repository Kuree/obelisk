// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed -e 's/extern_implementation_count = 2/extern_implementation_count = 0/' -e 's/extern_implementation_paths = \["top.q.work", "top.p.work"\]/extern_implementation_paths = []/' -e 's/extern_implementation_symbols = \[@s3.\$root::@s6.top::@s25.q::@s26.provider::@s27.work, @s3.\$root::@s6.top::@s15.p::@s16.provider::@s17.work\]/extern_implementation_symbols = []/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ZERO
// RUN: sed 's/extern_implementation_count = 2/extern_implementation_count = 3/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=METADATA

// A fork/join extern task statically spawns every provider through the
// ordinary task-call ABI. Output and inout retain completion-only copy-out,
// while ref stays live. A legal zero-provider instance reports a runtime error
// and returns without effect.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "I", name = "I", node_id = 0 : i64, sym_name = "s0.I"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "provider", name = "provider", node_id = 1 : i64, sym_name = "s1.provider"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 2 : i64, sym_name = "s2.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64, sym_name = "s3.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 4 : i64, sym_name = "s4"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 5 : i64, referenced_path = "top", referenced_symbol = @s2.top, sym_name = "s5.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 6 : i64, sym_name = "s6.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.x", is_uninstantiated = false, name = "x", node_id = 7 : i64, referenced_path = "I", referenced_symbol = @s0.I, sym_name = "s7.x"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.x", name = "I", node_id = 8 : i64, sym_name = "s8.I", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.x} {
            obelisk.sv.symbol.method_prototype attributes {extern_implementation_count = 2 : i64, extern_implementation_paths = ["top.q.work", "top.p.work"], extern_implementation_symbols = [@s3.$root::@s6.top::@s25.q::@s26.provider::@s27.work, @s3.$root::@s6.top::@s15.p::@s16.provider::@s17.work], hierarchical_name = "top.x.work", is_fork_join, is_interface_extern, name = "work", node_id = 9 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, subroutine_path = "top.x.work", subroutine_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.work::@s13.work, sym_name = "s9.work"} {
              obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "top.x.work.o", name = "o", node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s10.o"} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "top.x.work.io", name = "io", node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s11.io"} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.x.work.r", name = "r", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.r"} {
              }
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.x.work", is_interface_extern, name = "work", node_id = 13 : i64, prototype_path = "top.x.work", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.work, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s13.work", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 14 : i64} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "top.x.work.o", name = "o", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.o"} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "top.x.work.io", name = "io", node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.io"} {
                }
                obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.x.work.r", name = "r", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.r"} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.p", is_uninstantiated = false, name = "p", node_id = 18 : i64, referenced_path = "provider", referenced_symbol = @s1.provider, sym_name = "s15.p"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.p", name = "provider", node_id = 19 : i64, sym_name = "s16.provider", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.p.work", name = "work", node_id = 20 : i64, prototype_path = "top.x.work", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.work, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s17.work", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "top.p.bias", referenced_symbol = @s3.$root::@s6.top::@s15.p::@s16.provider::@s31.pbias, semantic_type = !obelisk.integral<32, false, true, 31 : 0, logic>} {
              }
              obelisk.sv.statement.timed attributes {node_id = 21 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 22 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 24 : i64} {
                }
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "top.p.work.o", name = "o", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s18.o"} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "top.p.work.io", name = "io", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s19.io"} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.p.work.r", name = "r", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s20.r"} {
              }
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "top.p.bias", is_implicit = false, name = "bias", net_kind = 1 : i32, node_id = 54 : i64, semantic_type = !obelisk.integral<32, false, true, 31 : 0, logic>, sym_name = "s31.pbias"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.q", is_uninstantiated = false, name = "q", node_id = 41 : i64, referenced_path = "provider", referenced_symbol = @s1.provider, sym_name = "s25.q"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.q", name = "provider", node_id = 42 : i64, sym_name = "s26.provider", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.q.work", name = "work", node_id = 43 : i64, prototype_path = "top.x.work", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.work, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s27.work", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 55 : i64, referenced_path = "top.q.bias", referenced_symbol = @s3.$root::@s6.top::@s25.q::@s26.provider::@s32.qbias, semantic_type = !obelisk.integral<32, false, true, 31 : 0, logic>} {
              }
              obelisk.sv.statement.timed attributes {node_id = 44 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 45 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 47 : i64} {
                }
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "top.q.work.o", name = "o", node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s28.o"} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "top.q.work.io", name = "io", node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s29.io"} {
              }
              obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.q.work.r", name = "r", node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s30.r"} {
              }
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "top.q.bias", is_implicit = false, name = "bias", net_kind = 1 : i32, node_id = 56 : i64, semantic_type = !obelisk.integral<32, false, true, 31 : 0, logic>, sym_name = "s32.qbias"} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.o", lifetime = 1 : i32, name = "o", node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s21.o"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.io", lifetime = 1 : i32, name = "io", node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.io"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.r"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 31 : i64, procedure_kind = 0 : i32, sym_name = "s24", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 33 : i64, referenced_path = "top.x.work", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.work::@s13.work, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 35 : i64, referenced_path = "top.o", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s21.o, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 38 : i64, referenced_path = "top.io", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s22.io, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 40 : i64, referenced_path = "top.r", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s23.r, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.statement.disable attributes {is_hierarchical = true, node_id = 51 : i64, target_path = "top.p.work", target_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.work::@s13.work} {
          }
          obelisk.sv.statement.disable attributes {is_hierarchical = true, node_id = 52 : i64, target_path = "top.x.work", target_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.work::@s13.work} {
          }
        }
      }
    }
  }
}

// CHECK: simulation.func private @[[AGG:unit_[0-9]+]]({{.*}}i32{{.*}}!simulation.ref<i32>{{.*}}i32{{.*}}!simulation.ref<i32>{{.*}}!simulation.ref<i32>
// CHECK-SAME: %[[PCAP:[^:]+]]: !simulation.net<!simulation.logic<32>>
// CHECK-SAME: %[[QCAP:[^:]+]]: !simulation.net<!simulation.logic<32>>
// CHECK-SAME: simulation.control_target_id = [[ACTRL:[0-9]+]]
// CHECK-SAME: simulation.hierarchical_name = "top.x.work"
// CHECK: simulation.spawn @[[B0:[^ (]+]]({{.*}}%[[PCAP]]) :
// The q implementation does not read its interface net, so the branch-local
// capture is pruned while the aggregate ABI still accepts every candidate.
// CHECK-NEXT: simulation.spawn @[[B1:[^ (]+]](%arg0, %arg2, %arg3, %arg4) :
// CHECK-NEXT: simulation.suspend.join all
// CHECK-SAME: processes 2 to
// CHECK: simulation.func private @[[P:unit_[0-9]+]]
// CHECK-SAME: simulation.control_target_id = [[PCTRL:[0-9]+]]
// CHECK-SAME: simulation.hierarchical_name = "top.p.work"
// CHECK: simulation.func private @[[Q:unit_[0-9]+]]
// CHECK-SAME: simulation.hierarchical_name = "top.q.work"
// CHECK: simulation.func private @{{unit_[0-9]+}}
// CHECK: simulation.task.call @[[AGG]]
// CHECK: simulation.control.disable [[PCTRL]]
// CHECK: simulation.control.disable [[ACTRL]]
// CHECK: simulation.func private @[[B0]]({{.*}}%[[B0CAP:[^:]+]]: !simulation.net<!simulation.logic<32>>{{[^)]*}}) attributes
// CHECK: %[[B0CTRL:.*]] = simulation.control.enter [[ACTRL]]
// CHECK: simulation.control.boundary %[[B0CTRL]]
// CHECK: simulation.task.call @[[P]]({{.*}}%[[B0CAP]], %{{[^,)]+}}) arguments 7 to
// CHECK: simulation.func private @[[B1]](%arg0: !simulation.context{{.*}}, %arg1: !simulation.ref<i32>{{.*}}, %arg2: i32{{.*}}, %arg3: !simulation.ref<i32>{{.*}}) attributes
// CHECK: %[[B1CTRL:.*]] = simulation.control.enter [[ACTRL]]
// CHECK: simulation.control.boundary %[[B1CTRL]]
// CHECK: simulation.task.call @[[Q]](%arg0, %arg1, %arg2, %arg3, %[[B1CTRL]]) arguments 4 to

// ZERO: simulation.func private @{{unit_[0-9]+}}
// ZERO-SAME: simulation.hierarchical_name = "top.x.work"
// ZERO: simulation.bytes.constant "ERROR: interface extern fork/join task has no implementation"
// ZERO-NEXT: simulation.display
// ZERO: simulation.error
// ZERO-NOT: $extern_forkjoin

// METADATA: error: modport-exported interface extern fork/join task has inconsistent implementation metadata
