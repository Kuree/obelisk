// RUN: obelisk-opt %s | FileCheck %s

module {
  simulation.design @coverage {
    simulation.scope.decl 0
    simulation.covergroup.decl @cg schema 1 debug "cg"
    simulation.covergroup.decl @base schema 2 debug "base"
    simulation.covergroup.decl @derived schema 3 base @base debug "derived"

    simulation.func @exercise(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %null = simulation.covergroup.null
        : !simulation.covergroup_handle<@cg>
      %derived_null = simulation.covergroup.null
        : !simulation.covergroup_handle<@derived>
      %base_view = simulation.covergroup.cast %derived_null
        : !simulation.covergroup_handle<@derived> to
          !simulation.covergroup_handle<@base>
      %derived_view = simulation.covergroup.cast %base_view
        : !simulation.covergroup_handle<@base> to
          !simulation.covergroup_handle<@derived>
      %handle = simulation.covergroup.create %ctx from @cg
        payloads [] argument_count 0 formal_ids [] expression_ids []
        : () -> !simulation.covergroup_handle<@cg>
      %enabled = simulation.covergroup.sample_enabled %ctx, %handle
        : (!simulation.context,
           !simulation.covergroup_handle<@cg>) -> i1
      %metric = arith.constant 22 : i32
      %name = simulation.string.literal "run.obcov"
      %saved = simulation.coverage.save %ctx metric %metric name %name
        : !simulation.context
      %merged = simulation.coverage.merge %ctx metric %metric name %name
        : !simulation.context
      %integral = arith.constant 37 : i7
      %logic = simulation.logic.constant 9 : i4, 2 : i4
        : !simulation.logic<4>
      %real = arith.constant 1.250000e+00 : f64
      simulation.covergroup.sample %ctx, %handle values [
          %integral, %logic, %real, %enabled] ids [101, 102, 103, 104]
        : (!simulation.context, !simulation.covergroup_handle<@cg>, i7,
           !simulation.logic<4>, f64, i1) -> ()
      simulation.covergroup.stop %ctx, %handle item 0
        : !simulation.context, !simulation.covergroup_handle<@cg>
      simulation.covergroup.start %ctx, %handle item 0
        : !simulation.context, !simulation.covergroup_handle<@cg>
      %percentage, %covered, %total =
        simulation.covergroup.instance_query %ctx, %handle item 0
        : !simulation.context,
          !simulation.covergroup_handle<@cg> -> (f64, i32, i32)
      %type_percentage, %type_covered, %type_total =
        simulation.covergroup.type_query %ctx from @cg item 0
        : !simulation.context -> (f64, i32, i32)
      simulation.return
    }
  }
}

// CHECK: simulation.covergroup.decl @cg schema 1
// CHECK: simulation.covergroup.decl @derived schema 3 base @base
// CHECK: simulation.covergroup.null
// CHECK: simulation.covergroup.cast
// CHECK: simulation.covergroup.create
// CHECK: simulation.covergroup.sample_enabled
// CHECK: simulation.coverage.save
// CHECK: simulation.coverage.merge
// CHECK: %[[INTEGRAL:.*]] = arith.constant 37 : i7
// CHECK: %[[LOGIC:.*]] = simulation.logic.constant
// CHECK: %[[REAL:.*]] = arith.constant 1.250000e+00 : f64
// CHECK: simulation.covergroup.sample {{.*}} values[%[[INTEGRAL]], %[[LOGIC]], %[[REAL]], %{{.*}}] ids [101, 102, 103, 104]
// CHECK: simulation.covergroup.stop
// CHECK: simulation.covergroup.start
// CHECK: simulation.covergroup.instance_query
// CHECK: simulation.covergroup.type_query
