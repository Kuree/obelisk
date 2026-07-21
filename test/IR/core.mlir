// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
//
// Exhaustive round trip of the low-level Obelisk simulation dialect through the
// canonical declarative assembly format. Every operation family is exercised so
// a change to any custom parser, printer, or verifier is caught here.
module {
  //===--------------------------------------------------------------------===//
  // Exact four-state logic
  //===--------------------------------------------------------------------===//

  // CHECK: obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
  %zero = obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
  %xz = obelisk.logic.constant -86 : i8, -1 : i8 : !obelisk.logic<8>

  // CHECK: obelisk.logic.unary negate
  %neg = obelisk.logic.unary negate %zero
      : !obelisk.logic<8> -> !obelisk.logic<8>
  // CHECK: obelisk.logic.binary add
  %sum = obelisk.logic.binary add %zero, %xz
      : (!obelisk.logic<8>, !obelisk.logic<8>) -> !obelisk.logic<8>
  // CHECK: obelisk.logic.compare case_eq
  %eq = obelisk.logic.compare case_eq %zero, %xz
      : (!obelisk.logic<8>, !obelisk.logic<8>) -> i1
  // CHECK: obelisk.logic.compare logical_eq
  %leq = obelisk.logic.compare logical_eq %zero, %xz
      : (!obelisk.logic<8>, !obelisk.logic<8>) -> !obelisk.logic<1>
  // CHECK: obelisk.logic.reduce xor
  %red = obelisk.logic.reduce xor %sum : !obelisk.logic<8> -> !obelisk.logic<1>
  // CHECK: obelisk.logic.concat
  %cat = obelisk.logic.concat %zero, %xz
      : (!obelisk.logic<8>, !obelisk.logic<8>) -> !obelisk.logic<16>
  // CHECK: obelisk.logic.extract
  %part = obelisk.logic.extract %sum from 2
      : !obelisk.logic<8> -> !obelisk.logic<4>
  %idx = arith.constant 3 : i32
  // CHECK: obelisk.logic.dyn_extract
  %dynpart = obelisk.logic.dyn_extract %sum from %idx
      : (!obelisk.logic<8>, i32) -> !obelisk.logic<4>
  // CHECK: obelisk.logic.shift shift_left
  %shl = obelisk.logic.shift shift_left %sum by %sum
      : (!obelisk.logic<8>, !obelisk.logic<8>) -> !obelisk.logic<8>
  // CHECK: obelisk.logic.bool_cast
  %bc = obelisk.logic.bool_cast %sum : !obelisk.logic<8> -> !obelisk.logic<1>
  // CHECK: obelisk.logic.replicate
  %rep = obelisk.logic.replicate %part : !obelisk.logic<4> -> !obelisk.logic<16>
  // CHECK: obelisk.logic.resize
  %rez = obelisk.logic.resize %part signed = true
      : !obelisk.logic<4> -> !obelisk.logic<8>
  // CHECK: obelisk.logic.insert
  %ins = obelisk.logic.insert %part into %sum at 2
      : (!obelisk.logic<8>, !obelisk.logic<4>) -> !obelisk.logic<8>
  // CHECK: obelisk.logic.mux
  %mux = obelisk.logic.mux %bc ? %zero : %xz
      : (!obelisk.logic<1>, !obelisk.logic<8>, !obelisk.logic<8>)
        -> !obelisk.logic<8>
  // CHECK: obelisk.logic.is_known
  %known = obelisk.logic.is_known %sum : !obelisk.logic<8> -> i1
  %packed = arith.constant 0 : i8
  // CHECK: obelisk.logic.from_bits
  %fb = obelisk.logic.from_bits %packed : i8 -> !obelisk.logic<8>
  // CHECK: obelisk.logic.to_bits
  %tb = obelisk.logic.to_bits %sum : !obelisk.logic<8> -> i8

  //===--------------------------------------------------------------------===//
  // Storage, nets, force/release, and nonblocking assignment
  //===--------------------------------------------------------------------===//

  // CHECK: obelisk.global @g
  obelisk.global @g : !obelisk.logic<8> {
    %init = obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
    obelisk.global.return %init : !obelisk.logic<8>
  }
  // CHECK: obelisk.get_global @g
  %gref = obelisk.get_global @g : !obelisk.ref<!obelisk.logic<8>>

  // CHECK: obelisk.var.alloc
  %var = obelisk.var.alloc = %sum : !obelisk.logic<8>
      : !obelisk.ref<!obelisk.logic<8>>
  // CHECK: obelisk.store
  obelisk.store %xz to %var
      : !obelisk.logic<8>, !obelisk.ref<!obelisk.logic<8>>
  // CHECK: obelisk.load
  %loaded = obelisk.load %var
      : !obelisk.ref<!obelisk.logic<8>> -> !obelisk.logic<8>
  // CHECK: obelisk.ref.extract
  %refpart = obelisk.ref.extract %var from 0
      : !obelisk.ref<!obelisk.logic<8>> -> !obelisk.ref<!obelisk.logic<4>>
  // CHECK: obelisk.ref.dyn_extract
  %refdyn = obelisk.ref.dyn_extract %var from %idx
      : (!obelisk.ref<!obelisk.logic<8>>, i32)
        -> !obelisk.ref<!obelisk.logic<1>>
  // CHECK: obelisk.ref.concat
  %refcat = obelisk.ref.concat %var, %var
      : (!obelisk.ref<!obelisk.logic<8>>, !obelisk.ref<!obelisk.logic<8>>)
        -> !obelisk.ref<!obelisk.logic<16>>

  // CHECK: obelisk.net.alloc wire
  %net = obelisk.net.alloc wire : !obelisk.net<!obelisk.logic<8>>
  // CHECK: obelisk.net.read
  %netval = obelisk.net.read %net
      : !obelisk.net<!obelisk.logic<8>> -> !obelisk.logic<8>
  %time = obelisk.time.constant 10 : i64
  // CHECK: obelisk.net.drive
  obelisk.net.drive %net[0] = %loaded after %time
      : (!obelisk.net<!obelisk.logic<8>>, !obelisk.logic<8>,
         !obelisk.time) -> ()
  // CHECK: obelisk.net.extract
  %netpart = obelisk.net.extract %net from 0
      : !obelisk.net<!obelisk.logic<8>> -> !obelisk.net<!obelisk.logic<4>>
  // CHECK: obelisk.net.dyn_extract
  %netdyn = obelisk.net.dyn_extract %net from %idx
      : (!obelisk.net<!obelisk.logic<8>>, i32)
        -> !obelisk.net<!obelisk.logic<1>>
  // CHECK: obelisk.net.concat
  %netcat = obelisk.net.concat %net, %net
      : (!obelisk.net<!obelisk.logic<8>>, !obelisk.net<!obelisk.logic<8>>)
        -> !obelisk.net<!obelisk.logic<16>>

  // CHECK: obelisk.force
  obelisk.force %var = %sum
      : !obelisk.ref<!obelisk.logic<8>>, !obelisk.logic<8>
  // CHECK: obelisk.release
  obelisk.release %var : !obelisk.ref<!obelisk.logic<8>>
  // CHECK: obelisk.nba.enqueue
  obelisk.nba.enqueue %sum to %var after %time
      : (!obelisk.logic<8>, !obelisk.ref<!obelisk.logic<8>>, !obelisk.time)
        -> ()

  //===--------------------------------------------------------------------===//
  // Time, scheduler regions, processes, and suspension
  //===--------------------------------------------------------------------===//

  // CHECK: obelisk.time.add
  %time2 = obelisk.time.add %time, %time
  // CHECK: obelisk.process @proc
  obelisk.process @proc always in active {
    obelisk.process.return
  }
  // CHECK: obelisk.process.spawn
  %child = obelisk.process.spawn @proc() in active : () -> !obelisk.process
  // CHECK: obelisk.process.join all
  obelisk.process.join all %child : !obelisk.process
  // CHECK: obelisk.process.kill
  obelisk.process.kill %child
  // CHECK: obelisk.region.enqueue
  obelisk.region.enqueue %child in nba after %time
  // CHECK: obelisk.suspend.delay
  obelisk.suspend.delay %time resume active
  %event = obelisk.event.create
  // CHECK: obelisk.suspend.event
  obelisk.suspend.event %event resume active
  // CHECK: obelisk.suspend.edge posedge
  obelisk.suspend.edge posedge %var resume active
      : !obelisk.ref<!obelisk.logic<8>>
  // CHECK: obelisk.suspend.condition
  obelisk.suspend.condition %bc resume active : !obelisk.logic<1>
  // CHECK: obelisk.event.trigger
  obelisk.event.trigger %event in active nonblocking = false

  //===--------------------------------------------------------------------===//
  // Classes, dynamic dispatch, and ownership
  //===--------------------------------------------------------------------===//

  // CHECK: obelisk.class @Base
  obelisk.class @Base {
    obelisk.class.end
  }
  // CHECK: obelisk.class @Derived extends @Base
  obelisk.class @Derived extends @Base {
    // CHECK: obelisk.class.field @field
    obelisk.class.field @field at 0 : !obelisk.logic<8>
    // CHECK: obelisk.class.method @method
    obelisk.class.method @method slot 0 virtual = true : (i32) -> i32 {
      obelisk.class.method_return
    }
    obelisk.class.end
  }
  // CHECK: obelisk.object.null
  %null = obelisk.object.null
  // CHECK: obelisk.object.new
  %obj = obelisk.object.new @Derived() : () -> !obelisk.object
  // CHECK: obelisk.object.retain
  obelisk.object.retain %obj
  // CHECK: obelisk.object.field_ref
  %fref = obelisk.object.field_ref %obj[@field]
      : !obelisk.ref<!obelisk.logic<8>>
  // CHECK: obelisk.object.call
  %callret = obelisk.object.call %obj[@method](%idx)
      : (!obelisk.object, i32) -> i32
  // CHECK: obelisk.object.release
  obelisk.object.release %obj
  // CHECK: obelisk.box
  %boxed = obelisk.box %sum : !obelisk.logic<8> -> !obelisk.any
  // CHECK: obelisk.unbox
  %unboxed = obelisk.unbox %boxed : !obelisk.any -> !obelisk.logic<8>

  //===--------------------------------------------------------------------===//
  // Dynamic containers and synchronization
  //===--------------------------------------------------------------------===//

  %size = arith.constant 4 : i64
  // CHECK: obelisk.dynarray.create
  %dyn = obelisk.dynarray.create %size default %sum
      : (i64, !obelisk.logic<8>) -> !obelisk.dynarray<!obelisk.logic<8>>
  // CHECK: obelisk.dynarray.size
  %dsize = obelisk.dynarray.size %dyn : !obelisk.dynarray<!obelisk.logic<8>>
  // CHECK: obelisk.dynarray.resize
  obelisk.dynarray.resize %dyn to %size
      : (!obelisk.dynarray<!obelisk.logic<8>>, i64) -> ()
  // CHECK: obelisk.dynarray.element_ref
  %delem = obelisk.dynarray.element_ref %dyn[%size]
      : (!obelisk.dynarray<!obelisk.logic<8>>, i64)
        -> !obelisk.ref<!obelisk.logic<8>>

  // CHECK: obelisk.assoc.create
  %assoc = obelisk.assoc.create : !obelisk.assoc<i64, !obelisk.logic<8>, false>
  %akey = arith.constant 1 : i64
  // CHECK: obelisk.assoc.element_ref
  %aelem = obelisk.assoc.element_ref %assoc[%akey]
      : (!obelisk.assoc<i64, !obelisk.logic<8>, false>, i64)
        -> !obelisk.ref<!obelisk.logic<8>>
  // CHECK: obelisk.assoc.exists
  %aexists = obelisk.assoc.exists %assoc[%akey]
      : !obelisk.assoc<i64, !obelisk.logic<8>, false>, i64
  // CHECK: obelisk.assoc.delete
  obelisk.assoc.delete %assoc[%akey]
      : (!obelisk.assoc<i64, !obelisk.logic<8>, false>, i64) -> ()

  // CHECK: obelisk.mailbox.create
  %mbox = obelisk.mailbox.create bound 4 : !obelisk.mailbox<!obelisk.logic<8>>
  // CHECK: obelisk.mailbox.put
  obelisk.mailbox.put %sum into %mbox
      : !obelisk.logic<8>, !obelisk.mailbox<!obelisk.logic<8>>
  // CHECK: obelisk.mailbox.get
  %got = obelisk.mailbox.get %mbox
      : !obelisk.mailbox<!obelisk.logic<8>> -> !obelisk.logic<8>
  // CHECK: obelisk.mailbox.try_get
  %tgok, %tgval = obelisk.mailbox.try_get %mbox
      : (!obelisk.mailbox<!obelisk.logic<8>>) -> (i1, !obelisk.logic<8>)

  %keys = arith.constant 1 : i64
  // CHECK: obelisk.semaphore.create
  %sem = obelisk.semaphore.create %keys
  // CHECK: obelisk.semaphore.get
  obelisk.semaphore.get %keys from %sem
  // CHECK: obelisk.semaphore.try_get
  %semok = obelisk.semaphore.try_get %keys from %sem
  // CHECK: obelisk.semaphore.put
  obelisk.semaphore.put %keys to %sem

  //===--------------------------------------------------------------------===//
  // Deterministic randomization and constraints
  //===--------------------------------------------------------------------===//

  %seed = arith.constant 42 : i64
  // CHECK: obelisk.random.stream_create
  %rng = obelisk.random.stream_create %seed
  %streamId = arith.constant 1 : i64
  // CHECK: obelisk.random.stream_split
  %child_rng = obelisk.random.stream_split %rng by %streamId
  // CHECK: obelisk.random.next
  %rand = obelisk.random.next %rng : i64
  // CHECK: obelisk.random.save
  %rstate = obelisk.random.save %rng
  // CHECK: obelisk.random.restore
  obelisk.random.restore %rng from %rstate
  // CHECK: obelisk.randc.create
  %randc = obelisk.randc.create %rng : !obelisk.randc_state<!obelisk.logic<8>>
  // CHECK: obelisk.randc.next
  %rcnext = obelisk.randc.next %randc
      : !obelisk.randc_state<!obelisk.logic<8>> -> !obelisk.logic<8>

  // CHECK: obelisk.constraint.set @cset
  obelisk.constraint.set @cset {
    %pred = arith.constant true
    obelisk.constraint.require %pred
    obelisk.constraint.end
  }
  // CHECK: obelisk.constraint.get @cset
  %cs = obelisk.constraint.get @cset
  // CHECK: obelisk.randomize
  %rzok = obelisk.randomize %obj using %rng with %cs

  //===--------------------------------------------------------------------===//
  // Assertions, coverage, foreign interfaces, and system effects
  //===--------------------------------------------------------------------===//

  // CHECK: obelisk.assert.immediate error
  obelisk.assert.immediate error %bc, "assertion failed"
      : !obelisk.logic<1>
  // CHECK: obelisk.assert.sample
  obelisk.assert.sample @prop in observed(%sum) : !obelisk.logic<8>
  // CHECK: obelisk.cover.sample
  obelisk.cover.sample @cp(%sum) : !obelisk.logic<8>
  // CHECK: obelisk.dpi.task_call
  %dpi = obelisk.dpi.task_call @foreign(%idx) : (i32) -> i32
  // CHECK: obelisk.system.call
  %sys = obelisk.system.call "$time"() : () -> i64
  // CHECK: obelisk.diagnostic info
  obelisk.diagnostic info "hello %0d" (%idx) : i32
  // CHECK: obelisk.finish
  obelisk.finish 1
  // CHECK: obelisk.stop
  obelisk.stop 0
  // CHECK: obelisk.trace "top.value"
  obelisk.trace "top.value" = %loaded at %time : !obelisk.logic<8>
}
