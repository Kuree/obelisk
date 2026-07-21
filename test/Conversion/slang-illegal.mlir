// RUN: not obelisk-opt --convert-slang-to-obelisk %s 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.variable attributes {
    lifetime = 0 : i32, node_id = 0 : i64, rand_mode = 0 : i32,
    sym_name = "illegal_nested_type",
    semantic_type = !slang.integral<8, false, true, 7 : 0, generic>
  } {
  }
}

// CHECK: failed to legalize operation 'obelisk.sv.symbol.variable'
