// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "child_if", name = "child_if", node_id = 0 : i64, sym_name = "child_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "parent_if", name = "parent_if", node_id = 1 : i64, sym_name = "parent_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 2 : i64, sym_name = "top_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @top_def, sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "top_body", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.parent", is_uninstantiated = false, name = "parent", node_id = 6 : i64, referenced_path = "parent_if", referenced_symbol = @parent_def, sym_name = "parent"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.parent", name = "parent_if", node_id = 7 : i64, sym_name = "parent_body", virtual_interface_identity = @root::@top_body::@parent} {
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.parent.child", is_uninstantiated = false, name = "child", node_id = 8 : i64, referenced_path = "child_if", referenced_symbol = @child_def, sym_name = "child"} {
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.parent.child", name = "child_if", node_id = 9 : i64, sym_name = "child_body", virtual_interface_identity = @root::@top_body::@parent_body::@child} {
                obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.parent.child.bump", name = "bump", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "parent_bump"} {}
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.other", is_uninstantiated = false, name = "other", node_id = 19 : i64, referenced_path = "parent_if", referenced_symbol = @parent_def, sym_name = "other"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.other", name = "parent_if", node_id = 20 : i64, sym_name = "other_body", virtual_interface_identity = @root::@top_body::@parent} {
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.other.child", is_uninstantiated = false, name = "child", node_id = 21 : i64, referenced_path = "child_if", referenced_symbol = @child_def, sym_name = "other_child"} {
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.other.child", name = "child_if", node_id = 22 : i64, sym_name = "other_child_body", virtual_interface_identity = @root::@top_body::@parent_body::@child} {
                obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.other.child.bump", name = "bump", node_id = 24 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "other_bump"} {}
              }
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.parent_handle", lifetime = 1 : i32, name = "parent_handle", node_id = 10 : i64, semantic_type = !obelisk.virtual_interface<@root::@top_body::@parent, "">, sym_name = "parent_handle"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.child_handle", lifetime = 1 : i32, name = "child_handle", node_id = 11 : i64, semantic_type = !obelisk.virtual_interface<@root::@top_body::@parent_body::@child, "">, sym_name = "child_handle"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "initial", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.virtual_interface<@root::@top_body::@parent_body::@child, "">} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.child_handle", referenced_symbol = @root::@top::@top_body::@child_handle, semantic_type = !obelisk.virtual_interface<@root::@top_body::@parent_body::@child, "">} {}
                obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "child", node_id = 17 : i64, referenced_path = "top.parent_if.child", referenced_symbol = @root::@top::@top_body::@parent::@parent_body::@child, semantic_type = !obelisk.virtual_interface<@root::@top_body::@parent_body::@child, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.parent_handle", referenced_symbol = @root::@top::@top_body::@parent_handle, semantic_type = !obelisk.virtual_interface<@root::@top_body::@parent, "">} {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "bump", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 31 : i64, referenced_path = "top.parent_if.child.bump", referenced_symbol = @root::@top::@top_body::@type_parent::@type_parent_body::@type_child::@type_child_body::@type_bump, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.parent_handle", referenced_symbol = @root::@top::@top_body::@parent_handle, semantic_type = !obelisk.virtual_interface<@root::@top_body::@parent, "">} {}
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.parent_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "parent_if", node_id = 25 : i64, referenced_path = "parent_if", referenced_symbol = @parent_def, sym_name = "type_parent"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.parent_if", is_virtual_interface_type_instance = true, name = "parent_if", node_id = 26 : i64, sym_name = "type_parent_body", virtual_interface_identity = @root::@top_body::@parent} {
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.parent_if.child", is_uninstantiated = false, name = "child", node_id = 27 : i64, referenced_path = "child_if", referenced_symbol = @child_def, sym_name = "type_child"} {
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.parent_if.child", name = "child_if", node_id = 28 : i64, sym_name = "type_child_body", virtual_interface_identity = @root::@top_body::@parent_body::@child} {
                obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.parent_if.child.bump", name = "bump", node_id = 29 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "type_bump"} {}
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: obelisk_sim.scope.decl [[PARENT:[0-9]+]] parent {{[0-9]+}} hierarchy "top.parent" {{.*}} interface "@root::@top_body::@parent"
// CHECK-DAG: obelisk_sim.scope.decl [[CHILD:[0-9]+]] parent [[PARENT]] hierarchy "top.parent.child" {{.*}} interface "@root::@top_body::@parent_body::@child"
// CHECK-DAG: obelisk_sim.scope.decl [[OTHER:[0-9]+]] parent {{[0-9]+}} hierarchy "top.other" {{.*}} interface "@root::@top_body::@parent"
// CHECK-DAG: obelisk_sim.scope.decl [[OTHER_CHILD:[0-9]+]] parent [[OTHER]] hierarchy "top.other.child" {{.*}} interface "@root::@top_body::@parent_body::@child"
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: [[EXPECTED:%.*]] = arith.constant [[PARENT]] : i64
// CHECK: [[SCOPE:%.*]] = obelisk_sim.virtual_interface.scope {{%.*}} : !obelisk_sim.virtual_interface<"@root::@top_body::@parent", "">
// CHECK: arith.cmpi eq, [[SCOPE]], [[EXPECTED]] : i64
// CHECK: [[BOUND:%.*]] = obelisk_sim.virtual_interface.bind [[CHILD]] : !obelisk_sim.virtual_interface<"@root::@top_body::@parent_body::@child", "">
// CHECK: cf.br {{.*}}([[BOUND]] : !obelisk_sim.virtual_interface<"@root::@top_body::@parent_body::@child", "">)
// CHECK: [[OTHER_EXPECTED:%.*]] = arith.constant {{.*}} [[OTHER]] : i64
// CHECK: arith.cmpi eq, [[SCOPE]], [[OTHER_EXPECTED]] : i64
// CHECK: [[OTHER_BOUND:%.*]] = obelisk_sim.virtual_interface.bind [[OTHER_CHILD]] : !obelisk_sim.virtual_interface<"@root::@top_body::@parent_body::@child", "">
// CHECK: cf.br {{.*}}([[OTHER_BOUND]] : !obelisk_sim.virtual_interface<"@root::@top_body::@parent_body::@child", "">)
// CHECK: obelisk_sim.fatal
// CHECK: obelisk_sim.call @unit_
// CHECK: obelisk_sim.call @unit_
