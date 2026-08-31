// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-sccp))' | FileCheck %s

// IEEE 1800-2017 6.6.6 gives supply0 and supply1 nets supply strength, and
// 28.12 resolves that implicit contribution against every explicit driver. A
// supply, tri0, tri1, or trireg net therefore has one driver more than its
// `driver.decl`s account for, so a single constant continuous assignment does
// not determine the resolved value and its reads are not SCCP boundaries. Only
// the kinds whose implicit contribution is high impedance -- wire, tri, uwire,
// wand, and wor -- keep the exact-constant fold.

module {
  obelisk_sim.design @implicit_net_driver {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.n.read_supply0.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.n.read_supply1.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 initial hierarchy "test.n.read_tri0.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 initial hierarchy "test.n.read_trireg.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 initial hierarchy "test.n.read_wire.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 initial hierarchy "test.n.read_wand.9000006"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 5 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {observability = 0 : i32, resolution_kind = 7 : i32}
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design {observability = 0 : i32, resolution_kind = 8 : i32}
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design {observability = 0 : i32, resolution_kind = 5 : i32}
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design {observability = 0 : i32, resolution_kind = 9 : i32}
    obelisk_sim.net.decl 4 in 0 : !obelisk_sim.logic<1> design {observability = 0 : i32}
    obelisk_sim.net.decl 5 in 0 : !obelisk_sim.logic<1> design {observability = 0 : i32, resolution_kind = 3 : i32}

    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 3 in 0 drives 3 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 4 in 0 drives 4 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 5 in 0 drives 5 : !obelisk_sim.logic<1> design

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %supply0 = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %supply1 = obelisk_sim.context.net %ctx[1] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %tri0 = obelisk_sim.context.net %ctx[2] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %trireg = obelisk_sim.context.net %ctx[3] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %wire = obelisk_sim.context.net %ctx[4] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %wand = obelisk_sim.context.net %ctx[5] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %d0 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d2 = obelisk_sim.context.driver %ctx[2] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d3 = obelisk_sim.context.driver %ctx[3] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d4 = obelisk_sim.context.driver %ctx[4] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d5 = obelisk_sim.context.driver %ctx[5] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %s0 = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %s1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %s2 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %s3 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %s4 = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %s5 = obelisk_sim.context.storage %ctx[5] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %d0 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %d1 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %d2 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %d3 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %d4 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %d5 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %p0 = obelisk_sim.spawn @read_supply0(%ctx, %supply0, %s0) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @read_supply1(%ctx, %supply1, %s1) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p2 = obelisk_sim.spawn @read_tri0(%ctx, %tri0, %s2) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p3 = obelisk_sim.spawn @read_trireg(%ctx, %trireg, %s3) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p4 = obelisk_sim.spawn @read_wire(%ctx, %wire, %s4) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p5 = obelisk_sim.spawn @read_wand(%ctx, %wand, %s5) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    // A supply0 net always resolves to its supply-strength 0, so the strong
    // constant driver never determines the read.
    // CHECK-LABEL: obelisk_sim.func private @read_supply0
    // CHECK: obelisk_sim.net.read
    obelisk_sim.func private @read_supply0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %out: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func private @read_supply1
    // CHECK: obelisk_sim.net.read
    obelisk_sim.func private @read_supply1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %out: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    // A tri0 pull-down still contributes when the explicit driver turns off.
    // CHECK-LABEL: obelisk_sim.func private @read_tri0
    // CHECK: obelisk_sim.net.read
    obelisk_sim.func private @read_tri0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %out: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    // IEEE 1800-2017 28.16.2: a trireg resolves retained charge once every
    // driver is high impedance.
    // CHECK-LABEL: obelisk_sim.func private @read_trireg
    // CHECK: obelisk_sim.net.read
    obelisk_sim.func private @read_trireg(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %out: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    // A plain wire keeps the fold: its implicit contribution is high impedance,
    // which 28.12 resolves away against the one explicit driver.
    // CHECK-LABEL: obelisk_sim.func private @read_wire
    // CHECK-NOT: obelisk_sim.net.read
    // CHECK: obelisk_sim.logic.constant true, false
    obelisk_sim.func private @read_wire(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 4 : i64},
        %out: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    // So does wand: 28.12.4 wired logic only decides equal-strength conflicts
    // between two explicit drivers.
    // CHECK-LABEL: obelisk_sim.func private @read_wand
    // CHECK-NOT: obelisk_sim.net.read
    // CHECK: obelisk_sim.logic.constant true, false
    obelisk_sim.func private @read_wand(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 5 : i64},
        %out: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 5 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000006 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}
