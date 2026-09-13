//===- Tokens.cpp - Raw SystemVerilog token listing -----------------------===//
//
// Lexes source files as written, without running the preprocessor, and lists
// every token and comment with its byte range. Tools that color source text
// (the web playground's embed snippets) use it so the colors come from the
// same lexer as the compiler, including the keyword set of the selected
// language version.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Frontend/Frontend.h"

#include "slang/diagnostics/Diagnostics.h"
#include "slang/parsing/Lexer.h"
#include "slang/text/SourceManager.h"
#include "slang/util/BumpAllocator.h"
#include "llvm/Support/raw_ostream.h"

#include <limits>

using namespace mlir;

namespace obelisk::frontend {

FailureOr<std::string>
listSystemVerilogTokens(llvm::ArrayRef<std::string> inputFilenames,
                        const FrontendOptions &options) {
  slang::SourceManager sourceManager;
  slang::parsing::LexerOptions lexerOptions;
  lexerOptions.languageVersion =
      options.languageVersion == LanguageVersion::IEEE1800_2017
          ? slang::LanguageVersion::v1800_2017
          : slang::LanguageVersion::v1800_2023;
  // A listing covers the whole file; the lexer would otherwise give up after
  // a few malformed tokens and report the rest as one disabled region.
  lexerOptions.maxErrors = std::numeric_limits<uint32_t>::max();

  std::string listing;
  llvm::raw_string_ostream os(listing);
  for (const std::string &filename : inputFilenames) {
    auto buffer = sourceManager.readSource(filename);
    if (!buffer) {
      llvm::errs() << "obelisk: error: could not read '" << filename
                   << "': " << buffer.error().message() << '\n';
      return failure();
    }
    if (inputFilenames.size() > 1)
      os << "file " << filename << '\n';

    // Lexer diagnostics are not reported: a listing describes the text as
    // written, and compiling the design is what reports its errors.
    slang::BumpAllocator alloc;
    slang::Diagnostics diagnostics;
    slang::parsing::Lexer lexer(*buffer, alloc, diagnostics, sourceManager,
                                lexerOptions);
    while (true) {
      slang::parsing::Token token = lexer.lex();
      uint64_t tokenOffset = token.location().offset();
      // Trivia is the contiguous text immediately before its token.
      uint64_t triviaOffset = tokenOffset;
      for (const slang::parsing::Trivia &trivia : token.trivia())
        triviaOffset -= trivia.getRawText().size();
      for (const slang::parsing::Trivia &trivia : token.trivia()) {
        size_t length = trivia.getRawText().size();
        if (trivia.kind != slang::parsing::TriviaKind::Whitespace &&
            trivia.kind != slang::parsing::TriviaKind::EndOfLine)
          os << triviaOffset << ' ' << length << ' '
             << slang::parsing::toString(trivia.kind) << '\n';
        triviaOffset += length;
      }
      if (token.kind == slang::parsing::TokenKind::EndOfFile)
        break;
      os << tokenOffset << ' ' << token.rawText().size() << ' '
         << slang::parsing::toString(token.kind) << '\n';
    }
  }
  return listing;
}

} // namespace obelisk::frontend
