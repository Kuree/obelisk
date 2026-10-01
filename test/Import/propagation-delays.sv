// RUN: obelisk -emit-slang %s 2>/dev/null | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s 2>/dev/null | FileCheck %s --check-prefix=OBELISK

`timescale 10ns / 1ps
module propagation_delays(input wire a, control, output wire x, y, z);
  assign #(1.25, 2.5, 3.75) x = a;
  assign #(1ns) y = a;
  wire #(4, 5) declared = a;
  wire #6 delayed_net;
  bufif1 #(2, 3, 4) (z, a, control);
endmodule

// Delay constants are frozen after lexical timeprecision rounding. Explicit
// time literals retain their own unit instead of using the surrounding 10ns.
// SLANG-DAG: slang.symbol.continuous_assign @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 12500000, 25000000, 37500000>
// SLANG-DAG: slang.symbol.continuous_assign @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 1000000>
// SLANG-DAG: slang.symbol.net @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 40000000, 50000000>{{.*}}name = "declared"
// SLANG-DAG: slang.symbol.net @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 60000000>{{.*}}name = "delayed_net"
// SLANG-DAG: slang.symbol.primitive_instance @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 20000000, 30000000, 40000000>{{.*}}primitive_name = "bufif1"
// OBELISK-DAG: obelisk.sv.symbol.continuous_assign @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 12500000, 25000000, 37500000>
// OBELISK-DAG: obelisk.sv.symbol.continuous_assign @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 1000000>
// OBELISK-DAG: obelisk.sv.symbol.net @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 40000000, 50000000>{{.*}}name = "declared"
// OBELISK-DAG: obelisk.sv.symbol.net @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 60000000>{{.*}}name = "delayed_net"
// OBELISK-DAG: obelisk.sv.symbol.primitive_instance @{{[^ ]+}} attributes {{.*}}delay_fs = array<i64: 20000000, 30000000, 40000000>{{.*}}primitive_name = "bufif1"
