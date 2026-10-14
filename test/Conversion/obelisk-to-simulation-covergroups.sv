// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// IEEE 19.5: the compiler evaluates each sampled expression once, in
// coverpoint order (cp's expression, its iff guard, then empty's expression).
// The sample operation carries typed values and schema identities; it does not
// encode bin decisions or table ordinals. The bin value lists (19.5.1) are
// create-time payloads in declaration order: values, range, removed, ignored,
// illegal, then the second coverpoint; a default bin has no value list.
// CHECK: simulation.covergroup.decl @[[DECL:__obelisk_covergroup_cg_[0-9A-F]+]] schema {{[1-9][0-9]*}}
// CHECK: simulation.storage.decl {{[0-9]+}} in {{[0-9]+}} : !simulation.covergroup_handle<@[[DECL]]>
// CHECK: simulation.covergroup.create {{.*}} from @[[DECL]] payloads[%c0_i32, %c1_i32, %c2_i32, %c3_i32, %c4_i32, %c1_i32, %c4_i32, %c2_i32, %c5_i32, %c5_i32]
// CHECK: simulation.covergroup.set_name {{.*}} name %{{.*}}
// CHECK: simulation.covergroup.sample_enabled {{.*}}, %{{.*}}
// CHECK: simulation.covergroup.sample {{.*}} values[%{{.*}}, %{{.*}}, %{{.*}}] ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}]{{.*}}!simulation.logic<32>, i1, !simulation.logic<32>)

// IEEE 19.8: stop, start and the instance and type coverage queries address
// the instance handle or the declaration, and write their two output
// arguments through the callee's references.
// CHECK: simulation.covergroup.stop
// CHECK: simulation.covergroup.start
// CHECK: %[[PCT:[a-z_0-9]+]], %[[COV:[a-z_0-9]+]], %[[TOT:[a-z_0-9]+]] = simulation.covergroup.instance_query
// CHECK: simulation.ref.store %[[COV]] to
// CHECK: simulation.ref.store %[[TOT]] to
// CHECK: %[[TPCT:[a-z_0-9]+]], %[[TCOV:[a-z_0-9]+]], %[[TTOT:[a-z_0-9]+]] = simulation.covergroup.type_query {{.*}} from @[[DECL]]
// CHECK: simulation.ref.store %[[TCOV]] to
// CHECK: simulation.ref.store %[[TTOT]] to
// CHECK-NOT: obelisk.sv.

module covergroup_lowering;
  int enclosing;
  covergroup cg with function sample(int sampled);
    cp: coverpoint sampled iff (sampled != enclosing) {
      bins values = {0, 1};
      bins range = {[2:3]};
      bins fallback = default;
      bins removed = {4};
      ignore_bins ignored = {1, 4};
      illegal_bins illegal = {2};
    }
    empty: coverpoint sampled {
      bins removed = {5};
      ignore_bins ignored = {5};
    }
  endgroup
  cg c;
  int covered, total;
  initial begin
    c = new;
    c.set_inst_name("named");
    c.sample(enclosing);
    c.stop();
    c.start();
    $display("%f", c.get_inst_coverage(covered, total));
    $display("%f", cg::get_coverage(covered, total));
  end
endmodule
