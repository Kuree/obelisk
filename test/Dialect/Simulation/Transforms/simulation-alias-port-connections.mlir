// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-alias-port-connections{vpi=off elements=true},simulation.func(cse)))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-alias-port-connections{vpi=full}))' | FileCheck %s --check-prefix=VPI
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-alias-port-connections{vpi=read}))' | FileCheck %s --check-prefix=VPI
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-alias-port-connections{vpi=off}))' | FileCheck %s --check-prefix=DEFAULT

// IEEE 1800-2023 23.3.3/4.9.6: a port connection is an implied continuous
// assignment. 4.9.1/4.7 allow its update immediately after the source write,
// so a single-driver, invisible sink can share the source storage.
module {
  // CHECK-LABEL: simulation.design @plain
  // CHECK: simulation.func @root
  // CHECK-NOT: simulation.spawn @copy
  // CHECK: %[[SRC:.*]] = simulation.context.storage %{{.*}}[0]
  // CHECK: simulation.spawn @reader(%{{.*}}, %[[SRC]])
  // CHECK: simulation.func private @reader({{.*}}simulation.descriptor_id = 0 : i64
  // VPI-LABEL: simulation.design @plain
  // VPI: simulation.spawn @copy
  simulation.design @plain {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reader"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %s) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }

  // An input from an element of a parent array: the child's capture becomes
  // the whole parent array and the child body selects its element. This is
  // only enabled by `elements`.
  // DEFAULT-LABEL: simulation.design @source_element
  // DEFAULT: simulation.spawn @copy
  // CHECK-LABEL: simulation.design @source_element
  // CHECK-NOT: simulation.spawn @copy
  // CHECK: simulation.func private @reader(%{{.*}}, %[[ARG:.*]]: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
  // CHECK: %[[ELEM:.*]] = simulation.ref.subelement %[[ARG]]{{\[\[}}1]]
  // CHECK: simulation.suspend.edge posedge %[[ELEM]]
  simulation.design @source_element {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %e = simulation.ref.subelement %src[[1]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      %v = simulation.ref.load %e : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }

  // An output into an element of a parent array: the child's blocking writer
  // now stores the parent element directly.
  // DEFAULT-LABEL: simulation.design @destination_element
  // DEFAULT: simulation.spawn @copy
  // CHECK-LABEL: simulation.design @destination_element
  // CHECK-NOT: simulation.spawn @copy
  // CHECK: simulation.func private @child(%{{.*}}, %[[ARG:.*]]: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
  // CHECK: %[[ELEM:.*]] = simulation.ref.subelement %[[ARG]]{{\[\[}}0]]
  // CHECK: simulation.ref.store %{{.*}} to %[[ELEM]]
  simulation.design @destination_element {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "child"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
      %i = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.process
      %1 = simulation.spawn @child(%ctx, %i, %s) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %e = simulation.ref.subelement %dst[[0]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %out : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }

  // A second writer of the sink (here a procedural store) keeps the copy.
  // CHECK-LABEL: simulation.design @extra_writer
  // CHECK: simulation.spawn @copy
  simulation.design @extra_writer {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 4 in 0 initial hierarchy "writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @writer(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }

  // IEEE 1800-2023 10.6.2: a force on the sink must not reach the source.
  // CHECK-LABEL: simulation.design @forced
  // CHECK: simulation.spawn @copy
  simulation.design @forced {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 4 in 0 initial hierarchy "forcer"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @forcer(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @forcer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %c = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.override %out = %c assign false : !simulation.ref<!simulation.logic<4>>, !simulation.logic<4>
      simulation.return
    }
  }

  // A nonblocking child output into a parent array element keeps its copy:
  // generated NBA commit needs one semantic root per site.
  // CHECK-LABEL: simulation.design @nonblocking_output
  // CHECK: simulation.spawn @copy
  simulation.design @nonblocking_output {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "child"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
      %i = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.process
      %1 = simulation.spawn @child(%ctx, %i, %s) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %e = simulation.ref.subelement %dst[[0]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %v to %e : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.nba.enqueue %v to %out : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }

  // An initializer read of the sink precedes every process (6.8).
  // CHECK-LABEL: simulation.design @root_read
  // CHECK: simulation.spawn @copy
  simulation.design @root_read {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %o = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %v = simulation.ref.load %d : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %o : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
  }
  // A delay-driven root (a testbench clock) is published by the runtime,
  // which walks every subscriber. The first copy below it and the copy below
  // that first copy (periodic clock extraction promotes its sink) are kept;
  // deeper copies share the second copy's storage.
  // CHECK-LABEL: simulation.design @calendar_clock
  // CHECK-DAG: simulation.spawn @copy1
  // CHECK-DAG: simulation.spawn @copy2
  // CHECK-NOT: simulation.spawn @copy3
  // CHECK: simulation.func private @reader({{.*}}simulation.descriptor_id = 2 : i64
  simulation.design @calendar_clock {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "copy1"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "copy2"
    simulation.code_unit.decl 4 in 0 port_input hierarchy "copy3"
    simulation.code_unit.decl 5 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 6 in 0 always hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %c = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %b = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %d = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @clock(%ctx, %c) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %1 = simulation.spawn @copy1(%ctx, %c, %a) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %2 = simulation.spawn @copy2(%ctx, %a, %b) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %3 = simulation.spawn @copy3(%ctx, %b, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %4 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %t = simulation.time.constant 1
      cf.br ^body
    ^body:
      %v = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %n = simulation.logic.unary bit_not %v : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %n to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.delay %t to ^body
    }
    simulation.func private @copy1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @copy2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %dst: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @copy3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %dst: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^body
    ^body:
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }

  // A consumer that reads storage it also writes settles against itself;
  // generated eval owns it only through its private input copy.
  // CHECK-LABEL: simulation.design @self_settling
  // CHECK: simulation.spawn @copy
  simulation.design @self_settling {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "settle"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %o = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @settle(%ctx, %d, %o) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @settle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %b = simulation.ref.load %out : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %c = simulation.logic.binary xor %a, %b : !simulation.logic<4>
      simulation.ref.store %c to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
  }

  // Chains resolve to the surviving root, and port reflection follows it.
  // CHECK-LABEL: simulation.design @chain
  // CHECK: simulation.port.decl 0 in 0 source 0 net = false at 0
  // CHECK-NOT: simulation.spawn @copy
  // CHECK: simulation.func private @reader({{.*}}simulation.descriptor_id = 0 : i64
  simulation.design @chain {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy1"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "copy2"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.port.decl 0 in 0 source 2 net = false at 0 : !simulation.logic<4> input ordinal 0 hierarchy "top.leaf.in"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %b = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %c = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy1(%ctx, %a, %b) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @copy2(%ctx, %b, %c) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @reader(%ctx, %c) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @copy2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
  }

  // A waveform dump observes the sink storage itself.
  // CHECK-LABEL: simulation.design @dumped
  // CHECK: simulation.spawn @copy
  simulation.design @dumped {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 4 in 0 initial hierarchy "flusher"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @flusher(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @flusher(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      simulation.dump.flush %ctx : (!simulation.context) -> ()
      simulation.return
    }
  }

  // Reflection only supports packed offsets; keep unpacked port views.
  // CHECK-LABEL: simulation.design @reflected_element
  // CHECK: simulation.spawn @copy
  simulation.design @reflected_element {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 3 in 0 always hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design {observability = 0 : i32}
    simulation.port.decl 0 in 0 source 1 net = false at 0 : !simulation.logic<1> input ordinal 0 hierarchy "child.in"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
      %d = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %0 = simulation.spawn @copy(%ctx, %s, %d) : !simulation.context, !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %1 = simulation.spawn @reader(%ctx, %d) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %e = simulation.ref.subelement %src[[1]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>> -> !simulation.ref<!simulation.logic<1>>
      %v = simulation.ref.load %e : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %v to %dst : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.unpacked_array<0 : 1 x !simulation.logic<1>>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }

  // A copy cycle has no surviving storage root.
  // CHECK-LABEL: simulation.design @cycle
  // CHECK: simulation.spawn @copy1
  // CHECK: simulation.spawn @copy2
  simulation.design @cycle {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy1"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "copy2"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.storage.decl 2 in 0 : !simulation.logic<4> design {observability = 0 : i32}
    simulation.port.decl 0 in 0 source 2 net = false at 0 : !simulation.logic<4> input ordinal 0 hierarchy "top.leaf.in"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<4>>
      %b = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<4>>
      %c = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %0 = simulation.spawn @copy1(%ctx, %a, %b) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %1 = simulation.spawn @copy2(%ctx, %b, %a) : !simulation.context, !simulation.ref<!simulation.logic<4>>, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      %2 = simulation.spawn @reader(%ctx, %c) : !simulation.context, !simulation.ref<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @copy2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %dst: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %src : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %v to %dst : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %src to ^body : !simulation.ref<!simulation.logic<4>>
    }
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<4>>
    }
  }
}
