// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp{vpi=off}))' > %t.off
// RUN: FileCheck %s < %t.off
// RUN: sed 's/observability = 0/observability = 1/g' %s \
// RUN:   | obelisk-opt --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp{vpi=read}))' \
// RUN:   | FileCheck %s
// RUN: sed 's/observability = 0/observability = 2/g' %s \
// RUN:   | obelisk-opt --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp{vpi=full}))' \
// RUN:   | FileCheck %s --check-prefix=FULL

// IEEE 1800-2017 6.6.6 gives supply0 and supply1 nets supply strength, and
// 28.12 resolves that implicit contribution against every explicit driver. A
// supply, tri0, tri1, or trireg net therefore has one driver more than its
// `driver.decl`s account for, so a single constant continuous assignment does
// not determine the resolved value and its reads are not SCCP boundaries. Only
// the kinds whose implicit contribution is high impedance -- wire, tri, uwire,
// wand, and wor -- keep the exact-constant fold.

module {
  // Read permission preserves these materializing drives while allowing the
  // same immutable internal net reads to fold as VPI-off. Writable VPI keeps
  // all six reads because a deposit can invalidate the constant facts.
  // CHECK-COUNT-6: simulation.driver.drive
  // FULL-COUNT-7: simulation.net.read
  simulation.design @implicit_net_driver {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.n.read_supply0.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.n.read_supply1.9000002"
    simulation.code_unit.decl 9000003 in 0 initial hierarchy "test.n.read_tri0.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.n.read_trireg.9000004"
    simulation.code_unit.decl 9000005 in 0 initial hierarchy "test.n.read_wire.9000005"
    simulation.code_unit.decl 9000006 in 0 initial hierarchy "test.n.read_wand.9000006"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design
    simulation.storage.decl 5 in 0 : !simulation.logic<1> design

    simulation.net.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32, resolution_kind = 7 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {observability = 0 : i32, resolution_kind = 8 : i32}
    simulation.net.decl 2 in 0 : !simulation.logic<1> design {observability = 0 : i32, resolution_kind = 5 : i32}
    simulation.net.decl 3 in 0 : !simulation.logic<1> design {observability = 0 : i32, resolution_kind = 9 : i32}
    simulation.net.decl 4 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.net.decl 5 in 0 : !simulation.logic<1> design {observability = 0 : i32, resolution_kind = 3 : i32}

    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 3 : !simulation.logic<1> design
    simulation.driver.decl 4 in 0 drives 4 : !simulation.logic<1> design
    simulation.driver.decl 5 in 0 drives 5 : !simulation.logic<1> design

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %supply0 = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %supply1 = simulation.context.net %ctx[1] : !simulation.net<!simulation.logic<1>>
      %tri0 = simulation.context.net %ctx[2] : !simulation.net<!simulation.logic<1>>
      %trireg = simulation.context.net %ctx[3] : !simulation.net<!simulation.logic<1>>
      %wire = simulation.context.net %ctx[4] : !simulation.net<!simulation.logic<1>>
      %wand = simulation.context.net %ctx[5] : !simulation.net<!simulation.logic<1>>
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %d2 = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %d3 = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %d4 = simulation.context.driver %ctx[4] : !simulation.driver<!simulation.logic<1>>
      %d5 = simulation.context.driver %ctx[5] : !simulation.driver<!simulation.logic<1>>
      %s0 = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %s1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %s2 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %s3 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<1>>
      %s4 = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %s5 = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<1>>
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %d0 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %d1 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %d2 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %d3 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %d4 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %d5 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %p0 = simulation.spawn @read_supply0(%ctx, %supply0, %s0) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %p1 = simulation.spawn @read_supply1(%ctx, %supply1, %s1) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %p2 = simulation.spawn @read_tri0(%ctx, %tri0, %s2) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %p3 = simulation.spawn @read_trireg(%ctx, %trireg, %s3) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %p4 = simulation.spawn @read_wire(%ctx, %wire, %s4) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %p5 = simulation.spawn @read_wand(%ctx, %wand, %s5) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    // A supply0 net always resolves to its supply-strength 0, so the strong
    // constant driver never determines the read.
    // CHECK-LABEL: simulation.func private @read_supply0
    // CHECK: simulation.net.read
    simulation.func private @read_supply0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    // CHECK-LABEL: simulation.func private @read_supply1
    // CHECK: simulation.net.read
    simulation.func private @read_supply1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    // A tri0 pull-down still contributes when the explicit driver turns off.
    // CHECK-LABEL: simulation.func private @read_tri0
    // CHECK: simulation.net.read
    simulation.func private @read_tri0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64},
        %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    // IEEE 1800-2017 28.16.2: a trireg resolves retained charge once every
    // driver is high impedance.
    // CHECK-LABEL: simulation.func private @read_trireg
    // CHECK: simulation.net.read
    simulation.func private @read_trireg(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 3 : i64},
        %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    // A plain wire keeps the fold: its implicit contribution is high impedance,
    // which 28.12 resolves away against the one explicit driver.
    // CHECK-LABEL: simulation.func private @read_wire
    // CHECK-NOT: simulation.net.read
    // CHECK: simulation.logic.constant true, false
    simulation.func private @read_wire(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 4 : i64},
        %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    // So does wand: 28.12.4 wired logic only decides equal-strength conflicts
    // between two explicit drivers.
    // CHECK-LABEL: simulation.func private @read_wand
    // CHECK-NOT: simulation.net.read
    // CHECK: simulation.logic.constant true, false
    simulation.func private @read_wand(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 5 : i64},
        %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000006 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }

// A dependency-driven force can change a net after its constant driver has
// initialized. It blocks the same closed-world fold as a static force.
// CHECK-LABEL: simulation.design @dynamic_override
// CHECK: simulation.driver.drive
// CHECK: simulation.dynamic_override
// CHECK: simulation.net.read
  simulation.design @dynamic_override {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design

    simulation.func @dynamic_override_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %driver = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %owner = simulation.process.current
      simulation.dynamic_override %net = %one owner %owner assign false claim true : !simulation.net<!simulation.logic<1>>, !simulation.logic<1>
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}
