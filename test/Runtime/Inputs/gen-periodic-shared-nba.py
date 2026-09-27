"""Generate a mixed runtime/periodic model with many independent NBA roots."""
import sys
count = 128
def out(text):
    if "--delayed" in sys.argv:
        # LRM 10.4.2 Example 6: the later immediate assignment at t=15
        # must overwrite this older delayed assignment in the same NBA slot.
        text = text.replace("%zero = arith.constant 0 : i32",
            "%hundred = simulation.logic.constant 100 : i32, 0 : i32 : !word\n      "
            "%due = simulation.time.constant 15\n      "
            "simulation.nba.enqueue %hundred to %first after %due : "
            "(!word, !ref, !simulation.time) -> ()\n      "
            "%zero = arith.constant 0 : i32")
    if "--timed" in sys.argv:
        # LRM 9.4.1: first sample at t=5, then every ten ticks. This observer
        # does not subscribe to the clock, so run_until can own its calendar.
        text = text.replace("cf.br ^wait(%zero : i32)",
            "%firstDelay = simulation.time.constant 5\n      "
            "simulation.suspend.delay %firstDelay to ^before(%zero : i32) "
            "{site = #schedule.continuation<id = 132>, timing = #schedule.timing_site<id = 2, kind = calendar>}")
        text = text.replace(
            "simulation.suspend.edge posedge %clk to ^before(%n : i32) {schedule.procedural_event_wait, site = #schedule.continuation<id = 130>} : !clock",
            "%gap = simulation.time.constant 9\n      "
            "simulation.suspend.delay %gap to ^before(%n : i32) {site = #schedule.continuation<id = 130>, timing = #schedule.timing_site<id = 3, kind = calendar>}")
    sys.stdout.write(text)
out('''!word = !simulation.logic<32>
!ref = !simulation.ref<!word>
!clock = !simulation.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 0 : i32} {
  simulation.design @shared_nba {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 initial hierarchy "observe"
''')
for i in range(count):
    out(f"    simulation.storage.decl {i+1} in 0 : !word design\n")
    out(f'    simulation.code_unit.decl {i+4} in 0 always hierarchy "writer{i}"\n')
out('''    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clock
      %zero = arith.constant false
      simulation.ref.store %zero to %clk : i1, !clock
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clock -> !simulation.process
''')
for i in range(count):
    out(f'''      %q{i} = simulation.context.storage %ctx[{i+1}] : !ref
      %v{i} = simulation.logic.constant {i} : i32, 0 : i32 : !word
      simulation.ref.store %v{i} to %q{i} : !word, !ref
      %p{i} = simulation.spawn @writer{i}(%ctx, %clk, %q{i}) : !simulation.context, !clock, !ref -> !simulation.process
''')
out(f'''      %observe = simulation.spawn @observe(%ctx, %clk, %q0, %q{count-1}) : !simulation.context, !clock, !ref, !ref -> !simulation.process
      simulation.return
    }}
    simulation.func @clock(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}, %clk: !clock {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}}) attributes {{entry_kind = 3 : i32, code_unit_id = 2 : i64}} {{
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^toggle {{site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}}
    ^toggle:
      %old = simulation.ref.load %clk : !clock -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clk : i1, !clock
      cf.br ^wait
    }}
''')
for i in range(count):
    out(f'''    simulation.func @writer{i}(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}, %clk: !clock {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}}, %q: !ref {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = {i+1} : i64}}) attributes {{entry_kind = 3 : i32, code_unit_id = {i+4} : i64}} {{
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^update {{site = #schedule.continuation<id = {i+2}>}} : !clock
    ^update:
      %old = simulation.ref.load %q : !ref -> !word
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !word
      %next = simulation.logic.binary add %old, %one : !word
      simulation.nba.enqueue %next to %q : (!word, !ref) -> ()
      cf.br ^wait
    }}
''')
out(f'''    simulation.func @observe(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}, %clk: !clock {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}}, %first: !ref {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}}, %last: !ref {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = {count} : i64}}) attributes {{entry_kind = 1 : i32, code_unit_id = 3 : i64}} {{
      %zero = arith.constant 0 : i32
      cf.br ^wait(%zero : i32)
    ^wait(%n: i32):
      simulation.suspend.edge posedge %clk to ^before(%n : i32) {{schedule.procedural_event_wait, site = #schedule.continuation<id = {count+2}>}} : !clock
    ^before(%iteration: i32):
      %a = simulation.ref.load %first : !ref -> !word
      %b = simulation.ref.load %last : !ref -> !word
      %real = arith.constant 1.0 : f64
      %fmt = simulation.bytes.constant "active %0d %0d %f"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %a, %b, %real) newline = true radix = <decimal> flags = [0, 0, 0, 4] : !simulation.bytes, !word, !word, f64
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^after(%iteration : i32) {{site = #schedule.continuation<id = {count+3}>, timing = #schedule.timing_site<id = 1, kind = calendar>}}
    ^after(%previous: i32):
      %c = simulation.ref.load %first : !ref -> !word
      %d = simulation.ref.load %last : !ref -> !word
      %fmt2 = simulation.bytes.constant "settled %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%fmt2, %c, %d) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      %one = arith.constant 1 : i32
      %four = arith.constant 4 : i32
      %next = arith.addi %previous, %one : i32
      %more = arith.cmpi ult, %next, %four : i32
      cf.cond_br %more, ^wait(%next : i32), ^done
    ^done:
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }}
  }}
}}
''')
