// RUN: obelisk-opt %s --split-input-file --verify-diagnostics --mlir-disable-threading --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures))'
// RUN: not obelisk-opt %s --split-input-file --mlir-disable-threading --mlir-print-ir-after-failure --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures))' -o /dev/null 2>&1 | FileCheck %s --check-prefix=PREFLIGHT

module {
  obelisk_sim.design @malformed_call_metadata {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = arith.constant 0 : i32
      // expected-error @below {{has malformed call argument metadata: expected 2 dictionaries but found 1}}
      obelisk_sim.call @target(%ctx, %zero) {arg_attrs = [{}]}
          : (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
    }
  }
}

// PREFLIGHT-LABEL: obelisk_sim.design @malformed_call_metadata
// PREFLIGHT: obelisk_sim.func private @target(%arg0: !obelisk_sim.context {{.*}}, %arg1: i32
// PREFLIGHT: obelisk_sim.call @target(%arg0, %c0_i32) {arg_attrs = [{}]} : (!obelisk_sim.context, i32) -> ()

// -----

module {
  obelisk_sim.design @malformed_bindings {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    // expected-error @below {{has malformed obelisk_sim.bindings entry #0: argument index 2 is outside the function signature}}
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "bad", argument = 2, kind = direct, copyOut = false>]} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead captures after compute-graph metadata exists}}
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
  // expected-error @below {{cannot eliminate dead captures after fragment ABI, effect-summary, or compiled-site metadata exists}}
  obelisk_sim.design @late_effect_summary {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64,
                    effect_summary = []} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead captures after fragment ABI, effect-summary, or compiled-site metadata exists}}
  obelisk_sim.design @late_fragment_abi {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64,
                    fragment_abi = #obelisk_sim.fragment_abi<
                      version = 1, fragments = []>} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // expected-error @below {{cannot eliminate dead captures after fragment ABI, effect-summary, or compiled-site metadata exists}}
  obelisk_sim.design @late_site_metadata {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %site = "arith.constant"() {
        test.site = #obelisk_sim.continuation<id = 1>, value = 0 : i32
      } : () -> i32
      obelisk_sim.return %site : i32
    }
  }
}
