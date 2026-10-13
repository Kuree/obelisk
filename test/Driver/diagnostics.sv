// RUN: not obelisk --max-include-depth=invalid 2>&1 | FileCheck %s --check-prefix=BAD-DEPTH
// RUN: not obelisk --max-include-depth=-1 2>&1 | FileCheck %s --check-prefix=BAD-DEPTH
// RUN: not obelisk --error-limit=invalid 2>&1 | FileCheck %s --check-prefix=BAD-LIMIT
// RUN: not obelisk --error-limit=-1 2>&1 | FileCheck %s --check-prefix=BAD-LIMIT
// RUN: not obelisk - - 2>&1 | FileCheck %s --check-prefix=DUP-STDIN
// RUN: not obelisk --definitely-unknown 2>&1 | FileCheck %s --check-prefix=UNKNOWN

// BAD-DEPTH: obelisk: error: invalid value
// BAD-DEPTH-SAME: for --max-include-depth

// BAD-LIMIT: obelisk: error: invalid value
// BAD-LIMIT-SAME: for --error-limit

// DUP-STDIN: obelisk: error: standard input may only appear once

// UNKNOWN: obelisk: error:
// UNKNOWN-SAME: unknown argument
