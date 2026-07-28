// RUN: obelisk -emit-obelisk %s \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --implicit-check-not=obelisk.sv.

module covergroup_lowering;
  logic [3:0] enclosing;

  covergroup cg with function sample(input logic [3:0] sampled);
    cp: coverpoint sampled iff (enclosing != 0) {
      bins values = {0, 1};
      bins range = {[2:3]};
      bins fallback = default;
    }
  endgroup

  cg c;
  int covered;
  int total;
  initial begin
    c = new;
    c.sample(enclosing);
    c.stop();
    c.start();
    $display("%f", c.get_inst_coverage());
    $display("%f", cg::get_coverage(covered, total));
  end
endmodule

// CHECK: obelisk_sim.covergroup.decl @[[DECL:__obelisk_covergroup_.*]] id 1 bins [3]
// CHECK: obelisk_sim.storage.decl {{[0-9]+}} in {{[0-9]+}} : !obelisk_sim.covergroup_handle<@[[DECL]]>
// CHECK: %[[HANDLE:.*]] = obelisk_sim.covergroup.create {{.*}} from @[[DECL]]
// CHECK: obelisk_sim.covergroup.sample_enabled {{.*}}, %{{.*}}
// CHECK: obelisk_sim.logic.is_true
// CHECK: obelisk_sim.covergroup.stop
// CHECK: obelisk_sim.covergroup.start
// CHECK: obelisk_sim.covergroup.instance_query
// CHECK: obelisk_sim.covergroup.type_query {{.*}} from @[[DECL]]
// CHECK: obelisk_sim.ref.store %covered
// CHECK: obelisk_sim.ref.store %total
// CHECK: obelisk_sim.logic.compare eq
// CHECK: obelisk_sim.logic.compare uge
// CHECK: obelisk_sim.logic.compare ule
// CHECK: obelisk_sim.covergroup.sample
