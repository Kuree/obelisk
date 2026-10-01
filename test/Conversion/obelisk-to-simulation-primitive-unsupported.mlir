// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64
  } {
    obelisk.sv.symbol.instance_body @s2.top attributes {
      hierarchical_name = "top",
      name = "top",
      node_id = 2 : i64
    } {
      obelisk.sv.symbol.primitive_instance @s3 attributes {
        hierarchical_name = "top",
        node_id = 3 : i64,
        primitive_name = "bufif0",
        time_precision_fs = 1000 : i64,
        time_unit_fs = 1000000 : i64,
        drive_strength0 = 1 : i32,
        drive_strength1 = 2 : i32,
        unsupported_delay = "5"
      } {
      }
    }
  }
}

// CHECK: error: primitive delays are not supported: 5
