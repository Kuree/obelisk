// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements,talos-sim-materialize-compute-fusion))' | FileCheck %s

// LRM 4.6 preserves statement order even when 4.7 allows other Active
// processes to interleave. A clocked reader need not wait on either output to
// observe that order. After the rising edge, (a,b) can be (0,0), (1,0), or (1,1),
// but never (0,1). Splitting the two stores into independent actors therefore
// needs a proof about all sampling readers, not only edge waits on a or b.
// `comb` is the only writer of a and b, so statement planning must keep both
// stores in one fragment, in source order.
// CHECK: #schedule.fragment<id = {{[0-9]+}}, function = @comb, block = 1, {{[^>]*}}effects = [#schedule.effect<effect = read, {{[^>]*}}descriptor = 0,{{[^>]*}}>, #schedule.effect<effect = write, {{[^>]*}}descriptor = 1,{{[^>]*}}>, #schedule.effect<effect = write, {{[^>]*}}descriptor = 2,{{[^>]*}}>, #schedule.effect<effect = watch,
// CHECK-NOT: function = @comb, block = 1,
!ref = !simulation.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @source_order {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "drive"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "comb"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %a = simulation.context.storage %ctx[1] : !ref
      %b = simulation.context.storage %ctx[2] : !ref
      %zero = arith.constant false
      simulation.ref.store %zero to %clk : i1, !ref
      %comb = simulation.spawn @comb(%ctx, %clk, %a, %b) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %sample = simulation.spawn @sample(%ctx, %clk, %a, %b) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %drive = simulation.spawn @drive(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func @comb(
    // CHECK: %[[VALUE:.*]] = simulation.ref.load %arg1
    // CHECK-NEXT: simulation.ref.store %[[VALUE]] to %arg2
    // CHECK-NEXT: simulation.ref.store %[[VALUE]] to %arg3
    // CHECK-NEXT: simulation.suspend.change %arg1
    // CHECK-NOT: simulation.func @comb
    simulation.func @comb(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %clk : !ref -> i1
      simulation.ref.store %value to %a : i1, !ref
      simulation.ref.store %value to %b : i1, !ref
      simulation.suspend.change %clk to ^body : !ref
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      // Read b first: even a source-ordered interleaving cannot invent 01.
      %bv = simulation.ref.load %b : !ref -> i1
      %av = simulation.ref.load %a : !ref -> i1
      %aw = arith.extui %av : i1 to i8
      %bw = arith.extui %bv : i1 to i8
      %one = arith.constant 1 : i8
      %shift = arith.shli %aw, %one : i8
      %pair = arith.ori %shift, %bw : i8
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      simulation.nba.enqueue %pair to %result : (i8, !simulation.ref<i8>) -> ()
      cf.br ^wait
    }
    simulation.func @drive(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^rise
    ^rise:
      %one = arith.constant true
      simulation.ref.store %one to %clk : i1, !ref
      %delay2 = simulation.time.constant 1
      simulation.suspend.delay %delay2 to ^check
    ^check:
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %pair = simulation.ref.load %result : !simulation.ref<i8> -> i8
      %fmt = simulation.bytes.constant "ordered sample=%0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %pair) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i8
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
