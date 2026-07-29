// RUN: %split-file %s %t
// RUN: obelisk-opt %t/off.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments))' | FileCheck %s --check-prefix=FUSED
// RUN: obelisk-opt %t/full.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments))' | FileCheck %s --check-prefix=VPI
// RUN: obelisk-opt %t/wait-view.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=WAIT-VIEW
// RUN: obelisk --native-scheduler=auto -emit-llvm %t/hybrid.sv -o %t/hybrid.ll
// RUN: FileCheck %s --check-prefix=HYBRID < %t/hybrid.ll
// RUN: obelisk -O3 -emit-sim %t/materialize.sv -o %t/materialized.mlir
// RUN: FileCheck %s --check-prefix=MATERIALIZED < %t/materialized.mlir
// RUN: obelisk -O3 --native-scheduler=generic %t/materialize.sv -o %t/generic
// RUN: obelisk -O3 --native-scheduler=aot %t/materialize.sv -o %t/aot
// RUN: %t/generic > %t/generic.out
// RUN: %t/aot > %t/aot.out
// RUN: diff %t/generic.out %t/aot.out
// RUN: obelisk -O0 --native-scheduler=generic %t/order.sv -o %t/order.unfused
// RUN: obelisk -O3 --native-scheduler=generic %t/order.sv -o %t/order.fused
// RUN: %t/order.unfused > %t/order.unfused.out
// RUN: %t/order.fused > %t/order.fused.out
// RUN: diff %t/order.unfused.out %t/order.fused.out
// RUN: FileCheck %s --check-prefix=ORDER < %t/order.fused.out
// RUN: obelisk -O3 -emit-sim %t/order.sv -o %t/order.mlir
// RUN: FileCheck %s --check-prefix=REJECTED < %t/order.mlir
// RUN: obelisk -O0 --native-scheduler=generic %t/random.sv -o %t/random.unfused
// RUN: obelisk -O3 --native-scheduler=generic %t/random.sv -o %t/random.optimized
// RUN: %t/random.unfused > %t/random.unfused.out
// RUN: %t/random.optimized > %t/random.optimized.out
// RUN: diff %t/random.unfused.out %t/random.optimized.out
// RUN: FileCheck %s --check-prefix=RANDOM < %t/random.optimized.out
// RUN: obelisk -O3 -emit-sim %t/random.sv -o %t/random.mlir
// RUN: FileCheck %s --check-prefix=REJECTED < %t/random.mlir
// RUN: obelisk -O0 --native-scheduler=generic %t/nba-order.sv -o %t/nba-order.unfused
// RUN: obelisk -O3 --native-scheduler=generic %t/nba-order.sv -o %t/nba-order.fused
// RUN: %t/nba-order.unfused > %t/nba-order.unfused.out
// RUN: %t/nba-order.fused > %t/nba-order.fused.out
// RUN: diff %t/nba-order.unfused.out %t/nba-order.fused.out
// RUN: FileCheck %s --check-prefix=NBA-ORDER < %t/nba-order.fused.out
// RUN: obelisk -O3 -emit-sim %t/rejected.sv -o %t/rejected.mlir
// RUN: FileCheck %s --check-prefix=REJECTED < %t/rejected.mlir
// RUN: obelisk -O0 --native-scheduler=generic %t/deadline-order.sv -o %t/deadline-order.unfused
// RUN: obelisk -O3 --native-scheduler=generic %t/deadline-order.sv -o %t/deadline-order.optimized
// RUN: %t/deadline-order.unfused > %t/deadline-order.unfused.out
// RUN: %t/deadline-order.optimized > %t/deadline-order.optimized.out
// RUN: diff %t/deadline-order.unfused.out %t/deadline-order.optimized.out
// RUN: FileCheck %s --check-prefix=DEADLINE < %t/deadline-order.optimized.out
// RUN: obelisk -O3 -emit-sim %t/deadline-order.sv -o %t/deadline-order.mlir
// RUN: FileCheck %s --check-prefix=REJECTED < %t/deadline-order.mlir
// RUN: obelisk -O3 -emit-sim %t/multiple-producers.sv -o %t/multiple-producers.mlir
// RUN: FileCheck %s --check-prefix=MULTIPLE-PRODUCERS < %t/multiple-producers.mlir
// RUN: obelisk -O3 --native-scheduler=generic %t/backedge.sv -o %t/backedge.generic
// RUN: obelisk -O3 --native-scheduler=aot %t/backedge.sv -o %t/backedge.aot
// RUN: %t/backedge.generic > %t/backedge.generic.out
// RUN: %t/backedge.aot > %t/backedge.aot.out
// RUN: diff %t/backedge.generic.out %t/backedge.aot.out
// RUN: FileCheck %s --check-prefix=BACKEDGE < %t/backedge.aot.out
// RUN: obelisk -O3 -emit-sim %t/backedge.sv -o %t/backedge.mlir
// RUN: FileCheck %s --check-prefix=BACKEDGE-IR < %t/backedge.mlir
// RUN: obelisk -O0 --native-scheduler=generic %t/entry-order.sv -o %t/entry-order.unfused
// RUN: obelisk -O3 --native-scheduler=generic %t/entry-order.sv -o %t/entry-order.optimized
// RUN: %t/entry-order.unfused > %t/entry-order.unfused.out
// RUN: %t/entry-order.optimized > %t/entry-order.optimized.out
// RUN: diff %t/entry-order.unfused.out %t/entry-order.optimized.out
// RUN: FileCheck %s --check-prefix=ENTRY < %t/entry-order.optimized.out
// RUN: obelisk -O3 -emit-sim %t/entry-order.sv -o %t/entry-order.mlir
// RUN: FileCheck %s --check-prefix=ENTRY-FUSED < %t/entry-order.mlir

