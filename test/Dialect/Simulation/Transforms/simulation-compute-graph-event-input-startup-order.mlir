// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// Ordinary settling ports are intentionally in consumer-before-producer
// order, forcing the generic settling sort to move them. The event-handle
// producer and consumer must remain ahead of the wait while that happens.
module {
  simulation.design @event_startup_order attributes {schedule.computed_event_startup} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "wait"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "consumer" {internal}
    simulation.code_unit.decl 4 in 0 port_input hierarchy "producer" {internal}
    simulation.code_unit.decl 5 in 0 port_input hierarchy "selector_producer" {internal}
    simulation.code_unit.decl 6 in 0 port_output hierarchy "ordinary_consumer" {internal}
    simulation.code_unit.decl 7 in 0 port_output hierarchy "ordinary_producer" {internal}
    simulation.storage.decl 0 in 0 : !simulation.event design hierarchy "source"
    simulation.storage.decl 1 in 0 : !simulation.event design hierarchy "middle"
    simulation.storage.decl 2 in 0 : !simulation.event design hierarchy "sink"
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design hierarchy "ordinary_source"
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design hierarchy "ordinary_middle"
    simulation.storage.decl 5 in 0 : !simulation.logic<1> design hierarchy "ordinary_sink"
    simulation.storage.decl 6 in 0 : !simulation.logic<1> design hierarchy "selector_source"
    simulation.storage.decl 7 in 0 : !simulation.logic<1> design hierarchy "selector_sink"

    // CHECK-LABEL: simulation.func @root
    // CHECK: simulation.spawn @selector_producer
    // CHECK-NEXT: simulation.spawn @z_producer
    // CHECK-NEXT: simulation.spawn @m_consumer
    // CHECK-NEXT: simulation.spawn @a_wait
    // CHECK-NEXT: simulation.spawn @ordinary_producer
    // CHECK-NEXT: simulation.spawn @ordinary_consumer
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %source = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.event>
      %middle = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.event>
      %sink = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.event>
      %ordinary_source = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<1>>
      %ordinary_middle = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %ordinary_sink = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<1>>
      %selector_source = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<1>>
      %selector_sink = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<1>>
      %selector = simulation.spawn @selector_producer(%ctx, %selector_source, %selector_sink) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %producer = simulation.spawn @z_producer(%ctx, %source, %middle) : !simulation.context, !simulation.ref<!simulation.event>, !simulation.ref<!simulation.event> -> !simulation.process
      %consumer = simulation.spawn @m_consumer(%ctx, %middle, %sink) : !simulation.context, !simulation.ref<!simulation.event>, !simulation.ref<!simulation.event> -> !simulation.process
      %wait = simulation.spawn @a_wait(%ctx, %sink) : !simulation.context, !simulation.ref<!simulation.event> -> !simulation.process
      %ordinary_consumer = simulation.spawn @ordinary_consumer(%ctx, %ordinary_middle, %ordinary_sink) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %ordinary_producer = simulation.spawn @ordinary_producer(%ctx, %ordinary_source, %ordinary_middle) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func @a_wait(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %sink: !simulation.ref<!simulation.event> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %event = simulation.ref.load %sink : !simulation.ref<!simulation.event> -> !simulation.event
      simulation.suspend.event %event to ^done
    ^done:
      simulation.return
    }

    simulation.func @m_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %middle: !simulation.ref<!simulation.event> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %sink: !simulation.ref<!simulation.event> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64, internal, schedule.computed_event_startup} {
      %event = simulation.ref.load %middle : !simulation.ref<!simulation.event> -> !simulation.event
      simulation.ref.store %event to %sink : !simulation.event, !simulation.ref<!simulation.event>
      simulation.return
    }

    simulation.func @z_producer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %source: !simulation.ref<!simulation.event> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %middle: !simulation.ref<!simulation.event> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 4 : i64, internal, schedule.computed_event_startup} {
      %event = simulation.ref.load %source : !simulation.ref<!simulation.event> -> !simulation.event
      simulation.ref.store %event to %middle : !simulation.event, !simulation.ref<!simulation.event>
      simulation.return
    }

    simulation.func @selector_producer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %source: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %sink: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 5 : i64, internal, schedule.computed_event_startup} {
      %value = simulation.ref.load %source : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %sink : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    simulation.func @ordinary_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %source: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %sink: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 6 : i64, internal} {
      %value = simulation.ref.load %source : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %sink : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    simulation.func @ordinary_producer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %source: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %sink: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 7 : i64, internal} {
      %value = simulation.ref.load %source : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %sink : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}
