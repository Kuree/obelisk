//===- SourceTokens.h - Raw SystemVerilog token listing ---------*- C++ -*-===//
//
// Lexes SystemVerilog text as written, without running the preprocessor, and
// reports every token and comment with its byte range. Tools that color source
// text use it so the colors come from the same lexer as the compiler,
// including the keyword set of the selected language version.
//
// The interface is C++17 and exposes no slang or MLIR types, so tools outside
// the compiler (obelisk-cov) can link ObeliskSourceTokens without the
// frontend.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_FRONTEND_SOURCETOKENS_H
#define OBELISK_FRONTEND_SOURCETOKENS_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace obelisk::frontend {

enum class LanguageVersion : uint8_t {
  IEEE1800_2017,
  IEEE1800_2023,
};

struct SourceToken {
  uint64_t offset;
  uint64_t length;
  /// slang's TokenKind or TriviaKind name, such as "ModuleKeyword" or
  /// "LineComment".
  llvm::StringRef kind;
};

/// Lex `text` as written and call `callback` for each token and comment in
/// source order. Whitespace and line ends are omitted. Text that does not lex
/// cleanly is still reported to the end, without diagnostics, because the
/// result describes text rather than a design.
void lexSystemVerilogTokens(
    llvm::StringRef text, LanguageVersion version,
    llvm::function_ref<void(const SourceToken &)> callback);

/// Write each token of `text` on its own line as
/// "<byte offset> <byte length> <kind>", the listing `obelisk -dump-tokens`
/// prints and web/embed-snippet.js reads.
void writeSystemVerilogTokenListing(llvm::StringRef text,
                                    LanguageVersion version,
                                    llvm::raw_ostream &os);

} // namespace obelisk::frontend

#endif // OBELISK_FRONTEND_SOURCETOKENS_H
