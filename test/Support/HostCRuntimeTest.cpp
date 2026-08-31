//===- HostCRuntimeTest.cpp - Synthetic host runtime layouts -------------===//

#include "HostCRuntime.h"

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <initializer_list>
#include <string>

using namespace llvm;
using namespace mlir;
using namespace obelisk::driver;

namespace {

IntrusiveRefCntPtr<vfs::InMemoryFileSystem>
makeTree(std::initializer_list<StringRef> files, bool addDynamicLinker = true) {
  auto fs = makeIntrusiveRefCnt<vfs::InMemoryFileSystem>();
  fs->setCurrentWorkingDirectory("/");
  if (addDynamicLinker)
    fs->addFile("/lib64/ld-linux-x86-64.so.2", 0,
                MemoryBuffer::getMemBuffer(""));
  for (StringRef path : files)
    fs->addFile(path, 0, MemoryBuffer::getMemBuffer(""));
  return fs;
}

bool checkLayout(StringRef directory) {
  auto fs =
      makeTree({(directory + "/Scrt1.o").str(), (directory + "/crti.o").str(),
                (directory + "/crtn.o").str(), (directory + "/libc.so").str(),
                (directory + "/libm.so").str()});
  FailureOr<HostCRuntimeInputs> inputs = discoverHostCRuntime(
      "x86_64-unknown-linux-gnu", "/opt/obelisk/bin/obelisk", fs);
  if (failed(inputs))
    return false;
  return StringRef(inputs->crt1).starts_with(directory) &&
         StringRef(inputs->crti).starts_with(directory) &&
         StringRef(inputs->crtn).starts_with(directory) &&
         StringRef(inputs->libc).starts_with(directory) &&
         StringRef(inputs->libm).starts_with(directory);
}

} // namespace

int main() {
  if (!checkLayout("/usr/lib/x86_64-linux-gnu")) {
    errs() << "multiarch C-runtime discovery failed\n";
    return 1;
  }
  if (!checkLayout("/lib64")) {
    errs() << "RHEL-like C-runtime discovery failed\n";
    return 1;
  }
  auto noDynamicLinker = makeTree(
      {"/usr/lib/x86_64-linux-gnu/Scrt1.o", "/usr/lib/x86_64-linux-gnu/crti.o",
       "/usr/lib/x86_64-linux-gnu/crtn.o", "/usr/lib/x86_64-linux-gnu/libc.so",
       "/usr/lib/x86_64-linux-gnu/libm.so"},
      /*addDynamicLinker=*/false);
  if (succeeded(discoverHostCRuntime("x86_64-unknown-linux-gnu",
                                     "/opt/obelisk/bin/obelisk",
                                     noDynamicLinker))) {
    errs() << "missing dynamic linker was accepted\n";
    return 1;
  }
  auto missing = makeTree({"/usr/lib/x86_64-linux-gnu/crti.o",
                           "/usr/lib/x86_64-linux-gnu/crtn.o",
                           "/usr/lib/x86_64-linux-gnu/libc.so",
                           "/usr/lib/x86_64-linux-gnu/libm.so"});
  if (succeeded(discoverHostCRuntime("x86_64-unknown-linux-gnu",
                                     "/opt/obelisk/bin/obelisk", missing))) {
    errs() << "missing Scrt1.o was accepted\n";
    return 1;
  }
  return 0;
}
