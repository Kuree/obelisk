//===- SourceTokens.cpp - Raw SystemVerilog token listing -----------------===//

#include "obelisk/Frontend/SourceTokens.h"

#include "slang/diagnostics/Diagnostics.h"
#include "slang/parsing/Lexer.h"
#include "slang/text/SourceManager.h"
#include "slang/util/BumpAllocator.h"
#include "llvm/Support/raw_ostream.h"

#include <limits>
#include <string_view>

namespace obelisk::frontend {

void lexSystemVerilogTokens(
    llvm::StringRef text, LanguageVersion version,
    llvm::function_ref<void(const SourceToken &)> callback) {
  slang::SourceManager sourceManager;
  slang::parsing::LexerOptions lexerOptions;
  lexerOptions.languageVersion = version == LanguageVersion::IEEE1800_2017
                                     ? slang::LanguageVersion::v1800_2017
                                     : slang::LanguageVersion::v1800_2023;
  // A listing covers the whole text; the lexer would otherwise give up after
  // a few malformed tokens and report the rest as one disabled region.
  lexerOptions.maxErrors = std::numeric_limits<uint32_t>::max();

  slang::SourceBuffer buffer = sourceManager.assignText(
      std::string_view(text.data(), text.size()));
  // Lexer diagnostics are not reported: a listing describes the text as
  // written, and compiling the design is what reports its errors.
  slang::BumpAllocator alloc;
  slang::Diagnostics diagnostics;
  slang::parsing::Lexer lexer(buffer, alloc, diagnostics, sourceManager,
                              lexerOptions);
  auto report = [&](uint64_t offset, uint64_t length, std::string_view kind) {
    callback(SourceToken{offset, length, llvm::StringRef(kind)});
  };
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
        report(triviaOffset, length, slang::parsing::toString(trivia.kind));
      triviaOffset += length;
    }
    if (token.kind == slang::parsing::TokenKind::EndOfFile)
      break;
    report(tokenOffset, token.rawText().size(),
           slang::parsing::toString(token.kind));
  }
}

void writeSystemVerilogTokenListing(llvm::StringRef text,
                                    LanguageVersion version,
                                    llvm::raw_ostream &os) {
  lexSystemVerilogTokens(text, version, [&](const SourceToken &token) {
    os << token.offset << ' ' << token.length << ' ' << token.kind << '\n';
  });
}

} // namespace obelisk::frontend
