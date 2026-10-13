// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

//
// CHECK: !simulation.mailbox<!simulation.string>
// CHECK: simulation.storage.decl {{.*}} : !simulation.semaphore
// CHECK: simulation.mailbox.create
// CHECK: simulation.mailbox.try_put
// CHECK: simulation.suspend.mailbox {{.*}} not_full
// CHECK: simulation.mailbox.try_peek
// CHECK: simulation.suspend.mailbox {{.*}} not_empty
// CHECK: simulation.mailbox.try_get
// CHECK: simulation.suspend.mailbox {{.*}} not_empty
// CHECK: simulation.mailbox.try_put
// CHECK: simulation.mailbox.try_peek
// CHECK: simulation.mailbox.try_get
// CHECK: simulation.mailbox.num
// CHECK: simulation.semaphore.create
// CHECK: simulation.semaphore.put
// CHECK: simulation.semaphore.try_get
// CHECK: simulation.suspend.semaphore

module top;
  mailbox #(string) m;
  semaphore sem;
  initial begin
    static string input_value = "abc";
    static string peeked;
    static string received;
    static int ok;
    m = new(1);
    m.put(input_value);
    m.peek(peeked);
    m.get(received);
    ok = m.try_put(input_value);
    ok = m.try_peek(peeked);
    ok = m.try_get(received);
    ok = m.num();
    sem = new(0);
    sem.put(2);
    void'(sem.try_get(1));
    sem.get(1);
  end
endmodule
