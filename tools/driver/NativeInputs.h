//===- NativeInputs.h - Direct source/native input classification -*- C++
//-*-===//

#ifndef OBELISK_TOOLS_DRIVER_NATIVEINPUTS_H
#define OBELISK_TOOLS_DRIVER_NATIVEINPUTS_H

#include "NativeBackend.h"

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/ArrayRef.h"

#include <string>
#include <vector>

namespace obelisk::driver {

struct ClassifiedInputs {
  std::vector<std::string> systemVerilog;
  std::vector<NativeLinkInput> nativeLinkInputs;
  std::vector<SharedLibraryInput> sharedLibraries;
};

/// Classify direct command-line inputs by their contents.  Command files are
/// deliberately absent: -f remains entirely under the SystemVerilog
/// frontend's ownership.
mlir::LogicalResult classifyDirectInputs(llvm::ArrayRef<std::string> inputs,
                                         bool nativeInputsAllowed,
                                         llvm::StringRef vpiMode,
                                         ClassifiedInputs &result);

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_NATIVEINPUTS_H
