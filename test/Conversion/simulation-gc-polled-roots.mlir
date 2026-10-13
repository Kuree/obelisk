// RUN: obelisk-opt %s --obelisk-sim-insert-gc-polls | FileCheck %s

// Handles live in an initial block across callees that collect at their own
// polls: an allocating loop, a recursive builder, string churn, and a virtual
// call whose overrides differ. The input is the native backend's view of this
// source (`obelisk -O0 --vpi=off`, so calls stay calls) ahead of its passes;
// ../Runtime/simulation-gc-uvm.test runs it with a collection at every
// opportunity, which reclaims a handle if any of these calls skips its roots.
//
// | // Handles live in a caller across callees that collect at their own polls:
// | // an allocating loop, a recursive builder, string churn, and a virtual call.
// | class node;
// |   int v;
// |   node next;
// |   function new(int v);
// |     this.v = v;
// |   endfunction
// |   virtual function int weight();
// |     return v;
// |   endfunction
// | endclass
// |
// | class heavy extends node;
// |   function new(int v);
// |     super.new(v);
// |   endfunction
// |   virtual function int weight();
// |     int s = 0;
// |     for (int i = 0; i < 8; i++) begin
// |       automatic node t = new(i);
// |       s += t.v;
// |     end
// |     return v + s;
// |   endfunction
// | endclass
// |
// | function automatic node build(int n);
// |   node head = null;
// |   for (int i = 0; i < n; i++) begin
// |     automatic node t = new(i);
// |     t.next = head;
// |     head = t;
// |   end
// |   return head;
// | endfunction
// |
// | function automatic int churn(int n);
// |   int s = 0;
// |   for (int i = 0; i < n; i++) begin
// |     automatic node t = new(i);
// |     s += t.v;
// |   end
// |   return s;
// | endfunction
// |
// | function automatic int rec(int d);
// |   node t = new(d);
// |   if (d == 0)
// |     return t.v;
// |   return rec(d - 1) + t.v;
// | endfunction
// |
// | function automatic string text(int n);
// |   string acc = "";
// |   for (int i = 0; i < n; i++)
// |     acc = {acc, $sformatf("%0d", i % 10)};
// |   return acc;
// | endfunction
// |
// | module tb3;
// |   initial begin
// |     automatic node keep = new(42);
// |     automatic node list = build(100);
// |     automatic string label = $sformatf("label%0d", keep.v);
// |     automatic int s = churn(1000);
// |     automatic int r = rec(50);
// |     automatic node h = new(7);
// |     automatic heavy hv = new(5);
// |     automatic int w;
// |     automatic string t = text(40);
// |     automatic int len = 0;
// |     h = hv;
// |     w = h.weight();
// |     for (node walk = list; walk != null; walk = walk.next)
// |       len++;
// |     $display("keep=%0d len=%0d s=%0d r=%0d w=%0d label=%s t=%0d", keep.v, len,
// |              s, r, w, label, t.len());
// |   end
// | endmodule

// node::new allocates nothing and stays poll-free at every call.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-NOT: simulation.gc.safepoint
// heavy::weight polls once per iteration of its allocating loop.
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK: simulation.gc.safepoint
// CHECK-NEXT: simulation.class.alloc
// build and churn: likewise.
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK: simulation.gc.safepoint
// CHECK-NEXT: simulation.class.alloc
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK: simulation.gc.safepoint
// CHECK-NEXT: simulation.class.alloc
// rec polls before recursing, not before its own allocation.
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK: simulation.class.alloc
// CHECK-NOT: simulation.gc.safepoint
// CHECK: simulation.class.direct_call @unit_0 {{.*}}{simulation.gc_poll_free}
// CHECK: simulation.gc.safepoint
// CHECK-NEXT: simulation.call @unit_6(
// text polls in its concatenation loop.
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK: simulation.gc.safepoint
// The initial block keeps its handles registered across every call that may
// poll, and only those.
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-NOT: simulation.gc.safepoint
// CHECK: simulation.class.direct_call @unit_0 {{.*}}{simulation.gc_poll_free}
// CHECK: simulation.call @unit_4(%{{.*}}) :
// CHECK: simulation.call @unit_5(%{{.*}}) :
// CHECK: simulation.call @unit_6(%{{.*}}) :
// CHECK: simulation.class.direct_call @unit_2 {{.*}}{simulation.gc_poll_free}
// CHECK: simulation.call @unit_7(%{{.*}}) :
// CHECK: simulation.class.virtual_call {{[^{]*}} :
// CHECK-NOT: simulation.gc.safepoint
// CHECK: simulation.return

