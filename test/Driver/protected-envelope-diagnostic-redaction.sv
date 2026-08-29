// RUN: not %protect-obelisk -emit-slang %s 2>&1 | FileCheck %s

`pragma protect data_method="x-caesar", data_keyname="rot13", begin_protected
`pragma protect encoding=(enctype="raw"), data_block
guvf vf abg inyvq FlfgrzIrevybt naq FRPERG_FRAGVARY_7s12;
`pragma protect end_protected

// CHECK: error: diagnostic in protected source (details suppressed)
// CHECK-NOT: SECRET_SENTINEL
// CHECK-NOT: FRPERG_FRAGVARY
