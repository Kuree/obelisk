// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.instance_body attributes {
      hierarchical_name = "top",
      name = "top",
      node_id = 2 : i64,
      sym_name = "s2.top"
    } {
      obelisk.sv.symbol.primitive_instance attributes {
        hierarchical_name = "top",
        node_id = 3 : i64,
        primitive_name = "bufif0",
        sym_name = "s3",
        time_precision_fs = 1000 : i64,
        time_unit_fs = 1000000 : i64,
        unsupported_delay = "5",
        unsupported_strength = "strong,pull"
      } {
      }
    }
  }
}

// CHECK: error: primitive strengths are not supported: strong,pull
// CHECK: error: primitive delays are not supported: 5
