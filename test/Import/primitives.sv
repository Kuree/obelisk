// RUN: obelisk -emit-slang %s 2>/dev/null | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s 2>/dev/null | FileCheck %s --check-prefix=OBELISK

`timescale 1ns / 1ps
module primitive_import(input wire a, b, control,
                        output wire gate_out, tri_out,
                        delayed_out, strength_out);
  and (gate_out, a, b);
  bufif0 (tri_out, a, control);
  bufif0 #5 (delayed_out, a, control);
  bufif0 (strong1, pull0) (strength_out, a, control);
endmodule

// SLANG-DAG: slang.symbol.primitive_instance attributes {{.*}}primitive_name = "and"{{.*}}time_precision_fs = 1000 : i64{{.*}}time_unit_fs = 1000000 : i64
// SLANG-DAG: slang.symbol.primitive_instance attributes {{.*}}primitive_name = "bufif0"{{.*}}time_precision_fs = 1000 : i64{{.*}}time_unit_fs = 1000000 : i64
// OBELISK-DAG: obelisk.sv.symbol.primitive_instance attributes {{.*}}primitive_name = "and"{{.*}}time_precision_fs = 1000 : i64{{.*}}time_unit_fs = 1000000 : i64
// OBELISK-DAG: obelisk.sv.symbol.primitive_instance attributes {{.*}}primitive_name = "bufif0"{{.*}}time_precision_fs = 1000 : i64{{.*}}time_unit_fs = 1000000 : i64
// SLANG-DAG: slang.symbol.primitive_instance attributes {{.*}}primitive_name = "bufif0"{{.*}}unsupported_delay = "#5 "
// SLANG-DAG: slang.symbol.primitive_instance attributes {{.*}}primitive_name = "bufif0"{{.*}}unsupported_strength = "Pull,Strong"
// OBELISK-DAG: obelisk.sv.symbol.primitive_instance attributes {{.*}}primitive_name = "bufif0"{{.*}}unsupported_delay = "#5 "
// OBELISK-DAG: obelisk.sv.symbol.primitive_instance attributes {{.*}}primitive_name = "bufif0"{{.*}}unsupported_strength = "Pull,Strong"
