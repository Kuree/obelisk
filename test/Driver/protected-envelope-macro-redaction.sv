// RUN: not %protect-obelisk -emit-slang %s 2>&1 | FileCheck %s --check-prefix=TEXT
// RUN: not %protect-obelisk -Xslang --diag-json -Xslang %t.json -emit-slang %s 2>&1 | FileCheck %s --check-prefix=TEXT
// RUN: FileCheck %s --check-prefix=JSON --input-file=%t.json

`pragma protect data_method="x-caesar", data_keyname="rot13", begin_protected
`pragma protect encoding=(enctype="raw"), data_block
`qrsvar FRPERG_ZNPEB jver FRPERG_ZNPEB_CYNVAGRKG_42 = ;
`pragma protect end_protected

module top;
  `SECRET_MACRO
endmodule

// TEXT: error: diagnostic in protected source (details suppressed)
// TEXT-NOT: SECRET_MACRO
// TEXT-NOT: SECRET_MACRO_PLAINTEXT_42
// TEXT-NOT: FRPERG_ZNPEB
// JSON: "message": "diagnostic in protected source (details suppressed)"
// JSON-NOT: SECRET_MACRO
// JSON-NOT: SECRET_MACRO_PLAINTEXT_42
// JSON-NOT: FRPERG_ZNPEB
