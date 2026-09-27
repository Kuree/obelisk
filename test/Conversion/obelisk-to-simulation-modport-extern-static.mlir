// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/extern_implementation_count = 1/extern_implementation_count = 0/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: sed 's/extern_implementation_count = 1/extern_implementation_count = 2/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=MULTI
// RUN: sed 's/extern_implementation_paths = \["top.p.foo"\]/extern_implementation_paths = []/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=METADATA
// RUN: sed 's/extern_implementation_paths = \["top.p.foo"\]/extern_implementation_paths = ["top.missing.foo"]/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=RESOLUTION
// RUN: sed 's/is_interface_extern, name = "foo"/is_fork_join, is_interface_extern, name = "foo"/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=FORKJOIN
// RUN: sed '/hierarchical_name = "top.p.foo"/s/subroutine_kind = 0/subroutine_kind = 1/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=ABI

// A static call names the empty interface-extern stub, but freezes the sole
// modport-exported implementation as an ordinary direct function call.

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
            obelisk.sv.symbol.method_prototype attributes {extern_implementation_count = 1 : i64, extern_implementation_paths = ["top.p.foo"], extern_implementation_symbols = [@s3.$root::@s6.top::@s11.p::@s12.provider::@s13.foo], hierarchical_name = "top.x.foo", is_interface_extern, name = "foo", node_id = 9 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, subroutine_path = "top.x.foo", subroutine_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.foo::@s10.foo, sym_name = "s9.foo"} {
              obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.x.foo", is_interface_extern, name = "foo", node_id = 10 : i64, prototype_path = "top.x.foo", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.foo, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s10.foo", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
                obelisk.sv.statement.list attributes {node_id = 11 : i64} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.p", is_uninstantiated = false, name = "p", node_id = 12 : i64, referenced_path = "provider", referenced_symbol = @s1.provider, sym_name = "s11.p"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.p", name = "provider", node_id = 13 : i64, sym_name = "s12.provider", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32, hierarchical_name = "top.p.foo", name = "foo", node_id = 14 : i64, prototype_path = "top.x.foo", prototype_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.foo, return_variable_path = "top.p.foo.foo", return_variable_symbol = @s3.$root::@s5.top::@s6.top::@s11.p::@s12.provider::@s13.foo::@s14.foo, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s13.foo", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "top.p.foo.foo", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s11.p::@s12.provider::@s13.foo::@s14.foo, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "top.p.bias", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s11.p::@s12.provider::@s18.bias, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.symbol.variable attributes {hierarchical_name = "top.p.foo.foo", is_compiler_generated, name = "foo", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.foo"} {
              }
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.p.bias", lifetime = 1 : i32, name = "bias", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s18.bias"} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 21 : i64, procedure_kind = 0 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "top.value", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s15.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "foo", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, node_id = 25 : i64, referenced_path = "top.x.foo", referenced_symbol = @s3.$root::@s5.top::@s6.top::@s7.x::@s8.I::@s9.foo::@s10.foo, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.storage.decl [[BIAS:[0-9]+]] in {{[0-9]+}} : i32 design hierarchy "top.p.bias"
// CHECK: simulation.code_unit.decl {{[0-9]+}} {{.*}} function hierarchy "top.x.foo"
// CHECK: simulation.code_unit.decl {{[0-9]+}} {{.*}} function hierarchy "top.p.foo"
// CHECK: simulation.func private @[[IMPL:unit_[0-9]+]]
// CHECK-SAME: descriptor = [[BIAS]]
// CHECK-SAME: simulation.hierarchical_name = "top.p.foo"
// CHECK: simulation.call @[[IMPL]](%{{.*}}) : (!simulation.context) -> i32

// MISSING: error: modport-exported interface extern has no implementation
// MULTI: error: modport-exported interface extern has 2 implementations; exactly one is executable
// METADATA: error: modport-exported interface extern has inconsistent implementation metadata
// RESOLUTION: error: modport-exported interface extern implementation does not resolve by matching symbol and path
// FORKJOIN: error: only an interface extern task may use fork/join aggregation
// ABI: error: modport-exported interface extern implementation has an incompatible subroutine ABI
