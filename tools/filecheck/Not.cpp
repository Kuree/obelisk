//===- Not.cpp - Invert a subprocess exit status -------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

int main(int argc, char **argv) {
  if (argc < 2) {
    errs() << "not: expected a command\n";
    return 1;
  }

  SmallVector<StringRef> arguments;
  for (int index = 1; index < argc; ++index)
    arguments.push_back(argv[index]);
  std::string error;
  bool executionFailed = false;
  int result = sys::ExecuteAndWait(arguments.front(), arguments,
                                   /*Env=*/std::nullopt,
                                   /*Redirects=*/{}, /*SecondsToWait=*/0,
                                   /*MemoryLimit=*/0, &error, &executionFailed);
  if (executionFailed) {
    errs() << "not: " << error << '\n';
    return 1;
  }
  return result == 0 ? 1 : 0;
}
