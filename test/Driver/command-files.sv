// Command files are expanded into the driver's own argument list before option
// parsing, so a `.f` may carry any obelisk option rather than only the subset
// the SystemVerilog frontend understands.

// RUN: (cd %S && obelisk -fno-lto -fno-lto -O0 -f Inputs/command_files/driver_options.f \
// RUN:   Inputs/command_files/macros.sv -o %t.options) \
// RUN:   && %t.options | FileCheck %s --check-prefix=OPTIONS

// RUN: (cd %S && obelisk -fno-lto -fno-lto -O0 -f Inputs/command_files/nested.f \
// RUN:   Inputs/command_files/macros.sv -o %t.nested) \
// RUN:   && %t.nested | FileCheck %s --check-prefix=NESTED

// RUN: (cd %S && obelisk -fno-lto -fno-lto -O0 -f Inputs/command_files/quoted.f \
// RUN:   --top=command_file_top Inputs/command_files/macros.sv -o %t.quoted) \
// RUN:   && %t.quoted | FileCheck %s --check-prefix=QUOTED

// RUN: (cd %S && COMMAND_FILE_ENV_VALUE=99 obelisk -fno-lto -fno-lto -O0 \
// RUN:   -f Inputs/command_files/env.f --top=command_file_top \
// RUN:   Inputs/command_files/macros.sv -o %t.env) \
// RUN:   && %t.env | FileCheck %s --check-prefix=ENV

// Expansion happens in place, so a command file's options keep the position
// the `-f` occupied. The first `-D` of a macro is the one that takes effect,
// which makes that position observable from either side.
// RUN: (cd %S && obelisk -fno-lto -fno-lto -O0 -DCOMMAND_FILE_MACRO=8 \
// RUN:   -f Inputs/command_files/driver_options.f \
// RUN:   Inputs/command_files/macros.sv -o %t.before) \
// RUN:   && %t.before | FileCheck %s --check-prefix=BEFORE
// RUN: (cd %S && obelisk -fno-lto -fno-lto -O0 -f Inputs/command_files/driver_options.f \
// RUN:   -DCOMMAND_FILE_MACRO=8 Inputs/command_files/macros.sv -o %t.after) \
// RUN:   && %t.after | FileCheck %s --check-prefix=AFTER

// RUN: (cd %S && not obelisk -O0 \
// RUN:   -f Inputs/command_files/self_including.f 2>&1) \
// RUN:   | FileCheck %s --check-prefix=CYCLE
// RUN: not obelisk -O0 -f %t.missing.f 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -O0 -f 2>&1 | FileCheck %s --check-prefix=NO-FILE

// OPTIONS: command-file macro = 7

// NESTED: command-file macro = 7
// NESTED: nested macro = 1

// QUOTED: quoted macro = 42

// ENV: env macro = 99

// BEFORE: command-file macro = 8
// AFTER: command-file macro = 7

// CYCLE: error: command file '{{.*}}self_including.f' includes itself
// MISSING: error: could not read command file '{{.*}}missing.f'
// NO-FILE: error: missing argument to '-f' (expected a command file)
