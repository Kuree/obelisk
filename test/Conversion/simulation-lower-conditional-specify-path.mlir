// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Hand-authored Simulation IR exercises conditional path selection without
// depending on SystemVerilog parsing. Both `if` predicates share one source;
// if both are true the shorter delay wins, while ifnone applies only when
// neither truth evaluator returns true. The condition nets are sampled by
// direct calls but do not enter the continuous actor's sensitivity wait.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @conditional_specify_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9920001 in 0 continuous
        hierarchy "conditional_specify_lowering.path"
    obelisk_sim.code_unit.decl 9920002 in 0 observer
        hierarchy "conditional_specify_lowering.c0"
    obelisk_sim.code_unit.decl 9920003 in 0 observer
        hierarchy "conditional_specify_lowering.c1"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func private @condition0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9920002 : i64,
                    obelisk_sim.lowered} {
      %value = obelisk_sim.net.read %condition :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %truth = obelisk_sim.logic.is_true %value : !obelisk_sim.logic<1>
      obelisk_sim.return %truth : i1
    }

    obelisk_sim.func private @condition1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 2 : i64}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9920003 : i64,
                    obelisk_sim.lowered} {
      %value = obelisk_sim.net.read %condition :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %truth = obelisk_sim.logic.is_true %value : !obelisk_sim.logic<1>
      obelisk_sim.return %truth : i1
    }

    obelisk_sim.func @path(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %snapshot: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %c0: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %c1: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9920001 : i64,
                    obelisk_sim.timing_path_rules = [
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
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.c0", argument = 4, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.c1", argument = 5, kind = direct, copyOut = false>]} {
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
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @path
// The shared source snapshot is read, compared, and updated exactly once.
// CHECK-COUNT-1: obelisk_sim.logic.compare case_ne
// CHECK-COUNT-1: obelisk_sim.ref.store
// CHECK: obelisk_sim.call @condition0(%arg0, %arg4)
// CHECK: obelisk_sim.call @condition1(%arg0, %arg5)
// CHECK: arith.ori
// CHECK: arith.xori
// CHECK-COUNT-3: obelisk_sim.time.scale
// CHECK: obelisk_sim.driver.drive_inertial
// Conditions are sampled on a source activation, not watched themselves.
// CHECK: obelisk_sim.suspend.change %arg2
// CHECK-NOT: obelisk_sim.suspend.any
