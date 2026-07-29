// RUN: %split-file %s %t
// RUN: obelisk-opt %t/off.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments))' | FileCheck %s --check-prefix=FUSED
// RUN: obelisk-opt %t/full.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments))' | FileCheck %s --check-prefix=VPI
// RUN: obelisk --native-scheduler=auto -emit-llvm %t/hybrid.sv -o %t/hybrid.ll
// RUN: FileCheck %s --check-prefix=HYBRID < %t/hybrid.ll

// FUSED: obelisk_sim.design @fusion attributes {
// FUSED-SAME: obelisk_sim.static_fusion = [#obelisk_sim.fusion<id = 0, fragments = [{{[0-9]+}}, {{[0-9]+}}]>]
// VPI: obelisk_sim.design @fusion
// VPI-NOT: obelisk_sim.static_fusion
// HYBRID: @__obelisk_aot_schedule_plan_v1
// HYBRID-COUNT-1: call i32 @obelisk_rt_v1_scheduler_add_aot
// HYBRID: call i32 @obelisk_rt_v1_scheduler_install_aot

//--- off.mlir
module {
  obelisk_sim.design @fusion {
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "fusion.a"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "fusion.b"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func private @a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      cf.br ^wait
    }

    obelisk_sim.func private @b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      cf.br ^wait
    }
  }
}

//--- full.mlir
module {
  obelisk_sim.design @fusion {
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "fusion.a"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "fusion.b"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func private @a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      cf.br ^wait
    }

    obelisk_sim.func private @b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      cf.br ^wait
    }
  }
}

//--- hybrid.sv
module hybrid_fusion;
  bit clock;
  bit count;
  string first;
  string second;

  always @(posedge clock)
    first = "first";

  always @(posedge clock)
    second = "second";

  always @(posedge clock)
    count = ~count;

  initial begin
    #1 clock = 1;
    #1;
    $display("%s %s %0d", first, second, count);
    $finish;
  end
endmodule
