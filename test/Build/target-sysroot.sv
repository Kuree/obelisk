// RUN: %python %S/../Inputs/check-target-sysroot.py \
// RUN:   %cmake %target_package_cache %target_sysroot \
// RUN:   %target_provision_script %t %source_root %llvm_dist

// This is a build-system regression exercised by lit; it is intentionally not
// compiled as SystemVerilog.
