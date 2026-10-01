// RUN: %protect-obelisk -emit-obelisk %s | obelisk-opt --verify-each | FileCheck %s --check-prefix=IR
// RUN: %protect-obelisk -O0 %s -o %t
// RUN: %t | FileCheck %s --check-prefix=OUTPUT
// RUN: not %protect-obelisk --test-max-protect-depth=7 -emit-slang %s 2>&1 | FileCheck %s --check-prefix=LIMIT

// IEEE 1800-2017 34.2 requires at least eight levels of nesting, and 34.3.2
// requires decrypted replacement text to be preprocessed recursively. Exact
// raw byte counts allow inner cleartext pragma spellings inside outer blocks.
`pragma protect data_method="x-caesar", data_keyname="rot13"
`pragma protect encoding=(enctype="raw", bytes=1381), begin_protected
`pragma protect data_block
`centzn cebgrpg qngn_zrgubq="k-pnrfne", qngn_xrlanzr="ebg13"
`centzn cebgrpg rapbqvat=(rapglcr="enj", olgrf=1197), ortva_cebgrpgrq
`centzn cebgrpg qngn_oybpx
`pragma protect data_method="x-caesar", data_keyname="rot13"
`pragma protect encoding=(enctype="raw", bytes=1013), begin_protected
`pragma protect data_block
`centzn cebgrpg qngn_zrgubq="k-pnrfne", qngn_xrlanzr="ebg13"
`centzn cebgrpg rapbqvat=(rapglcr="enj", olgrf=830), ortva_cebgrpgrq
`centzn cebgrpg qngn_oybpx
`pragma protect data_method="x-caesar", data_keyname="rot13"
`pragma protect encoding=(enctype="raw", bytes=647), begin_protected
`pragma protect data_block
`centzn cebgrpg qngn_zrgubq="k-pnrfne", qngn_xrlanzr="ebg13"
`centzn cebgrpg rapbqvat=(rapglcr="enj", olgrf=464), ortva_cebgrpgrq
`centzn cebgrpg qngn_oybpx
`pragma protect data_method="x-caesar", data_keyname="rot13"
`pragma protect encoding=(enctype="raw", bytes=281), begin_protected
`pragma protect data_block
`centzn cebgrpg qngn_zrgubq="k-pnrfne", qngn_xrlanzr="ebg13"
`centzn cebgrpg rapbqvat=(rapglcr="enj", olgrf=99), ortva_cebgrpgrq
`centzn cebgrpg qngn_oybpx
module protected_nested;
  initial begin
    $display("PROTECT_NESTED_OK");
    $finish;
  end
endmodule

`centzn cebgrpg raq_cebgrpgrq

`pragma protect end_protected

`centzn cebgrpg raq_cebgrpgrq

`pragma protect end_protected

`centzn cebgrpg raq_cebgrpgrq

`pragma protect end_protected

`centzn cebgrpg raq_cebgrpgrq

`pragma protect end_protected

// IR: obelisk.sv.symbol.definition @{{[^ ]+}} attributes {{.*}}name = "protected_nested"
// IR-NOT: qngn_oybpx
// OUTPUT: PROTECT_NESTED_OK
// LIMIT: error: diagnostic in protected source (details suppressed)
// LIMIT-NOT: protected_nested
// LIMIT-NOT: cebgrpgrq_arfgrq
