// RUN: obelisk-opt %s | FileCheck %s

module {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @cg schema 1 debug "cg"
    obelisk_sim.covergroup.decl @base schema 2 debug "base"
    obelisk_sim.covergroup.decl @derived schema 3 base @base debug "derived"

    obelisk_sim.func @exercise(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %null = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@cg>
      %derived_null = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@derived>
      %base_view = obelisk_sim.covergroup.cast %derived_null
        : !obelisk_sim.covergroup_handle<@derived> to
          !obelisk_sim.covergroup_handle<@base>
      %derived_view = obelisk_sim.covergroup.cast %base_view
        : !obelisk_sim.covergroup_handle<@base> to
          !obelisk_sim.covergroup_handle<@derived>
      %handle = obelisk_sim.covergroup.create %ctx from @cg
        payloads [] argument_count 0 formal_ids [] expression_ids []
        : () -> !obelisk_sim.covergroup_handle<@cg>
      %enabled = obelisk_sim.covergroup.sample_enabled %ctx, %handle
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>) -> i1
      %metric = arith.constant 22 : i32
      %name = obelisk_sim.string.literal "run.obcov"
      %saved = obelisk_sim.coverage.save %ctx metric %metric name %name
        : !obelisk_sim.context
      %merged = obelisk_sim.coverage.merge %ctx metric %metric name %name
        : !obelisk_sim.context
      %integral = arith.constant 37 : i7
      %logic = obelisk_sim.logic.constant 9 : i4, 2 : i4
        : !obelisk_sim.logic<4>
      %real = arith.constant 1.250000e+00 : f64
      obelisk_sim.covergroup.sample %ctx, %handle values [
          %integral, %logic, %real, %enabled] ids [101, 102, 103, 104]
        : (!obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>, i7,
           !obelisk_sim.logic<4>, f64, i1) -> ()
      obelisk_sim.covergroup.stop %ctx, %handle item 0
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.start %ctx, %handle item 0
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      %percentage, %covered, %total =
        obelisk_sim.covergroup.instance_query %ctx, %handle item 0
        : !obelisk_sim.context,
          !obelisk_sim.covergroup_handle<@cg> -> (f64, i32, i32)
      %type_percentage, %type_covered, %type_total =
        obelisk_sim.covergroup.type_query %ctx from @cg item 0
        : !obelisk_sim.context -> (f64, i32, i32)
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.covergroup.decl @cg schema 1
// CHECK: obelisk_sim.covergroup.decl @derived schema 3 base @base
// CHECK: obelisk_sim.covergroup.null
// CHECK: obelisk_sim.covergroup.cast
// CHECK: obelisk_sim.covergroup.create
// CHECK: obelisk_sim.covergroup.sample_enabled
// CHECK: obelisk_sim.coverage.save
// CHECK: obelisk_sim.coverage.merge
// CHECK: %[[INTEGRAL:.*]] = arith.constant 37 : i7
// CHECK: %[[LOGIC:.*]] = obelisk_sim.logic.constant
// CHECK: %[[REAL:.*]] = arith.constant 1.250000e+00 : f64
// CHECK: obelisk_sim.covergroup.sample {{.*}} values[%[[INTEGRAL]], %[[LOGIC]], %[[REAL]], %{{.*}}] ids [101, 102, 103, 104]
// CHECK: obelisk_sim.covergroup.stop
// CHECK: obelisk_sim.covergroup.start
// CHECK: obelisk_sim.covergroup.instance_query
// CHECK: obelisk_sim.covergroup.type_query
