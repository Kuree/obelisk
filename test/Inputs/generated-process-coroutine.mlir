module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @generated_execution {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.generated_execution.execution_process.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.generated_execution.failing_process.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 initial hierarchy "test.generated_execution.event_trigger_process.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 initial hierarchy "test.generated_execution.short_child.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 initial hierarchy "test.generated_execution.await_child.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 function hierarchy "test.generated_execution.consume_automatic_ref.9000006"
    obelisk_sim.code_unit.decl 9000007 in 0 initial hierarchy "test.generated_execution.automatic_child.9000007"
    obelisk_sim.code_unit.decl 9000008 in 0 initial hierarchy "test.generated_execution.automatic_process.9000008"
    obelisk_sim.code_unit.decl 9000009 in 0 initial hierarchy "test.generated_execution.automatic_loop_process.9000009"
    obelisk_sim.code_unit.decl 9000010 in 0 initial hierarchy "test.generated_execution.long_child.9000010"
    obelisk_sim.code_unit.decl 9000011 in 0 initial hierarchy "test.generated_execution.orchestration_process.9000011"
    obelisk_sim.scope.decl 0

    obelisk_sim.func @execution_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %capture: i64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %one = arith.constant 1 : i64
      %next = arith.addi %capture, %one : i64
      %first_delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %first_delay to ^second(%next : i64)
    ^second(%value: i64):
      %second_delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %second_delay to ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @failing_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %descriptor: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^failed
    ^failed:
      obelisk_sim.dump.flush %ctx : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }

    obelisk_sim.func @event_trigger_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^trigger
    ^trigger:
      obelisk_sim.event.trigger %event nonblocking = false
      obelisk_sim.return
    }

    obelisk_sim.func @short_child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      %stdout = arith.constant -2147483647 : i32
      %message = obelisk_sim.bytes.constant "join-short"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func @await_child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      %stdout = arith.constant -2147483647 : i32
      %message = obelisk_sim.bytes.constant "await-child"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func @consume_automatic_ref(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i64> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i64> -> i64
      obelisk_sim.return
    }

    obelisk_sim.func @automatic_child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i64> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^check
    ^check:
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i64> -> i64
      %expected = arith.constant 5 : i64
      %valid = arith.cmpi eq, %value, %expected : i64
      cf.cond_br %valid, ^done, ^failed
    ^failed:
      %invalid = arith.constant 999 : i32
      obelisk_sim.file.flush %ctx, %invalid :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @automatic_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000008 : i64} {
      cf.br ^allocate
    ^allocate:
      %initial = arith.constant 5 : i64
      %replacement = arith.constant 9 : i64
      %delay = obelisk_sim.time.constant 2
      %local = obelisk_sim.ref.alloc %initial :
          i64 -> !obelisk_sim.ref<i64>
      obelisk_sim.call @consume_automatic_ref(%ctx, %local) :
          (!obelisk_sim.context, !obelisk_sim.ref<i64>) -> ()
      %child = obelisk_sim.spawn @automatic_child(%ctx, %local) :
          !obelisk_sim.context, !obelisk_sim.ref<i64> -> !obelisk_sim.process
      obelisk_sim.nba.enqueue %replacement to %local after %delay :
          (i64, !obelisk_sim.ref<i64>, !obelisk_sim.time) -> ()
      obelisk_sim.return
    }

    obelisk_sim.func @automatic_loop_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000009 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^allocate(%zero : i32)
    ^allocate(%iteration: i32):
      %initial = arith.extsi %iteration : i32 to i64
      %local = obelisk_sim.ref.alloc %initial :
          i64 -> !obelisk_sim.ref<i64>
      cf.br ^use(%local, %iteration : !obelisk_sim.ref<i64>, i32)
    ^use(%reference: !obelisk_sim.ref<i64>, %iteration_live: i32):
      %value = obelisk_sim.ref.load %reference :
          !obelisk_sim.ref<i64> -> i64
      %one = arith.constant 1 : i32
      %limit = arith.constant 3 : i32
      %next = arith.addi %iteration_live, %one : i32
      %continue = arith.cmpi ult, %next, %limit : i32
      cf.cond_br %continue, ^allocate(%next : i32), ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @long_child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000010 : i64} {
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      %stdout = arith.constant -2147483647 : i32
      %message = obelisk_sim.bytes.constant "join-long"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func @orchestration_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000011 : i64} {
      %event = obelisk_sim.context.event %ctx[77] : !obelisk_sim.event
      %trigger = obelisk_sim.spawn @event_trigger_process(%ctx, %event) :
          !obelisk_sim.context, !obelisk_sim.event -> !obelisk_sim.process
      obelisk_sim.suspend.event %event to ^after_event
    ^after_event:
      %short = obelisk_sim.spawn @short_child(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %long = obelisk_sim.spawn @long_child(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.suspend.join all %short, %long processes 2 to ^after_join :
          !obelisk_sim.process, !obelisk_sim.process
    ^after_join:
      %stdout = arith.constant -2147483647 : i32
      %joined = obelisk_sim.bytes.constant "joined"
      obelisk_sim.display %ctx to %stdout(%joined) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      %awaited = obelisk_sim.spawn @await_child(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.suspend.await %awaited to ^done
    ^done:
      %stdout_done = arith.constant -2147483647 : i32
      %awaited_message = obelisk_sim.bytes.constant "awaited"
      obelisk_sim.display %ctx to %stdout_done(%awaited_message) newline = true
          radix = 10 flags = [0] : !obelisk_sim.bytes
      %invalid = arith.constant 999 : i32
      obelisk_sim.file.flush %ctx, %invalid :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
    }
  }
}
