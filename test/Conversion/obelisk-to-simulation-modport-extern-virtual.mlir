// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed -e 's/is_interface_extern, name/is_fork_join, is_interface_extern, name/g' -e 's/() -> !obelisk.void, false/() -> (), true/g' -e 's/subroutine_kind = 0/subroutine_kind = 1/g' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=FORKJOIN

// Virtual dispatch compares the receiver against the real interface-instance
// scopes, while invoking the provider implementations in different modules.

module {
  obelisk.sv.symbol.definition @s0.I attributes {definition_kind = 1 : i32, hierarchical_name = "I", name = "I", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @s1.provider attributes {definition_kind = 0 : i32, hierarchical_name = "provider", name = "provider", node_id = 1 : i64} {
  }
  obelisk.sv.symbol.definition @s2.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 2 : i64} {
  }
  obelisk.sv.symbol.root @s3.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64} {
    obelisk.sv.symbol.compilation_unit @s4 attributes {hierarchical_name = "$unit", node_id = 4 : i64} {
    }
    obelisk.sv.symbol.instance @s5.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 5 : i64, referenced_path = "top", referenced_symbol = @s2.top} {
      obelisk.sv.symbol.instance_body @s6.top attributes {hierarchical_name = "top", name = "top", node_id = 6 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance @s7.x attributes {hierarchical_name = "top.x", is_uninstantiated = false, name = "x", node_id = 7 : i64, referenced_path = "I", referenced_symbol = @s0.I} {
          obelisk.sv.symbol.instance_body @s8.I attributes {hierarchical_name = "top.x", name = "I", node_id = 8 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.x} {
            obelisk.sv.symbol.method_prototype @s9.ping attributes {extern_implementation_count = 1 : i64, extern_implementation_paths = ["top.p.ping"], extern_implementation_symbols = [@s3.$root::@s6.top::@s15.p::@s16.provider::@s17.ping], hierarchical_name = "top.x.ping", is_interface_extern, name = "ping", node_id = 9 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, subroutine_path = "top.x.ping", subroutine_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.ping::@s10.ping} {
              obelisk.sv.symbol.subroutine @s10.ping attributes {hierarchical_name = "top.x.ping", is_interface_extern, name = "ping", node_id = 10 : i64, prototype_path = "top.x.ping", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.ping, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 11 : i64} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance @s11.y attributes {hierarchical_name = "top.y", is_uninstantiated = false, name = "y", node_id = 12 : i64, referenced_path = "I", referenced_symbol = @s0.I} {
          obelisk.sv.symbol.instance_body @s12.I attributes {hierarchical_name = "top.y", name = "I", node_id = 13 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.x} {
            obelisk.sv.symbol.method_prototype @s13.ping attributes {extern_implementation_count = 1 : i64, extern_implementation_paths = ["top.q.ping"], extern_implementation_symbols = [@s3.$root::@s6.top::@s19.q::@s20.provider::@s21.ping], hierarchical_name = "top.y.ping", is_interface_extern, name = "ping", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, subroutine_path = "top.y.ping", subroutine_symbol = @s3.$root::@s5.top::@s6.top::@s11.y::@s12.I::@s13.ping::@s14.ping} {
              obelisk.sv.symbol.subroutine @s14.ping attributes {hierarchical_name = "top.y.ping", is_interface_extern, name = "ping", node_id = 15 : i64, prototype_path = "top.y.ping", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s11.y::@s12.I::@s13.ping, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 16 : i64} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance @s15.p attributes {hierarchical_name = "top.p", is_uninstantiated = false, name = "p", node_id = 17 : i64, referenced_path = "provider", referenced_symbol = @s1.provider} {
          obelisk.sv.symbol.instance_body @s16.provider attributes {hierarchical_name = "top.p", name = "provider", node_id = 18 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.subroutine @s17.ping attributes {hierarchical_name = "top.p.ping", name = "ping", node_id = 19 : i64, prototype_path = "top.x.ping", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.ping, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 40 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 44 : i64, referenced_path = "top.p.bias", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s15.p::@s16.provider::@s40.bias, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 45 : i64, referenced_path = "top.p.bias", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s15.p::@s16.provider::@s40.bias, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.symbol.variable @s40.bias attributes {hierarchical_name = "top.p.bias", lifetime = 1 : i32, name = "bias", node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.instance @s19.q attributes {hierarchical_name = "top.q", is_uninstantiated = false, name = "q", node_id = 20 : i64, referenced_path = "provider", referenced_symbol = @s1.provider} {
          obelisk.sv.symbol.instance_body @s20.provider attributes {hierarchical_name = "top.q", name = "provider", node_id = 21 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.subroutine @s21.ping attributes {hierarchical_name = "top.q.ping", name = "ping", node_id = 22 : i64, prototype_path = "top.y.ping", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s11.y::@s12.I::@s13.ping, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            }
          }
        }
        obelisk.sv.symbol.variable @s22.vif attributes {hierarchical_name = "top.vif", lifetime = 1 : i32, name = "vif", node_id = 23 : i64, semantic_type = !obelisk.virtual_interface<@s3.$root::@s6.top::@s7.x, "consumer">} {
        }
        obelisk.sv.symbol.procedural_block @s23 attributes {hierarchical_name = "top", node_id = 24 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 25 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 27 : i64, semantic_type = !obelisk.virtual_interface<@s3.$root::@s6.top::@s7.x, "consumer">} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.vif", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s22.vif, semantic_type = !obelisk.virtual_interface<@s3.$root::@s6.top::@s7.x, "consumer">} {
                }
                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 29 : i64, semantic_type = !obelisk.virtual_interface<@s3.$root::@s6.top::@s7.x, "consumer">} {
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 30 : i64, referenced_path = "top.y", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s11.y, semantic_type = !obelisk.virtual_interface<@s3.$root::@s6.top::@s7.x, "">} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
              obelisk.sv.expression.call attributes {virtual_interface_call_import, virtual_interface_call_modport = "consumer", argument_count = 0 : i64, callee_name = "ping", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 32 : i64, referenced_path = "top.I.ping", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s24.I::@s25.I::@s26.ping::@s27.ping, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.vif", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s22.vif, semantic_type = !obelisk.virtual_interface<@s3.$root::@s6.top::@s7.x, "consumer">} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance @s24.I attributes {hierarchical_name = "top.I", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "I", node_id = 34 : i64, referenced_path = "I", referenced_symbol = @s0.I} {
          obelisk.sv.symbol.instance_body @s25.I attributes {hierarchical_name = "top.I", is_virtual_interface_type_instance = true, name = "I", node_id = 35 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s3.$root::@s6.top::@s7.x} {
            obelisk.sv.symbol.method_prototype @s26.ping attributes {extern_implementation_count = 0 : i64, extern_implementation_paths = [], extern_implementation_symbols = [], hierarchical_name = "top.I.ping", is_interface_extern, name = "ping", node_id = 36 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, subroutine_path = "top.I.ping", subroutine_symbol = @s3.$root::@s5.top::@s6.top::@s24.I::@s25.I::@s26.ping::@s27.ping} {
              obelisk.sv.symbol.subroutine @s27.ping attributes {hierarchical_name = "top.I.ping", is_interface_extern, name = "ping", node_id = 37 : i64, prototype_path = "top.I.ping", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s24.I::@s25.I::@s26.ping, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            }
          }
        }
        }
      }
    }
  }
}

// CHECK: simulation.storage.decl [[PBIAS:[0-9]+]] in {{[0-9]+}} : i32 design hierarchy "top.p.bias"
// CHECK: simulation.code_unit.decl {{[0-9]+}} in [[X:[0-9]+]] function hierarchy "top.x.ping"
// CHECK: simulation.code_unit.decl {{[0-9]+}} in [[Y:[0-9]+]] function hierarchy "top.y.ping"
// CHECK: simulation.func private @[[P:unit_[0-9]+]]{{.*}}descriptor = [[PBIAS]]{{.*}}simulation.hierarchical_name = "top.p.ping"
// CHECK: simulation.func private @[[Q:unit_[0-9]+]]{{.*}}simulation.hierarchical_name = "top.q.ping"
// CHECK: %[[XCONST:.*]] = arith.constant [[X]] : i64
// CHECK: %[[SCOPE:.*]] = simulation.virtual_interface.scope
// CHECK: arith.cmpi eq, %[[SCOPE]], %[[XCONST]]
// CHECK: simulation.call @[[P]]
// CHECK: %[[YCONST:.*]] = arith.constant {{.*}}[[Y]] : i64
// CHECK: arith.cmpi eq, %[[SCOPE]], %[[YCONST]]
// CHECK: simulation.call @[[Q]]

// A fork/join task still dispatches by the real interface-instance scope; the
// selected stub then invokes its statically frozen provider aggregate.
// FORKJOIN: simulation.func private @[[XAGG:unit_[0-9]+]]
// FORKJOIN-SAME: simulation.hierarchical_name = "top.x.ping"
// FORKJOIN: simulation.spawn @[[XB:[^ (]+]]
// FORKJOIN-NEXT: simulation.suspend.join all
// FORKJOIN: simulation.func private @[[YAGG:unit_[0-9]+]]
// FORKJOIN-SAME: simulation.hierarchical_name = "top.y.ping"
// FORKJOIN: simulation.spawn @[[YB:[^ (]+]]
// FORKJOIN-NEXT: simulation.suspend.join all
// FORKJOIN: simulation.virtual_interface.scope
// FORKJOIN: simulation.task.call @[[XAGG]]
// FORKJOIN: simulation.task.call @[[YAGG]]
// FORKJOIN: simulation.func private @[[XB]]
// FORKJOIN: simulation.task.call @{{unit_[0-9]+}}
// FORKJOIN: simulation.func private @[[YB]]
// FORKJOIN: simulation.task.call @{{unit_[0-9]+}}
