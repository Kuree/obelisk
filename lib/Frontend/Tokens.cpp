//===- Tokens.cpp - Raw SystemVerilog token listing of input files --------===//
//
// `obelisk -dump-tokens`: the ObeliskSourceTokens listing for each input file.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Frontend/Frontend.h"
#include "obelisk/Frontend/SourceTokens.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace obelisk::frontend {

FailureOr<std::string>
listSystemVerilogTokens(llvm::ArrayRef<std::string> inputFilenames,
                        const FrontendOptions &options) {
  std::string listing;
  llvm::raw_string_ostream os(listing);
  for (const std::string &filename : inputFilenames) {
    auto buffer = llvm::MemoryBuffer::getFile(filename, /*IsText=*/false,
                                              /*RequiresNullTerminator=*/false);
    if (!buffer) {
      llvm::errs() << "obelisk: error: could not read '" << filename
                   << "': " << buffer.getError().message() << '\n';
      return failure();
    }
    if (inputFilenames.size() > 1)
      os << "file " << filename << '\n';
    writeSystemVerilogTokenListing((*buffer)->getBuffer(),
                                   options.languageVersion, os);
  }
  return listing;
}

} // namespace obelisk::frontend
