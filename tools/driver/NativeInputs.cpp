//===- NativeInputs.cpp - Direct source/native input classification -------===//

#include "NativeInputs.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
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

// Universal files are linked through their native slice, which may be an
// object, an archive, or a dylib.
Expected<file_magic> identifyUniversalSlice(StringRef path) {
  auto buffer = MemoryBuffer::getFile(path);
  if (!buffer)
    return errorCodeToError(buffer.getError());
  auto universal =
      object::MachOUniversalBinary::create((*buffer)->getMemBufferRef());
  if (!universal)
    return universal.takeError();
  auto slice = (*universal)->getObjectForArch("arm64");
  if (!slice)
    return slice.takeError();
  StringRef contents = (*buffer)->getBuffer();
  if (slice->getOffset() > contents.size() ||
      slice->getSize() > contents.size() - slice->getOffset())
    return createStringError(inconvertibleErrorCode(),
                             "universal arm64 slice is truncated");
  return identify_magic(contents.substr(slice->getOffset(), slice->getSize()));
}

struct MachODylib {
  std::string installName;
  // Undefined symbols left for the simulator image to provide at load time.
  std::vector<std::string> hostImports;
  bool flatNamespace = false;
  bool hasVPIStartup = false;
};

Expected<MachODylib> readMachODylib(StringRef path) {
  auto buffer = MemoryBuffer::getFile(path);
  if (!buffer)
    return errorCodeToError(buffer.getError());
  auto binary = object::createBinary((*buffer)->getMemBufferRef());
  if (!binary)
    return binary.takeError();
  std::unique_ptr<object::MachOObjectFile> slice;
  auto *macho = dyn_cast<object::MachOObjectFile>(binary->get());
  if (auto *universal = dyn_cast<object::MachOUniversalBinary>(binary->get())) {
    auto selected = universal->getMachOObjectForArch("arm64");
    if (!selected)
      return selected.takeError();
    slice = std::move(*selected);
    macho = slice.get();
  }
  if (!macho || !macho->is64Bit() ||
      macho->getArch() != Triple(OBELISK_NATIVE_TARGET_TRIPLE).getArch() ||
      macho->getHeader64().cpusubtype != MachO::CPU_SUBTYPE_ARM64_ALL)
    return createStringError(inconvertibleErrorCode(),
                             "Mach-O library is not for the native ARM64 target");
  if (macho->getHeader64().filetype != MachO::MH_DYLIB)
    return createStringError(inconvertibleErrorCode(),
                             "Mach-O input is not a dynamic library");
  MachODylib result;
  // Inspect the export trie rather than dlopen: a DPI/VPI library may import
  // simulator symbols that only the generated executable will provide.
  Error exportError = Error::success();
  for (const object::ExportEntry &entry : macho->exports(exportError))
    result.hasVPIStartup |= entry.name() == "_vlog_startup_routines";
  if (exportError)
    return std::move(exportError);
  // A two-level image marks the imports it expects from its host with the
  // dynamic-lookup ordinal. Weak imports are optional and may stay unbound.
  result.flatNamespace = !(macho->getHeader64().flags & MachO::MH_TWOLEVEL);
  if (!result.flatNamespace) {
    for (const object::SymbolRef &symbol : macho->symbols()) {
      MachO::nlist_64 entry =
          macho->getSymbol64TableEntry(symbol.getRawDataRefImpl());
      if ((entry.n_type & MachO::N_STAB) ||
          (entry.n_type & MachO::N_TYPE) != MachO::N_UNDF ||
          !(entry.n_type & MachO::N_EXT) || entry.n_value != 0 ||
          (entry.n_desc & MachO::N_WEAK_REF) ||
          MachO::GET_LIBRARY_ORDINAL(entry.n_desc) !=
              MachO::DYNAMIC_LOOKUP_ORDINAL)
        continue;
      Expected<StringRef> name = symbol.getName();
      if (!name)
        return name.takeError();
      result.hostImports.push_back(name->str());
    }
  }
  for (const auto &command : macho->load_commands()) {
    if (command.C.cmd != MachO::LC_ID_DYLIB)
      continue;
    auto dylib = macho->getDylibIDLoadCommand(command);
    if (dylib.dylib.name < sizeof(MachO::dylib_command) ||
        dylib.dylib.name >= command.C.cmdsize)
      break;
    StringRef name(command.Ptr + dylib.dylib.name,
                   command.C.cmdsize - dylib.dylib.name);
    size_t end = name.find('\0');
    if (end == StringRef::npos || end == 0)
      break;
    result.installName = name.take_front(end).str();
    return std::move(result);
  }
  return createStringError(inconvertibleErrorCode(),
                           "Mach-O library has no valid LC_ID_DYLIB install name");
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
  const bool macho = magic == file_magic::macho_dynamically_linked_shared_lib ||
                     magic == file_magic::macho_universal_binary;
  std::optional<std::string> embeddedName;
  bool hasVPIStartup = false;
  std::vector<std::string> hostImports;
  bool flatNamespace = false;
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
  } else if (macho) {
    Expected<MachODylib> dylib = readMachODylib(canonical);
    if (!dylib) {
      error(Twine("could not inspect dylib '") + suppliedPath +
            "': " + toString(dylib.takeError()));
      return failure();
    }
    // dyld resolves a relative install name against the working directory,
    // and the executable cannot rename it without invalidating its signature.
    if (!StringRef(dylib->installName).starts_with("/") &&
        !StringRef(dylib->installName).starts_with("@")) {
      error(Twine("dylib '") + suppliedPath + "' has relative install name '" +
            dylib->installName + "'; relink it with -install_name @rpath/" +
            sys::path::filename(dylib->installName));
      return failure();
    }
    embeddedName = std::move(dylib->installName);
    hostImports = std::move(dylib->hostImports);
    flatNamespace = dylib->flatNamespace;
    hasVPIStartup = dylib->hasVPIStartup;
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

  if (!coff && !macho) {
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
  input.hostImports = std::move(hostImports);
  input.flatNamespace = flatNamespace;
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
  static const Triple target(OBELISK_NATIVE_TARGET_TRIPLE);
  switch (magic) {
  case file_magic::elf_relocatable:
  case file_magic::elf_shared_object:
  case file_magic::elf_executable:
    return target.isOSBinFormatELF();
  case file_magic::coff_object:
  case file_magic::coff_import_library:
  case file_magic::pecoff_executable:
    return target.isOSBinFormatCOFF();
  case file_magic::macho_object:
  case file_magic::macho_executable:
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_universal_binary:
    return target.isOSBinFormatMachO();
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
    file_magic linkMagic = magic;
    if (magic == file_magic::macho_universal_binary) {
      Expected<file_magic> slice = identifyUniversalSlice(input);
      if (!slice) {
        error(Twine("could not inspect universal binary '") + input +
              "': " + toString(slice.takeError()));
        return failure();
      }
      linkMagic = *slice;
    }
    bool native = false;
    switch (linkMagic) {
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
    case file_magic::macho_object:
    case file_magic::bitcode:
    case file_magic::archive:
      native = true;
      break;
    case file_magic::elf_shared_object:
    case file_magic::pecoff_executable:
    case file_magic::macho_dynamically_linked_shared_lib:
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
    case file_magic::macho_executable:
      error(Twine("Mach-O executable input is not linkable: '") + input + "'");
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
