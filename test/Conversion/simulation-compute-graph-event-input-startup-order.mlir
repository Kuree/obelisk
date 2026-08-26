// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// Ordinary settling ports are intentionally in consumer-before-producer
// order, forcing the generic settling sort to move them. The event-handle
// producer and consumer must remain ahead of the wait while that happens.
module {
  obelisk_sim.design @event_startup_order attributes {obelisk_sim.computed_event_startup} {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "wait"
    obelisk_sim.code_unit.decl 3 in 0 port_input hierarchy "consumer" {internal}
    obelisk_sim.code_unit.decl 4 in 0 port_input hierarchy "producer" {internal}
    obelisk_sim.code_unit.decl 5 in 0 port_input hierarchy "selector_producer" {internal}
    obelisk_sim.code_unit.decl 6 in 0 port_output hierarchy "ordinary_consumer" {internal}
    obelisk_sim.code_unit.decl 7 in 0 port_output hierarchy "ordinary_producer" {internal}
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.event design hierarchy "source"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.event design hierarchy "middle"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.event design hierarchy "sink"
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<1> design hierarchy "ordinary_source"
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.logic<1> design hierarchy "ordinary_middle"
    obelisk_sim.storage.decl 5 in 0 : !obelisk_sim.logic<1> design hierarchy "ordinary_sink"
    obelisk_sim.storage.decl 6 in 0 : !obelisk_sim.logic<1> design hierarchy "selector_source"
    obelisk_sim.storage.decl 7 in 0 : !obelisk_sim.logic<1> design hierarchy "selector_sink"

    // CHECK-LABEL: obelisk_sim.func @root
    // CHECK: obelisk_sim.spawn @selector_producer
    // CHECK-NEXT: obelisk_sim.spawn @z_producer
    // CHECK-NEXT: obelisk_sim.spawn @m_consumer
    // CHECK-NEXT: obelisk_sim.spawn @a_wait
    // CHECK-NEXT: obelisk_sim.spawn @ordinary_producer
    // CHECK-NEXT: obelisk_sim.spawn @ordinary_consumer
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %source = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.event>
      %middle = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.event>
      %sink = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.event>
      %ordinary_source = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %ordinary_middle = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %ordinary_sink = obelisk_sim.context.storage %ctx[5] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %selector_source = obelisk_sim.context.storage %ctx[6] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %selector_sink = obelisk_sim.context.storage %ctx[7] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %selector = obelisk_sim.spawn @selector_producer(%ctx, %selector_source, %selector_sink) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %producer = obelisk_sim.spawn @z_producer(%ctx, %source, %middle) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.event>, !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.process
      %consumer = obelisk_sim.spawn @m_consumer(%ctx, %middle, %sink) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.event>, !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.process
      %wait = obelisk_sim.spawn @a_wait(%ctx, %sink) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.process
      %ordinary_consumer = obelisk_sim.spawn @ordinary_consumer(%ctx, %ordinary_middle, %ordinary_sink) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %ordinary_producer = obelisk_sim.spawn @ordinary_producer(%ctx, %ordinary_source, %ordinary_middle) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @a_wait(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %sink: !obelisk_sim.ref<!obelisk_sim.event> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %event = obelisk_sim.ref.load %sink : !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.event
      obelisk_sim.suspend.event %event to ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @m_consumer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %middle: !obelisk_sim.ref<!obelisk_sim.event> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %sink: !obelisk_sim.ref<!obelisk_sim.event> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64, internal, obelisk_sim.computed_event_startup} {
      %event = obelisk_sim.ref.load %middle : !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.event
      obelisk_sim.ref.store %event to %sink : !obelisk_sim.event, !obelisk_sim.ref<!obelisk_sim.event>
      obelisk_sim.return
    }

    obelisk_sim.func @z_producer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %source: !obelisk_sim.ref<!obelisk_sim.event> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %middle: !obelisk_sim.ref<!obelisk_sim.event> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 4 : i64, internal, obelisk_sim.computed_event_startup} {
      %event = obelisk_sim.ref.load %source : !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.event
      obelisk_sim.ref.store %event to %middle : !obelisk_sim.event, !obelisk_sim.ref<!obelisk_sim.event>
      obelisk_sim.return
    }

    obelisk_sim.func @selector_producer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %source: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 6 : i64}, %sink: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 7 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 5 : i64, internal, obelisk_sim.computed_event_startup} {
      %value = obelisk_sim.ref.load %source : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %sink : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    obelisk_sim.func @ordinary_consumer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %source: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}, %sink: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 5 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 6 : i64, internal} {
      %value = obelisk_sim.ref.load %source : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %sink : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    obelisk_sim.func @ordinary_producer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %source: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %sink: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 7 : i64, internal} {
      %value = obelisk_sim.ref.load %source : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %sink : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}
