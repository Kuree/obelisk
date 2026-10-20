//===- NativeInputs.cpp - Direct source/native input classification -------===//

#include "NativeInputs.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include <cctype>
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
  Triple targetTriple(OBELISK_NATIVE_TARGET_TRIPLE);
  // ELF64LE covers every Linux target in the pinned LLVM distribution.
  if (elf->getHeader().e_machine !=
      ELF::convertArchNameToEMachine(targetTriple.getArchName()))
    return createStringError(inconvertibleErrorCode(),
                             "ELF shared object is not for the native target");
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

LogicalResult classifySharedLibrary(StringRef suppliedPath, file_magic magic,
                                    StringRef vpiMode,
                                    StringSet<> &canonicalPaths,
                                    DenseSet<sys::fs::UniqueID> &uniqueFiles,
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
  sys::fs::UniqueID uniqueID;
  if (std::error_code ec = sys::fs::getUniqueID(canonical, uniqueID)) {
    error(Twine("could not identify shared library '") + suppliedPath +
          "': " + ec.message());
    return failure();
  }
  if (!uniqueFiles.insert(uniqueID).second)
    return success();

  const bool coff = magic == file_magic::pecoff_executable;
  std::optional<std::string> embeddedName;
  bool hasVPIStartup = false;
  if (coff) {
    auto buffer = MemoryBuffer::getFile(canonical);
    if (!buffer) {
      error(Twine("could not read DLL '") + suppliedPath +
            "': " + buffer.getError().message());
      return failure();
    }
    auto object = object::COFFObjectFile::create((*buffer)->getMemBufferRef());
    if (!object) {
      error(Twine("could not inspect DLL '") + suppliedPath +
            "': " + toString(object.takeError()));
      return failure();
    }
    if (!((*object)->getCharacteristics() & COFF::IMAGE_FILE_DLL)) {
      error(Twine("PE executable input is not linkable: '") + suppliedPath +
            "'");
      return failure();
    }
    Triple target(OBELISK_NATIVE_TARGET_TRIPLE);
    // ARM64EC and ARM64X use a hybrid ABI, distinct from native ARM64.
    if (!(*object)->is64() || (*object)->getArch() != target.getArch() ||
        (*object)->getMachine() == COFF::IMAGE_FILE_MACHINE_ARM64EC ||
        (*object)->getMachine() == COFF::IMAGE_FILE_MACHINE_ARM64X) {
      error(Twine("DLL is not for the native Windows target '") + target.str() +
            "': '" + suppliedPath + "'");
      return failure();
    }
    for (const object::ExportDirectoryEntryRef &entry :
         (*object)->export_directories()) {
      StringRef name;
      if (Error ec = entry.getSymbolName(name)) {
        error(Twine("could not inspect DLL exports: ") +
              toString(std::move(ec)));
        return failure();
      }
      hasVPIStartup |= name == "vlog_startup_routines";
    }
  } else {
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
    embeddedName = std::move(*soname);
  }

  SmallString<256> parent(sys::path::parent_path(suppliedPath));
  sys::path::remove_dots(parent, true);
  std::string basename = sys::path::filename(suppliedPath).str();
  if (basename.empty()) {
    error(Twine("shared library input has no basename: '") + suppliedPath +
          "'");
    return failure();
  }
  // Windows DLL dependencies are staged beside the executable and loaded by
  // basename. ELF retains its supplied directory through the native runpath.
  std::string loaderName = embeddedName.value_or(basename);
  std::string identityName = coff ? StringRef(basename).lower() : loaderName;
  auto identity = loaderIdentities.try_emplace(identityName, canonical.str());
  if (!identity.second && identity.first->second != canonical) {
    error(Twine("shared libraries '") + identity.first->second + "' and '" +
          suppliedPath + "' have duplicate runtime loader identity '" +
          identityName + "'");
    return failure();
  }

  if (!coff) {
    std::string loaderDiagnostic;
    sys::DynamicLibrary library =
        sys::DynamicLibrary::getLibrary(canonical.c_str(), &loaderDiagnostic);
    if (!library.isValid()) {
      error(Twine("could not load shared library '") + suppliedPath +
            "' while probing VPI startup: " + loaderDiagnostic);
      return failure();
    }
    hasVPIStartup =
        library.getAddressOfSymbol("vlog_startup_routines") != nullptr;
    sys::DynamicLibrary::closeLibrary(library);
  }
  if (hasVPIStartup && vpiMode == "off") {
    error(Twine("shared library '") + suppliedPath +
          "' exports vlog_startup_routines but --vpi=off");
    return failure();
  }

  SharedLibraryInput input;
  input.canonicalPath = canonical.str().str();
  input.suppliedDirectory = parent.str().str();
  if (input.suppliedDirectory.empty())
    input.suppliedDirectory = ".";
  input.basename = std::move(basename);
  input.loaderName = std::move(loaderName);
  input.hasEmbeddedLoaderIdentity = embeddedName.has_value();
  input.hasVPIStartup = hasVPIStartup;
  input.suppliedPathWasAbsolute = sys::path::is_absolute(suppliedPath);
  result.sharedLibraries.push_back(std::move(input));
  result.nativeLinkInputs.push_back({NativeLinkInput::Kind::SharedLibrary,
                                     {},
                                     result.sharedLibraries.size() - 1});
  return success();
}

} // namespace

// The registered native backend links one object format. Native inputs in
// the other format are rejected like any other unsupported binary.
bool matchesNativeObjectFormat(file_magic magic) {
  static const bool coff =
      Triple(OBELISK_NATIVE_TARGET_TRIPLE).isOSBinFormatCOFF();
  switch (magic) {
  case file_magic::elf_relocatable:
  case file_magic::elf_shared_object:
  case file_magic::elf_executable:
    return !coff;
  case file_magic::coff_object:
  case file_magic::coff_import_library:
  case file_magic::pecoff_executable:
    return coff;
  default:
    return true;
  }
}

LogicalResult classifyDirectInputs(ArrayRef<std::string> inputs,
                                   bool nativeInputsAllowed, StringRef vpiMode,
                                   ClassifiedInputs &result) {
  StringSet<> canonicalSharedLibraries;
  DenseSet<sys::fs::UniqueID> uniqueSharedLibraries;
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
    if (!matchesNativeObjectFormat(magic)) {
      error(Twine("unsupported binary input format for '") + input + "'");
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
    case file_magic::coff_object:
    case file_magic::coff_import_library:
    case file_magic::bitcode:
    case file_magic::archive:
      native = true;
      break;
    case file_magic::elf_shared_object:
    case file_magic::pecoff_executable:
      native = true;
      if (!nativeInputsAllowed) {
        error(Twine("native input '") + input +
              "' is only valid when linking a final executable");
        return failure();
      }
      if (failed(classifySharedLibrary(
              input, magic, vpiMode, canonicalSharedLibraries,
              uniqueSharedLibraries, loaderIdentities, result)))
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