#loc = loc(unknown)
#loc2 = loc("tb3.sv":3:1)
#loc5 = loc("tb3.sv":61:1)
#loc6 = loc("tb3.sv":6:3)
#loc7 = loc("tb3.sv":9:11)
#loc8 = loc("tb3.sv":15:3)
#loc9 = loc("tb3.sv":18:11)
#loc10 = loc("tb3.sv":28:1)
#loc11 = loc("tb3.sv":38:1)
#loc12 = loc("tb3.sv":47:1)
#loc13 = loc("tb3.sv":54:1)
#loc14 = loc("tb3.sv":62:3)
#loc19 = loc("tb3.sv":20:14)
#loc28 = loc("tb3.sv":30:12)
#loc35 = loc("tb3.sv":40:12)
#loc52 = loc("tb3.sv":56:12)
#loc71 = loc("tb3.sv":72:19)
#loc72 = loc("tb3.sv":75:15)
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32", llvm.target_triple = "aarch64-unknown-linux-gnu", obelisk.coverage.language_version = 2023 : i32, obelisk.native.optimization_level = 0 : i32, schedule.native_scheduler = 0 : i32, schedule.native_scheduler.auto_requested} {
  simulation.design @design attributes {compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1, nodes = [#schedule.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 1, function = @unit_8, block = 0, region = active, action = continue, tier = native, cost = 25, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 2, function = @unit_8, block = 1, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 3, function = @unit_8, block = 2, region = active, action = continue, tier = native, cost = 9, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 4, function = @unit_8, block = 3, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 5, function = @unit_8, block = 4, region = active, action = continue, tier = native, cost = 6, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 6, function = @unit_8, block = 5, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 7, function = @unit_8, block = 6, region = active, action = continue, tier = native, cost = 24, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 8, function = @unit_8, block = 7, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 9, function = @unit_8, block = 8, region = active, action = continue, tier = native, cost = 2, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 10, function = @unit_8, block = 9, region = active, action = continue, tier = native, cost = 3, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 11, function = @unit_8, block = 10, region = active, action = continue, tier = native, cost = 3, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 12, function = @unit_8, block = 11, region = active, action = terminate, tier = native, cost = 4, lane = 0, twoState = true, effects = []>], edges = [#schedule.edge<source = 0, target = 1, kind = spawn>, #schedule.edge<source = 1, target = 2, kind = process_order>, #schedule.edge<source = 1, target = 3, kind = process_order>, #schedule.edge<source = 3, target = 4, kind = process_order>, #schedule.edge<source = 3, target = 5, kind = process_order>, #schedule.edge<source = 5, target = 6, kind = process_order>, #schedule.edge<source = 5, target = 7, kind = process_order>, #schedule.edge<source = 7, target = 8, kind = process_order>, #schedule.edge<source = 7, target = 9, kind = process_order>, #schedule.edge<source = 9, target = 10, kind = process_order>, #schedule.edge<source = 10, target = 11, kind = process_order>, #schedule.edge<source = 10, target = 12, kind = process_order>, #schedule.edge<source = 11, target = 10, kind = process_order>], regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0], schedule = acyclic, feedback = []>, #schedule.group<fragments = [1], schedule = acyclic, feedback = []>, #schedule.group<fragments = [2], schedule = acyclic, feedback = []>, #schedule.group<fragments = [3], schedule = acyclic, feedback = []>, #schedule.group<fragments = [4], schedule = acyclic, feedback = []>, #schedule.group<fragments = [5], schedule = acyclic, feedback = []>, #schedule.group<fragments = [6], schedule = acyclic, feedback = []>, #schedule.group<fragments = [7], schedule = acyclic, feedback = []>, #schedule.group<fragments = [8], schedule = acyclic, feedback = []>, #schedule.group<fragments = [9], schedule = acyclic, feedback = []>, #schedule.group<fragments = [10, 11], schedule = control_loop, feedback = []>, #schedule.group<fragments = [12], schedule = acyclic, feedback = []>]>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>, schedule.nba.change_watched = array<i64>, schedule.nba.private_influence = array<i64>, schedule.nba.transient_observable = array<i64>, time_precision_fs = 1000000 : i64} {
    simulation.class.decl @__obelisk_class_s24_heavy id 1 extends @__obelisk_class_s3_node {debug_name = "heavy", is_abstract = false, is_final = false, is_interface = false} loc(#loc1)
    simulation.class.decl @__obelisk_class_s3_node id 2 {debug_name = "node", is_abstract = false, is_final = false, is_interface = false, simulation.constraint_mode_field = @__obelisk_class_s3_node_field___obelisk_constraint_mode, simulation.random_increment_field = @__obelisk_class_s3_node_field___obelisk_rng_increment, simulation.random_mode_field = @__obelisk_class_s3_node_field___obelisk_rand_mode, simulation.random_state_field = @__obelisk_class_s3_node_field___obelisk_rng_state} loc(#loc2)
    simulation.class.field @__obelisk_class_s3_node_field_0 of @__obelisk_class_s3_node at 0 : i32 {debug_name = "v", is_static = false, is_weak = false} loc(#loc3)
    simulation.class.field @__obelisk_class_s3_node_field_1 of @__obelisk_class_s3_node at 1 : !simulation.class_handle<@__obelisk_class_s3_node> {debug_name = "next", is_static = false, is_weak = false} loc(#loc4)
    simulation.class.field @__obelisk_class_s3_node_field___obelisk_rng_state of @__obelisk_class_s3_node at 2 : i64 {debug_name = "__obelisk_rng_state", is_static = false, is_weak = false} loc(#loc2)
    simulation.class.field @__obelisk_class_s3_node_field___obelisk_rng_increment of @__obelisk_class_s3_node at 3 : i64 {debug_name = "__obelisk_rng_increment", is_static = false, is_weak = false} loc(#loc2)
    simulation.class.field @__obelisk_class_s3_node_field___obelisk_rand_mode of @__obelisk_class_s3_node at 4 : i64 {debug_name = "__obelisk_rand_mode", is_static = false, is_weak = false} loc(#loc2)
    simulation.class.field @__obelisk_class_s3_node_field___obelisk_constraint_mode of @__obelisk_class_s3_node at 5 : i64 {debug_name = "__obelisk_constraint_mode", is_static = false, is_weak = false} loc(#loc2)
    simulation.scope.decl 0 hierarchy "\\$root " debug "$root" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64} loc(#loc)
    simulation.vpi_definition.decl @__obelisk_vpi_definition_0 type 32 name "tb3" definition_loc #loc5 loc(#loc5)
    simulation.scope.decl 1 parent 0 hierarchy "tb3" debug "tb3" source_definition "tb3" vpi_kind 32 definition @__obelisk_vpi_definition_0 {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64} loc(#loc5)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_0 id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "" {definition_loc = #loc2, is_compilation_unit, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 9 : i32, value = "$unit">, #simulation.vpi_property<selector = 600 : i32, value = true>, #simulation.vpi_property<selector = 602 : i32, value = true>]>} loc(#loc2)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_1 id 1 type 652 in 0 parent @__obelisk_vpi_anchor_0 ordinal 0 hierarchy "node" debug "node" {backing = #simulation.vpi_backing<kind = class, symbol = @__obelisk_class_s3_node>} loc(#loc2)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_2 id 2 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 0 hierarchy "node::new" debug "new" {backing = #simulation.vpi_backing<kind = code_unit, id = 2772960151288808329 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc6)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_3 id 3 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 1 hierarchy "node::weight" debug "weight" {backing = #simulation.vpi_backing<kind = code_unit, id = 4543492299532994929 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc7)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_4 id 4 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 2 hierarchy "node::randomize" debug "randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_5 id 5 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 3 hierarchy "node::pre_randomize" debug "pre_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_6 id 6 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 4 hierarchy "node::post_randomize" debug "post_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_7 id 7 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 5 hierarchy "node::get_randstate" debug "get_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_8 id 8 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 6 hierarchy "node::set_randstate" debug "set_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_9 id 9 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 7 hierarchy "node::srandom" debug "srandom" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_10 id 10 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 8 hierarchy "node::rand_mode" debug "rand_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_11 id 11 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 9 hierarchy "node::constraint_mode" debug "constraint_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_12 id 12 type 652 in 0 parent @__obelisk_vpi_anchor_0 ordinal 1 hierarchy "heavy" debug "heavy" {backing = #simulation.vpi_backing<kind = class, symbol = @__obelisk_class_s24_heavy>} loc(#loc1)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_13 id 13 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 0 hierarchy "heavy::new" debug "new" {backing = #simulation.vpi_backing<kind = code_unit, id = 2708832572412856858 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc8)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_14 id 14 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 1 hierarchy "heavy::weight" debug "weight" {backing = #simulation.vpi_backing<kind = code_unit, id = 1345606877515627212 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc9)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_15 id 15 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 2 hierarchy "heavy::randomize" debug "randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_16 id 16 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 3 hierarchy "heavy::pre_randomize" debug "pre_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_17 id 17 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 4 hierarchy "heavy::post_randomize" debug "post_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_18 id 18 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 5 hierarchy "heavy::get_randstate" debug "get_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_19 id 19 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 6 hierarchy "heavy::set_randstate" debug "set_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_20 id 20 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 7 hierarchy "heavy::srandom" debug "srandom" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_21 id 21 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 8 hierarchy "heavy::rand_mode" debug "rand_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_22 id 22 type 20 in 0 parent @__obelisk_vpi_anchor_12 ordinal 9 hierarchy "heavy::constraint_mode" debug "constraint_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_23 id 23 type 20 in 0 parent @__obelisk_vpi_anchor_0 ordinal 2 hierarchy "build" debug "build" {backing = #simulation.vpi_backing<kind = code_unit, id = 10440836131523331 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc10)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_24 id 24 type 20 in 0 parent @__obelisk_vpi_anchor_0 ordinal 3 hierarchy "churn" debug "churn" {backing = #simulation.vpi_backing<kind = code_unit, id = 1121781645360795653 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc11)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_25 id 25 type 20 in 0 parent @__obelisk_vpi_anchor_0 ordinal 4 hierarchy "rec" debug "rec" {backing = #simulation.vpi_backing<kind = code_unit, id = 714309932613419113 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc12)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_26 id 26 type 20 in 0 parent @__obelisk_vpi_anchor_0 ordinal 5 hierarchy "text" debug "text" {backing = #simulation.vpi_backing<kind = code_unit, id = 8792421680297623678 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>} loc(#loc13)
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_27 id 27 type 32 in 1 ordinal 1 hierarchy "tb3" debug "tb3" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 7 : i32, value = true>, #simulation.vpi_property<selector = 9 : i32, value = "tb3">, #simulation.vpi_property<selector = 600 : i32, value = true>]>} loc(#loc5)
    simulation.code_unit.decl 832639515527371617 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer" {internal} loc(#loc)
    simulation.code_unit.decl 2772960151288808329 in 0 function hierarchy "node::new" debug "new" loc(#loc6)
    simulation.code_unit.decl 4543492299532994929 in 0 function hierarchy "node::weight" debug "weight" loc(#loc7)
    simulation.code_unit.decl 2708832572412856858 in 0 function hierarchy "heavy::new" debug "new" loc(#loc8)
    simulation.code_unit.decl 1345606877515627212 in 0 function hierarchy "heavy::weight" debug "weight" loc(#loc9)
    simulation.code_unit.decl 10440836131523331 in 0 function hierarchy "build" debug "build" loc(#loc10)
    simulation.code_unit.decl 1121781645360795653 in 0 function hierarchy "churn" debug "churn" loc(#loc11)
    simulation.code_unit.decl 714309932613419113 in 0 function hierarchy "rec" debug "rec" loc(#loc12)
    simulation.code_unit.decl 8792421680297623678 in 0 function hierarchy "text" debug "text" loc(#loc13)
    simulation.code_unit.decl 2995347208804877048 in 1 initial hierarchy "tb3.$code_unit_281" debug "" loc(#loc14)
    simulation.func @__obelisk_root(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc(unknown)) attributes {code_unit_id = 832639515527371617 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 0 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>, home_region = 2 : i32} {
      %0 = simulation.spawn @unit_8(%arg0) : !simulation.context -> !simulation.process loc(#loc14)
      simulation.return loc(#loc)
    } loc(#loc)
    simulation.func private @unit_0(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":6:3), %arg1: !simulation.class_handle<@__obelisk_class_s3_node> {simulation.capture_kind = 1 : i32} loc("tb3.sv":6:3), %arg2: i32 {simulation.capture_kind = 1 : i32} loc("tb3.sv":6:3)) attributes {code_unit_id = 2772960151288808329 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "node::new"} {
      %0 = simulation.class.field_ref %arg1[@__obelisk_class_s3_node_field_0] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i32, @__obelisk_class_s3_node> loc(#loc15)
      simulation.managed.store %arg2 to %0 : i32, !simulation.managed_ref<i32, @__obelisk_class_s3_node> loc(#loc15)
      simulation.return loc(#loc6)
    } loc(#loc6)
    simulation.func private @unit_1(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":9:11), %arg1: !simulation.class_handle<@__obelisk_class_s3_node> {simulation.capture_kind = 1 : i32} loc("tb3.sv":9:11)) -> i32 attributes {code_unit_id = 4543492299532994929 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "node::weight", simulation.return_variable_path = "node::weight.weight"} {
      %0 = simulation.class.field_ref %arg1[@__obelisk_class_s3_node_field_0] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i32, @__obelisk_class_s3_node> loc(#loc16)
      %1 = simulation.managed.load %0 : !simulation.managed_ref<i32, @__obelisk_class_s3_node> -> i32 loc(#loc16)
      simulation.return %1 : i32 loc(#loc17)
    } loc(#loc7)
    simulation.func private @unit_2(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":15:3), %arg1: !simulation.class_handle<@__obelisk_class_s24_heavy> {simulation.capture_kind = 1 : i32} loc("tb3.sv":15:3), %arg2: i32 {simulation.capture_kind = 1 : i32} loc("tb3.sv":15:3)) attributes {code_unit_id = 2708832572412856858 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "heavy::new"} {
      %0 = simulation.class.cast %arg1 : !simulation.class_handle<@__obelisk_class_s24_heavy> to !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc18)
      simulation.class.direct_call @unit_0 %0(%arg2) : (!simulation.class_handle<@__obelisk_class_s3_node>, i32) -> () loc(#loc18)
      simulation.return loc(#loc8)
    } loc(#loc8)
    simulation.func private @unit_3(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":18:11), %arg1: !simulation.class_handle<@__obelisk_class_s24_heavy> {simulation.capture_kind = 1 : i32} loc("tb3.sv":18:11)) -> i32 attributes {code_unit_id = 1345606877515627212 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "heavy::weight", simulation.return_variable_path = "heavy::weight.weight"} {
      %c1_i32 = arith.constant 1 : i32 loc(#loc)
      %c1_i64 = arith.constant 1 : i64 loc(#loc)
      %c8_i32 = arith.constant 8 : i32 loc(#loc)
      %c0_i32 = arith.constant 0 : i32 loc(#loc)
      cf.br ^bb1(%c0_i32, %c0_i32 : i32, i32) loc(#loc9)
    ^bb1(%0: i32 loc("tb3.sv":18:11), %1: i32 loc("tb3.sv":20:14)):  // 2 preds: ^bb0, ^bb2
      %c8_i32_0 = arith.constant {simulation.rematerialized} 8 : i32 loc(#loc)
      %2 = arith.cmpi slt, %1, %c8_i32_0 : i32 loc(#loc20)
      cf.cond_br %2, ^bb2, ^bb3 {schedule.bounded_loop_header} loc(#loc21)
    ^bb2:  // pred: ^bb1
      %3 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc22)
      %4 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc22)
      %5 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc22)
      %c1_i64_1 = arith.constant {simulation.rematerialized} 1 : i64 loc(#loc)
      %6 = arith.ori %5, %c1_i64_1 : i64 loc(#loc22)
      %7 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc22)
      %8 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc22)
      simulation.managed.store %4 to %7 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc22)
      simulation.managed.store %6 to %8 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc22)
      simulation.class.direct_call @unit_0 %3(%1) : (!simulation.class_handle<@__obelisk_class_s3_node>, i32) -> () loc(#loc22)
      %9 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field_0] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i32, @__obelisk_class_s3_node> loc(#loc23)
      %10 = simulation.managed.load %9 : !simulation.managed_ref<i32, @__obelisk_class_s3_node> -> i32 loc(#loc23)
      %11 = arith.addi %0, %10 : i32 loc(#loc24)
      %c1_i32_2 = arith.constant {simulation.rematerialized} 1 : i32 loc(#loc)
      %12 = arith.addi %1, %c1_i32_2 : i32 loc(#loc25)
      cf.br ^bb1(%11, %12 : i32, i32) {schedule.bounded_loop_latch} loc(#loc9)
    ^bb3:  // pred: ^bb1
      %13 = simulation.class.field_ref %arg1[@__obelisk_class_s3_node_field_0] : !simulation.class_handle<@__obelisk_class_s24_heavy> -> !simulation.managed_ref<i32, @__obelisk_class_s24_heavy> loc(#loc26)
      %14 = simulation.managed.load %13 : !simulation.managed_ref<i32, @__obelisk_class_s24_heavy> -> i32 loc(#loc26)
      %15 = arith.addi %14, %0 : i32 loc(#loc26)
      simulation.return %15 : i32 loc(#loc27)
    } loc(#loc9)
    simulation.func private @unit_4(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":28:1)) -> !simulation.class_handle<@__obelisk_class_s3_node> attributes {code_unit_id = 10440836131523331 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "build", simulation.return_variable_path = "build.build"} {
      %c1_i32 = arith.constant 1 : i32 loc(#loc)
      %c1_i64 = arith.constant 1 : i64 loc(#loc)
      %c0_i32 = arith.constant 0 : i32 loc(#loc)
      %0 = simulation.class.null : !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc)
      %c100_i32 = arith.constant 100 : i32 loc(#loc)
      cf.br ^bb1(%0, %c0_i32 : !simulation.class_handle<@__obelisk_class_s3_node>, i32) loc(#loc10)
    ^bb1(%1: !simulation.class_handle<@__obelisk_class_s3_node> loc("tb3.sv":28:1), %2: i32 loc("tb3.sv":30:12)):  // 2 preds: ^bb0, ^bb2
      %c100_i32_0 = arith.constant {simulation.rematerialized} 100 : i32 loc(#loc)
      %3 = arith.cmpi slt, %2, %c100_i32_0 : i32 loc(#loc29)
      cf.cond_br %3, ^bb2, ^bb3 {schedule.bounded_loop_header} loc(#loc30)
    ^bb2:  // pred: ^bb1
      %4 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc31)
      %5 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc31)
      %6 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc31)
      %c1_i64_1 = arith.constant {simulation.rematerialized} 1 : i64 loc(#loc)
      %7 = arith.ori %6, %c1_i64_1 : i64 loc(#loc31)
      %8 = simulation.class.field_ref %4[@__obelisk_class_s3_node_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc31)
      %9 = simulation.class.field_ref %4[@__obelisk_class_s3_node_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc31)
      simulation.managed.store %5 to %8 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc31)
      simulation.managed.store %7 to %9 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc31)
      simulation.class.direct_call @unit_0 %4(%2) : (!simulation.class_handle<@__obelisk_class_s3_node>, i32) -> () loc(#loc31)
      %10 = simulation.class.field_ref %4[@__obelisk_class_s3_node_field_1] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<!simulation.class_handle<@__obelisk_class_s3_node>, @__obelisk_class_s3_node> loc(#loc32)
      simulation.managed.store %1 to %10 : !simulation.class_handle<@__obelisk_class_s3_node>, !simulation.managed_ref<!simulation.class_handle<@__obelisk_class_s3_node>, @__obelisk_class_s3_node> loc(#loc32)
      %c1_i32_2 = arith.constant {simulation.rematerialized} 1 : i32 loc(#loc)
      %11 = arith.addi %2, %c1_i32_2 : i32 loc(#loc33)
      cf.br ^bb1(%4, %11 : !simulation.class_handle<@__obelisk_class_s3_node>, i32) {schedule.bounded_loop_latch} loc(#loc10)
    ^bb3:  // pred: ^bb1
      simulation.return %1 : !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc34)
    } loc(#loc10)
    simulation.func private @unit_5(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":38:1)) -> i32 attributes {code_unit_id = 1121781645360795653 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "churn", simulation.return_variable_path = "churn.churn"} {
      %c1_i32 = arith.constant 1 : i32 loc(#loc)
      %c1_i64 = arith.constant 1 : i64 loc(#loc)
      %c0_i32 = arith.constant 0 : i32 loc(#loc)
      %c1000_i32 = arith.constant 1000 : i32 loc(#loc)
      cf.br ^bb1(%c0_i32, %c0_i32 : i32, i32) loc(#loc11)
    ^bb1(%0: i32 loc("tb3.sv":38:1), %1: i32 loc("tb3.sv":40:12)):  // 2 preds: ^bb0, ^bb2
      %c1000_i32_0 = arith.constant {simulation.rematerialized} 1000 : i32 loc(#loc)
      %2 = arith.cmpi slt, %1, %c1000_i32_0 : i32 loc(#loc36)
      cf.cond_br %2, ^bb2, ^bb3 {schedule.bounded_loop_header} loc(#loc37)
    ^bb2:  // pred: ^bb1
      %3 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc38)
      %4 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc38)
      %5 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc38)
      %c1_i64_1 = arith.constant {simulation.rematerialized} 1 : i64 loc(#loc)
      %6 = arith.ori %5, %c1_i64_1 : i64 loc(#loc38)
      %7 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc38)
      %8 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc38)
      simulation.managed.store %4 to %7 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc38)
      simulation.managed.store %6 to %8 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc38)
      simulation.class.direct_call @unit_0 %3(%1) : (!simulation.class_handle<@__obelisk_class_s3_node>, i32) -> () loc(#loc38)
      %9 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field_0] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i32, @__obelisk_class_s3_node> loc(#loc39)
      %10 = simulation.managed.load %9 : !simulation.managed_ref<i32, @__obelisk_class_s3_node> -> i32 loc(#loc39)
      %11 = arith.addi %0, %10 : i32 loc(#loc40)
      %c1_i32_2 = arith.constant {simulation.rematerialized} 1 : i32 loc(#loc)
      %12 = arith.addi %1, %c1_i32_2 : i32 loc(#loc41)
      cf.br ^bb1(%11, %12 : i32, i32) {schedule.bounded_loop_latch} loc(#loc11)
    ^bb3:  // pred: ^bb1
      simulation.return %0 : i32 loc(#loc42)
    } loc(#loc11)
    simulation.func private @unit_6(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":47:1), %arg1: i32 {simulation.capture_kind = 1 : i32} loc("tb3.sv":47:1)) -> i32 attributes {code_unit_id = 714309932613419113 : i64, domain = 0 : i32, effect_summary = [#schedule.effect<effect = read, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "rec", simulation.return_variable_path = "rec.rec"} {
      %c1_i32 = arith.constant 1 : i32 loc(#loc)
      %c0_i32 = arith.constant 0 : i32 loc(#loc)
      %c1_i64 = arith.constant 1 : i64 loc(#loc)
      %0 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc43)
      %1 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc43)
      %2 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc43)
      %3 = arith.ori %2, %c1_i64 : i64 loc(#loc43)
      %4 = simulation.class.field_ref %0[@__obelisk_class_s3_node_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc43)
      %5 = simulation.class.field_ref %0[@__obelisk_class_s3_node_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc43)
      simulation.managed.store %1 to %4 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc43)
      simulation.managed.store %3 to %5 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc43)
      simulation.class.direct_call @unit_0 %0(%arg1) : (!simulation.class_handle<@__obelisk_class_s3_node>, i32) -> () loc(#loc43)
      %6 = arith.cmpi eq, %arg1, %c0_i32 : i32 loc(#loc44)
      cf.cond_br %6, ^bb1, ^bb2 loc(#loc44)
    ^bb1:  // pred: ^bb0
      %7 = simulation.class.field_ref %0[@__obelisk_class_s3_node_field_0] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i32, @__obelisk_class_s3_node> loc(#loc45)
      %8 = simulation.managed.load %7 : !simulation.managed_ref<i32, @__obelisk_class_s3_node> -> i32 loc(#loc45)
      simulation.return %8 : i32 loc(#loc46)
    ^bb2:  // pred: ^bb0
      %c1_i32_0 = arith.constant {simulation.rematerialized} 1 : i32 loc(#loc)
      %9 = arith.subi %arg1, %c1_i32_0 : i32 loc(#loc47)
      %10 = simulation.call @unit_6(%arg0, %9) : (!simulation.context, i32) -> i32 loc(#loc48)
      %11 = simulation.termination.requested %arg0 loc(#loc48)
      cf.cond_br %11, ^bb3, ^bb4 loc(#loc48)
    ^bb3:  // pred: ^bb2
      %c0_i32_1 = arith.constant {simulation.rematerialized} 0 : i32 loc(#loc)
      simulation.return %c0_i32_1 : i32 loc(#loc48)
    ^bb4:  // pred: ^bb2
      %12 = simulation.class.field_ref %0[@__obelisk_class_s3_node_field_0] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i32, @__obelisk_class_s3_node> loc(#loc49)
      %13 = simulation.managed.load %12 : !simulation.managed_ref<i32, @__obelisk_class_s3_node> -> i32 loc(#loc49)
      %14 = arith.addi %10, %13 : i32 loc(#loc48)
      simulation.return %14 : i32 loc(#loc50)
    } loc(#loc12)
    simulation.func private @unit_7(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":54:1)) -> !simulation.string attributes {code_unit_id = 8792421680297623678 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "text", simulation.return_variable_path = "text.text"} {
      %c10_i32 = arith.constant 10 : i32 loc(#loc)
      %c1_i32 = arith.constant 1 : i32 loc(#loc)
      %0 = simulation.bytes.constant "%0d" loc(#loc)
      %c0_i32 = arith.constant 0 : i32 loc(#loc)
      %c40_i32 = arith.constant 40 : i32 loc(#loc)
      %1 = simulation.string.literal "" loc(#loc51)
      cf.br ^bb1(%1, %c0_i32 : !simulation.string, i32) loc(#loc13)
    ^bb1(%2: !simulation.string loc("tb3.sv":54:1), %3: i32 loc("tb3.sv":56:12)):  // 2 preds: ^bb0, ^bb2
      %c40_i32_0 = arith.constant {simulation.rematerialized} 40 : i32 loc(#loc)
      %4 = arith.cmpi slt, %3, %c40_i32_0 : i32 loc(#loc53)
      cf.cond_br %4, ^bb2, ^bb3 {schedule.bounded_loop_header} loc(#loc54)
    ^bb2:  // pred: ^bb1
      %c10_i32_1 = arith.constant {simulation.rematerialized} 10 : i32 loc(#loc)
      %5 = arith.remsi %3, %c10_i32_1 : i32 loc(#loc55)
      %6 = simulation.logic.from_bits %5 : i32 -> !simulation.logic<32> loc(#loc55)
      %7 = simulation.bytes.constant "%0d" {simulation.rematerialized} loc(#loc)
      %8 = simulation.string.output_format %arg0(%7, %6) radix = <decimal> flags = [32, 1] {library_cell = "work.$unit", scope = "text", time_multiplier = 1 : i64, time_precision = -9 : i32} : !simulation.bytes, !simulation.logic<32> loc(#loc56)
      %9 = simulation.string.concat %2, %8 : (!simulation.string, !simulation.string) -> !simulation.string loc(#loc57)
      %c1_i32_2 = arith.constant {simulation.rematerialized} 1 : i32 loc(#loc)
      %10 = arith.addi %3, %c1_i32_2 : i32 loc(#loc58)
      cf.br ^bb1(%9, %10 : !simulation.string, i32) {schedule.bounded_loop_latch} loc(#loc13)
    ^bb3:  // pred: ^bb1
      simulation.return %2 : !simulation.string loc(#loc59)
    } loc(#loc13)
    simulation.func private @unit_8(%arg0: !simulation.context {simulation.capture_kind = 0 : i32} loc("tb3.sv":62:3)) attributes {code_unit_id = 2995347208804877048 : i64, domain = 0 : i32, effect_summary = [#schedule.effect<effect = read, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>], entry_kind = 1 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]>, home_region = 2 : i32, simulation.hierarchical_name = "tb3"} {
      %0 = simulation.bytes.constant "keep=%0d len=%0d s=%0d r=%0d w=%0d label=%s t=%0d" loc(#loc)
      %c1_i32 = arith.constant 1 : i32 loc(#loc)
      %c5_i32 = arith.constant 5 : i32 loc(#loc)
      %c7_i32 = arith.constant 7 : i32 loc(#loc)
      %c50_i32 = arith.constant 50 : i32 loc(#loc)
      %1 = simulation.bytes.constant "label%0d" loc(#loc)
      %c42_i32 = arith.constant 42 : i32 loc(#loc)
      %c1_i64 = arith.constant 1 : i64 loc(#loc)
      %2 = simulation.class.null : !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc)
      %c0_i32 = arith.constant 0 : i32 loc(#loc)
      %3 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc60)
      %4 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc60)
      %5 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc60)
      %6 = arith.ori %5, %c1_i64 : i64 loc(#loc60)
      %7 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc60)
      %8 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc60)
      simulation.managed.store %4 to %7 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc60)
      simulation.managed.store %6 to %8 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc60)
      simulation.class.direct_call @unit_0 %3(%c42_i32) : (!simulation.class_handle<@__obelisk_class_s3_node>, i32) -> () loc(#loc60)
      %9 = simulation.call @unit_4(%arg0) : (!simulation.context) -> !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc61)
      %10 = simulation.termination.requested %arg0 loc(#loc61)
      cf.cond_br %10, ^bb1, ^bb2 loc(#loc61)
    ^bb1:  // pred: ^bb0
      simulation.return loc(#loc61)
    ^bb2:  // pred: ^bb0
      %11 = simulation.class.field_ref %3[@__obelisk_class_s3_node_field_0] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i32, @__obelisk_class_s3_node> loc(#loc62)
      %12 = simulation.managed.load %11 : !simulation.managed_ref<i32, @__obelisk_class_s3_node> -> i32 loc(#loc62)
      %13 = simulation.bytes.constant "label%0d" {simulation.rematerialized} loc(#loc)
      %14 = simulation.string.output_format %arg0(%13, %12) radix = <decimal> flags = [32, 1] {library_cell = "work.tb3", scope = "tb3", time_multiplier = 1 : i64, time_precision = -9 : i32} : !simulation.bytes, i32 loc(#loc63)
      %15 = simulation.call @unit_5(%arg0) : (!simulation.context) -> i32 loc(#loc64)
      %16 = simulation.termination.requested %arg0 loc(#loc64)
      cf.cond_br %16, ^bb3, ^bb4 loc(#loc64)
    ^bb3:  // pred: ^bb2
      simulation.return loc(#loc64)
    ^bb4:  // pred: ^bb2
      %c50_i32_0 = arith.constant {simulation.rematerialized} 50 : i32 loc(#loc)
      %17 = simulation.call @unit_6(%arg0, %c50_i32_0) : (!simulation.context, i32) -> i32 loc(#loc65)
      %18 = simulation.termination.requested %arg0 loc(#loc65)
      cf.cond_br %18, ^bb5, ^bb6 loc(#loc65)
    ^bb5:  // pred: ^bb4
      simulation.return loc(#loc65)
    ^bb6:  // pred: ^bb4
      %19 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc66)
      %20 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc66)
      %21 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc66)
      %c1_i64_1 = arith.constant {simulation.rematerialized} 1 : i64 loc(#loc)
      %22 = arith.ori %21, %c1_i64_1 : i64 loc(#loc66)
      %23 = simulation.class.field_ref %19[@__obelisk_class_s3_node_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc66)
      %24 = simulation.class.field_ref %19[@__obelisk_class_s3_node_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc66)
      simulation.managed.store %20 to %23 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc66)
      simulation.managed.store %22 to %24 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_node> loc(#loc66)
      %c7_i32_2 = arith.constant {simulation.rematerialized} 7 : i32 loc(#loc)
      simulation.class.direct_call @unit_0 %19(%c7_i32_2) : (!simulation.class_handle<@__obelisk_class_s3_node>, i32) -> () loc(#loc66)
      %25 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s24_heavy> loc(#loc67)
      %26 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc67)
      %27 = simulation.random.next %arg0 : (!simulation.context) -> i64 loc(#loc67)
      %c1_i64_3 = arith.constant {simulation.rematerialized} 1 : i64 loc(#loc)
      %28 = arith.ori %27, %c1_i64_3 : i64 loc(#loc67)
      %29 = simulation.class.field_ref %25[@__obelisk_class_s3_node_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s24_heavy> -> !simulation.managed_ref<i64, @__obelisk_class_s24_heavy> loc(#loc67)
      %30 = simulation.class.field_ref %25[@__obelisk_class_s3_node_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s24_heavy> -> !simulation.managed_ref<i64, @__obelisk_class_s24_heavy> loc(#loc67)
      simulation.managed.store %26 to %29 : i64, !simulation.managed_ref<i64, @__obelisk_class_s24_heavy> loc(#loc67)
      simulation.managed.store %28 to %30 : i64, !simulation.managed_ref<i64, @__obelisk_class_s24_heavy> loc(#loc67)
      %c5_i32_4 = arith.constant {simulation.rematerialized} 5 : i32 loc(#loc)
      simulation.class.direct_call @unit_2 %25(%c5_i32_4) : (!simulation.class_handle<@__obelisk_class_s24_heavy>, i32) -> () loc(#loc67)
      %31 = simulation.call @unit_7(%arg0) : (!simulation.context) -> !simulation.string loc(#loc68)
      %32 = simulation.termination.requested %arg0 loc(#loc68)
      cf.cond_br %32, ^bb7, ^bb8 loc(#loc68)
    ^bb7:  // pred: ^bb6
      simulation.return loc(#loc68)
    ^bb8:  // pred: ^bb6
      %33 = simulation.class.cast %25 : !simulation.class_handle<@__obelisk_class_s24_heavy> to !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc69)
      %34 = simulation.class.virtual_call %33[@__obelisk_class_s3_node_method_1] slot 0 signature_id 3203784351818086605() : (!simulation.class_handle<@__obelisk_class_s3_node>) -> i32 loc(#loc70)
      %c0_i32_5 = arith.constant {simulation.rematerialized} 0 : i32 loc(#loc)
      cf.br ^bb9(%c0_i32_5, %9 : i32, !simulation.class_handle<@__obelisk_class_s3_node>) loc(#loc14)
    ^bb9(%35: i32 loc("tb3.sv":72:19), %36: !simulation.class_handle<@__obelisk_class_s3_node> loc("tb3.sv":75:15)):  // 2 preds: ^bb8, ^bb10
      %37 = simulation.class.id %36 : !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc73)
      %38 = simulation.class.null {simulation.rematerialized} : !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc)
      %39 = simulation.class.id %38 : !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc73)
      %40 = arith.cmpi ne, %37, %39 : i64 loc(#loc73)
      cf.cond_br %40, ^bb10, ^bb11 loc(#loc74)
    ^bb10:  // pred: ^bb9
      %c1_i32_6 = arith.constant {simulation.rematerialized} 1 : i32 loc(#loc)
      %41 = arith.addi %35, %c1_i32_6 : i32 loc(#loc75)
      %42 = simulation.class.field_ref %36[@__obelisk_class_s3_node_field_1] : !simulation.class_handle<@__obelisk_class_s3_node> -> !simulation.managed_ref<!simulation.class_handle<@__obelisk_class_s3_node>, @__obelisk_class_s3_node> loc(#loc76)
      %43 = simulation.managed.load %42 : !simulation.managed_ref<!simulation.class_handle<@__obelisk_class_s3_node>, @__obelisk_class_s3_node> -> !simulation.class_handle<@__obelisk_class_s3_node> loc(#loc76)
      cf.br ^bb9(%41, %43 : i32, !simulation.class_handle<@__obelisk_class_s3_node>) loc(#loc14)
    ^bb11:  // pred: ^bb9
      %44 = simulation.managed.load %11 : !simulation.managed_ref<i32, @__obelisk_class_s3_node> -> i32 loc(#loc77)
      %45 = simulation.string.length %31 : (!simulation.string) -> i64 loc(#loc78)
      %46 = arith.trunci %45 : i64 to i32 loc(#loc78)
      %47 = simulation.bytes.constant "keep=%0d len=%0d s=%0d r=%0d w=%0d label=%s t=%0d" {simulation.rematerialized} loc(#loc)
      %c1_i32_7 = arith.constant {simulation.rematerialized} 1 : i32 loc(#loc)
      simulation.display %arg0 to %c1_i32_7(%47, %44, %35, %15, %17, %34, %14, %46) newline = true radix = <decimal> flags = [0, 1, 1, 1, 1, 1, 136, 1] {library_cell = "work.tb3", scope = "tb3", time_multiplier = 1 : i64, time_precision = -9 : i32} : !simulation.bytes, i32, i32, i32, i32, i32, !simulation.string, i32 loc(#loc79)
      simulation.return loc(#loc14)
    } loc(#loc14)
    simulation.class.method @__obelisk_class_s24_heavy_method_1 of @__obelisk_class_s24_heavy slot 0 signature_id 3203784351818086605 implemented_by @unit_3 : (!simulation.context, !simulation.class_handle<@__obelisk_class_s24_heavy>) -> i32 {debug_name = "weight", is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true} loc(#loc9)
    simulation.class.method @__obelisk_class_s3_node_method_1 of @__obelisk_class_s3_node slot 0 signature_id 3203784351818086605 implemented_by @unit_1 : (!simulation.context, !simulation.class_handle<@__obelisk_class_s3_node>) -> i32 {debug_name = "weight", is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true} loc(#loc7)
  } loc(#loc)
} loc(#loc)
#loc1 = loc("tb3.sv":14:1)
#loc3 = loc("tb3.sv":4:7)
#loc4 = loc("tb3.sv":5:8)
#loc15 = loc("tb3.sv":7:5)
#loc16 = loc("tb3.sv":10:12)
#loc17 = loc("tb3.sv":10:5)
#loc18 = loc("tb3.sv":16:5)
#loc20 = loc("tb3.sv":20:21)
#loc21 = loc("tb3.sv":20:5)
#loc22 = loc("tb3.sv":21:26)
#loc23 = loc("tb3.sv":22:12)
#loc24 = loc("tb3.sv":22:7)
#loc25 = loc("tb3.sv":20:28)
#loc26 = loc("tb3.sv":24:12)
#loc27 = loc("tb3.sv":24:5)
#loc29 = loc("tb3.sv":30:19)
#loc30 = loc("tb3.sv":30:3)
#loc31 = loc("tb3.sv":31:24)
#loc32 = loc("tb3.sv":32:5)
#loc33 = loc("tb3.sv":30:26)
#loc34 = loc("tb3.sv":35:3)
#loc36 = loc("tb3.sv":40:19)
#loc37 = loc("tb3.sv":40:3)
#loc38 = loc("tb3.sv":41:24)
#loc39 = loc("tb3.sv":42:10)
#loc40 = loc("tb3.sv":42:5)
#loc41 = loc("tb3.sv":40:26)
#loc42 = loc("tb3.sv":44:3)
#loc43 = loc("tb3.sv":48:12)
#loc44 = loc("tb3.sv":49:7)
#loc45 = loc("tb3.sv":50:12)
#loc46 = loc("tb3.sv":50:5)
#loc47 = loc("tb3.sv":51:14)
#loc48 = loc("tb3.sv":51:10)
#loc49 = loc("tb3.sv":51:23)
#loc50 = loc("tb3.sv":51:3)
#loc51 = loc("tb3.sv":55:16)
#loc53 = loc("tb3.sv":56:19)
#loc54 = loc("tb3.sv":56:3)
#loc55 = loc("tb3.sv":57:34)
#loc56 = loc("tb3.sv":57:17)
#loc57 = loc("tb3.sv":57:11)
#loc58 = loc("tb3.sv":56:26)
#loc59 = loc("tb3.sv":58:3)
#loc60 = loc("tb3.sv":63:27)
#loc61 = loc("tb3.sv":64:27)
#loc62 = loc("tb3.sv":65:52)
#loc63 = loc("tb3.sv":65:30)
#loc64 = loc("tb3.sv":66:23)
#loc65 = loc("tb3.sv":67:23)
#loc66 = loc("tb3.sv":68:24)
#loc67 = loc("tb3.sv":69:26)
#loc68 = loc("tb3.sv":71:26)
#loc69 = loc("tb3.sv":73:9)
#loc70 = loc("tb3.sv":74:9)
#loc73 = loc("tb3.sv":75:28)
#loc74 = loc("tb3.sv":75:5)
#loc75 = loc("tb3.sv":76:7)
#loc76 = loc("tb3.sv":75:49)
#loc77 = loc("tb3.sv":77:67)
#loc78 = loc("tb3.sv":78:30)
#loc79 = loc("tb3.sv":77:5)
