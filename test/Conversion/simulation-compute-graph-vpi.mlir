// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=OFF
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=READ
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=FULL

module {
  obelisk_sim.design @vpi {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.vpi.repeats.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.vpi.once.9000002"
    obelisk_sim.scope.decl 0

    // VPI capability is a property of the whole compilation, so it reaches
    // every visible descriptor.
    // OFF: obelisk_sim.storage.decl 0 {{.*}}observability = 0
    // READ: obelisk_sim.storage.decl 0 {{.*}}observability = 1
    // FULL: obelisk_sim.storage.decl 0 {{.*}}observability = 2
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @repeats(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %delay = obelisk_sim.time.constant 1
      cf.br ^loop
    ^loop:
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // A repeated immediate assignment to one known root accumulates in
      // generated state. Unrestricted writable VPI can observe or rewrite that
      // root between staging and commit, so it falls back to the frontier.
      // OFF: storage = root_accumulator
      // READ: storage = root_accumulator
      // FULL: storage = dynamic_frontier
      obelisk_sim.nba.enqueue %value to %dst : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.suspend.delay %delay to ^loop
    }

    obelisk_sim.func @once(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // A site proven single-shot keeps its fixed slot in every VPI mode:
      // there is no second update for an external writer to interleave with.
      // OFF: storage = fixed_slot
      // READ: storage = fixed_slot
      // FULL: storage = fixed_slot
      obelisk_sim.nba.enqueue %value to %dst : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.return
    }
  }
}
