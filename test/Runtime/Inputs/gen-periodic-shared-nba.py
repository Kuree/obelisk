"""Generate a mixed runtime/periodic model with many independent NBA roots."""
import sys
count = 128
def out(text):
    if "--delayed" in sys.argv:
        # LRM 10.4.2 Example 6: the later immediate assignment at t=15
        # must overwrite this older delayed assignment in the same NBA slot.
        text = text.replace("%zero = arith.constant 0 : i32",
            "%hundred = obelisk_sim.logic.constant 100 : i32, 0 : i32 : !word\n      "
            "%due = obelisk_sim.time.constant 15\n      "
            "obelisk_sim.nba.enqueue %hundred to %first after %due : "
            "(!word, !ref, !obelisk_sim.time) -> ()\n      "
            "%zero = arith.constant 0 : i32")
    if "--timed" in sys.argv:
        # LRM 9.4.1: first sample at t=5, then every ten ticks. This observer
        # does not subscribe to the clock, so run_until can own its calendar.
        text = text.replace("cf.br ^wait(%zero : i32)",
            "%firstDelay = obelisk_sim.time.constant 5\n      "
            "obelisk_sim.suspend.delay %firstDelay to ^before(%zero : i32) "
            "{site = #schedule.continuation<id = 132>, timing = #schedule.timing_site<id = 2, kind = calendar>}")
        text = text.replace(
            "obelisk_sim.suspend.edge posedge %clk to ^before(%n : i32) {schedule.procedural_event_wait, site = #schedule.continuation<id = 130>} : !clock",
            "%gap = obelisk_sim.time.constant 9\n      "
            "obelisk_sim.suspend.delay %gap to ^before(%n : i32) {site = #schedule.continuation<id = 130>, timing = #schedule.timing_site<id = 3, kind = calendar>}")
    sys.stdout.write(text)
out('''!word = !obelisk_sim.logic<32>
!ref = !obelisk_sim.ref<!word>
!clock = !obelisk_sim.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 0 : i32} {
  obelisk_sim.design @shared_nba {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "observe"
''')
for i in range(count):
    out(f"    obelisk_sim.storage.decl {i+1} in 0 : !word design\n")
    out(f'    obelisk_sim.code_unit.decl {i+4} in 0 always hierarchy "writer{i}"\n')
out('''    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !clock
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clk : i1, !clock
      %clock = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !clock -> !obelisk_sim.process
''')
for i in range(count):
    out(f'''      %q{i} = obelisk_sim.context.storage %ctx[{i+1}] : !ref
      %v{i} = obelisk_sim.logic.constant {i} : i32, 0 : i32 : !word
      obelisk_sim.ref.store %v{i} to %q{i} : !word, !ref
      %p{i} = obelisk_sim.spawn @writer{i}(%ctx, %clk, %q{i}) : !obelisk_sim.context, !clock, !ref -> !obelisk_sim.process
''')
out(f'''      %observe = obelisk_sim.spawn @observe(%ctx, %clk, %q0, %q{count-1}) : !obelisk_sim.context, !clock, !ref, !ref -> !obelisk_sim.process
      obelisk_sim.return
    }}
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {{obelisk_sim.capture_kind = 0 : i32}}, %clk: !clock {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}}) attributes {{entry_kind = 3 : i32, code_unit_id = 2 : i64}} {{
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %delay to ^toggle {{site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !clock -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %clk : i1, !clock
      cf.br ^wait
    }}
''')
for i in range(count):
    out(f'''    obelisk_sim.func @writer{i}(%ctx: !obelisk_sim.context {{obelisk_sim.capture_kind = 0 : i32}}, %clk: !clock {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}}, %q: !ref {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = {i+1} : i64}}) attributes {{entry_kind = 3 : i32, code_unit_id = {i+4} : i64}} {{
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^update {{site = #schedule.continuation<id = {i+2}>}} : !clock
    ^update:
      %old = obelisk_sim.ref.load %q : !ref -> !word
      %one = obelisk_sim.logic.constant 1 : i32, 0 : i32 : !word
      %next = obelisk_sim.logic.binary add %old, %one : !word
      obelisk_sim.nba.enqueue %next to %q : (!word, !ref) -> ()
      cf.br ^wait
    }}
''')
out(f'''    obelisk_sim.func @observe(%ctx: !obelisk_sim.context {{obelisk_sim.capture_kind = 0 : i32}}, %clk: !clock {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}}, %first: !ref {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}}, %last: !ref {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = {count} : i64}}) attributes {{entry_kind = 1 : i32, code_unit_id = 3 : i64}} {{
      %zero = arith.constant 0 : i32
      cf.br ^wait(%zero : i32)
    ^wait(%n: i32):
      obelisk_sim.suspend.edge posedge %clk to ^before(%n : i32) {{schedule.procedural_event_wait, site = #schedule.continuation<id = {count+2}>}} : !clock
    ^before(%iteration: i32):
      %a = obelisk_sim.ref.load %first : !ref -> !word
      %b = obelisk_sim.ref.load %last : !ref -> !word
      %real = arith.constant 1.0 : f64
      %fmt = obelisk_sim.bytes.constant "active %0d %0d %f"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %a, %b, %real) newline = true radix = 10 flags = [0, 0, 0, 4] : !obelisk_sim.bytes, !word, !word, f64
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^after(%iteration : i32) {{site = #schedule.continuation<id = {count+3}>, timing = #schedule.timing_site<id = 1, kind = calendar>}}
    ^after(%previous: i32):
      %c = obelisk_sim.ref.load %first : !ref -> !word
      %d = obelisk_sim.ref.load %last : !ref -> !word
      %fmt2 = obelisk_sim.bytes.constant "settled %0d %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%fmt2, %c, %d) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !word, !word
      %one = arith.constant 1 : i32
      %four = arith.constant 4 : i32
      %next = arith.addi %previous, %one : i32
      %more = arith.cmpi ult, %next, %four : i32
      cf.cond_br %more, ^wait(%next : i32), ^done
    ^done:
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }}
  }}
}}
''')
