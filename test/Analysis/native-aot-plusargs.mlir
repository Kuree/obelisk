// RUN: %split-file %s %t
// RUN: obelisk-opt %t/transient.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELIGIBLE
// RUN: obelisk-opt %t/persistent.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=MANAGED
// RUN: obelisk-opt %t/suspended.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=MANAGED
// RUN: obelisk-opt %t/unrelated.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=MANAGED

// Only block-local query strings are exempt from managed-state rejection.
// Stored strings, suspension-live strings, and unrelated managed operations
// must still retain their existing lifecycle classification.
// ELIGIBLE: native-aot eligible=true fully=true
// ELIGIBLE-NOT: reason
// MANAGED: native-aot eligible=false fully=false
// MANAGED: reason managed or string state is present

//--- transient.mlir
module {
  obelisk_sim.design @plusargs {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %prefix = obelisk_sim.string.literal "n="
      %test = "obelisk_sim.plusarg.test"(%ctx, %prefix) : (!obelisk_sim.context, !obelisk_sim.string) -> i32
      %tail, %found = "obelisk_sim.plusarg.value"(%ctx, %prefix) : (!obelisk_sim.context, !obelisk_sim.string) -> (!obelisk_sim.string, i32)
      %parsed = obelisk_sim.plusarg.parse_logic %tail {radix = 10 : i32} : (!obelisk_sim.string) -> !obelisk_sim.logic<32>
      obelisk_sim.return
    }
  }
}

//--- persistent.mlir
module {
  obelisk_sim.design @plusargs {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %prefix = obelisk_sim.string.literal "n="
      %tail, %found = "obelisk_sim.plusarg.value"(%ctx, %prefix) : (!obelisk_sim.context, !obelisk_sim.string) -> (!obelisk_sim.string, i32)
      %saved = obelisk_sim.ref.alloc %tail : !obelisk_sim.string -> !obelisk_sim.ref<!obelisk_sim.string>
      obelisk_sim.return
    }
  }
}

//--- suspended.mlir
module {
  obelisk_sim.design @plusargs {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %prefix = obelisk_sim.string.literal "n="
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%prefix : !obelisk_sim.string)
          {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^resume(%text: !obelisk_sim.string):
      %found = "obelisk_sim.plusarg.test"(%ctx, %text) : (!obelisk_sim.context, !obelisk_sim.string) -> i32
      obelisk_sim.return
    }
  }
}

//--- unrelated.mlir
module {
  obelisk_sim.design @plusargs {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %text = obelisk_sim.string.literal "not a query"
      obelisk_sim.return
    }
  }
}

