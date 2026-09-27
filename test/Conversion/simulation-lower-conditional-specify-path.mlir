// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Hand-authored Simulation IR exercises conditional path selection without
// depending on SystemVerilog parsing. Both `if` predicates share one source;
// if both are true the shorter delay wins, while ifnone applies only when
// neither truth evaluator returns true. The condition nets are sampled by
// direct calls but do not enter the continuous actor's sensitivity wait.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @conditional_specify_lowering {
    simulation.scope.decl 0
    simulation.code_unit.decl 9920001 in 0 continuous
        hierarchy "conditional_specify_lowering.path"
    simulation.code_unit.decl 9920002 in 0 observer
        hierarchy "conditional_specify_lowering.c0"
    simulation.code_unit.decl 9920003 in 0 observer
        hierarchy "conditional_specify_lowering.c1"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design

    simulation.func private @condition0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9920002 : i64,
                    simulation.lowered} {
      %value = simulation.net.read %condition :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %truth = simulation.logic.is_true %value : !simulation.logic<1>
      simulation.return %truth : i1
    }

    simulation.func private @condition1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 2 : i64}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9920003 : i64,
                    simulation.lowered} {
      %value = simulation.net.read %condition :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %truth = simulation.logic.is_true %value : !simulation.logic<1>
      simulation.return %truth : i1
    }

    simulation.func @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %source: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64},
        %snapshot: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %c0: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64},
        %c1: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9920001 : i64,
                    simulation.timing_path_rules = [
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       polarity = 0 : i32, delays = array<i64: 5>,
                       condition_kind = 1 : i32, condition_group = 0 : i32,
                       condition_evaluator = @condition0,
                       condition_captures = ["top.c0"]},
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       polarity = 1 : i32, delays = array<i64: 2>,
                       condition_kind = 1 : i32, condition_group = 0 : i32,
                       condition_evaluator = @condition1,
                       condition_captures = ["top.c1"]},
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       polarity = 2 : i32, delays = array<i64: 7>,
                       condition_kind = 2 : i32, condition_group = 0 : i32}],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.c0", argument = 4, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.c1", argument = 5, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.named_value attributes {
            node_id = 3 : i64, referenced_path = "top.source",
            referenced_symbol = @source, semantic_type = !logic1} {}
      }
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @path
// The shared source snapshot is read, compared, and updated exactly once.
// CHECK-COUNT-1: simulation.logic.compare case_ne
// CHECK-COUNT-1: simulation.ref.store
// CHECK: simulation.call @condition0(%arg0, %arg4)
// CHECK: simulation.call @condition1(%arg0, %arg5)
// CHECK: arith.ori
// CHECK: arith.xori
// CHECK-COUNT-3: simulation.time.scale
// CHECK: simulation.driver.drive_inertial
// Conditions are sampled on a source activation, not watched themselves.
// CHECK: simulation.suspend.change %arg2
// CHECK-NOT: simulation.suspend.any
