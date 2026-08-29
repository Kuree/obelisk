// RUN: %protect-obelisk -I%S -emit-obelisk %s | obelisk-opt --verify-each | FileCheck %s --check-prefix=IR
// RUN: %protect-obelisk -I%S -fno-lto -O0 %s -o %t
// RUN: %t | FileCheck %s --check-prefix=OUTPUT

`pragma protect data_method="x-caesar", data_keyname="rot13"
`include "Inputs/protected-envelope-cross-file.svh"

// IR: obelisk.sv.symbol.definition attributes {{.*}}name = "protected_cross_file"
// OUTPUT: PROTECT_CROSS_FILE_OK
