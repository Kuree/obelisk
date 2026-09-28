// Lists a file's tokens through the ObeliskSourceTokens callback API. It is
// built as C++17 without the frontend, the way coverage tools consume the
// library, and its output must match `obelisk -dump-tokens`.

#include "obelisk/Frontend/SourceTokens.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>

using namespace obelisk::frontend;

int main(int argc, char **argv) {
  LanguageVersion version = LanguageVersion::IEEE1800_2023;
  const char *path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--std=1800-2017"))
      version = LanguageVersion::IEEE1800_2017;
    else if (!std::strcmp(argv[i], "--std=1800-2023"))
      version = LanguageVersion::IEEE1800_2023;
    else
      path = argv[i];
  }
  if (!path) {
    llvm::errs() << "usage: source-tokens-test [--std=1800-2017|1800-2023] "
                    "FILE\n";
    return 2;
  }
  auto buffer = llvm::MemoryBuffer::getFile(path, /*IsText=*/false,
                                            /*RequiresNullTerminator=*/false);
  if (!buffer) {
    llvm::errs() << "could not read '" << path
                 << "': " << buffer.getError().message() << '\n';
    return 1;
  }
  lexSystemVerilogTokens((*buffer)->getBuffer(), version,
                         [](const SourceToken &token) {
                           llvm::outs() << token.offset << ' ' << token.length
                                        << ' ' << token.kind << '\n';
                         });
  return 0;
}
