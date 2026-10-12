// RUN: not obelisk -emit-obelisk %s > %t.stdout 2> %t.stderr
// RUN: FileCheck %s --input-file=%t.stderr
// RUN: test ! -s %t.stdout
// RUN: not obelisk -emit-obelisk %s -o %t.mlir 2> %t.file.stderr
// RUN: FileCheck %s --input-file=%t.file.stderr
// RUN: test ! -e %t.mlir
// RUN: not obelisk -Wno-protected-envelope -emit-obelisk %s -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2017 Clause 34 encrypted/protected IP is an explicit production
// exclusion. Slang's protected-envelope warning is promoted to an error
// through its supported diagnostic option; no source patch is needed.
`pragma protect data_method="x-caesar", data_keyname="rot13", begin_protected
`pragma protect encoding=(enctype="raw"), data_block
zbqhyr FRPERG_CYNVAGRKG_42; raqzbqhyr
`pragma protect end_protected

// CHECK-NOT: SECRET_PLAINTEXT_42
// CHECK-NOT: obelisk.sv
// CHECK-NOT: slang.
// CHECK: error: protected envelopes cannot be decrypted and will be skipped entirely
// CHECK-NOT: SECRET_PLAINTEXT_42
// CHECK-NOT: obelisk.sv
// CHECK-NOT: slang.
