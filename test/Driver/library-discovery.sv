// Module-library options are a driver/frontend contract, so these checks emit
// the first MLIR representation after elaboration rather than inspecting a
// third-party AST dump.
// RUN: obelisk --help | FileCheck %s --check-prefix=LIBRARY-HELP

// The conventional plus spelling is an ordered extension list. Reversing it
// reverses which same-named library file is selected.
// RUN: obelisk -emit-slang -y %S/Inputs/library/discovery/extensions \
// RUN:   +libext+.first+.first+.second %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=FIRST-EXT
// RUN: obelisk -emit-slang -y %S/Inputs/library/discovery/extensions \
// RUN:   +libext+.second+.first %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=SECOND-EXT

// Directory order is the outer search dimension. An unused syntax-error file
// proves that -y discovery does not enumerate or parse unrelated sources.
// RUN: obelisk -emit-slang -y %S/Inputs/library/discovery/dir_first \
// RUN:   -y %S/Inputs/library/discovery/dir_second -Y .svlib \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=FIRST-DIR
// RUN: obelisk -emit-slang -y %S/Inputs/library/discovery/dir_second \
// RUN:   -y %S/Inputs/library/discovery/dir_first -Y .svlib \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=SECOND-DIR

// -v is the conventional spelling; the preexisting -l spelling remains a
// compatibility alias and shares the same occurrence order.
// RUN: obelisk -emit-slang -Wno-error=duplicate-definition \
// RUN:   -v %S/Inputs/library/discovery/explicit_first.sv \
// RUN:   -l %S/Inputs/library/discovery/explicit_last.sv \
// RUN:   %S/Inputs/library/discovery/top.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXPLICIT-LAST
// RUN: obelisk -emit-slang \
// RUN:   -v first=%S/Inputs/library/discovery/explicit_first.sv \
// RUN:   -v second=%S/Inputs/library/discovery/explicit_last.sv \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=NAMED-FIRST
// RUN: obelisk -emit-slang \
// RUN:   -v second=%S/Inputs/library/discovery/explicit_last.sv \
// RUN:   -v first=%S/Inputs/library/discovery/explicit_first.sv \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=NAMED-SECOND

// Map includes and all paths within them are relative to the containing map.
// The included library also contributes its private include directory.
// RUN: obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/root.map \
// RUN:   %S/Inputs/library/discovery/map_top.sv \
// RUN:   | FileCheck %s --check-prefix=MAP-INCLUDE
// RUN: not obelisk -emit-slang --single-unit \
// RUN:   --libmap %S/Inputs/library/discovery/maps/macro.map \
// RUN:   %S/Inputs/library/discovery/macro_top.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MAP-MACRO-ISOLATED
// RUN: obelisk -emit-slang --single-unit --libraries-inherit-macros \
// RUN:   --libmap %S/Inputs/library/discovery/maps/macro.map \
// RUN:   %S/Inputs/library/discovery/macro_top.sv \
// RUN:   | FileCheck %s --check-prefix=MAP-MACRO-INHERITED
// RUN: obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/hierarchical.map \
// RUN:   %S/Inputs/library/discovery/map_top.sv \
// RUN:   | FileCheck %s --check-prefix=MAP-INCLUDE
// RUN: env L14_LIBRARY_SOURCE=%S/Inputs/library/discovery obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/environment.map \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=FIRST-MAP
// RUN: obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/specificity.map \
// RUN:   %S/Inputs/library/discovery/map_top.sv \
// RUN:   | FileCheck %s --check-prefix=MAP-INCLUDE

// Command files are expanded before option parsing, including plus spellings.
// RUN: (cd %S/Inputs/library/discovery && obelisk -emit-slang \
// RUN:   -f library.f) | FileCheck %s --check-prefix=FIRST-EXT

// Multiple maps are applied in command-line order, which establishes the
// default library search priority.
// RUN: obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/first.map \
// RUN:   --libmap %S/Inputs/library/discovery/maps/second.map \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=FIRST-MAP
// RUN: obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/second.map \
// RUN:   --libmap %S/Inputs/library/discovery/maps/first.map \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=SECOND-MAP
// RUN: obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/first.map \
// RUN:   -v explicit=%S/Inputs/library/discovery/explicit_last.sv \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=FIRST-MAP
// RUN: obelisk -emit-slang \
// RUN:   -v explicit=%S/Inputs/library/discovery/explicit_last.sv \
// RUN:   --libmap %S/Inputs/library/discovery/maps/first.map \
// RUN:   %S/Inputs/library/discovery/top.sv \
// RUN:   | FileCheck %s --check-prefix=EXPLICIT-PRIORITY

// Equal-specificity mappings of one file into two libraries are an error.
// RUN: not obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/duplicate.map \
// RUN:   %S/Inputs/library/discovery/map_top.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DUPLICATE-MAP

// Missing operands and empty plus-list elements are rejected by the Obelisk
// option layer before the frontend is invoked.
// RUN: not obelisk -emit-slang -v 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING-V
// RUN: not obelisk -emit-slang --libmap 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING-MAP
// RUN: not obelisk -emit-slang +libext+.first++second \
// RUN:   %S/Inputs/library/discovery/top.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EMPTY-EXT
// RUN: not obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/missing.map \
// RUN:   %S/Inputs/library/discovery/map_top.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING-MAP-FILE
// RUN: not obelisk -emit-slang \
// RUN:   --libmap %S/Inputs/library/discovery/maps/recursive.map \
// RUN:   %S/Inputs/library/discovery/map_top.sv 2>&1 \
// RUN:   | FileCheck %s --check-prefix=RECURSIVE-MAP

// LIBRARY-HELP: --libmap <file>
// LIBRARY-HELP: -v <file>
// FIRST-EXT: hierarchical_name = "library_top.choice.first_extension"
// SECOND-EXT: hierarchical_name = "library_top.choice.second_extension"
// FIRST-DIR: hierarchical_name = "library_top.choice.first_directory"
// SECOND-DIR: hierarchical_name = "library_top.choice.second_directory"
// EXPLICIT-LAST: warning: duplicate definition of 'library_choice'
// EXPLICIT-LAST: hierarchical_name = "library_top.choice.explicit_last"
// EXPLICIT-PRIORITY: hierarchical_name = "library_top.choice.explicit_last"
// NAMED-FIRST: hierarchical_name = "library_top.choice.explicit_first"
// NAMED-SECOND: hierarchical_name = "library_top.choice.explicit_last"
// MAP-INCLUDE: !slang.packed_array<6 : 0 x
// MAP-INCLUDE: hierarchical_name = "map_top.mapped.from_map_include"
// MAP-MACRO-ISOLATED: error: unknown macro or compiler directive
// MAP-MACRO-INHERITED: !slang.packed_array<10 : 0 x
// FIRST-MAP: hierarchical_name = "library_top.choice.first_map"
// SECOND-MAP: hierarchical_name = "library_top.choice.second_map"
// DUPLICATE-MAP: error: {{.*}}mapped.sv{{.*}}matches multiple libraries
// MISSING-V: error: -v: missing argument
// MISSING-MAP: error: --libmap: missing argument
// EMPTY-EXT: error: empty module library extension in '+libext+.first++second'
// MISSING-MAP-FILE: error: {{.*}}missing.map
// RECURSIVE-MAP: error: library map{{.*}}includes itself recursively
