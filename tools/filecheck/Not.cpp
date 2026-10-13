//===- Not.cpp - Invert a subprocess exit status -------------------------===//
//
// `not cmd...` succeeds when cmd fails. `not --status=N cmd...` succeeds only
// when cmd exits with status N, for tests of a specific failure status.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>

using namespace llvm;

int main(int argc, char **argv) {
  int first = 1;
  std::optional<int> expectedStatus;
  if (argc > 1 && StringRef(argv[1]).starts_with("--status=")) {
    int status;
    if (StringRef(argv[1]).drop_front(9).getAsInteger(10, status) ||
        status <= 0) {
      errs() << "not: --status expects a positive exit status\n";
      return 1;
    }
    expectedStatus = status;
    ++first;
  }
  if (argc <= first) {
    errs() << "not: expected a command\n";
    return 1;
  }

  SmallVector<StringRef> arguments;
  for (int index = first; index < argc; ++index)
    arguments.push_back(argv[index]);
  // Like LLVM's not, resolve a bare program name through PATH.
  std::string program = arguments.front().str();
  if (!StringRef(program).contains('/'))
    if (ErrorOr<std::string> found = sys::findProgramByName(program))
      program = *found;
  std::string error;
  bool executionFailed = false;
  int result = sys::ExecuteAndWait(program, arguments,
                                   /*Env=*/std::nullopt,
                                   /*Redirects=*/{}, /*SecondsToWait=*/0,
                                   /*MemoryLimit=*/0, &error, &executionFailed);
  if (executionFailed) {
    errs() << "not: " << error << '\n';
    return 1;
  }
  if (expectedStatus) {
    if (result == *expectedStatus)
      return 0;
    errs() << "not: expected exit status " << *expectedStatus << ", got "
           << result << '\n';
    return 1;
  }
  return result == 0 ? 1 : 0;
}
