// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=INIT

// IEEE 1800-2023 18.14.1: every module, interface, program, and package
// initialization RNG starts with the same default seed. Each scope initializes
// a random scalar and class object, then exercises the optional $urandom(seed)
// argument before its static processes are seeded from that stream.
// An additional process in a must leave b's first process unchanged.
// The Final process observes completed static threads in all event regions.
// RUNTIME: scope initializers 1 1 1 1 1
// RUNTIME-NEXT: static threads 1 1 1
// RUNTIME-NEXT: static objects 1 1 1 1 1
// RUNTIME-NEXT: object solves 1
// RUNTIME-NEXT: seeded initializers 1 1 1 1 1

// INIT-LABEL: simulation.func @__obelisk_root
// INIT: %[[P:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 4 : i64}
// INIT-NEXT: simulation.call
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[P]]
// INIT: %[[PO:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 4 : i64}
// INIT-NEXT: simulation.call
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[PO]]
// INIT: %[[Q:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 5 : i64}
// INIT-NEXT: simulation.call
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[Q]]
// INIT: %[[QO:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 5 : i64}
// INIT-NEXT: simulation.call
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[QO]]
// INIT: simulation.random.initialization_enter {{.*}} {scope = 7 : i64}
// INIT-NEXT: simulation.call
// INIT: simulation.random.initialization_enter {{.*}} {scope = 8 : i64}
// INIT-NEXT: simulation.call
// INIT: simulation.random.initialization_enter {{.*}} {scope = 9 : i64}
// INIT-NEXT: simulation.call
// INIT: simulation.random.initialization_enter {{.*}} {scope = 10 : i64}
// INIT-NEXT: simulation.call
// INIT: %[[A:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 7 : i64}
// INIT-NEXT: {{.*}}simulation.spawn
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[A]]
// INIT: %[[AX:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 7 : i64}
// INIT-NEXT: {{.*}}simulation.spawn
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[AX]]
// INIT: %[[B:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 8 : i64}
// INIT-NEXT: {{.*}}simulation.spawn
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[B]]
// INIT: %[[I:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 9 : i64}
// INIT-NEXT: {{.*}}simulation.spawn
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[I]]
// INIT: %[[G:[a-zA-Z0-9_]+]] = simulation.random.initialization_enter {{.*}} {scope = 10 : i64}
// INIT-NEXT: {{.*}}simulation.spawn
// INIT-NEXT: simulation.random.initialization_leave {{.*}}, %[[G]]

package cp;
  class C;
    rand int unsigned value;
  endclass
endpackage

package p;
  import cp::*;
  int unsigned initialized = $urandom();
  C object = new;
  int unsigned seeded = $urandom(99);
endpackage

package q;
  import cp::*;
  int unsigned initialized = $urandom();
  C object = new;
  int unsigned seeded = $urandom(99);
endpackage

module definition_a;
  import cp::*;
  int unsigned initialized = $urandom();
  C object = new;
  int unsigned seeded = $urandom(99);
  int unsigned thread;
  initial thread = $urandom();
  int unsigned extra;
  initial extra = $urandom();
endmodule

module definition_b;
  import cp::*;
  int unsigned initialized = $urandom();
  C object = new;
  int unsigned seeded = $urandom(99);
  int unsigned thread;
  initial thread = $urandom();
endmodule

interface definition_i;
  import cp::*;
  int unsigned initialized = $urandom();
  C object = new;
  int unsigned seeded = $urandom(99);
  int unsigned thread;
  initial thread = $urandom();
endinterface

program definition_g;
  import cp::*;
  int unsigned initialized = $urandom();
  C object = new;
  int unsigned seeded = $urandom(99);
  int unsigned thread;
  initial thread = $urandom();
endprogram

program definition_reporter;
  int succeeded = 1;
  final begin
    succeeded &= top.a.object.randomize();
    succeeded &= top.b.object.randomize();
    succeeded &= top.i.object.randomize();
    succeeded &= top.g.object.randomize();
    succeeded &= p::object.randomize();
    succeeded &= q::object.randomize();
    $display("scope initializers %0d %0d %0d %0d %0d",
             p::initialized == q::initialized,
             top.a.initialized == top.b.initialized,
             top.a.initialized == top.i.initialized,
             top.a.initialized == top.g.initialized,
             top.a.initialized == p::initialized);
    $display("static threads %0d %0d %0d", top.a.thread == top.b.thread,
             top.a.thread == top.i.thread, top.a.thread == top.g.thread);
    $display("static objects %0d %0d %0d %0d %0d",
             top.a.object.value == top.b.object.value,
             top.a.object.value == top.i.object.value,
             top.a.object.value == top.g.object.value,
             top.a.object.value == p::object.value,
             p::object.value == q::object.value);
    $display("object solves %0d", succeeded == 1);
    $display("seeded initializers %0d %0d %0d %0d %0d",
             top.a.seeded == top.b.seeded, top.a.seeded == top.i.seeded,
             top.a.seeded == top.g.seeded, top.a.seeded == p::seeded,
             p::seeded == q::seeded);
  end
endprogram

module top;
  definition_a a();
  definition_b b();
  definition_i i();
  definition_g g();
  definition_reporter reporter();
endmodule
