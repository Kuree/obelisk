// RUN: obelisk -h | FileCheck %s --check-prefix=SHORT-HELP
// RUN: obelisk --help-hidden | FileCheck %s --check-prefix=HIDDEN-HELP

// RUN: obelisk -emit-slang -o %t %S/Inputs/macros.sv
// RUN: FileCheck %s --check-prefix=OUTPUT --input-file=%t
// RUN: obelisk -emit-slang --mlir-print-debuginfo \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=DEBUG

// RUN: obelisk -emit-slang --include-directory %S/Inputs/include \
// RUN:   %S/Inputs/include_user.sv | FileCheck %s --check-prefix=INCLUDE
// RUN: obelisk -emit-slang -isystem %S/Inputs/include \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=SYSTEM-INCLUDE
// RUN: obelisk -emit-slang -D FROM_DRIVER \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=MACRO-DEFINED
// RUN: obelisk -emit-slang -DFROM_DRIVER -UFROM_DRIVER \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=MACRO-UNDEFINED
// RUN: obelisk -emit-slang --max-include-depth=3 -I%S/Inputs/depth \
// RUN:   %S/Inputs/depth/main.sv | FileCheck %s --check-prefix=DEPTH
// RUN: not obelisk -emit-slang --max-include-depth=1 \
// RUN:   -I%S/Inputs/depth %S/Inputs/depth/main.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEPTH-ERROR

// RUN: cd %S && obelisk -emit-slang --filelist driver_flags.f -I%S \
// RUN:   | FileCheck %s --check-prefix=FILELIST-ALIAS
// RUN: obelisk -emit-slang -y %S/Inputs/library/search -Y .svlib \
// RUN:   %S/Inputs/library/search/top.sv \
// RUN:   | FileCheck %s --check-prefix=LIBRARY-SEARCH
// RUN: obelisk -emit-slang -l %S/Inputs/library/explicit_library.sv \
// RUN:   %S/Inputs/library/explicit_top.sv \
// RUN:   | FileCheck %s --check-prefix=LIBRARY-FILE
// RUN: not obelisk -emit-slang %S/Inputs/single_unit_define.sv \
// RUN:   %S/Inputs/single_unit_use.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SEPARATE-UNITS
// RUN: obelisk -emit-slang --single-unit \
// RUN:   %S/Inputs/single_unit_define.sv %S/Inputs/single_unit_use.sv \
// RUN:   | FileCheck %s --check-prefix=SINGLE-UNIT
// RUN: obelisk -emit-slang --single-unit --libraries-inherit-macros \
// RUN:   -l %S/Inputs/library/inherit_library.sv \
// RUN:   %S/Inputs/library/inherit_primary.sv \
// RUN:   | FileCheck %s --check-prefix=INHERIT-MACROS
// RUN: obelisk -emit-slang --top=selected_top -G WIDTH=12 \
// RUN:   %S/Inputs/tops.sv | FileCheck %s --check-prefix=TOP
// RUN: obelisk -emit-slang --timescale=10ns/1ns \
// RUN:   %S/Inputs/timescale.sv | FileCheck %s --check-prefix=TIMESCALE
// RUN: obelisk -emit-slang %S/Inputs/exact_constants.sv \
// RUN:   | FileCheck %s --check-prefix=EXACT-CONSTANT
// RUN: obelisk -emit-slang --allow-use-before-declare \
// RUN:   %S/Inputs/use_before_declare.sv \
// RUN:   | FileCheck %s --check-prefix=USE-BEFORE-DECLARE
// RUN: obelisk -emit-slang --ignore-unknown-modules \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=IGNORE-UNKNOWN

// RUN: obelisk -emit-slang -Wno-unused --error-limit=5 \
// RUN:   --suppress-warnings=%S/Inputs %S/Inputs/macros.sv \
// RUN:   | FileCheck %s --check-prefix=DIAGNOSTICS
// RUN: obelisk -emit-slang -Xslang --ignore-unknown-modules \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=XSLANG

// RUN: cd %S/Inputs && obelisk -emit-slang -- -dash_input.sv \
// RUN:   | FileCheck %s --check-prefix=DASH-DASH
// RUN: obelisk -emit-slang - < %S/Inputs/macros.sv \
// RUN:   | FileCheck %s --check-prefix=STDIN

// The last action option controls the representation that is emitted.
// RUN: obelisk -emit-obelisk -emit-slang %S/Inputs/macros.sv \
// RUN:   | FileCheck %s --check-prefix=LAST-SLANG
// RUN: obelisk -emit-slang -emit-obelisk %S/Inputs/macros.sv \
// RUN:   | FileCheck %s --check-prefix=LAST-OBELISK

// SHORT-HELP: OVERVIEW: Obelisk ahead-of-time SystemVerilog compiler
// SHORT-HELP: -emit-slang
// HIDDEN-HELP: -Xslang

// OUTPUT: slang.symbol.instance attributes {{.*}}hierarchical_name = "macro_was_undefined"
// DEBUG: slang.symbol.instance attributes {{.*}}hierarchical_name = "macro_was_undefined"
// DEBUG: loc(

// INCLUDE: slang.symbol.instance attributes {{.*}}hierarchical_name = "include_user"
// INCLUDE: !slang.packed_array<6 : 0 x
// SYSTEM-INCLUDE: hierarchical_name = "macro_was_undefined"
// MACRO-DEFINED: hierarchical_name = "macro_was_defined"
// MACRO-UNDEFINED: hierarchical_name = "macro_was_undefined"
// DEPTH: hierarchical_name = "nested_include"
// DEPTH: !slang.packed_array<5 : 0 x
// DEPTH-ERROR: error: exceeded max include depth

// FILELIST-ALIAS: hierarchical_name = "driver_helper"
// LIBRARY-SEARCH: name = "searched_library"
// LIBRARY-FILE: name = "explicit_library"
// SEPARATE-UNITS: error: unknown macro or compiler directive
// SINGLE-UNIT: !slang.packed_array<9 : 0 x
// INHERIT-MACROS: name = "inherited_library"
// INHERIT-MACROS: !slang.packed_array<8 : 0 x
// TOP: hierarchical_name = "selected_top"
// TOP: !slang.packed_array<11 : 0 x
// TOP-NOT: @unused_top
// TIMESCALE: slang.timing.delay
// TIMESCALE: slang.expression.integer_literal
// TIMESCALE: slang.expression.time_literal attributes {constant_value = "0.12345678901234561"
// TIMESCALE-SAME: time_scale = "10ns / 1ns"
// EXACT-CONSTANT: constant_value = "160'b10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz10xz"
// EXACT-CONSTANT-NOT: ...
// USE-BEFORE-DECLARE: hierarchical_name = "use_before_declare"
// IGNORE-UNKNOWN: hierarchical_name = "macro_was_undefined"

// DIAGNOSTICS: hierarchical_name = "macro_was_undefined"
// XSLANG: hierarchical_name = "macro_was_undefined"

// DASH-DASH: hierarchical_name = "dash_dash_input"
// STDIN: hierarchical_name = "macro_was_undefined"

// LAST-SLANG: slang.symbol.instance attributes {{.*}}hierarchical_name = "macro_was_undefined"
// LAST-SLANG-NOT: obelisk.
// LAST-OBELISK: obelisk.sv.symbol.instance attributes {{.*}}hierarchical_name = "macro_was_undefined"
// LAST-OBELISK-NOT: slang.
