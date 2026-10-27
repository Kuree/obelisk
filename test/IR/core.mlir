// RUN: talos-opt %s | talos-opt | FileCheck %s
//
// Exhaustive round trip of the low-level Talos simulation dialect through the
// canonical declarative assembly format. Every operation family is exercised so
// a change to any custom parser, printer, or verifier is caught here.
module {
  //===--------------------------------------------------------------------===//
  // Exact four-state logic
  //===--------------------------------------------------------------------===//

  // CHECK: talos.logic.constant 0 : i8, 0 : i8 : !talos.logic<8>
  %zero = talos.logic.constant 0 : i8, 0 : i8 : !talos.logic<8>
  %xz = talos.logic.constant -86 : i8, -1 : i8 : !talos.logic<8>

  // CHECK: talos.logic.unary negate
  %neg = talos.logic.unary negate %zero
      : !talos.logic<8> -> !talos.logic<8>
  // CHECK: talos.logic.binary add
  %sum = talos.logic.binary add %zero, %xz
      : (!talos.logic<8>, !talos.logic<8>) -> !talos.logic<8>
  // CHECK: talos.logic.compare case_eq
  %eq = talos.logic.compare case_eq %zero, %xz
      : (!talos.logic<8>, !talos.logic<8>) -> i1
  // CHECK: talos.logic.compare logical_eq
  %leq = talos.logic.compare logical_eq %zero, %xz
      : (!talos.logic<8>, !talos.logic<8>) -> !talos.logic<1>
  // CHECK: talos.logic.reduce xor
  %red = talos.logic.reduce xor %sum : !talos.logic<8> -> !talos.logic<1>
  // CHECK: talos.logic.concat
  %cat = talos.logic.concat %zero, %xz
      : (!talos.logic<8>, !talos.logic<8>) -> !talos.logic<16>
  // CHECK: talos.logic.extract
  %part = talos.logic.extract %sum from 2
      : !talos.logic<8> -> !talos.logic<4>
  %idx = arith.constant 3 : i32
  // CHECK: talos.logic.dyn_extract
  %dynpart = talos.logic.dyn_extract %sum from %idx
      : (!talos.logic<8>, i32) -> !talos.logic<4>
  // CHECK: talos.logic.shift shift_left
  %shl = talos.logic.shift shift_left %sum by %sum
      : (!talos.logic<8>, !talos.logic<8>) -> !talos.logic<8>
  // CHECK: talos.logic.bool_cast
  %bc = talos.logic.bool_cast %sum : !talos.logic<8> -> !talos.logic<1>
  // CHECK: talos.logic.replicate
  %rep = talos.logic.replicate %part : !talos.logic<4> -> !talos.logic<16>
  // CHECK: talos.logic.resize
  %rez = talos.logic.resize %part signed = true
      : !talos.logic<4> -> !talos.logic<8>
  // CHECK: talos.logic.insert
  %ins = talos.logic.insert %part into %sum at 2
      : (!talos.logic<8>, !talos.logic<4>) -> !talos.logic<8>
  // CHECK: talos.logic.mux
  %mux = talos.logic.mux %bc ? %zero : %xz
      : (!talos.logic<1>, !talos.logic<8>, !talos.logic<8>)
        -> !talos.logic<8>
  // CHECK: talos.logic.is_known
  %known = talos.logic.is_known %sum : !talos.logic<8> -> i1
  %packed = arith.constant 0 : i8
  // CHECK: talos.logic.from_bits
  %fb = talos.logic.from_bits %packed : i8 -> !talos.logic<8>
  // CHECK: talos.logic.to_bits
  %tb = talos.logic.to_bits %sum : !talos.logic<8> -> i8

  //===--------------------------------------------------------------------===//
  // Storage, nets, force/release, and nonblocking assignment
  //===--------------------------------------------------------------------===//

  // CHECK: talos.global @g
  talos.global @g : !talos.logic<8> {
    %init = talos.logic.constant 0 : i8, 0 : i8 : !talos.logic<8>
    talos.global.return %init : !talos.logic<8>
  }
  // CHECK: talos.get_global @g
  %gref = talos.get_global @g : !talos.ref<!talos.logic<8>>

  // CHECK: talos.var.alloc
  %var = talos.var.alloc = %sum : !talos.logic<8>
      : !talos.ref<!talos.logic<8>>
  // CHECK: talos.store
  talos.store %xz to %var
      : !talos.logic<8>, !talos.ref<!talos.logic<8>>
  // CHECK: talos.load
  %loaded = talos.load %var
      : !talos.ref<!talos.logic<8>> -> !talos.logic<8>
  // CHECK: talos.ref.extract
  %refpart = talos.ref.extract %var from 0
      : !talos.ref<!talos.logic<8>> -> !talos.ref<!talos.logic<4>>
  // CHECK: talos.ref.dyn_extract
  %refdyn = talos.ref.dyn_extract %var from %idx
      : (!talos.ref<!talos.logic<8>>, i32)
        -> !talos.ref<!talos.logic<1>>
  // CHECK: talos.ref.concat
  %refcat = talos.ref.concat %var, %var
      : (!talos.ref<!talos.logic<8>>, !talos.ref<!talos.logic<8>>)
        -> !talos.ref<!talos.logic<16>>

  // CHECK: talos.net.alloc wire
  %net = talos.net.alloc wire : !talos.net<!talos.logic<8>>
  // CHECK: talos.net.read
  %netval = talos.net.read %net
      : !talos.net<!talos.logic<8>> -> !talos.logic<8>
  %time = talos.time.constant 10 : i64
  // CHECK: talos.net.drive
  talos.net.drive %net[0] = %loaded after %time
      : (!talos.net<!talos.logic<8>>, !talos.logic<8>,
         !talos.time) -> ()
  // CHECK: talos.net.extract
  %netpart = talos.net.extract %net from 0
      : !talos.net<!talos.logic<8>> -> !talos.net<!talos.logic<4>>
  // CHECK: talos.net.dyn_extract
  %netdyn = talos.net.dyn_extract %net from %idx
      : (!talos.net<!talos.logic<8>>, i32)
        -> !talos.net<!talos.logic<1>>
  // CHECK: talos.net.concat
  %netcat = talos.net.concat %net, %net
      : (!talos.net<!talos.logic<8>>, !talos.net<!talos.logic<8>>)
        -> !talos.net<!talos.logic<16>>

  // CHECK: talos.force
  talos.force %var = %sum
      : !talos.ref<!talos.logic<8>>, !talos.logic<8>
  // CHECK: talos.release
  talos.release %var : !talos.ref<!talos.logic<8>>
  // CHECK: talos.nba.enqueue
  talos.nba.enqueue %sum to %var after %time
      : (!talos.logic<8>, !talos.ref<!talos.logic<8>>, !talos.time)
        -> ()

  //===--------------------------------------------------------------------===//
  // Time, scheduler regions, processes, and suspension
  //===--------------------------------------------------------------------===//

  // CHECK: talos.time.add
  %time2 = talos.time.add %time, %time
  // CHECK: talos.process @proc
  talos.process @proc always in active {
    talos.process.return
  }
  // CHECK: talos.process.spawn
  %child = talos.process.spawn @proc() in active : () -> !talos.process
  // CHECK: talos.process.join all
  talos.process.join all %child : !talos.process
  // CHECK: talos.process.kill
  talos.process.kill %child
  // CHECK: talos.region.enqueue
  talos.region.enqueue %child in nba after %time
  // CHECK: talos.suspend.delay
  talos.suspend.delay %time resume active
  %event = talos.event.create
  // CHECK: talos.suspend.event
  talos.suspend.event %event resume active
  // CHECK: talos.suspend.edge posedge
  talos.suspend.edge posedge %var resume active
      : !talos.ref<!talos.logic<8>>
  // CHECK: talos.suspend.condition
  talos.suspend.condition %bc resume active : !talos.logic<1>
  // CHECK: talos.event.trigger
  talos.event.trigger %event in active nonblocking = false

  //===--------------------------------------------------------------------===//
  // Classes, dynamic dispatch, and ownership
  //===--------------------------------------------------------------------===//

  // CHECK: talos.class @Base
  talos.class @Base {
    talos.class.end
  }
  // CHECK: talos.class @Derived extends @Base
  talos.class @Derived extends @Base {
    // CHECK: talos.class.field @field
    talos.class.field @field at 0 : !talos.logic<8>
    // CHECK: talos.class.method @method
    talos.class.method @method slot 0 virtual = true : (i32) -> i32 {
      talos.class.method_return
    }
    talos.class.end
  }
  // CHECK: talos.object.null
  %null = talos.object.null
  // CHECK: talos.object.new
  %obj = talos.object.new @Derived() : () -> !talos.object
  // CHECK: talos.object.retain
  talos.object.retain %obj
  // CHECK: talos.object.field_ref
  %fref = talos.object.field_ref %obj[@field]
      : !talos.ref<!talos.logic<8>>
  // CHECK: talos.object.call
  %callret = talos.object.call %obj[@method](%idx)
      : (!talos.object, i32) -> i32
  // CHECK: talos.object.release
  talos.object.release %obj
  // CHECK: talos.box
  %boxed = talos.box %sum : !talos.logic<8> -> !talos.any
  // CHECK: talos.unbox
  %unboxed = talos.unbox %boxed : !talos.any -> !talos.logic<8>

  //===--------------------------------------------------------------------===//
  // Dynamic containers and synchronization
  //===--------------------------------------------------------------------===//

  %size = arith.constant 4 : i64
  // CHECK: talos.dynarray.create
  %dyn = talos.dynarray.create %size default %sum
      : (i64, !talos.logic<8>) -> !talos.dynarray<!talos.logic<8>>
  // CHECK: talos.dynarray.size
  %dsize = talos.dynarray.size %dyn : !talos.dynarray<!talos.logic<8>>
  // CHECK: talos.dynarray.resize
  talos.dynarray.resize %dyn to %size
      : (!talos.dynarray<!talos.logic<8>>, i64) -> ()
  // CHECK: talos.dynarray.element_ref
  %delem = talos.dynarray.element_ref %dyn[%size]
      : (!talos.dynarray<!talos.logic<8>>, i64)
        -> !talos.ref<!talos.logic<8>>

  // CHECK: talos.assoc.create
  %assoc = talos.assoc.create : !talos.assoc<i64, !talos.logic<8>, false>
  %akey = arith.constant 1 : i64
  // CHECK: talos.assoc.element_ref
  %aelem = talos.assoc.element_ref %assoc[%akey]
      : (!talos.assoc<i64, !talos.logic<8>, false>, i64)
        -> !talos.ref<!talos.logic<8>>
  // CHECK: talos.assoc.exists
  %aexists = talos.assoc.exists %assoc[%akey]
      : !talos.assoc<i64, !talos.logic<8>, false>, i64
  // CHECK: talos.assoc.delete
  talos.assoc.delete %assoc[%akey]
      : (!talos.assoc<i64, !talos.logic<8>, false>, i64) -> ()

  // CHECK: talos.mailbox.create
  %mbox = talos.mailbox.create bound 4 : !talos.mailbox<!talos.logic<8>>
  // CHECK: talos.mailbox.put
  talos.mailbox.put %sum into %mbox
      : !talos.logic<8>, !talos.mailbox<!talos.logic<8>>
  // CHECK: talos.mailbox.get
  %got = talos.mailbox.get %mbox
      : !talos.mailbox<!talos.logic<8>> -> !talos.logic<8>
  // CHECK: talos.mailbox.try_get
  %tgok, %tgval = talos.mailbox.try_get %mbox
      : (!talos.mailbox<!talos.logic<8>>) -> (i1, !talos.logic<8>)

  %keys = arith.constant 1 : i64
  // CHECK: talos.semaphore.create
  %sem = talos.semaphore.create %keys
  // CHECK: talos.semaphore.get
  talos.semaphore.get %keys from %sem
  // CHECK: talos.semaphore.try_get
  %semok = talos.semaphore.try_get %keys from %sem
  // CHECK: talos.semaphore.put
  talos.semaphore.put %keys to %sem

  //===--------------------------------------------------------------------===//
  // Deterministic randomization and constraints
  //===--------------------------------------------------------------------===//

  %seed = arith.constant 42 : i64
  // CHECK: talos.random.stream_create
  %rng = talos.random.stream_create %seed
  %streamId = arith.constant 1 : i64
  // CHECK: talos.random.stream_split
  %child_rng = talos.random.stream_split %rng by %streamId
  // CHECK: talos.random.next
  %rand = talos.random.next %rng : i64
  // CHECK: talos.random.save
  %rstate = talos.random.save %rng
  // CHECK: talos.random.restore
  talos.random.restore %rng from %rstate
  // CHECK: talos.randc.create
  %randc = talos.randc.create %rng : !talos.randc_state<!talos.logic<8>>
  // CHECK: talos.randc.next
  %rcnext = talos.randc.next %randc
      : !talos.randc_state<!talos.logic<8>> -> !talos.logic<8>

  // CHECK: talos.constraint.set @cset
  talos.constraint.set @cset {
    %pred = arith.constant true
    talos.constraint.require %pred
    talos.constraint.end
  }
  // CHECK: talos.constraint.get @cset
  %cs = talos.constraint.get @cset
  // CHECK: talos.randomize
  %rzok = talos.randomize %obj using %rng with %cs

  //===--------------------------------------------------------------------===//
  // Assertions, coverage, foreign interfaces, and system effects
  //===--------------------------------------------------------------------===//

  // CHECK: talos.assert.immediate error
  talos.assert.immediate error %bc, "assertion failed"
      : !talos.logic<1>
  // CHECK: talos.assert.sample
  talos.assert.sample @prop in observed(%sum) : !talos.logic<8>
  // CHECK: talos.cover.sample
  talos.cover.sample @cp(%sum) : !talos.logic<8>
  // CHECK: talos.dpi.task_call
  %dpi = talos.dpi.task_call @foreign(%idx) : (i32) -> i32
  // CHECK: talos.system.call
  %sys = talos.system.call "$time"() : () -> i64
  // CHECK: talos.diagnostic info
  talos.diagnostic info "hello %0d" (%idx) : i32
  // CHECK: talos.finish
  talos.finish 1
  // CHECK: talos.stop
  talos.stop 0
  // CHECK: talos.trace "top.value"
  talos.trace "top.value" = %loaded at %time : !talos.logic<8>
}
