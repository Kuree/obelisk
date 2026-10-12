// XFAIL: *
// RUN: not %obelisk -E %S/protected-envelope-unsupported.sv > %t.stdout 2> %t.stderr
// RUN: FileCheck %s --input-file=%t.stderr
// RUN: test ! -s %t.stdout

// Slang v11.0's runPreprocessor checks the raw diagnostic severity and ignores
// -Werror=protected-envelope. Record the missing rejection without patching
// its diagnostic handling.
// CHECK: error: protected envelopes cannot be decrypted and will be skipped entirely
