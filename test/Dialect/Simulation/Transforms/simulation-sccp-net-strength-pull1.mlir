// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-sccp{vpi=off}))' | FileCheck %s

// The mirrored case: a strength1 driver of a constant one.
// CHECK-LABEL: simulation.func private @reader
// CHECK: %[[KNOWN:.*]] = simulation.logic.constant {{(true|false)}}, false
// CHECK: %[[BAD:.*]] = simulation.net.read
// CHECK: simulation.ref.store %[[BAD]]
// CHECK-NOT: simulation.net.read
// CHECK: simulation.ref.store %[[KNOWN]]
// CHECK-LABEL: simulation.design @custom_resolution
// CHECK-LABEL: simulation.func private @reader
// CHECK: %[[CUSTOM:.*]] = simulation.net.read
// CHECK: simulation.ref.store %[[CUSTOM]]

module {
  simulation.design @net_strength {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "net_strength.reader"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design {strength1 = 0 : i32}
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %bad = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %good = simulation.context.net %ctx[1] : !simulation.net<!simulation.logic<1>>
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %s0 = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %s1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %value = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %d0 = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %d1 = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %reader = simulation.spawn @reader(%ctx, %bad, %good, %s0, %s1) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %bad: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %good: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64}, %s0: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %s1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %bad_value = simulation.net.read %bad : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %bad_value to %s0 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %good_value = simulation.net.read %good : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %good_value to %s1 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }

  // A custom resolver can transform even one driver's known value (6.6.7).
  // Neither SCCP nor the inductive known-state proof models that resolver.
  simulation.design @custom_resolution {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @resolve id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.resolve" debug "resolve"
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top hierarchy "top.nt" debug "nt" {
      resolution_function = @resolve,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<1> design {
      nettype = @nt,
      vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 14 : i32>]>,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design
    simulation.storage.decl 0 in 1 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.reader"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %value = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %reader = simulation.spawn @reader(%ctx, %net, %storage) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %storage: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %storage : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}
