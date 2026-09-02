// RUN: %python %S/../Inputs/check-native-support.py \
// RUN:   %cmake %t %source_root %llvm_dist %slang_source_root %obj_root/include

// This is a build-system regression exercised by lit; it is intentionally not
// compiled as SystemVerilog.
