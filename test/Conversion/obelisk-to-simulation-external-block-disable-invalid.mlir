// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

// A named block reached from another process cannot be approximated by killing
// its complete logical process. Lowering must reject it until the runtime can
// resume at the target block's exit continuation.
// CHECK: error: disable of a nonlocal statement block is not executable yet

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "external_block_disable",
    name = "external_block_disable",
    node_id = 0 : i64,
    sym_name = "s0.external_block_disable"
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
      hierarchical_name = "external_block_disable",
      is_uninstantiated = false,
      name = "external_block_disable",
      node_id = 3 : i64,
      referenced_path = "external_block_disable",
      referenced_symbol = @s0.external_block_disable,
      sym_name = "s3.external_block_disable"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "external_block_disable",
        name = "external_block_disable",
        node_id = 4 : i64,
        sym_name = "s4.external_block_disable"
      } {
        obelisk.sv.symbol.statement_block attributes {
          block_kind = 0 : i32,
          hierarchical_name = "external_block_disable.target",
          name = "target",
          node_id = 5 : i64,
          sym_name = "s5.target"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "external_block_disable",
          node_id = 6 : i64,
          procedure_kind = 2 : i32,
          sym_name = "s6",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {
            block_path = "external_block_disable.target",
            block_symbol = @s1.$root::@s3.external_block_disable::@s4.external_block_disable::@s5.target,
            node_id = 7 : i64
          } {
            obelisk.sv.statement.empty attributes {node_id = 8 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "external_block_disable",
          node_id = 9 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s7",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.disable attributes {
            is_hierarchical = true,
            node_id = 10 : i64,
            target_path = "external_block_disable.target",
            target_symbol = @s1.$root::@s3.external_block_disable::@s4.external_block_disable::@s5.target
          } {
          }
        }
      }
    }
  }
}
