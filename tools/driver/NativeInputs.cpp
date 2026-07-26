//===- NativeInputs.cpp - Direct source/native input classification -------===//

#include "NativeInputs.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

using namespace llvm;
using namespace mlir;

namespace obelisk::driver {
namespace {

void error(const Twine &message) {
  WithColor::error(errs(), "obelisk") << message << '\n';
}

bool isTextual(StringRef contents) {
  // SystemVerilog sources are byte streams in practice.  Accept ordinary
  // printable bytes and whitespace, including UTF-8/non-ASCII source text,
  // while rejecting the control bytes characteristic of unsupported binary
  // containers.  NUL is never meaningful in a source file.
  for (unsigned char byte : contents.bytes()) {
    if (byte == 0)
      return false;
    if (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r' &&
        byte != '\f')
      return false;
  }
  return true;
}

Expected<std::optional<std::string>> readELFSoname(StringRef path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> buffer = MemoryBuffer::getFile(path);
  if (!buffer)
    return errorCodeToError(buffer.getError());
  Expected<object::ELFFile<object::ELF64LE>> elf =
      object::ELFFile<object::ELF64LE>::create((*buffer)->getBuffer());
  if (!elf)
    return elf.takeError();
  if (elf->getHeader().e_machine != ELF::EM_X86_64)
    return createStringError(inconvertibleErrorCode(),
                             "ELF shared object is not x86-64");
  Expected<object::ELFFile<object::ELF64LE>::Elf_Shdr_Range> sections =
      elf->sections();
  if (!sections)
    return sections.takeError();
  for (const object::ELF64LE::Shdr &section : *sections) {
    if (section.sh_type != ELF::SHT_DYNAMIC)
      continue;
    Expected<ArrayRef<object::ELF64LE::Dyn>> entries =
        elf->template getSectionContentsAsArray<object::ELF64LE::Dyn>(section);
    if (!entries)
      return entries.takeError();
    Expected<const object::ELF64LE::Shdr *> stringSection =
        elf->getSection(section.sh_link);
    if (!stringSection)
      return stringSection.takeError();
    Expected<StringRef> strings = elf->getStringTable(**stringSection);
    if (!strings)
      return strings.takeError();
    for (const object::ELF64LE::Dyn &entry : *entries) {
      if (entry.d_tag != ELF::DT_SONAME)
        continue;
      uint64_t offset = entry.getVal();
      if (offset >= strings->size())
        return createStringError(inconvertibleErrorCode(),
                                 "DT_SONAME has an invalid string offset");
      StringRef tail = strings->drop_front(offset);
      size_t terminator = tail.find('\0');
      if (terminator == StringRef::npos)
        return createStringError(inconvertibleErrorCode(),
                                 "DT_SONAME is not NUL terminated");
      return std::optional<std::string>(tail.take_front(terminator).str());
    }
  }
  return std::optional<std::string>();
}

LogicalResult classifySharedLibrary(StringRef suppliedPath, StringRef vpiMode,
                                    StringSet<> &canonicalPaths,
                                    StringMap<std::string> &loaderIdentities,
                                    ClassifiedInputs &result) {
  SmallString<256> canonical;
  if (std::error_code ec = sys::fs::real_path(suppliedPath, canonical)) {
    error(Twine("could not resolve shared library '") + suppliedPath +
          "': " + ec.message());
    return failure();
  }
  if (!canonicalPaths.insert(canonical).second)
    return success();

  Expected<std::optional<std::string>> soname = readELFSoname(canonical);
  if (!soname) {
    error(Twine("could not inspect shared library '") + suppliedPath +
          "': " + toString(soname.takeError()));
    return failure();
  }
  if (soname->has_value() && (*soname)->empty()) {
    error(Twine("shared library '") + suppliedPath +
          "' has an empty DT_SONAME");
    return failure();
  }
  if (soname->has_value() && (*soname)->find('/') != std::string::npos) {
    error(Twine("shared library '") + suppliedPath +
          "' has a DT_SONAME containing '/'");
    return failure();
  }

  std::filesystem::path supplied(suppliedPath.str());
  std::filesystem::path parent = supplied.has_parent_path()
                                     ? supplied.parent_path()
                                     : std::filesystem::path(".");
  std::string basename = supplied.filename().string();
  if (basename.empty()) {
    error(Twine("shared library input has no basename: '") + suppliedPath +
          "'");
    return failure();
  }
  std::string loaderName = soname->has_value() ? **soname : basename;
  auto identity = loaderIdentities.try_emplace(loaderName, canonical.str());
  if (!identity.second && identity.first->second != canonical) {
    error(Twine("shared libraries '") + identity.first->second + "' and '" +
          suppliedPath + "' have duplicate runtime loader identity '" +
          loaderName + "'");
    return failure();
  }

  std::string loaderDiagnostic;
  sys::DynamicLibrary library =
      sys::DynamicLibrary::getLibrary(canonical.c_str(), &loaderDiagnostic);
  if (!library.isValid()) {
    error(Twine("could not load shared library '") + suppliedPath +
          "' while probing VPI startup: " + loaderDiagnostic);
    return failure();
  }
  bool hasVPIStartup =
      library.getAddressOfSymbol("vlog_startup_routines") != nullptr;
  sys::DynamicLibrary::closeLibrary(library);
  if (hasVPIStartup && vpiMode == "off") {
    error(Twine("shared library '") + suppliedPath +
          "' exports vlog_startup_routines but --vpi=off");
    return failure();
  }

  SharedLibraryInput input;
  input.canonicalPath = canonical.str().str();
  input.suppliedDirectory = parent.lexically_normal().string();
  if (input.suppliedDirectory.empty())
    input.suppliedDirectory = ".";
  input.basename = std::move(basename);
  input.loaderName = std::move(loaderName);
  input.hasSoname = soname->has_value();
  input.hasVPIStartup = hasVPIStartup;
  input.suppliedPathWasAbsolute = supplied.is_absolute();
  result.sharedLibraries.push_back(std::move(input));
  result.nativeLinkInputs.push_back({NativeLinkInput::Kind::SharedLibrary,
                                     {},
                                     result.sharedLibraries.size() - 1});
  return success();
}

} // namespace

LogicalResult classifyDirectInputs(ArrayRef<std::string> inputs,
                                   bool nativeInputsAllowed, StringRef vpiMode,
                                   ClassifiedInputs &result) {
  StringSet<> canonicalSharedLibraries;
  StringMap<std::string> loaderIdentities;
  for (const std::string &input : inputs) {
    if (input == "-") {
      result.systemVerilog.push_back(input);
      continue;
    }
    file_magic magic;
    if (std::error_code ec = identify_magic(input, magic)) {
      error(Twine("could not inspect input '") + input + "': " + ec.message());
      return failure();
    }
    bool native = false;
    switch (magic) {
    case file_magic::unknown: {
      ErrorOr<std::unique_ptr<MemoryBuffer>> buffer =
          MemoryBuffer::getFile(input);
      if (!buffer) {
        error(Twine("could not read input '") + input +
              "': " + buffer.getError().message());
        return failure();
      }
      if (!isTextual((*buffer)->getBuffer())) {
        error(Twine("unsupported binary input format for '") + input + "'");
        return failure();
      }
      result.systemVerilog.push_back(input);
      continue;
    }
    case file_magic::elf_relocatable:
    case file_magic::bitcode:
    case file_magic::archive:
      native = true;
      break;
    case file_magic::elf_shared_object:
      native = true;
      if (!nativeInputsAllowed) {
        error(Twine("native input '") + input +
              "' is only valid when linking a final executable");
        return failure();
      }
      if (failed(classifySharedLibrary(input, vpiMode, canonicalSharedLibraries,
                                       loaderIdentities, result)))
        return failure();
      continue;
    case file_magic::elf_executable:
      error(Twine("ELF executable input is not linkable: '") + input + "'");
      return failure();
    default:
      error(Twine("unsupported binary input format for '") + input + "'");
      return failure();
    }
    if (native) {
      if (!nativeInputsAllowed) {
        error(Twine("native input '") + input +
              "' is only valid when linking a final executable");
        return failure();
      }
      result.nativeLinkInputs.push_back(
          {NativeLinkInput::Kind::File, input, 0});
    }
  }
  return success();
}

} // namespace obelisk::driver
