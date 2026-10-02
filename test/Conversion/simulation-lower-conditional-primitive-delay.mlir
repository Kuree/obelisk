// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @conditional_delay {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "conditional_delay.bufif0"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "conditional_delay.bufif1_one"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "conditional_delay.notif1_two"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "conditional_delay.path_selected"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 0 : !simulation.logic<1> design
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design

    simulation.func @bufif0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out_low: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64, simulation.strength_driver_bank = 0 : i32},
        %out_high: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64, simulation.strength_driver_bank = 1 : i32},
        %data: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %control: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64,
                    schedule.primitive_name = "bufif0",
                    simulation.propagation_delays = array<i64: 2, 3, 4>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.out", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.data", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.control", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.data", referenced_symbol = @data, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 5 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      simulation.return
    }

    simulation.func @bufif1_one(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out_low: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64, simulation.strength_driver_bank = 0 : i32},
        %out_high: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64, simulation.strength_driver_bank = 1 : i32},
        %data: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %control: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64,
                    schedule.primitive_name = "bufif1",
                    simulation.propagation_delays = array<i64: 5>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.out", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.data", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.control", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 13 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.data", referenced_symbol = @data, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      simulation.return
    }

    simulation.func @notif1_two(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out_low: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64, simulation.strength_driver_bank = 0 : i32},
        %out_high: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64, simulation.strength_driver_bank = 1 : i32},
        %data: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %control: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64,
                    schedule.primitive_name = "notif1",
                    simulation.propagation_delays = array<i64: 7, 11>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.out", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.data", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.control", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 21 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 23 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.data", referenced_symbol = @data, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      simulation.return
    }

    simulation.func @path_selected(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out_low: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64, simulation.strength_driver_bank = 0 : i32},
        %out_high: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64, simulation.strength_driver_bank = 1 : i32},
        %data: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %control: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %data_snapshot: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %control_snapshot: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64,
                    schedule.primitive_name = "bufif1",
                    simulation.timing_path_rules = [
                      {input = "top.data", snapshot = "top.data_snapshot", polarity = 1 : i32, delays = array<i64: 2, 3, 4>},
                      {input = "top.control", snapshot = "top.control_snapshot", polarity = 2 : i32, delays = array<i64: 5, 7, 11>}],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.path_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.path_out", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.data", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.control", argument = 4, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.data_snapshot", argument = 5, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.control_snapshot", argument = 6, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 31 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "top.path_out", referenced_symbol = @path_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 33 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.data", referenced_symbol = @data, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      simulation.return
    }
  }
}

// IEEE 1800-2017 28.6 and 28.16: the two strength banks are one logical
// inertial gate output. The ordinary four-state transition chooses the delay,
// and the banks are scheduled by one paired operation.
// CHECK-LABEL: simulation.func @bufif0
// CHECK: %[[DATA:.*]] = simulation.ref.load %arg3
// CHECK: %[[CONTROL:.*]] = simulation.ref.load %arg4
// CHECK: %[[DRIVEN:.*]] = simulation.logic.binary and %[[DATA]],
// Clause 28.12.2 strength banks use direct polarity enables. In particular,
// an unknown control with data 0 must leave the high bank at Z, not merge Z
// with Z through the Clause 11.4.11 conditional operator and turn it into X.
// CHECK: %[[ACTIVE_CONTROL:.*]] = simulation.logic.unary bit_not %[[CONTROL]]
// CHECK: %[[NOT_DRIVEN:.*]] = simulation.logic.unary bit_not %[[DRIVEN]]
// CHECK: %[[LOW_ENABLE:.*]] = simulation.logic.binary and %[[NOT_DRIVEN]], %[[ACTIVE_CONTROL]]
// CHECK: %[[HIGH_ENABLE:.*]] = simulation.logic.binary and %[[DRIVEN]], %[[ACTIVE_CONTROL]]
// CHECK: %[[LOW_BANK:.*]] = simulation.logic.mux %[[LOW_ENABLE]]
// CHECK: %[[HIGH_BANK:.*]] = simulation.logic.mux %[[HIGH_ENABLE]]
// CHECK-DAG: %[[ACTIVE:.*]] = simulation.logic.compare case_eq %[[CONTROL]], %{{.*}} : {{.*}} -> i1
// CHECK-DAG: %[[INACTIVE:.*]] = simulation.logic.compare case_eq %[[CONTROL]], %{{.*}} : {{.*}} -> i1
// CHECK: %[[ACTIVE_VALUE:.*]] = arith.select %[[ACTIVE]], %[[DRIVEN]], %{{.*}}
// CHECK: %[[TRANSITION:.*]] = arith.select %[[INACTIVE]], %{{.*}}, %[[ACTIVE_VALUE]]
// CHECK-DAG: %[[RISE:.*]] = simulation.time.constant 2
// CHECK-DAG: %[[FALL:.*]] = simulation.time.constant 3
// CHECK-DAG: %[[OFF:.*]] = simulation.time.constant 4
// CHECK: simulation.driver.drive_inertial_strength_pair %arg1 = %[[LOW_BANK]], %arg2 = %[[HIGH_BANK]] transition %[[TRANSITION]] after[%[[RISE]], %[[FALL]], %[[OFF]]] site 1 : 0

// One delay expands to the same rise, fall, and turn-off delay.
// CHECK-LABEL: simulation.func @bufif1_one
// CHECK-COUNT-3: simulation.time.constant 5
// CHECK: simulation.driver.drive_inertial_strength_pair {{.*}} after[{{.*}}, {{.*}}, {{.*}}] site 2 : 0

// Two delays use min(rise, fall) for turn-off. This also covers an inverting,
// active-high conditional primitive.
// CHECK-LABEL: simulation.func @notif1_two
// CHECK: simulation.logic.unary bit_not
// CHECK-DAG: %[[RISE2:.*]] = simulation.time.constant 7
// CHECK-DAG: %[[FALL2:.*]] = simulation.time.constant 11
// CHECK-DAG: %[[OFF2:.*]] = simulation.time.constant 7
// CHECK: simulation.driver.drive_inertial_strength_pair {{.*}} after[%[[RISE2]], %[[FALL2]], %[[OFF2]]] site 3 : 0

// Overlapping path delays are selected by statically unrolled source-change
// comparisons and persistent snapshots; the driver remains one inertial site.
// Positive and negative polarity remain descriptive static metadata and do
// not swap the rise/fall/turnoff banks selected by the destination transition.
// CHECK-LABEL: simulation.func @path_selected
// CHECK: simulation.ref.load %arg5
// CHECK: simulation.logic.compare case_ne
// CHECK: simulation.ref.store
// CHECK: simulation.ref.load %arg6
// CHECK: simulation.logic.compare case_ne
// CHECK: simulation.ref.store
// CHECK-COUNT-3: simulation.time.scale
// CHECK: simulation.driver.drive_inertial_strength_pair {{.*}} after[{{.*}}, {{.*}}, {{.*}}] site 4 : 0
