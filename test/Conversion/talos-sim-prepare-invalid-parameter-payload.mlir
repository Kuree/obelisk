// RUN: not talos-opt %s --talos-sim-prepare --mlir-disable-threading=false -o /dev/null 2>&1 | FileCheck %s
// RUN: not talos-opt %s --talos-sim-prepare --mlir-disable-threading -o /dev/null 2>&1 | FileCheck %s
// Invalid payloads must fail preparation in both execution modes.
// CHECK: params.sv:12:17: error: invalid digit in integer literal 'not_an_integer'

module {
  talos.sv.symbol.root @root attributes {hierarchical_name = "root",
      name = "$root", node_id = 0 : i64} {
    talos.sv.symbol.package @params attributes {hierarchical_name = "params",
        name = "params", node_id = 1 : i64} {
      talos.sv.symbol.parameter @valid attributes {constant_value = "32'hffff_ffff",
          hierarchical_name = "params::VALID", name = "VALID", node_id = 2 : i64,
          semantic_type = !talos.integral<32, true, false, 31 : 0, int>
      } {}
      talos.sv.symbol.parameter @invalid attributes {constant_value = "not_an_integer",
          hierarchical_name = "params::INVALID", name = "INVALID", node_id = 3 : i64,
          semantic_type = !talos.integral<32, true, false, 31 : 0, int>
      } {} loc("params.sv":12:17)
    }
  }
}
