// RUN: not obelisk-opt %s --obelisk-sim-prepare --mlir-disable-threading=false -o /dev/null 2>&1 | FileCheck %s
// RUN: not obelisk-opt %s --obelisk-sim-prepare --mlir-disable-threading -o /dev/null 2>&1 | FileCheck %s
// Invalid payloads must fail preparation in both execution modes.
// CHECK: params.sv:12:17: error: invalid digit in integer literal 'not_an_integer'

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "root",
      name = "$root", node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.symbol.package attributes {hierarchical_name = "params",
        name = "params", node_id = 1 : i64, sym_name = "params"} {
      obelisk.sv.symbol.parameter attributes {constant_value = "32'hffff_ffff",
          hierarchical_name = "params::VALID", name = "VALID", node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "valid"} {}
      obelisk.sv.symbol.parameter attributes {constant_value = "not_an_integer",
          hierarchical_name = "params::INVALID", name = "INVALID", node_id = 3 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "invalid"} {} loc("params.sv":12:17)
    }
  }
}
