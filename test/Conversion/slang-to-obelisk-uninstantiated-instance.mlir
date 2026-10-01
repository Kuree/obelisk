// RUN: obelisk-opt --convert-slang-to-obelisk %s | FileCheck %s

module {
  slang.symbol.definition @required_parameter attributes {
    definition_kind = 0 : i32, hierarchical_name = "required_parameter",
    name = "required_parameter", node_id = 0 : i64
  } {
  }
  slang.symbol.root @root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    slang.symbol.instance @invalid_instance attributes {
      hierarchical_name = "$unit", is_uninstantiated = true,
      node_id = 2 : i64, referenced_path = "required_parameter",
      referenced_symbol = @required_parameter
    } {
      slang.symbol.instance_body @invalid_body attributes {
        hierarchical_name = "$unit", name = "required_parameter",
        node_id = 3 : i64
      } {
        slang.symbol.parameter @width attributes {
          constant_value = "<unset>", hierarchical_name = ".WIDTH",
          name = "WIDTH", node_id = 4 : i64,
          semantic_type = !slang.error<false>
        } {
        }
      }
    }
  }
}

// CHECK: obelisk.sv.symbol.definition
// CHECK: obelisk.sv.symbol.root
// CHECK-NOT: obelisk.sv.symbol.instance
