//===- Options.h - Talos compiler driver options -------------*- C++ -*-===//

#ifndef TALOS_TOOLS_DRIVER_OPTIONS_H
#define TALOS_TOOLS_DRIVER_OPTIONS_H

#include "llvm/Option/OptTable.h"

namespace talos::driver {

namespace options {

enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
  LastOption
};

} // namespace options

const llvm::opt::OptTable &getDriverOptTable();

} // namespace talos::driver

#endif // TALOS_TOOLS_DRIVER_OPTIONS_H