// FUSED: obelisk_sim.design @fusion attributes {
// FUSED-SAME: obelisk_sim.static_fusion = [#obelisk_sim.fusion<id = 0, fragments = [{{[0-9]+}}, {{[0-9]+}}]>]
// VPI: obelisk_sim.design @fusion
// VPI-NOT: obelisk_sim.static_fusion
// WAIT-VIEW: obelisk_sim.design @wait_view
// WAIT-VIEW-NOT: __obelisk_fused_
// HYBRID: @__obelisk_aot_schedule_plan_v1
// HYBRID-COUNT-1: call i32 @obelisk_rt_v1_scheduler_add_aot
// HYBRID: call i32 @obelisk_rt_v1_scheduler_install_aot
// MATERIALIZED: obelisk_sim.spawn @__obelisk_fused_0
// MATERIALIZED-COUNT-1: obelisk_sim.func private @__obelisk_fused_0
// MATERIALIZED-COUNT-1: obelisk_sim.suspend.edge posedge
// MATERIALIZED-COUNT-2: obelisk_sim.nba.enqueue
// MATERIALIZED-NOT: obelisk_sim.func private @unit_0
// MATERIALIZED-NOT: obelisk_sim.func private @unit_1
// ORDER: 1
// RANDOM: 1202223563 1622423293
// NBA-ORDER: 2
// DEADLINE: 1
// ENTRY: 1 1
// ENTRY-FUSED: __obelisk_fused_
// BACKEDGE: A
// BACKEDGE-NEXT: X
// BACKEDGE-NEXT: B
// BACKEDGE-IR: obelisk_sim.static_fusion
// BACKEDGE-IR-NOT: __obelisk_fused_
// MULTIPLE-PRODUCERS: obelisk_sim.design
// MULTIPLE-PRODUCERS-NOT: obelisk_sim.static_fusion
// MULTIPLE-PRODUCERS-NOT: __obelisk_fused_
// REJECTED-NOT: __obelisk_fused_

