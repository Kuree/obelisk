// RUN: obelisk-opt %s --test-obelisk-nba-influence -o /dev/null 2>&1 | FileCheck %s
// Sparse backward fixed point: transitive/cyclic cones, NBA cuts, aliases, and top.
#g = #schedule.graph<version = 1, vpi = off, workers = 1, nodes = [], edges = [], regions = [#schedule.region<kind = active, groups = []>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>
module {
  simulation.design @cones attributes {compute_graph = #g} {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 static
    simulation.storage.decl 1 in 0 : i1 static
    simulation.storage.decl 2 in 0 : i1 static {test.observer}
    simulation.storage.decl 3 in 0 : i1 static
    simulation.storage.decl 4 in 0 : i1 static
    simulation.storage.decl 5 in 0 : i1 static
    simulation.storage.decl 6 in 0 : i1 static
    simulation.storage.decl 7 in 0 : i1 static
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {test.observer}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
    simulation.code_unit.decl 1 in 0 always hierarchy "test"
    simulation.func @f0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
    simulation.code_unit.decl 2 in 0 always hierarchy "test"
    simulation.func @f1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
    simulation.code_unit.decl 3 in 0 always hierarchy "test"
    simulation.func @f2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
    simulation.code_unit.decl 4 in 0 always hierarchy "test"
    simulation.func @f3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
    simulation.code_unit.decl 5 in 0 always hierarchy "test"
    simulation.func @f4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = nba, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
    simulation.code_unit.decl 6 in 0 always hierarchy "test"
    simulation.func @f5(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 5, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 5, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
    simulation.code_unit.decl 7 in 0 always hierarchy "test"
    simulation.func @f6(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 6, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
  }
// CHECK-LABEL: influence cones
// CHECK: storage 0 observed=1
// CHECK: storage 1 observed=1
// CHECK: storage 2 observed=1
// CHECK: storage 3 observed=1
// CHECK: storage 4 observed=0
// CHECK-SAME: unread=0
// CHECK: storage 5 observed=0
// CHECK-SAME: unread=0
// CHECK: storage 6 observed=1
// CHECK: storage 7 observed=0 unread=1
// CHECK: net 0 observed=1
// CHECK: net 1 observed=1
  simulation.design @unknown attributes {compute_graph = #g} {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 static
    simulation.storage.decl 1 in 0 : i1 static {test.observer}
    simulation.code_unit.decl 1 in 0 always hierarchy "test"
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64, effect_summary = [#schedule.effect<effect = write, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
  }
// CHECK-LABEL: influence unknown
// CHECK: storage 0 observed=1
// CHECK: storage 1 observed=1
  simulation.design @formal attributes {compute_graph = #g} {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 static
    simulation.storage.decl 1 in 0 : i1 static {test.observer}
    simulation.code_unit.decl 1 in 0 always hierarchy "test"
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64, effect_summary = [#schedule.effect<effect = write, resource = storage, target = formal, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]} { simulation.return }
  }
// CHECK-LABEL: influence formal
// CHECK: storage 0 observed=1
// CHECK: storage 1 observed=1
  simulation.design @dynamic attributes {compute_graph = #g} {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 static
    simulation.storage.decl 1 in 0 : i1 static {test.observer}
    simulation.code_unit.decl 1 in 0 always hierarchy "test"
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 1, dynamic = true, deferred = false, trigger = none>]} { simulation.return }
  }
// CHECK-LABEL: influence dynamic
// CHECK: storage 0 observed=1
// CHECK: storage 1 observed=1
}
