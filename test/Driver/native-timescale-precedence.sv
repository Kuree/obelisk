// RUN: obelisk -fno-lto -O3 --top=native_timescale_precedence \
// RUN:   --timescale=1us/100ns %S/Inputs/timescale-directive.sv \
// RUN:   %S/Inputs/timescale-default.sv %s -o %t.separate.native
// RUN: %t.separate.native | FileCheck %s --check-prefix=SEPARATE
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode \
// RUN:   --top=native_timescale_precedence --timescale=1us/100ns \
// RUN:   %S/Inputs/timescale-directive.sv %S/Inputs/timescale-default.sv \
// RUN:   %s -o %t.separate.bytecode
// RUN: %t.separate.bytecode | FileCheck %s --check-prefix=SEPARATE
// RUN: obelisk -fno-lto -O3 --single-unit \
// RUN:   --top=native_timescale_precedence --timescale=1us/100ns \
// RUN:   %S/Inputs/timescale-directive.sv %S/Inputs/timescale-default.sv \
// RUN:   %s -o %t.single.native
// RUN: %t.single.native | FileCheck %s --check-prefix=SINGLE
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode --single-unit \
// RUN:   --top=native_timescale_precedence --timescale=1us/100ns \
// RUN:   %S/Inputs/timescale-directive.sv %S/Inputs/timescale-default.sv \
// RUN:   %s -o %t.single.bytecode
// RUN: %t.single.bytecode | FileCheck %s --check-prefix=SINGLE

// IEEE 1800-2017 3.14.2.3 and 22.7: a directive affects following design
// elements in its compilation unit, while the command-line scale supplies the
// default for a separate unit that has no declaration or directive.

module native_timescale_precedence;
  timeunit 1ns/1ns;
  bit directive_done;
  bit default_done;
  directive_delay directive_i(directive_done);
  default_delay default_i(default_done);
  initial begin
    #11 $display("at11 directive=%b default=%b", directive_done, default_done);
    #989 $display("at1000 directive=%b default=%b", directive_done, default_done);
  end
endmodule

// SEPARATE: at11 directive=1 default=0
// SEPARATE-NEXT: at1000 directive=1 default=1

// SINGLE: at11 directive=1 default=1
// SINGLE-NEXT: at1000 directive=1 default=1
