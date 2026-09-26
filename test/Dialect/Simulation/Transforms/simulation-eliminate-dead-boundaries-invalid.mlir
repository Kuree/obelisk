// RUN: obelisk-opt %s --split-input-file --verify-diagnostics --mlir-disable-threading --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries))'
// RUN: not obelisk-opt %s --split-input-file --mlir-disable-threading --mlir-print-ir-after-failure --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries))' -o /dev/null 2>&1 | FileCheck %s --check-prefix=PREFLIGHT

module {
  obelisk_sim.design @malformed_result_metadata {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        -> (i8, i32) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = arith.constant 1 : i8
      %b = arith.constant 2 : i32
      obelisk_sim.return %a, %b : i8, i32
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = arith.constant 0 : i32
      // expected-error @below {{has malformed call result metadata: expected 2 dictionaries but found 1}}
      %a, %b = obelisk_sim.call @target(%ctx, %zero)
          {arg_attrs = [{}, {}], res_attrs = [{}]}
          : (!obelisk_sim.context, i32) -> (i8, i32)
      obelisk_sim.return
    }
  }
}

// PREFLIGHT-LABEL: obelisk_sim.design @malformed_result_metadata
// PREFLIGHT: obelisk_sim.func private @target(%arg0: !obelisk_sim.context {{.*}}, %arg1: i32
// PREFLIGHT-SAME: -> (i8, i32)
// PREFLIGHT: obelisk_sim.return %c1_i8, %c2_i32 : i8, i32
// PREFLIGHT: obelisk_sim.call @target(%arg0, %c0_i32) {arg_attrs = [{}, {}], res_attrs = [{}]} : (!obelisk_sim.context, i32) -> (i8, i32)

// -----

module {
  // expected-error @below {{cannot eliminate dead boundaries after compute-graph metadata exists}}
  obelisk_sim.design @late_graph attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1, nodes = [], edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = []>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead boundaries after fragment ABI, effect-summary, or compiled-site metadata exists}}
  obelisk_sim.design @late_fragment_abi {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64,
                    fragment_abi = #obelisk_sim.fragment_abi<
                      version = 1, fragments = []>} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead boundaries after fragment ABI, effect-summary, or compiled-site metadata exists}}
  obelisk_sim.design @late_site_metadata {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %site = "arith.constant"() {
        test.site = #obelisk_sim.continuation<id = 1>, value = 0 : i32
      } : () -> i32
      obelisk_sim.return %site : i32
    }
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead boundaries after fragment ABI, effect-summary, or compiled-site metadata exists}}
  obelisk_sim.design @late_effect_summary {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64,
                    effect_summary = []} {
      obelisk_sim.return
    }
  }
}
