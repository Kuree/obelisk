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
  simulation.design @sparse {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "sleep"
    simulation.code_unit.decl 3 in 0 always hierarchy "hot"
    simulation.code_unit.decl 4 in 0 initial hierarchy "drive"
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %dormant = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %count = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %false = arith.constant false
      simulation.ref.store %zero to %count : i32, !simulation.ref<i32>
      simulation.ref.store %false to %clock : i1, !simulation.ref<i1>
      simulation.ref.store %false to %dormant : i1, !simulation.ref<i1>
''')
for index in range(512):
    print(f'''      %s{index} = simulation.spawn @sleep(%ctx, %dormant) :
          !simulation.context, !simulation.ref<i1> -> !simulation.process''')
for index in range(32):
    print(f'''      %h{index} = simulation.spawn @hot(%ctx, %clock, %count) :
          !simulation.context, !simulation.ref<i1>, !simulation.ref<i32>
          -> !simulation.process''')
print('''      %d = simulation.spawn @drive(%ctx, %clock, %count) :
          !simulation.context, !simulation.ref<i1>, !simulation.ref<i32>
          -> !simulation.process
      simulation.return
    }
    simulation.func @sleep(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i1> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %ref to ^wait : !simulation.ref<i1>
    }
    simulation.func @hot(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i1> {simulation.capture_kind = 1 : i32},
        %count: !simulation.ref<i32> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %ref to ^resume : !simulation.ref<i1>
    ^resume:
      %old = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %count : i32, !simulation.ref<i32>
      cf.br ^wait
    }
    simulation.func @drive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i1> {simulation.capture_kind = 1 : i32},
        %count: !simulation.ref<i32> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^wait(%zero : i32)
    ^wait(%iteration: i32):
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle(%iteration : i32)
    ^toggle(%iteration2: i32):
      %old = simulation.ref.load %ref : !simulation.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %ref : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %iteration2, %one : i32
      %limit = arith.constant 1000 : i32
      %more = arith.cmpi ult, %next, %limit : i32
      cf.cond_br %more, ^wait(%next : i32), ^settle
    ^settle:
      %delay2 = simulation.time.constant 1
      simulation.suspend.delay %delay2 to ^done
    ^done:
      %value = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %fmt = simulation.bytes.constant "cohort=%0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %value) newline = true
          radix = <decimal> flags = [0, 0] : !simulation.bytes, i32
      simulation.return
    }
  }
}''')
