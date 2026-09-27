// RUN: obelisk-opt %s --split-input-file --obelisk-sim-extract-periodic-clocks | FileCheck %s --implicit-check-not=__obelisk_periodic_tick_ --implicit-check-not=schedule.periodic_control

// CHECK: simulation.design @unequal
module {
  simulation.design @unequal {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %b = simulation.time.constant 5
      simulation.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: simulation.design @zero
module {
  simulation.design @zero {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 0
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %b = simulation.time.constant 0
      simulation.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: simulation.design @finite
module {
  simulation.design @finite {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %b = simulation.time.constant 4
      simulation.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      simulation.return
    }
  }
}

// -----

// CHECK: simulation.design @zero_time_cycle
module {
  simulation.design @zero_time_cycle {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %b = simulation.time.constant 4
      simulation.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      cf.br ^spin
    ^spin:
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      cf.cond_br %v, ^spin, ^wait
    }
  }
}

// -----

// CHECK: simulation.design @reentrant
module {
  simulation.design @reentrant {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %q = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %b = simulation.time.constant 4
      simulation.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: simulation.design @controlled
module {
  simulation.design @controlled {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.process.control kill %p to ^done
    ^done:
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %b = simulation.time.constant 4
      simulation.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: simulation.design @mixed_waits
module {
  simulation.design @mixed_waits {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %b = simulation.time.constant 4
      simulation.suspend.edge posedge %clock to ^high : !simulation.ref<i1>
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: simulation.design @program
module {
  simulation.design @program {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64, domain = 1 : i32, home_region = 10 : i32} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %b = simulation.time.constant 4
      simulation.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: simulation.design @canonical
module {
  simulation.design @canonical {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: simulation.design @dynamic
module {
  simulation.design @dynamic {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %p = simulation.spawn @clock(%ctx, %ref) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = simulation.time.constant 4
      simulation.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %raw = arith.extui %v : i1 to i64
      %b = simulation.time.scale %raw by 4 signed = false : i64
      simulation.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      simulation.ref.store %one to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
  }
}
