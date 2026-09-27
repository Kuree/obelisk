// RUN: obelisk-opt %s --split-input-file --obelisk-sim-extract-periodic-clocks | FileCheck %s --implicit-check-not=__obelisk_periodic_tick_ --implicit-check-not=obelisk_sim.periodic_control

// CHECK: obelisk_sim.design @unequal
module {
  obelisk_sim.design @unequal {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %b = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: obelisk_sim.design @zero
module {
  obelisk_sim.design @zero {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 0
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %b = obelisk_sim.time.constant 0
      obelisk_sim.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: obelisk_sim.design @finite
module {
  obelisk_sim.design @finite {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %b = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      obelisk_sim.return
    }
  }
}

// -----

// CHECK: obelisk_sim.design @zero_time_cycle
module {
  obelisk_sim.design @zero_time_cycle {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %b = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^spin
    ^spin:
      %v = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      cf.cond_br %v, ^spin, ^wait
    }
  }
}

// -----

// CHECK: obelisk_sim.design @reentrant
module {
  obelisk_sim.design @reentrant {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %q = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %b = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: obelisk_sim.design @controlled
module {
  obelisk_sim.design @controlled {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.process.control kill %p to ^done
    ^done:
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %b = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: obelisk_sim.design @mixed_waits
module {
  obelisk_sim.design @mixed_waits {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %b = obelisk_sim.time.constant 4
      obelisk_sim.suspend.edge posedge %clock to ^high : !obelisk_sim.ref<i1>
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: obelisk_sim.design @program
module {
  obelisk_sim.design @program {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64, domain = 1 : i32, home_region = 10 : i32} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %b = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: obelisk_sim.design @canonical
module {
  obelisk_sim.design @canonical {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %old = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
  }
}

// -----

// CHECK: obelisk_sim.design @dynamic
module {
  obelisk_sim.design @dynamic {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %p = obelisk_sim.spawn @clock(%ctx, %ref) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %a = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %a to ^low
    ^low:
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clock : i1, !obelisk_sim.ref<i1>
      %v = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %raw = arith.extui %v : i1 to i64
      %b = obelisk_sim.time.scale %raw by 4 signed = false : i64
      obelisk_sim.suspend.delay %b to ^high
    ^high:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
  }
}
