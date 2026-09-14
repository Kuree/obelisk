"""Generate independent NBA writers spanning several scheduler ready words.

Each writer watches one clock and updates its own packed bit. A delayed
observer checks that ALL active words drain before advancing past the NBA
barrier, including a second activation after the ready set became empty.
"""

import sys

count = int(sys.argv[1])
eval_mode = "--eval" in sys.argv[2:]
assert count >= 65
out = sys.stdout.write
out(f'''!bit = !obelisk_sim.logic<1>
!clockref = !obelisk_sim.ref<!bit>
!data = !obelisk_sim.logic<{count}>
!dataref = !obelisk_sim.ref<!data>
module attributes {{llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.native_scheduler = {3 if eval_mode else 2} : i32}} {{
  obelisk_sim.design @multiword {{
    obelisk_sim.scope.decl 0 hierarchy "multiword"
    obelisk_sim.storage.decl 0 in 0 : !bit design
    obelisk_sim.storage.decl 1 in 0 : !data design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "multiword.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "multiword.clock"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "multiword.check"
''')
for i in range(count):
    out(f'    obelisk_sim.code_unit.decl {i + 4} in 0 always hierarchy "multiword.writer{i}"\n')
out(f'''
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {{obelisk_sim.capture_kind = 0 : i32}}) attributes {{entry_kind = 0 : i32, code_unit_id = 1 : i64}} {{
      %clk = obelisk_sim.context.storage %ctx[0] : !clockref
      %data = obelisk_sim.context.storage %ctx[1] : !dataref
      %zero = obelisk_sim.logic.constant false, false : !bit
      %zeros = obelisk_sim.logic.constant 0 : i{count}, 0 : i{count} : !data
      obelisk_sim.ref.store %zero to %clk : !bit, !clockref
      obelisk_sim.ref.store %zeros to %data : !data, !dataref
      %clock = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !clockref -> !obelisk_sim.process
''')
for i in range(count):
    out(f'      %writer{i} = obelisk_sim.spawn @writer{i}(%ctx, %clk, %data) : !obelisk_sim.context, !clockref, !dataref -> !obelisk_sim.process\n')
out('''      %check = obelisk_sim.spawn @check(%ctx, %data) : !obelisk_sim.context, !dataref -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clockref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^toggle {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !clockref -> !bit
      %new = obelisk_sim.logic.unary bit_not %old : (!bit) -> !bit
      obelisk_sim.ref.store %new to %clk : !bit, !clockref
      cf.br ^wait
    }
''')
for i in range(count):
    out(f'''    obelisk_sim.func @writer{i}(%ctx: !obelisk_sim.context {{obelisk_sim.capture_kind = 0 : i32}}, %clk: !clockref {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}}, %data: !dataref {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}}) attributes {{entry_kind = 3 : i32, code_unit_id = {i + 4} : i64}} {{
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clk to ^update {{site = #obelisk_sim.continuation<id = {i + 2}>}} : !clockref
    ^update:
      %value = obelisk_sim.ref.load %clk : !clockref -> !bit
      %target = obelisk_sim.ref.extract %data from {i} : !dataref -> !clockref
      obelisk_sim.nba.enqueue %value to %target : (!bit, !clockref) -> ()
''')
    if i == count - 1 and not eval_mode:
        # A late Active actor must still see the OLD value of every bit.
        # A premature NBA barrier between ready words exposes a partial value.
        out(f'''      %old = obelisk_sim.ref.load %data : !dataref -> !data
      %format = obelisk_sim.bytes.constant "active %0{(count + 3) // 4}h"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %old) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !data
''')
    out('''
      cf.br ^wait
    }
''')
out(f'''    obelisk_sim.func @check(%ctx: !obelisk_sim.context {{obelisk_sim.capture_kind = 0 : i32}}, %data: !dataref {{obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}}) attributes {{entry_kind = 1 : i32, code_unit_id = 3 : i64}} {{
      %format = obelisk_sim.bytes.constant "%0{(count + 3) // 4}h"
      %stdout = arith.constant 1 : i32
      %one = obelisk_sim.time.constant 1
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %one to ^before {{site = #obelisk_sim.continuation<id = {count + 2}>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}}
    ^before:
      %before = obelisk_sim.ref.load %data : !dataref -> !data
      obelisk_sim.display %ctx to %stdout(%format, %before) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !data
      obelisk_sim.suspend.delay %two to ^after {{site = #obelisk_sim.continuation<id = {count + 3}>, timing = #obelisk_sim.timing_site<id = 2, kind = calendar>}}
    ^after:
      %after = obelisk_sim.ref.load %data : !dataref -> !data
      obelisk_sim.display %ctx to %stdout(%format, %after) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !data
      obelisk_sim.suspend.delay %two to ^again {{site = #obelisk_sim.continuation<id = {count + 4}>, timing = #obelisk_sim.timing_site<id = 3, kind = calendar>}}
    ^again:
      %again = obelisk_sim.ref.load %data : !dataref -> !data
      obelisk_sim.display %ctx to %stdout(%format, %again) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !data
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }}
  }}
}}
''')
