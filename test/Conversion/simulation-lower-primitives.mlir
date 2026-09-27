// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @primitive_units {
    simulation.scope.decl 0
    simulation.code_unit.decl 9100001 in 0 continuous hierarchy "test.primitives.and"
    simulation.code_unit.decl 9100002 in 0 continuous hierarchy "test.primitives.xnor"
    simulation.code_unit.decl 9100003 in 0 continuous hierarchy "test.primitives.bufif0"
    simulation.code_unit.decl 9100004 in 0 continuous hierarchy "test.primitives.notif1"
    simulation.code_unit.decl 9100005 in 0 continuous hierarchy "test.primitives.buf"
    simulation.code_unit.decl 9100006 in 0 continuous hierarchy "test.primitives.pullup"
    simulation.code_unit.decl 9100007 in 0 continuous hierarchy "test.primitives.pulldown"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.decl 3 in 0 : !simulation.logic<1> design
    simulation.net.decl 4 in 0 : !simulation.logic<1> design
    simulation.net.decl 5 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 3 : !simulation.logic<1> design
    simulation.driver.decl 4 in 0 drives 2 : !simulation.logic<1> design
    simulation.driver.decl 5 in 0 drives 3 : !simulation.logic<1> design
    simulation.driver.decl 6 in 0 drives 4 : !simulation.logic<1> design
    simulation.driver.decl 7 in 0 drives 5 : !simulation.logic<1> design

    // CHECK-LABEL: simulation.func @primitive_and
    // CHECK: %[[A:.*]] = simulation.ref.load %arg2
    // CHECK: %[[B:.*]] = simulation.ref.load %arg3
    // CHECK: %[[RESULT:.*]] = simulation.logic.binary and %[[A]], %[[B]]
    // CHECK-DAG: %[[AND_RISE:.*]] = simulation.time.constant 7
    // CHECK-DAG: %[[AND_FALL:.*]] = simulation.time.constant 11
    // CHECK-DAG: %[[AND_OFF:.*]] = simulation.time.constant 7
    // CHECK: simulation.driver.drive_inertial %arg1 = %[[RESULT]] after[%[[AND_RISE]], %[[AND_FALL]], %[[AND_OFF]]] site 9100001 : 0 vector = false
    // CHECK: simulation.suspend.any %arg2, %arg3
    simulation.func @primitive_and(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100001 : i64,
                    schedule.primitive_name = "and",
                    simulation.propagation_delays = array<i64: 7, 11>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.and_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.b", argument = 3, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 10 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.and_out", referenced_symbol = @and_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 12 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic1} {}
      simulation.return
    }

    // N-input xnor is XOR reduction followed by one inversion, including for
    // arities greater than two.
    // CHECK-LABEL: simulation.func @primitive_xnor
    // CHECK: %[[XOR0:.*]] = simulation.logic.binary xor
    // CHECK: %[[XOR1:.*]] = simulation.logic.binary xor %[[XOR0]],
    // CHECK: %[[XNOR:.*]] = simulation.logic.unary bit_not %[[XOR1]]
    // CHECK: simulation.driver.drive %arg1 = %[[XNOR]]
    simulation.func @primitive_xnor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64},
        %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %c: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100002 : i64,
                    schedule.primitive_name = "xnor",
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.xnor_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.b", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.c", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.xnor_out", referenced_symbol = @xnor_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 22 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.c", referenced_symbol = @c, semantic_type = !logic1} {}
      simulation.return
    }

    // IEEE 1800-2017 Table 28-4: a buf input of z drives x, so the pass-through
    // gate cannot forward its input unchanged.
    // CHECK-LABEL: simulation.func @primitive_buf
    // CHECK: %[[IN:.*]] = simulation.ref.load %arg2
    // CHECK: %[[ONES:.*]] = simulation.logic.constant true, false
    // CHECK: %[[RESULT:.*]] = simulation.logic.binary and %[[IN]], %[[ONES]]
    // CHECK: simulation.driver.drive %arg1 = %[[RESULT]]
    simulation.func @primitive_buf(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100005 : i64,
                    schedule.primitive_name = "buf",
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.buf_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 40 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.buf_out", referenced_symbol = @buf_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 42 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      simulation.return
    }

    // CHECK-LABEL: simulation.func @primitive_bufif0
    // CHECK: %[[Z:.*]] = simulation.logic.constant true, true
    // CHECK: %[[LOW_ENABLE:.*]] = simulation.logic.binary and
    // CHECK: %[[HIGH_ENABLE:.*]] = simulation.logic.binary and
    // CHECK: %[[LOW:.*]] = simulation.logic.mux %[[LOW_ENABLE]] ? %{{.*}} : %[[Z]]
    // CHECK: %[[HIGH:.*]] = simulation.logic.mux %[[HIGH_ENABLE]] ? %{{.*}} : %[[Z]]
    // CHECK: simulation.driver.drive %arg1 = %[[LOW]]
    // CHECK-SAME: schedule.defer_net_resolution
    // CHECK: simulation.driver.drive %arg2 = %[[HIGH]]
    simulation.func @primitive_bufif0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out_low: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64, simulation.strength_driver_bank = 0 : i32},
        %out_high: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 4 : i64, simulation.strength_driver_bank = 1 : i32},
        %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %control: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100003 : i64,
                    schedule.primitive_name = "bufif0",
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.bufif0_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.bufif0_out", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.a", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.control", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 30 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.bufif0_out", referenced_symbol = @bufif0_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 32 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      simulation.return
    }

    // CHECK-LABEL: simulation.func @primitive_notif1
    // CHECK: %[[INVERTED:.*]] = simulation.logic.unary bit_not
    // CHECK: %[[Z:.*]] = simulation.logic.constant true, true
    // CHECK: %[[LOW_ENABLE:.*]] = simulation.logic.binary and
    // CHECK: %[[HIGH_ENABLE:.*]] = simulation.logic.binary and
    // CHECK: %[[LOW:.*]] = simulation.logic.mux %[[LOW_ENABLE]] ? %{{.*}} : %[[Z]]
    // CHECK: %[[HIGH:.*]] = simulation.logic.mux %[[HIGH_ENABLE]] ? %{{.*}} : %[[Z]]
    // CHECK: simulation.driver.drive %arg1 = %[[LOW]]
    // CHECK-SAME: schedule.defer_net_resolution
    // CHECK: simulation.driver.drive %arg2 = %[[HIGH]]
    simulation.func @primitive_notif1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out_low: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64, simulation.strength_driver_bank = 0 : i32},
        %out_high: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 5 : i64, simulation.strength_driver_bank = 1 : i32},
        %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %control: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100004 : i64,
                    schedule.primitive_name = "notif1",
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.notif1_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.notif1_out", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.a", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.control", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 40 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.notif1_out", referenced_symbol = @notif1_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 42 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      simulation.return
    }

    // IEEE 1800-2017 28.10: pullup and pulldown have no inputs and drive a
    // constant 1 or 0. Strength is carried by the driver declaration.
    // CHECK-LABEL: simulation.func @primitive_pullup
    // CHECK: %[[PULL1:.*]] = simulation.logic.constant true, false
    // CHECK-NEXT: simulation.driver.drive %arg1 = %[[PULL1]]
    // CHECK-NEXT: simulation.return
    simulation.func @primitive_pullup(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 6 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100006 : i64,
                    schedule.primitive_name = "pullup",
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.pullup_out", argument = 1, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 50 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "top.pullup_out", referenced_symbol = @pullup_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 52 : i64, semantic_type = !logic1} {}
      }
      simulation.return
    }

    // CHECK-LABEL: simulation.func @primitive_pulldown
    // CHECK: %[[PULL0:.*]] = simulation.logic.constant false, false
    // CHECK-NEXT: simulation.driver.drive %arg1 = %[[PULL0]]
    // CHECK-NEXT: simulation.return
    simulation.func @primitive_pulldown(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 7 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100007 : i64,
                    schedule.primitive_name = "pulldown",
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.pulldown_out", argument = 1, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 60 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 61 : i64, referenced_path = "top.pulldown_out", referenced_symbol = @pulldown_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 62 : i64, semantic_type = !logic1} {}
      }
      simulation.return
    }
  }
}
