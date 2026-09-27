// RUN: obelisk-opt %s --split-input-file --verify-diagnostics --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries))'
// RUN: not obelisk-opt %s --split-input-file --mlir-disable-threading --mlir-print-ir-after-failure --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries))' -o /dev/null 2>&1 | FileCheck %s --check-prefix=PREFLIGHT

module {
  simulation.design @malformed_result_metadata {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "top.target"
    simulation.func private @target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i32 {simulation.capture_kind = 1 : i32})
        -> (i8, i32) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = arith.constant 1 : i8
      %b = arith.constant 2 : i32
      simulation.return %a, %b : i8, i32
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = arith.constant 0 : i32
      // expected-error @below {{has malformed call result metadata: expected 2 dictionaries but found 1}}
      %a, %b = simulation.call @target(%ctx, %zero)
          {arg_attrs = [{}, {}], res_attrs = [{}]}
          : (!simulation.context, i32) -> (i8, i32)
      simulation.return
    }
  }
}

// PREFLIGHT-LABEL: simulation.design @malformed_result_metadata
// PREFLIGHT: simulation.func private @target(%arg0: !simulation.context {{.*}}, %arg1: i32
// PREFLIGHT-SAME: -> (i8, i32)
// PREFLIGHT: simulation.return %c1_i8, %c2_i32 : i8, i32
// PREFLIGHT: simulation.call @target(%arg0, %c0_i32) {arg_attrs = [{}, {}], res_attrs = [{}]} : (!simulation.context, i32) -> (i8, i32)

// -----

module {
  // expected-error @below {{cannot eliminate dead boundaries after compute-graph metadata exists}}
  simulation.design @late_graph attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1, nodes = [], edges = [],
      regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead boundaries after fragment ABI, effect-summary, or compiled-site metadata exists}}
  simulation.design @late_fragment_abi {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "top.target"
    simulation.func private @target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64,
                    fragment_abi = #schedule.fragment_abi<
                      version = 1, fragments = []>} {
      simulation.return
    }
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead boundaries after fragment ABI, effect-summary, or compiled-site metadata exists}}
  simulation.design @late_site_metadata {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "top.target"
    simulation.func private @target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %site = "arith.constant"() {
        test.site = #schedule.continuation<id = 1>, value = 0 : i32
      } : () -> i32
      simulation.return %site : i32
    }
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead boundaries after fragment ABI, effect-summary, or compiled-site metadata exists}}
  simulation.design @late_effect_summary {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "top.target"
    simulation.func private @target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64,
                    effect_summary = []} {
      simulation.return
    }
  }
}
