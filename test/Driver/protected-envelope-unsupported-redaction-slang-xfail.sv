// XFAIL: *
// RUN: not %obelisk -emit-obelisk %S/protected-envelope-unsupported.sv 2>&1 | FileCheck %s

// Slang v11.0 includes the protected data line in its unsupported-envelope
// diagnostic. Keep the missing redaction visible without a source patch.
// CHECK-NOT: SECRET_PLAINTEXT_42
// CHECK-NOT: FRPERG_CYNVAGRKG_42
// CHECK: error: protected envelopes cannot be decrypted and will be skipped entirely
// CHECK-NOT: SECRET_PLAINTEXT_42
// CHECK-NOT: FRPERG_CYNVAGRKG_42
