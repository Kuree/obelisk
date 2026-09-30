// RUN: %split-file %s %t
// RUN: obelisk-opt %t/transient.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELIGIBLE
// RUN: obelisk-opt %t/persistent.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELIGIBLE
// RUN: obelisk-opt %t/suspended.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=MANAGED
// RUN: obelisk-opt %t/unrelated.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELIGIBLE

// Native roots support both temporary and stored strings within an
// activation. Only an actual suspension with managed live operands requires
// the continuation lifecycle boundary.
// ELIGIBLE: native-aot eligible=true fully=true
// ELIGIBLE-NOT: reason
// MANAGED: native-aot eligible=false fully=false
// MANAGED: reason suspension retains managed state

//--- transient.mlir
module {
  simulation.design @plusargs {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %prefix = simulation.string.literal "n="
      %test = "simulation.plusarg.test"(%ctx, %prefix) : (!simulation.context, !simulation.string) -> i32
      %tail, %found = "simulation.plusarg.value"(%ctx, %prefix) : (!simulation.context, !simulation.string) -> (!simulation.string, i32)
      %parsed = simulation.plusarg.parse_logic %tail {radix = #simulation.radix<decimal>} : (!simulation.string) -> !simulation.logic<32>
      simulation.return
    }
  }
}

//--- persistent.mlir
module {
  simulation.design @plusargs {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %prefix = simulation.string.literal "n="
      %tail, %found = "simulation.plusarg.value"(%ctx, %prefix) : (!simulation.context, !simulation.string) -> (!simulation.string, i32)
      %saved = simulation.ref.alloc %tail : !simulation.string -> !simulation.ref<!simulation.string>
      simulation.return
    }
  }
}

//--- suspended.mlir
module {
  simulation.design @plusargs {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %prefix = simulation.string.literal "n="
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%prefix : !simulation.string)
          {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^resume(%text: !simulation.string):
      %found = "simulation.plusarg.test"(%ctx, %text) : (!simulation.context, !simulation.string) -> i32
      simulation.return
    }
  }
}

//--- unrelated.mlir
module {
  simulation.design @plusargs {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %text = simulation.string.literal "not a query"
      simulation.return
    }
  }
}
