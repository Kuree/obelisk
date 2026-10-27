// The report is strictly opt-in and empty for an ordinary hierarchy.
// RUN: talos -emit-bindings %s > %t
// RUN: FileCheck %s --check-prefix=EMPTY --allow-empty < %t
// RUN: talos -emit-slang %s \
// RUN:   | FileCheck %s --check-prefix=SLANG
// RUN: talos --help | FileCheck %s --check-prefix=HELP

module child;
endmodule

module emit_bindings_empty;
  child child_inst();
endmodule

// EMPTY-NOT: binding
// SLANG-NOT: configuration
// SLANG-NOT: selected_cell
// SLANG-NOT: is_from_bind
// SLANG: slang.symbol.instance
// HELP: -emit-bindings
