// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @primitive_units {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9100001 in 0 continuous hierarchy "test.primitives.and"
    obelisk_sim.code_unit.decl 9100002 in 0 continuous hierarchy "test.primitives.xnor"
    obelisk_sim.code_unit.decl 9100003 in 0 continuous hierarchy "test.primitives.bufif0"
    obelisk_sim.code_unit.decl 9100004 in 0 continuous hierarchy "test.primitives.notif1"
    obelisk_sim.code_unit.decl 9100005 in 0 continuous hierarchy "test.primitives.buf"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 3 in 0 drives 3 : !obelisk_sim.logic<1> design

    // CHECK-LABEL: obelisk_sim.func @primitive_and
    // CHECK: %[[A:.*]] = obelisk_sim.ref.load %arg2
    // CHECK: %[[B:.*]] = obelisk_sim.ref.load %arg3
    // CHECK: %[[RESULT:.*]] = obelisk_sim.logic.binary and %[[A]], %[[B]]
    // CHECK: obelisk_sim.driver.drive %arg1 = %[[RESULT]]
    // CHECK: obelisk_sim.suspend.any %arg2, %arg3
    obelisk_sim.func @primitive_and(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100001 : i64,
                    obelisk_sim.primitive_name = "and",
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.and_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.b", argument = 3, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 10 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.and_out", referenced_symbol = @and_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 12 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic1} {}
      obelisk_sim.return
    }

    // N-input xnor is XOR reduction followed by one inversion, including for
    // arities greater than two.
    // CHECK-LABEL: obelisk_sim.func @primitive_xnor
    // CHECK: %[[XOR0:.*]] = obelisk_sim.logic.binary xor
    // CHECK: %[[XOR1:.*]] = obelisk_sim.logic.binary xor %[[XOR0]],
    // CHECK: %[[XNOR:.*]] = obelisk_sim.logic.unary bit_not %[[XOR1]]
    // CHECK: obelisk_sim.driver.drive %arg1 = %[[XNOR]]
    obelisk_sim.func @primitive_xnor(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %c: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100002 : i64,
                    obelisk_sim.primitive_name = "xnor",
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.xnor_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.b", argument = 3, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.c", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.xnor_out", referenced_symbol = @xnor_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 22 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.c", referenced_symbol = @c, semantic_type = !logic1} {}
      obelisk_sim.return
    }

    // IEEE 1800-2017 Table 28-6: a buf input of z drives x, so the pass-through
    // gate cannot forward its input unchanged.
    // CHECK-LABEL: obelisk_sim.func @primitive_buf
    // CHECK: %[[IN:.*]] = obelisk_sim.ref.load %arg2
    // CHECK: %[[ONES:.*]] = obelisk_sim.logic.constant true, false
    // CHECK: %[[RESULT:.*]] = obelisk_sim.logic.binary and %[[IN]], %[[ONES]]
    // CHECK: obelisk_sim.driver.drive %arg1 = %[[RESULT]]
    obelisk_sim.func @primitive_buf(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100005 : i64,
                    obelisk_sim.primitive_name = "buf",
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.buf_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 40 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.buf_out", referenced_symbol = @buf_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 42 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @primitive_bufif0
    // CHECK: %[[Z:.*]] = obelisk_sim.logic.constant true, true
    // CHECK: %[[MUX:.*]] = obelisk_sim.logic.mux %{{.*}} ? %[[Z]] : %{{.*}}
    // CHECK: obelisk_sim.driver.drive %arg1 = %[[MUX]]
    obelisk_sim.func @primitive_bufif0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %control: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100003 : i64,
                    obelisk_sim.primitive_name = "bufif0",
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.bufif0_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.control", argument = 3, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 30 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.bufif0_out", referenced_symbol = @bufif0_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 32 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @primitive_notif1
    // CHECK: %[[INVERTED:.*]] = obelisk_sim.logic.unary bit_not
    // CHECK: %[[Z:.*]] = obelisk_sim.logic.constant true, true
    // CHECK: %[[MUX:.*]] = obelisk_sim.logic.mux %{{.*}} ? %[[INVERTED]] : %[[Z]]
    // CHECK: obelisk_sim.driver.drive %arg1 = %[[MUX]]
    obelisk_sim.func @primitive_notif1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %control: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9100004 : i64,
                    obelisk_sim.primitive_name = "notif1",
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.notif1_out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.control", argument = 3, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 40 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.notif1_out", referenced_symbol = @notif1_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 42 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      obelisk_sim.return
    }
  }
}
