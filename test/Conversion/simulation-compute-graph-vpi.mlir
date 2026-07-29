// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=OFF
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=READ
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=FULL
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba))' | FileCheck %s --check-prefix=READ-SPEC
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba))' | FileCheck %s --check-prefix=FULL-SPEC
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-plan-static-superstep{missed-remarks=true}))' > %t.no-superstep 2> %t.superstep-remarks
// RUN: FileCheck %s --check-prefix=NO-SUPERSTEP < %t.no-superstep
// RUN: FileCheck %s --check-prefix=SUPERSTEP-REMARK < %t.superstep-remarks

module {
  // NO-SUPERSTEP-NOT: obelisk_sim.static_superstep
  // SUPERSTEP-REMARK: remark: static superstep not planned: missing root initializer
  obelisk_sim.design @vpi {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.vpi.repeats.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.vpi.once.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 initial hierarchy "test.vpi.override.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 initial hierarchy "test.vpi.wide.9000004"
    obelisk_sim.scope.decl 0

    // VPI capability is a property of the whole compilation, so it reaches
    // every visible descriptor.
    // OFF: obelisk_sim.storage.decl 0 {{.*}}observability = 0
    // READ: obelisk_sim.storage.decl 0 {{.*}}observability = 1
    // FULL: obelisk_sim.storage.decl 0 {{.*}}observability = 2
    // READ-SPEC: obelisk_sim.static_specialization<version = 1, maxPackedWidth = 64
    // READ-SPEC-SAME: static_state_root<descriptor = 0, width = 8, direct = true, guarded = false, nba = true>
    // READ-SPEC-SAME: static_state_root<descriptor = 1, width = 256, direct = false, guarded = false, nba = true>
    // READ-SPEC-SAME: static_state_root<descriptor = 2, width = 8, direct = false, guarded = true, nba = false>
    // READ-SPEC-SAME: static_actor_root<function = @once, descriptor = 0, read = false, write = true>
    // READ-SPEC-SAME: static_actor_root<function = @repeats, descriptor = 0, read = false, write = true>
    // READ-SPEC-SAME: nbaRoots = [0, 1]
    // FULL-SPEC: obelisk_sim.static_specialization<version = 1, maxPackedWidth = 64
    // FULL-SPEC-SAME: static_state_root<descriptor = 0, width = 8, direct = false, guarded = true, nba = true>
    // FULL-SPEC-SAME: static_state_root<descriptor = 1, width = 256, direct = false, guarded = false, nba = true>
    // FULL-SPEC-SAME: static_state_root<descriptor = 2, width = 8, direct = false, guarded = true, nba = false>
    // FULL-SPEC-SAME: nbaRoots = [0, 1]
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.unpacked_array<0 : 7 x i32> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @repeats(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %delay = obelisk_sim.time.constant 1
      cf.br ^loop
    ^loop:
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // A repeated immediate assignment to one known root accumulates in
      // generated state. Writable VPI deposits enter through the bytecode
      // transition stage at a scheduler boundary, so they cannot interleave
      // with staging and commit.
      // OFF: storage = root_accumulator
      // READ: storage = root_accumulator
      // FULL: storage = root_accumulator
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

    obelisk_sim.func @override(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %value = obelisk_sim.logic.constant 42 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.override %dst = %value assign false : !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      obelisk_sim.release_override %dst assign false : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    // Wide fixed packed NBA roots participate in the same canonical plan as
    // narrow direct-state roots. Native lowering may select its generated
    // accumulator while bytecode consumes the same ordered site inventory.
    obelisk_sim.func @wide(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.ref<!obelisk_sim.unpacked_array<0 : 7 x i32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %word = arith.constant 1 : i32
      %value = obelisk_sim.aggregate.construct %word, %word, %word, %word, %word, %word, %word, %word : (i32, i32, i32, i32, i32, i32, i32, i32) -> !obelisk_sim.unpacked_array<0 : 7 x i32>
      obelisk_sim.nba.enqueue %value to %dst : (!obelisk_sim.unpacked_array<0 : 7 x i32>, !obelisk_sim.ref<!obelisk_sim.unpacked_array<0 : 7 x i32>>) -> ()
      obelisk_sim.return
    }
  }
}
