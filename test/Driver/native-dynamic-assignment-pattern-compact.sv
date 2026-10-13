// RUN: obelisk -O3 --top=top %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --top=top --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -
// RUN: obelisk -O0 --top=top -emit-sim %s -o - | FileCheck %s --check-prefix=SIM

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
// SIM-COUNT-3: simulation.container.write
// SIM: simulation.aggregate.splat
// CHECK: compact dynamic and fixed patterns passed
