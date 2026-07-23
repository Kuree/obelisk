// RUN: obelisk -emit-sim %s | FileCheck %s

module supported_fork;
  initial begin
    fork
      begin #1; end
      begin #2; end
    join_any
    fork
    join
    fork
      begin : named_branch
      end
    join_none
    wait fork;
    disable fork;
  end
endmodule

// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} fork hierarchy "supported_fork.{{.*}}.$fork.
// CHECK: obelisk_sim.func private @{{.*}} attributes {{.*}}entry_kind = 13 : i32
// CHECK: obelisk_sim.control.enter
// CHECK: obelisk_sim.control.leave
// CHECK: obelisk_sim.spawn @
// CHECK: obelisk_sim.suspend.join any
// CHECK: obelisk_sim.suspend.children
// CHECK: obelisk_sim.children.disable
