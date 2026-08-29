// RUN: not %protect-obelisk -emit-slang %s 2>&1 | FileCheck %s --check-prefix=TEXT
// RUN: not %protect-obelisk -Xslang --diag-json -Xslang %t.json -emit-slang %s 2>&1 | FileCheck %s --check-prefix=TEXT
// RUN: FileCheck %s --check-prefix=JSON --input-file=%t.json

`pragma protect data_method="x-caesar", data_keyname="rot13", begin_protected
`pragma protect encoding=(enctype="base64"), data_block
U0VDUkVUX0NJUEhFUlRFWFRfODFkNA!!
`pragma protect end_protected

// TEXT: warning: diagnostic in protected source (details suppressed)
// TEXT-NOT: U0VDUkVUX0NJUEhFUlRFWFRfODFkNA
// TEXT-NOT: SECRET_CIPHERTEXT
// JSON: "message": "diagnostic in protected source (details suppressed)"
// JSON-NOT: U0VDUkVUX0NJUEhFUlRFWFRfODFkNA
// JSON-NOT: SECRET_CIPHERTEXT
