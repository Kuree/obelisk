// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=OFF
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=VISIBLE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=VISIBLE
// RUN: sed 's@// DUMP@@' %s | obelisk-opt --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=VISIBLE

// Forwarding can leave several writes and no reads of a private design root.
// Its value may be discarded only when neither state nor change is observable.
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @write_only {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "write"
    simulation.code_unit.decl 3 in 0 function hierarchy "read"
    simulation.code_unit.decl 4 in 0 always hierarchy "watch"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 static {obelisk.coverage.toggle_observable}

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 23 : i8
      // DUMP %levels = arith.constant 0 : i64
      // DUMP %scope = simulation.bytes.constant ""
      // DUMP simulation.dump.vars %ctx, %levels, %scope : (!simulation.context, i64, !simulation.bytes) -> ()
      simulation.call @write(%ctx, %value) : (!simulation.context, i8) -> ()
      %read = simulation.call @read(%ctx) : (!simulation.context) -> i8
      %actor = simulation.spawn @watch(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }

    // OFF-LABEL: simulation.func private @write(
    // OFF: %[[DEAD:.*]] = simulation.context.storage %arg0[0]
    // OFF-NOT: simulation.ref.store {{.*}} to %[[DEAD]]
    // OFF: %[[READ:.*]] = simulation.context.storage %arg0[1]
    // OFF: simulation.ref.store %arg1 to %[[READ]]
    // OFF: %[[WATCH:.*]] = simulation.context.storage %arg0[2]
    // OFF: simulation.ref.store %arg1 to %[[WATCH]]
    // OFF: %[[COV:.*]] = simulation.context.storage %arg0[3]
    // OFF: simulation.ref.store %arg1 to %[[COV]]
    // OFF: simulation.return
    // VISIBLE-LABEL: simulation.func private @write(
    // VISIBLE: %[[DEAD:.*]] = simulation.context.storage %arg0[0]
    // VISIBLE-COUNT-2: simulation.ref.store %arg1 to %[[DEAD]]
    // VISIBLE: simulation.return
    simulation.func private @write(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %dead = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      simulation.ref.store %value to %dead : i8, !simulation.ref<i8>
      simulation.ref.store %value to %dead : i8, !simulation.ref<i8>
      %read = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      simulation.ref.store %value to %read : i8, !simulation.ref<i8>
      %watch = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      simulation.ref.store %value to %watch : i8, !simulation.ref<i8>
      %cov = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      simulation.ref.store %value to %cov : i8, !simulation.ref<i8>
      simulation.return
    }
    simulation.func private @read(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %ref = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %value = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }
    simulation.func private @watch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %ref = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %ref to ^done : !simulation.ref<i8>
    ^done:
      simulation.return
    }
  }
}
