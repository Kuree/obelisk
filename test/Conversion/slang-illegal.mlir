// RUN: not talos-opt --convert-slang-to-talos %s 2>&1 | FileCheck %s

module {
  talos.sv.symbol.variable @illegal_nested_type attributes {
    lifetime = 0 : i32, node_id = 0 : i64, rand_mode = 0 : i32,
    semantic_type = !slang.integral<8, false, true, 7 : 0, generic>
  } {
  }
}

// CHECK: failed to legalize operation 'talos.sv.symbol.variable'
