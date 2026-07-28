// RUN: obelisk-opt %s | FileCheck %s

module {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @cg id 1 bins [2, 4] debug "cg"

    obelisk_sim.func @exercise(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %null = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@cg>
      %handle = obelisk_sim.covergroup.create %ctx from @cg
        : !obelisk_sim.context -> !obelisk_sim.covergroup_handle<@cg>
      %enabled = obelisk_sim.covergroup.sample_enabled %ctx, %handle
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>) -> i1
      obelisk_sim.covergroup.bin_hit %ctx, %handle[0, 1]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.sample %ctx, %handle[
          %enabled, %enabled, %enabled, %enabled, %enabled, %enabled]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.stop %ctx, %handle
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.start %ctx, %handle
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      %percentage, %covered, %total =
        obelisk_sim.covergroup.instance_query %ctx, %handle
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>) -> (f64, i32, i32)
      %type_percentage, %type_covered, %type_total =
        obelisk_sim.covergroup.type_query %ctx from @cg
        : !obelisk_sim.context -> (f64, i32, i32)
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.covergroup.decl @cg id 1 bins [2, 4]
// CHECK: obelisk_sim.covergroup.null
// CHECK: obelisk_sim.covergroup.create
// CHECK: obelisk_sim.covergroup.sample_enabled
// CHECK: obelisk_sim.covergroup.bin_hit
// CHECK: obelisk_sim.covergroup.sample
// CHECK: obelisk_sim.covergroup.stop
// CHECK: obelisk_sim.covergroup.start
// CHECK: obelisk_sim.covergroup.instance_query
// CHECK: obelisk_sim.covergroup.type_query
