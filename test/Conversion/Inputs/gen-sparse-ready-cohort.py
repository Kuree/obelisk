"""Generate a sparse native ready cohort, or check its deterministic work bound."""

import re
import sys

if len(sys.argv) > 1:
    with open(sys.argv[1]) as log:
        result = log.read()
    assert "cohort=32000" in result, result
    visits = int(re.search(r"candidate_inventory_visits=(\d+)", result)[1])
    # Includes startup of all 545 actors. Steady-state cache reconstruction
    # must visit the 32 ready actors, not all 512 unrelated sleeping actors.
    assert 0 < visits < 500000, visits
    print("sparse ready cohort: PASS")
    sys.exit(0)

print('''module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @sparse {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i1 design
    obelisk_sim.storage.decl 2 in 0 : i32 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "sleep"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "hot"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "drive"
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %dormant = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i1>
      %count = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<i32>
      %zero = arith.constant 0 : i32
      %false = arith.constant false
      obelisk_sim.ref.store %zero to %count : i32, !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %false to %clock : i1, !obelisk_sim.ref<i1>
      obelisk_sim.ref.store %false to %dormant : i1, !obelisk_sim.ref<i1>
''')
for index in range(512):
    print(f'''      %s{index} = obelisk_sim.spawn @sleep(%ctx, %dormant) :
          !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process''')
for index in range(32):
    print(f'''      %h{index} = obelisk_sim.spawn @hot(%ctx, %clock, %count) :
          !obelisk_sim.context, !obelisk_sim.ref<i1>, !obelisk_sim.ref<i32>
          -> !obelisk_sim.process''')
print('''      %d = obelisk_sim.spawn @drive(%ctx, %clock, %count) :
          !obelisk_sim.context, !obelisk_sim.ref<i1>, !obelisk_sim.ref<i32>
          -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @sleep(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %ref to ^wait : !obelisk_sim.ref<i1>
    }
    obelisk_sim.func @hot(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 1 : i32},
        %count: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %ref to ^resume : !obelisk_sim.ref<i1>
    ^resume:
      %old = obelisk_sim.ref.load %count : !obelisk_sim.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      obelisk_sim.ref.store %next to %count : i32, !obelisk_sim.ref<i32>
      cf.br ^wait
    }
    obelisk_sim.func @drive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 1 : i32},
        %count: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^wait(%zero : i32)
    ^wait(%iteration: i32):
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle(%iteration : i32)
    ^toggle(%iteration2: i32):
      %old = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      obelisk_sim.ref.store %new to %ref : i1, !obelisk_sim.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %iteration2, %one : i32
      %limit = arith.constant 1000 : i32
      %more = arith.cmpi ult, %next, %limit : i32
      cf.cond_br %more, ^wait(%next : i32), ^settle
    ^settle:
      %delay2 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay2 to ^done
    ^done:
      %value = obelisk_sim.ref.load %count : !obelisk_sim.ref<i32> -> i32
      %fmt = obelisk_sim.bytes.constant "cohort=%0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %value) newline = true
          radix = 10 flags = [0, 0] : !obelisk_sim.bytes, i32
      obelisk_sim.return
    }
  }
}''')
