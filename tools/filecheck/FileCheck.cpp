//===- FileCheck.cpp - Build-local LLVM FileCheck driver -----------------===//
//
// Standalone LLVM distributions may provide the LLVMFileCheck library without
// installing the FileCheck executable. This intentionally small driver keeps
// Obelisk's tests self-contained. It implements only the options used by this
// suite and rejects unknown options so unsupported checks cannot pass silently.
//
//===----------------------------------------------------------------------===//

#include "llvm/FileCheck/FileCheck.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace llvm;

namespace {

struct Options {
  std::string checkFile;
  std::string inputFile = "-";
  std::vector<std::string> prefixes;
  std::vector<std::string> implicitCheckNot;
  bool allowEmpty = false;
  bool allowUnusedPrefixes = false;
};

static bool consumeValue(int &index, int argc, char **argv, StringRef option,
                         StringRef argument, std::string &value) {
  if (argument.consume_front(option) && argument.consume_front("=")) {
    value = argument.str();
    return true;
  }
  if (argument != option)
    return false;
  if (++index >= argc) {
    errs() << "FileCheck: missing value for " << option << '\n';
    std::exit(2);
  }
  value = argv[index];
  return true;
}

static bool parseOptions(int argc, char **argv, Options &options) {
  for (int index = 1; index < argc; ++index) {
    StringRef argument(argv[index]);
    std::string value;
    if (consumeValue(index, argc, argv, "--input-file", argument, value)) {
      options.inputFile = std::move(value);
      continue;
    }
    if (consumeValue(index, argc, argv, "--check-prefix", argument, value)) {
      options.prefixes.push_back(std::move(value));
      continue;
    }
    if (consumeValue(index, argc, argv, "--implicit-check-not", argument,
                     value)) {
      options.implicitCheckNot.push_back(std::move(value));
      continue;
    }
    if (argument == "--allow-empty") {
      options.allowEmpty = true;
      continue;
    }
    if (argument == "--allow-unused-prefixes") {
      options.allowUnusedPrefixes = true;
      continue;
    }
    if (argument.starts_with("-")) {
      errs() << "FileCheck: unsupported option '" << argument << "'\n";
      return false;
    }
    if (!options.checkFile.empty()) {
      errs() << "FileCheck: multiple check files specified\n";
      return false;
    }
    options.checkFile = argument.str();
  }
  if (options.checkFile.empty()) {
    errs() << "FileCheck: <check-file> not specified\n";
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM init(argc, argv);
  Options options;
  if (!parseOptions(argc, argv, options))
    return 2;

  FileCheckRequest request;
  for (const std::string &prefix : options.prefixes)
    request.CheckPrefixes.push_back(prefix);
  for (const std::string &pattern : options.implicitCheckNot)
    request.ImplicitCheckNot.push_back(pattern);
  request.AllowEmptyInput = options.allowEmpty;
  request.AllowUnusedPrefixes = options.allowUnusedPrefixes;

  FileCheck checker(request);
  if (!checker.ValidateCheckPrefixes())
    return 2;

  ErrorOr<std::unique_ptr<MemoryBuffer>> checkBufferOrError =
      MemoryBuffer::getFileOrSTDIN(options.checkFile, /*IsText=*/true);
  if (std::error_code error = checkBufferOrError.getError()) {
    errs() << "FileCheck: cannot open check file '" << options.checkFile
           << "': " << error.message() << '\n';
    return 2;
  }

  SourceMgr sourceManager;
  MemoryBuffer &checkBuffer = *checkBufferOrError.get();
  SmallString<4096> canonicalCheckStorage;
  StringRef canonicalCheck =
      checker.CanonicalizeFile(checkBuffer, canonicalCheckStorage);
  sourceManager.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(canonicalCheck,
                                 checkBuffer.getBufferIdentifier()),
      SMLoc());
  if (checker.readCheckFile(sourceManager, canonicalCheck))
    return 2;

  ErrorOr<std::unique_ptr<MemoryBuffer>> inputBufferOrError =
      MemoryBuffer::getFileOrSTDIN(options.inputFile, /*IsText=*/true);
  if (std::error_code error = inputBufferOrError.getError()) {
    errs() << "FileCheck: cannot open input file '" << options.inputFile
           << "': " << error.message() << '\n';
    return 2;
  }

  MemoryBuffer &inputBuffer = *inputBufferOrError.get();
  if (inputBuffer.getBufferSize() == 0 && !options.allowEmpty) {
    errs() << "FileCheck: input is empty\n";
    return 2;
  }
  SmallString<4096> canonicalInputStorage;
  StringRef canonicalInput =
      checker.CanonicalizeFile(inputBuffer, canonicalInputStorage);
  sourceManager.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(canonicalInput,
                                 inputBuffer.getBufferIdentifier()),
      SMLoc());
  return checker.checkInput(sourceManager, canonicalInput) ? 0 : 1;
}
