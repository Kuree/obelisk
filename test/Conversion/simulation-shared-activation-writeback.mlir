// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-materialize-compute-fusion,schedule-localize-private-eval-storage))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-materialize-compute-fusion,schedule-localize-private-eval-storage,schedule-promote-eval-storage))' | FileCheck %s --check-prefix=SSA
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-materialize-compute-fusion,schedule-localize-private-eval-storage))' | FileCheck %s --check-prefix=READ
// READ-NOT: simulation.ref.alloc
// LRM 4.6/10.4.1: local reads observe preceding blocking assignments.
// LRM 4.7 permits this synchronous activation; it does not mandate atomicity.
// LRM 9.4.2: every watched transition must remain visible. Unwatched shared
// roots flush on return; partial/conditional writes preserve untouched state.
// Callees, escaped references, coverage and external observation deny merging.
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @repeated_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "repeated_scalar"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK: %[[CANON:.*]] = simulation.context.storage %{{.*}}[1]
// CHECK-NOT: simulation.ref.load %[[CANON]]
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
// CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
// CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
// CHECK: %[[FINAL:.*]] = simulation.ref.load %[[LOCAL]]
// CHECK-NEXT: simulation.ref.store %[[FINAL]] to %[[CANON]]
// CHECK-NEXT: simulation.return
// SSA-LABEL: simulation.func private @process.__obelisk_eval_body_
// SSA-NOT: simulation.ref.alloc
// SSA: simulation.ref.store
// SSA-NOT: simulation.ref.store
// SSA: simulation.return
  simulation.design @live_in {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "live_in"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %b = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK: %[[CANON:.*]] = simulation.context.storage %{{.*}}[1]
// CHECK: %[[IN:.*]] = simulation.ref.load %[[CANON]]
// CHECK-NEXT: %[[LOCAL:.*]] = simulation.ref.alloc %[[IN]]
// CHECK: simulation.ref.store %{{.*}} to %[[CANON]]
// CHECK: simulation.return
  simulation.design @partial_array {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "partial_array"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<3 : 2 x i8> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i8>>
      %observed = simulation.ref.load %out : !simulation.ref<!simulation.unpacked_array<3 : 2 x i8>> -> !simulation.unpacked_array<3 : 2 x i8>
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i8>>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %v = arith.addi %a, %a : i8
      %first = simulation.ref.subelement %out[[0]] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i8>> -> !simulation.ref<i8>
      simulation.ref.store %v to %first : i8, !simulation.ref<i8>
      %copy = simulation.ref.load %first : !simulation.ref<i8> -> i8
      %sum = arith.addi %copy, %v : i8
      simulation.ref.store %sum to %first : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK: %[[CANON:.*]] = simulation.context.storage %{{.*}}[1]
// CHECK: %[[IN:.*]] = simulation.ref.load %[[CANON]]
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc %[[IN]]
// CHECK: simulation.ref.subelement %[[LOCAL]]{{\[\[0\]\]}}
// CHECK: simulation.ref.store %{{.*}} to %[[CANON]]
// CHECK: simulation.return
  simulation.design @large_partial {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "large_partial"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<3 : 2 x i512> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>>
      %observed = simulation.ref.load %out : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>> -> !simulation.unpacked_array<3 : 2 x i512>
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %v = arith.extui %a : i8 to i512
      %first = simulation.ref.subelement %out[[0]] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>> -> !simulation.ref<i512>
      simulation.ref.store %v to %first : i512, !simulation.ref<i512>
      %copy = simulation.ref.load %first : !simulation.ref<i512> -> i512
      %sum = arith.addi %copy, %v : i512
      simulation.ref.store %sum to %first : i512, !simulation.ref<i512>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: %[[FIRST:.*]] = simulation.ref.subelement
// CHECK: simulation.ref.store %[[VALUE:.*]] to %[[FIRST]]
// CHECK-NOT: simulation.ref.load
// CHECK: arith.addi %[[VALUE]], %[[VALUE]]
// CHECK: simulation.ref.store %{{.*}} to %[[FIRST]]
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.return
  simulation.design @large_whole {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "large_whole"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<3 : 2 x i512> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>>
      %observed = simulation.ref.load %out : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>> -> !simulation.unpacked_array<3 : 2 x i512>
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %v = arith.extui %a : i8 to i512
      %first = simulation.ref.subelement %out[[0]] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>> -> !simulation.ref<i512>
      simulation.ref.store %v to %first : i512, !simulation.ref<i512>
      %copy = simulation.ref.load %first : !simulation.ref<i512> -> i512
      %sum = arith.addi %copy, %v : i512
      simulation.ref.store %sum to %first : i512, !simulation.ref<i512>
      %second = simulation.ref.subelement %out[[1]] : !simulation.ref<!simulation.unpacked_array<3 : 2 x i512>> -> !simulation.ref<i512>
      simulation.ref.store %v to %second : i512, !simulation.ref<i512>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK: %[[CANON:.*]] = simulation.context.storage %{{.*}}[1]
// CHECK-NOT: simulation.ref.load %[[CANON]]
// CHECK: %[[ZERO:.*]] = simulation.aggregate.splat
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc %[[ZERO]]
// CHECK: %[[FINAL:.*]] = simulation.ref.load %[[LOCAL]]
// CHECK-NEXT: simulation.ref.store %[[FINAL]] to %[[CANON]]
// CHECK-NEXT: simulation.return
  simulation.design @watched_change {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "watched_change"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process
      %watcher = simulation.spawn @watcher(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    simulation.code_unit.decl 2 in 0 always hierarchy "watcher"
    simulation.func @watcher(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %ref = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %ref to ^wait : !simulation.ref<i8>
    }
  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.return
  simulation.design @watched_edge {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "watched_edge"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process
      %watcher = simulation.spawn @watcher(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    simulation.code_unit.decl 2 in 0 always hierarchy "watcher"
    simulation.func @watcher(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %ref = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %ref to ^wait : !simulation.ref<i8>
    }
  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.return
  simulation.design @callee_reads {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "callee_reads"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.call @peek(%ctx) : (!simulation.context) -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "peek"
    simulation.func private @peek(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %ref = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %v = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.return
  simulation.design @transitive_callee_reads {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "transitive_callee_reads"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.call @wrapper(%ctx) : (!simulation.context) -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "peek"
    simulation.func private @peek(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %ref = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %v = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 3 in 0 function hierarchy "wrapper"
    simulation.func private @wrapper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %v = simulation.call @peek(%ctx) : (!simulation.context) -> i8
      simulation.return %v : i8
    }
  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.return
  simulation.design @reference_escape {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "reference_escape"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.call @peek(%ctx, %out) : (!simulation.context, !simulation.ref<i8>) -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "peek"
    simulation.func private @peek(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %v = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.return
  simulation.design @toggle_observed {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "toggle_observed"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design {obelisk.coverage.toggle_observable}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.return
  simulation.design @conditional_write {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "conditional_write"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %zero = arith.constant 0 : i8
      %test = arith.cmpi ne, %a, %zero : i8
      cf.cond_br %test, ^write, ^finish
      ^write:
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      cf.br ^finish
      ^finish:
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK: %[[CANON:.*]] = simulation.context.storage %{{.*}}[1]
// CHECK: %[[IN:.*]] = simulation.ref.load %[[CANON]]
// CHECK-NEXT: %[[LOCAL:.*]] = simulation.ref.alloc %[[IN]]
// CHECK: cf.cond_br
// CHECK: simulation.ref.store %{{.*}} to %[[CANON]]
// CHECK: simulation.return
  simulation.design @four_state {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "four_state"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %observed = simulation.ref.load %out : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %x = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      %z = simulation.logic.constant -1 : i8, -1 : i8 : !simulation.logic<8>
      simulation.ref.store %x to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %z to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK: %[[CANON:.*]] = simulation.context.storage %{{.*}}[1]
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
// CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
// CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
// CHECK: simulation.ref.store %{{.*}} to %[[CANON]]
// CHECK-NEXT: simulation.return
// SSA-LABEL: simulation.design @four_state
// SSA: simulation.func private @process.__obelisk_eval_body_
// SSA: %[[Z:.*]] = simulation.logic.constant -1 : i8, -1 : i8
// SSA-NOT: simulation.ref.alloc
// SSA: simulation.ref.store %[[Z]]
// SSA-NOT: simulation.ref.store
// SSA: simulation.return
  simulation.design @forced {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "forced"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %forced = arith.constant 7 : i8
      simulation.override %out = %forced assign false : !simulation.ref<i8>, i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %b = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.design @forced
// CHECK-NOT: simulation.ref.alloc
  simulation.design @duplicate_handles {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "duplicate_handles"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %out : i8, !simulation.ref<i8>
      %other = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %b = simulation.ref.load %other : !simulation.ref<i8> -> i8
      %c = arith.addi %a, %b : i8
      simulation.ref.store %c to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

  }
// CHECK-LABEL: simulation.func private @process.__obelisk_eval_body_
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
// CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
// CHECK: simulation.ref.load %[[LOCAL]]
// CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
// CHECK: simulation.return
}
