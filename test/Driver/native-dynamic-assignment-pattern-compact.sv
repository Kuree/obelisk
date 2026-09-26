// RUN: obelisk -O0 --top=top -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 --top=top %s -o %t.o0.native
// RUN: obelisk -O0 --execution-tier=bytecode --top=top %s -o %t.o0.bytecode
// RUN: obelisk -O3 --top=top %s -o %t.o3.native
// RUN: obelisk -O3 --execution-tier=bytecode --top=top %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module top;
  int values[];
  int fixed_values[4096];
  initial begin
    // IEEE 1800-2017 10.9.1: a large dynamic-array replication stays one
    // counted initialization loop in compiler IR instead of one operation per
    // result element.
    values = '{65536{23}};
    assert (values.size() == 65536 && values[0] == 23 &&
            values[65535] == 23);
    values = '{65535: 9, default: 4};
    assert (values.size() == 65536 && values[0] == 4 &&
            values[65534] == 4 && values[65535] == 9);
    // A homogeneous fixed-array fill likewise remains one aggregate splat;
    // native lowering expands it logarithmically and bytecode uses one
    // replication instruction.
    fixed_values = '{int: 17};
    assert (fixed_values[0] == 17 && fixed_values[4095] == 17);
    $display("compact dynamic and fixed patterns passed");
  end
endmodule

// SIM: cf.cond_br
// One write in the replication loop, one in the default loop, and one
// explicit keyed overwrite: none scale with the 65,536-element result size.
// SIM-COUNT-3: obelisk_sim.container.write
// SIM: obelisk_sim.aggregate.splat
// CHECK: compact dynamic and fixed patterns passed
