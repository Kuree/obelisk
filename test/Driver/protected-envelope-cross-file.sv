// XFAIL: *
// Upstream Slang v11.0 has no decryption or protected-source redaction API.
// Keep this source case visible without downstream Slang patches.
// RUN: %talos -I%S -emit-talos %s | talos-opt --verify-each | FileCheck %s --check-prefix=IR
// RUN: %talos -I%S -O0 %s -o %t
// RUN: %t | FileCheck %s --check-prefix=OUTPUT

`pragma protect data_method="x-caesar", data_keyname="rot13"
`include "Inputs/protected-envelope-cross-file.svh"

// IR: talos.sv.symbol.definition @{{[^ ]+}} attributes {{.*}}name = "protected_cross_file"
// OUTPUT: PROTECT_CROSS_FILE_OK
