module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @generated_execution {
    obelisk_sim.scope.decl 0

    obelisk_sim.func @execution_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %capture: i64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %first_delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %first_delay to ^second(%capture : i64)
    ^second(%value: i64):
      %second_delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %second_delay to ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @failing_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %descriptor: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^failed
    ^failed:
      obelisk_sim.file.flush %ctx, %descriptor :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
    }
  }
}
