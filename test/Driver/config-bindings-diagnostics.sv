// Configuration diagnostics are produced before a report can describe an
// invalid effective hierarchy.
// RUN: not obelisk -emit-bindings -Werror=dup-config-rule --top=broken %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERROR

module leaf;
endmodule

module top;
  leaf selected();
endmodule

config broken;
  design top;
  instance top.selected use leaf;
  instance top.selected use leaf;
endconfig

// ERROR: error: duplicate config rule for instance 'top.selected' -- ignoring this one
// ERROR: note: previous definition here
