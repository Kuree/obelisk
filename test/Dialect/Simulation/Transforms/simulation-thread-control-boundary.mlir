// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-suspension)))' -o %t
// RUN: FileCheck %s < %t
// RUN: talos-opt %t --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-suspension)))' | FileCheck %s

module {
  simulation.design @control_capture {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "control_capture.process"
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %live = arith.addi %input, %input : i32
      %body_only = arith.muli %input, %input : i32
      %control = simulation.control.enter 7
      simulation.control.boundary %control resume ^exit body ^body
    ^body:
      simulation.file.flush %ctx, %body_only : (!simulation.context, i32) -> ()
      simulation.control.leave %control
      cf.br ^exit
    ^exit:
      simulation.file.flush %ctx, %live : (!simulation.context, i32) -> ()
      simulation.return
    }
  }
}
// CHECK-LABEL: simulation.func @process
// CHECK: %[[LIVE:.*]] = arith.addi
// CHECK: %[[BODY:.*]] = arith.muli
// CHECK: %[[CONTROL:.*]] = simulation.control.enter 7
// CHECK: simulation.control.boundary %[[CONTROL]] resume ^[[EXIT:bb[0-9]+]](%[[LIVE]] : i32) body ^[[BODYBLOCK:bb[0-9]+]]
// CHECK: ^[[BODYBLOCK]]:
// CHECK: simulation.file.flush %{{.*}}, %[[BODY]]
// CHECK: simulation.control.leave %[[CONTROL]]
// CHECK: cf.br ^[[EXIT]](%[[LIVE]] : i32)
// CHECK: ^[[EXIT]](%[[RESTORED:.*]]: i32):
// CHECK: simulation.file.flush %{{.*}}, %[[RESTORED]]
