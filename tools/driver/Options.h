//===- Options.h - Obelisk compiler driver options -------------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_OPTIONS_H
#define OBELISK_TOOLS_DRIVER_OPTIONS_H

#include "llvm/Option/OptTable.h"

namespace obelisk::driver {

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

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_OPTIONS_H
