// RUN: %protect-obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: %protect-obelisk -emit-obelisk %s | obelisk-opt --verify-each | FileCheck %s --check-prefix=OBELISK
// RUN: %protect-obelisk -O3 %s -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=OUTPUT
// RUN: %protect-obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s --check-prefix=OUTPUT

// IEEE 1800-2017 34.2 makes these envelope descriptors effective in source
// order even though they are split across directives before begin_protected.
`pragma protect data_method="x-caesar"
`pragma protect data_keyname="rot13"
`pragma protect encoding=(enctype="raw")
`pragma protect begin_protected
`pragma protect data_block
zbqhyr cebgrpgrq_ubbx;
  `qrsvar CEBGRPGRQ_INYHR 42
  vavgvny ortva
    vs (`CEBGRPGRQ_INYHR == 42) $qvfcynl("CEBGRPG_BX");
    $svavfu;
  raq
raqzbqhyr
`pragma protect end_protected

// SLANG: slang.symbol.definition @{{[^ ]+}} attributes {{.*}}name = "protected_hook"
// OBELISK: obelisk.sv.symbol.definition @{{[^ ]+}} attributes {{.*}}name = "protected_hook"
// OBELISK-NOT: data_block
// OBELISK-NOT: cebgrpgrq_ubbx
// OUTPUT: PROTECT_OK
// NO-PROVIDER: error: encrypted/protected IP is unsupported (IEEE 1800-2017 Clause 34)
// SIZE-LIMIT: error: protected envelope rejected (resource limit)
// SIZE-LIMIT-NOT: protected_hook
// COUNT-LIMIT: error: protected envelope rejected (resource limit)
// COUNT-LIMIT-NOT: cebgrpgrq_ubbx