//--- off.mlir
module {
  obelisk_sim.design @fusion {
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "fusion.a"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "fusion.b"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %a = obelisk_sim.spawn @a(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      %b = obelisk_sim.spawn @b(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      obelisk_sim.return
    }

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

//--- wait-view.mlir
module {
  obelisk_sim.design @wait_view {
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "wait_view.a"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "wait_view.b"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %a = obelisk_sim.spawn @a(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      %b = obelisk_sim.spawn @b(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      %view = obelisk_sim.ref.extract %clock from 0 :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.suspend.edge posedge %view to ^body :
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

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %a = obelisk_sim.spawn @a(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      %b = obelisk_sim.spawn @b(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      obelisk_sim.return
    }

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

//--- materialize.sv
module materialize_fusion;
  bit clock;
  int a;
  int b;

  always @(posedge clock)
    a <= a + 1;

  always @(posedge clock)
    b <= b + 2;

  initial begin
    repeat (4) begin
      #1 clock = 1;
      #1 clock = 0;
    end
    $display("%0d %0d", a, b);
    $finish;
  end
endmodule

//--- order.sv
module activation_order;
  bit c;
  bit d;
  bit trigger;
  int x;
  int sampled;

  always @(posedge c)
    x = 1;

  always @(posedge d)
    sampled = x;

  always @(posedge c)
    x = 2;

  initial begin
    #1 trigger = 1;
    c = trigger;
    d = trigger;
    #1;
    $display("%0d", sampled);
    $finish;
  end
endmodule

//--- random.sv
module process_random;
  bit clock;
  int a;
  int b;

  always @(posedge clock)
    a = $urandom;

  always @(posedge clock)
    b = $urandom;

  initial begin
    #1 clock = 1;
    #1;
    $display("%0d %0d", a, b);
    $finish;
  end
endmodule

//--- nba-order.sv
module same_root_nba_order;
  bit clock;
  int value;

  always @(posedge clock)
    value <= 1;

  always @(posedge clock)
    value <= 2;

  initial begin
    #1 clock = 1;
    #1;
    $display("%0d", value);
    $finish;
  end
endmodule

//--- rejected.sv
module rejected_fusions;
  bit clock;
  bit published;
  bit other;
  bit observed;
  int controlled_a;
  int controlled_b;
  string managed_a;
  string managed_b;

  always @(posedge clock)
    published = 1;

  always @(posedge clock)
    other = 1;

  always @(posedge published)
    observed = other;

  always @(posedge clock) begin : controlled_first
    controlled_a = 1;
  end

  always @(posedge clock) begin : controlled_second
    controlled_b = 1;
  end

  always @(posedge clock)
    managed_a = "a";

  always @(posedge clock)
    managed_b = "b";

  initial begin
    #1 clock = 1;
    #1;
    $finish;
  end
endmodule

//--- deadline-order.sv
module deadline_activation_order;
  bit clock;
  int value;
  int sampled;

  always @(posedge clock)
    value = 1;

  initial
    #1 clock = 1;

  initial begin
    #1 sampled = value;
    #1;
    $display("%0d", sampled);
    $finish;
  end

  always @(posedge clock)
    value = 2;
endmodule

//--- entry-order.sv
module initial_entry_order;
  bit clock;
  int first;
  int second;

  always @(posedge clock)
    first = 1;

  initial
    clock = 1;

  always @(posedge clock)
    second = 1;

  initial begin
    #1;
    $display("%0d %0d", first, second);
    $finish;
  end
endmodule

//--- multiple-producers.sv
module multiple_clock_producers;
  bit clock;
  int first;
  int second;

  always @(posedge clock)
    first = 1;

  initial
    #1 clock = 1;

  initial
    #1 clock = 0;

  always @(posedge clock)
    second = 1;

  initial begin
    #2;
    $finish;
  end
endmodule

//--- backedge.sv
module activating_backedge;
  bit clock;
  bit published;

  always @(posedge published)
    $display("X");

  always @(posedge clock) begin
    $display("A");
    published = 1;
  end

  always @(posedge clock)
    $display("B");

  initial
    #1 clock = 1;
endmodule
