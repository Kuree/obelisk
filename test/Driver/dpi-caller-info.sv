// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_caller_impl.c \
// RUN:   -I$(obelisk --print-resource-dir)/include -o %t.o
// RUN: obelisk --dpi-link=%t.o %S/Inputs/dpi-caller-a.sv \
// RUN:   %S/Inputs/dpi-caller-b.sv -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk --execution-tier=bytecode --dpi-link=%t.o \
// RUN:   %S/Inputs/dpi-caller-a.sv %S/Inputs/dpi-caller-b.sv -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

// CHECK-DAG: caller-a 11
// CHECK-DAG: caller-b 22
