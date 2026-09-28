module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @generated_execution {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.generated_execution.execution_process.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.generated_execution.failing_process.9000002"
    simulation.code_unit.decl 9000003 in 0 initial hierarchy "test.generated_execution.event_trigger_process.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.generated_execution.short_child.9000004"
    simulation.code_unit.decl 9000005 in 0 initial hierarchy "test.generated_execution.await_child.9000005"
    simulation.code_unit.decl 9000006 in 0 function hierarchy "test.generated_execution.consume_automatic_ref.9000006"
    simulation.code_unit.decl 9000007 in 0 initial hierarchy "test.generated_execution.automatic_child.9000007"
    simulation.code_unit.decl 9000008 in 0 initial hierarchy "test.generated_execution.automatic_process.9000008"
    simulation.code_unit.decl 9000009 in 0 initial hierarchy "test.generated_execution.automatic_loop_process.9000009"
    simulation.code_unit.decl 9000010 in 0 initial hierarchy "test.generated_execution.long_child.9000010"
    simulation.code_unit.decl 9000011 in 0 initial hierarchy "test.generated_execution.orchestration_process.9000011"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i64 design
    simulation.code_unit.decl 9000012 in 0 continuous hierarchy "test.generated_execution.group_process"

    simulation.storage.decl 2 in 0 : !simulation.logic<65> design
    simulation.storage.decl 3 in 0 : !simulation.logic<65> design
    simulation.code_unit.decl 9000013 in 0 port_input hierarchy "test.generated_execution.copy_process"
    simulation.func @copy_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<65>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %sink: !simulation.ref<!simulation.logic<65>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 9000013 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %source : !simulation.ref<!simulation.logic<65>> -> !simulation.logic<65>
      simulation.ref.store %value to %sink : !simulation.logic<65>, !simulation.ref<!simulation.logic<65>>
      simulation.suspend.change %source to ^body : !simulation.ref<!simulation.logic<65>>
    }

    simulation.code_unit.decl 9000014 in 0 port_input hierarchy "test.generated_execution.copy_process_second"
    simulation.func @copy_process_second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<65>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %sink: !simulation.ref<!simulation.logic<65>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 9000014 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %source : !simulation.ref<!simulation.logic<65>> -> !simulation.logic<65>
      simulation.ref.store %value to %sink : !simulation.logic<65>, !simulation.ref<!simulation.logic<65>>
      simulation.suspend.change %source to ^body {site = #schedule.continuation<id = 7>} : !simulation.ref<!simulation.logic<65>>
    }

    simulation.code_unit.decl 9000015 in 0 always hierarchy "test.generated_execution.table_process"
    simulation.func @table_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9000015 : i64} {
      simulation.suspend.change %source to ^choose
          {site = #schedule.continuation<id = 3>, schedule.procedural_event_wait} : !simulation.ref<i64>
    ^choose:
      %value = simulation.ref.load %source : !simulation.ref<i64> -> i64
      %zero = arith.constant 0 : i64
      %first = arith.cmpi eq, %value, %zero : i64
      cf.cond_br %first, ^rise, ^any
    ^rise:
      %eleven = arith.constant 11 : i64
      simulation.ref.store %eleven to %sink : i64, !simulation.ref<i64>
      simulation.suspend.edge posedge %source to ^choose
          {site = #schedule.continuation<id = 7>} : !simulation.ref<i64>
    ^any:
      %twenty_two = arith.constant 22 : i64
      simulation.ref.store %twenty_two to %sink : i64, !simulation.ref<i64>
      simulation.suspend.any %source, %sink edges [0, 2] to ^done
          {site = #schedule.continuation<id = 13>, resume_region = 16 : i32} :
          !simulation.ref<i64>, !simulation.ref<i64>
    ^done:
      simulation.return
    }

    // Post-materialization group fixture. Its changing continuation lane
    // models the snapshots/dirty state carried by a union-wait group. The
    // direct executor must retain this canonical frame state across Tier-3
    // visits, even though it has no coroutine frame of its own.
    simulation.func @group_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %signal: !simulation.ref<i64> {simulation.capture_kind = 3 : i32,
                                      simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9000012 : i64,
                    schedule.native.region_body,
                    schedule.eval.reconstructs_continuation_args} {
      %zero = arith.constant 0 : i64
      cf.br ^body(%zero : i64)
    ^body(%value: i64):
      %one = arith.constant 1 : i64
      %next = arith.addi %value, %one : i64
      %now = simulation.time.now %ctx
      %published = arith.addi %next, %now : i64
      simulation.ref.store %published to %signal : i64, !simulation.ref<i64>
      simulation.suspend.change %signal to ^body(%next : i64) :
          !simulation.ref<i64>
    }

    simulation.func @execution_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %one = arith.constant 1 : i64
      %next = arith.addi %capture, %one : i64
      %first_delay = simulation.time.constant 3
      simulation.suspend.delay %first_delay to ^second(%next : i64)
    ^second(%value: i64):
      %second_delay = simulation.time.constant 5
      simulation.suspend.delay %second_delay to ^done
    ^done:
      simulation.return
    }

    simulation.func @failing_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %descriptor: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^failed
    ^failed:
      simulation.dump.flush %ctx : (!simulation.context) -> ()
      simulation.return
    }

    simulation.func @event_trigger_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %event: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^trigger
    ^trigger:
      simulation.event.trigger %event nonblocking = false
      simulation.return
    }

    simulation.func @short_child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^done
    ^done:
      %stdout = arith.constant -2147483647 : i32
      %message = simulation.bytes.constant "join-short"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func @await_child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^done
    ^done:
      %stdout = arith.constant -2147483647 : i32
      %message = simulation.bytes.constant "await-child"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func @consume_automatic_ref(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i64> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<i64> -> i64
      simulation.return
    }

    simulation.func @automatic_child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i64> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^check
    ^check:
      %value = simulation.ref.load %ref : !simulation.ref<i64> -> i64
      %expected = arith.constant 5 : i64
      %valid = arith.cmpi eq, %value, %expected : i64
      cf.cond_br %valid, ^done, ^failed
    ^failed:
      %invalid = arith.constant 999 : i32
      simulation.file.flush %ctx, %invalid :
          (!simulation.context, i32) -> ()
      simulation.return
    ^done:
      simulation.return
    }

    simulation.func @automatic_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000008 : i64} {
      cf.br ^allocate
    ^allocate:
      %initial = arith.constant 5 : i64
      %replacement = arith.constant 9 : i64
      %delay = simulation.time.constant 2
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      simulation.call @consume_automatic_ref(%ctx, %local) :
          (!simulation.context, !simulation.ref<i64>) -> ()
      %child = simulation.spawn @automatic_child(%ctx, %local) :
          !simulation.context, !simulation.ref<i64> -> !simulation.process
      simulation.nba.enqueue %replacement to %local after %delay :
          (i64, !simulation.ref<i64>, !simulation.time) -> ()
      simulation.return
    }

    simulation.func @automatic_loop_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000009 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^allocate(%zero : i32)
    ^allocate(%iteration: i32):
      %initial = arith.extsi %iteration : i32 to i64
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      cf.br ^use(%local, %iteration : !simulation.ref<i64>, i32)
    ^use(%reference: !simulation.ref<i64>, %iteration_live: i32):
      %value = simulation.ref.load %reference :
          !simulation.ref<i64> -> i64
      %one = arith.constant 1 : i32
      %limit = arith.constant 3 : i32
      %next = arith.addi %iteration_live, %one : i32
      %continue = arith.cmpi ult, %next, %limit : i32
      cf.cond_br %continue, ^allocate(%next : i32), ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }

    simulation.func @long_child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000010 : i64} {
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^done
    ^done:
      %stdout = arith.constant -2147483647 : i32
      %message = simulation.bytes.constant "join-long"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func @orchestration_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000011 : i64} {
      %event = simulation.context.event %ctx[77] : !simulation.event
      %trigger = simulation.spawn @event_trigger_process(%ctx, %event) :
          !simulation.context, !simulation.event -> !simulation.process
      simulation.suspend.event %event to ^after_event
    ^after_event:
      %short = simulation.spawn @short_child(%ctx) :
          !simulation.context -> !simulation.process
      %long = simulation.spawn @long_child(%ctx) :
          !simulation.context -> !simulation.process
      simulation.suspend.join all %short, %long processes 2 to ^after_join :
          !simulation.process, !simulation.process
    ^after_join:
      %stdout = arith.constant -2147483647 : i32
      %joined = simulation.bytes.constant "joined"
      simulation.display %ctx to %stdout(%joined) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      %awaited = simulation.spawn @await_child(%ctx) :
          !simulation.context -> !simulation.process
      simulation.suspend.await %awaited to ^done
    ^done:
      %stdout_done = arith.constant -2147483647 : i32
      %awaited_message = simulation.bytes.constant "awaited"
      simulation.display %ctx to %stdout_done(%awaited_message) newline = true
          radix = <decimal> flags = [0] : !simulation.bytes
      %invalid = arith.constant 999 : i32
      simulation.file.flush %ctx, %invalid :
          (!simulation.context, i32) -> ()
      simulation.return
    }
  }
}
