// RUN: not %protect-obelisk -emit-slang %s 2>&1 | FileCheck %s

`pragma protect data_method="x-throw", data_keyname="rot13", begin_protected
`pragma protect encoding=(enctype="raw"), data_block
FRPERG_QRPELCRG_CYNVAGRKG_2o19
`pragma protect end_protected

// CHECK: error: protected envelope rejected (provider failure)
// CHECK-NOT: SECRET_PROVIDER_EXCEPTION
// CHECK-NOT: SECRET_DECRYPT_PLAINTEXT
// CHECK-NOT: FRPERG_QRPELCRG
