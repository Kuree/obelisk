// RUN: obelisk -h | FileCheck %s --check-prefix=SHORT-HELP
// RUN: obelisk --help-hidden | FileCheck %s --check-prefix=HIDDEN-HELP

// RUN: obelisk -emit-moore -o %t %S/Inputs/macros.sv
// RUN: FileCheck %s --check-prefix=OUTPUT --input-file=%t
// RUN: obelisk -emit-moore --mlir-print-debuginfo \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=DEBUG

// RUN: obelisk -emit-moore --include-directory %S/Inputs/include \
// RUN:   %S/Inputs/include_user.sv | FileCheck %s --check-prefix=INCLUDE
// RUN: obelisk -emit-moore -isystem %S/Inputs/include \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=SYSTEM-INCLUDE
// RUN: obelisk -emit-moore -D FROM_DRIVER \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=MACRO-DEFINED
// RUN: obelisk -emit-moore -DFROM_DRIVER -UFROM_DRIVER \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=MACRO-UNDEFINED
// RUN: obelisk -emit-moore --max-include-depth=3 -I%S/Inputs/depth \
// RUN:   %S/Inputs/depth/main.sv | FileCheck %s --check-prefix=DEPTH
// RUN: not obelisk -emit-moore --max-include-depth=1 \
// RUN:   -I%S/Inputs/depth %S/Inputs/depth/main.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEPTH-ERROR

// RUN: obelisk -emit-moore --filelist %S/driver_flags.f -I%S \
// RUN:   | FileCheck %s --check-prefix=FILELIST-ALIAS
// RUN: obelisk -emit-moore -y %S/Inputs/library/search -Y .svlib \
// RUN:   %S/Inputs/library/search/top.sv \
// RUN:   | FileCheck %s --check-prefix=LIBRARY-SEARCH
// RUN: obelisk -emit-moore -l %S/Inputs/library/explicit_library.sv \
// RUN:   %S/Inputs/library/explicit_top.sv \
// RUN:   | FileCheck %s --check-prefix=LIBRARY-FILE
// RUN: not obelisk -emit-moore %S/Inputs/single_unit_define.sv \
// RUN:   %S/Inputs/single_unit_use.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SEPARATE-UNITS
// RUN: obelisk -emit-moore --single-unit \
// RUN:   %S/Inputs/single_unit_define.sv %S/Inputs/single_unit_use.sv \
// RUN:   | FileCheck %s --check-prefix=SINGLE-UNIT
// RUN: obelisk -emit-moore --single-unit --libraries-inherit-macros \
// RUN:   -l %S/Inputs/library/inherit_library.sv \
// RUN:   %S/Inputs/library/inherit_primary.sv \
// RUN:   | FileCheck %s --check-prefix=INHERIT-MACROS
// RUN: obelisk -emit-moore --top=selected_top -G WIDTH=12 \
// RUN:   %S/Inputs/tops.sv | FileCheck %s --check-prefix=TOP
// RUN: obelisk -emit-moore --timescale=10ns/1ns \
// RUN:   %S/Inputs/timescale.sv | FileCheck %s --check-prefix=TIMESCALE
// RUN: obelisk -emit-moore --allow-use-before-declare \
// RUN:   %S/Inputs/use_before_declare.sv \
// RUN:   | FileCheck %s --check-prefix=USE-BEFORE-DECLARE
// RUN: obelisk -emit-moore --ignore-unknown-modules \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=IGNORE-UNKNOWN

// RUN: obelisk -emit-moore -Wno-unused --error-limit=5 \
// RUN:   --suppress-warnings=%S/Inputs %S/Inputs/macros.sv \
// RUN:   | FileCheck %s --check-prefix=DIAGNOSTICS
// RUN: obelisk -emit-moore -Xslang --ignore-unknown-modules \
// RUN:   %S/Inputs/macros.sv | FileCheck %s --check-prefix=XSLANG

// RUN: cd %S/Inputs && obelisk -emit-moore -- -dash_input.sv \
// RUN:   | FileCheck %s --check-prefix=DASH-DASH
// RUN: obelisk -emit-moore - < %S/Inputs/macros.sv \
// RUN:   | FileCheck %s --check-prefix=STDIN

// The last action option controls the representation that is emitted.
// RUN: obelisk -emit-obelisk -emit-moore %S/Inputs/macros.sv \
// RUN:   | FileCheck %s --check-prefix=LAST-MOORE
// RUN: obelisk -emit-moore -emit-obelisk %S/Inputs/macros.sv \
// RUN:   | FileCheck %s --check-prefix=LAST-OBELISK

// SHORT-HELP: OVERVIEW: Obelisk ahead-of-time SystemVerilog compiler
// SHORT-HELP: -emit-moore
// HIDDEN-HELP: -Xslang

// OUTPUT: moore.module @macro_was_undefined
// DEBUG: moore.module @macro_was_undefined
// DEBUG: moore.output loc(

// INCLUDE: moore.module @include_user
// INCLUDE: moore.variable : <l7>
// SYSTEM-INCLUDE: moore.module @macro_was_undefined
// MACRO-DEFINED: moore.module @macro_was_defined
// MACRO-UNDEFINED: moore.module @macro_was_undefined
// DEPTH: moore.module @nested_include
// DEPTH: moore.variable : <l6>
// DEPTH-ERROR: error: exceeded max include depth

// FILELIST-ALIAS: moore.module{{.*}}@driver_helper
// LIBRARY-SEARCH: moore.module private @searched_library
// LIBRARY-FILE: moore.module private @explicit_library
// SEPARATE-UNITS: error: unknown macro or compiler directive
// SINGLE-UNIT: moore.variable : <l10>
// INHERIT-MACROS: moore.module private @inherited_library
// INHERIT-MACROS: moore.variable : <l9>
// TOP: moore.module @selected_top
// TOP: moore.variable : <l12>
// TOP-NOT: @unused_top
// TIMESCALE: moore.constant_time 10000000 fs
// USE-BEFORE-DECLARE: moore.module @use_before_declare
// IGNORE-UNKNOWN: moore.module @macro_was_undefined

// DIAGNOSTICS: moore.module @macro_was_undefined
// XSLANG: moore.module @macro_was_undefined

// DASH-DASH: moore.module @dash_dash_input
// STDIN: moore.module @macro_was_undefined

// LAST-MOORE: moore.module @macro_was_undefined
// LAST-MOORE-NOT: obelisk.
// LAST-OBELISK: obelisk.semantic.graph_symbol @macro_was_undefined module
// LAST-OBELISK-NOT: moore.
