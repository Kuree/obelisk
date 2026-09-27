"""Generate independent NBA writers spanning several scheduler ready words.

Each writer watches one clock and updates its own packed bit. A delayed
observer checks that ALL active words drain before advancing past the NBA
barrier, including a second activation after the ready set became empty.
"""

import sys

count = int(sys.argv[1])
eval_mode = "--eval" in sys.argv[2:]
forwarded = "--forwarded" in sys.argv[2:]
assert count >= 65
out = sys.stdout.write
out(f'''!bit = !simulation.logic<1>
!clockref = !simulation.ref<!bit>
!data = !simulation.logic<{count}>
!dataref = !simulation.ref<!data>
module attributes {{llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = {3 if eval_mode else 2} : i32}} {{
  simulation.design @multiword {{
    simulation.scope.decl 0 hierarchy "multiword"
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !data design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "multiword.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "multiword.clock"
    simulation.code_unit.decl 3 in 0 initial hierarchy "multiword.check"
''')
for i in range(count):
    out(f'    simulation.code_unit.decl {i + 4} in 0 always hierarchy "multiword.writer{i}"\n')
if forwarded:
    out(f'''    simulation.storage.decl 2 in 0 : !bit design
    simulation.code_unit.decl {count + 4} in 0 always hierarchy "multiword.forward"
''')
out(f'''
    simulation.func @root(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}) attributes {{entry_kind = 0 : i32, code_unit_id = 1 : i64}} {{
      %clk = simulation.context.storage %ctx[0] : !clockref
      %data = simulation.context.storage %ctx[1] : !dataref
      %zero = simulation.logic.constant false, false : !bit
      %zeros = simulation.logic.constant 0 : i{count}, 0 : i{count} : !data
      simulation.ref.store %zero to %clk : !bit, !clockref
      simulation.ref.store %zeros to %data : !data, !dataref
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clockref -> !simulation.process
''')
for i in range(count):
    if i == 0 and forwarded:
        out('''      %forwarded = simulation.context.storage %ctx[2] : !clockref
      %forward = simulation.spawn @forward(%ctx, %clk, %forwarded) : !simulation.context, !clockref, !clockref -> !simulation.process
''')
    clock_arg = "%forwarded" if forwarded else "%clk"
    out(f'      %writer{i} = simulation.spawn @writer{i}(%ctx, {clock_arg}, %data) : !simulation.context, !clockref, !dataref -> !simulation.process\n')
out('''      %check = simulation.spawn @check(%ctx, %data) : !simulation.context, !dataref -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !clockref -> !bit
      %new = simulation.logic.unary bit_not %old : (!bit) -> !bit
      simulation.ref.store %new to %clk : !bit, !clockref
      cf.br ^wait
    }
''')
if forwarded:
    out(f'''    simulation.func @forward(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}, %clk: !clockref {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}}, %target: !clockref {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}}) attributes {{entry_kind = 3 : i32, code_unit_id = {count + 4} : i64}} {{
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^copy {{site = #schedule.continuation<id = {count + 5}>}} : !clockref
    ^copy:
      %value = simulation.ref.load %clk : !clockref -> !bit
      simulation.ref.store %value to %target : !bit, !clockref
      cf.br ^wait
    }}
''')
for i in range(count):
    out(f'''    simulation.func @writer{i}(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}, %clk: !clockref {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = {2 if forwarded else 0} : i64}}, %data: !dataref {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}}) attributes {{entry_kind = 3 : i32, code_unit_id = {i + 4} : i64}} {{
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^update {{site = #schedule.continuation<id = {i + 2}>}} : !clockref
    ^update:
      %value = simulation.ref.load %clk : !clockref -> !bit
      %target = simulation.ref.extract %data from {i} : !dataref -> !clockref
      simulation.nba.enqueue %value to %target : (!bit, !clockref) -> ()
''')
    if i == count - 1 and not eval_mode:
        # A late Active actor must still see the OLD value of every bit.
        # A premature NBA barrier between ready words exposes a partial value.
        out(f'''      %old = simulation.ref.load %data : !dataref -> !data
      %format = simulation.bytes.constant "active %0{(count + 3) // 4}h"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %old) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !data
''')
    out('''
      cf.br ^wait
    }
''')
out(f'''    simulation.func @check(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}, %data: !dataref {{simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}}) attributes {{entry_kind = 1 : i32, code_unit_id = 3 : i64}} {{
      %format = simulation.bytes.constant "%0{(count + 3) // 4}h"
      %stdout = arith.constant 1 : i32
      %one = simulation.time.constant 1
      %two = simulation.time.constant 2
      simulation.suspend.delay %one to ^before {{site = #schedule.continuation<id = {count + 2}>, timing = #schedule.timing_site<id = 1, kind = calendar>}}
    ^before:
      %before = simulation.ref.load %data : !dataref -> !data
      simulation.display %ctx to %stdout(%format, %before) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !data
      simulation.suspend.delay %two to ^after {{site = #schedule.continuation<id = {count + 3}>, timing = #schedule.timing_site<id = 2, kind = calendar>}}
    ^after:
      %after = simulation.ref.load %data : !dataref -> !data
      simulation.display %ctx to %stdout(%format, %after) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !data
      simulation.suspend.delay %two to ^again {{site = #schedule.continuation<id = {count + 4}>, timing = #schedule.timing_site<id = 3, kind = calendar>}}
    ^again:
      %again = simulation.ref.load %data : !dataref -> !data
      simulation.display %ctx to %stdout(%format, %again) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !data
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }}
  }}
}}
''')
