//===- driver.cpp - Obelisk compiler driver -------------------------------===//
//
// This executable owns compilation policy: source files, command files,
// include paths, macros, libraries, language revision, and the selected output
// action.
//
//===----------------------------------------------------------------------===//

#include "NativeInputs.h"
#if OBELISK_HAS_NATIVE_BACKEND
#include "HostCRuntime.h"
#endif
#include "DriverMain.h"
#include "Options.h"
#include "TargetBackend.h"

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Conversion/SlangToObelisk.h"
#include "obelisk/Dialect/Obelisk/ObeliskDialect.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Slang/SlangDialect.h"
#include "obelisk/Dialect/Slang/SlangOps.h"
#include "obelisk/Frontend/Frontend.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/PassManager.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::opt;
using namespace mlir;
using namespace obelisk::driver::options;

namespace {

static std::string driverExecutablePath;

static void emitDriverError(const Twine &message) {
  WithColor::error(errs(), "obelisk") << message << '\n';
}

static bool validateCoverageConfiguration(StringRef path,
                                          std::string &contents) {
  auto buffer = MemoryBuffer::getFile(path);
  if (!buffer) {
    emitDriverError(Twine("cannot read coverage configuration '") + path +
                    "': " + buffer.getError().message());
    return false;
  }
  contents = (*buffer)->getBuffer().str();
  Expected<json::Value> parsed = json::parse(contents);
  if (!parsed) {
    emitDriverError(Twine("invalid coverage configuration '") + path +
                    "': " + toString(parsed.takeError()));
    return false;
  }
  json::Object *root = parsed->getAsObject();
  if (!root) {
    emitDriverError("coverage configuration root must be an object");
    return false;
  }
  for (const auto &field : *root)
    if (field.first != "include" && field.first != "exclude") {
      emitDriverError(Twine("unknown coverage configuration field '") +
                      field.first.str() + "'");
      return false;
    }
  for (StringRef group : {"include", "exclude"}) {
    const json::Value *records = root->get(group);
    if (!records)
      continue;
    const json::Array *array = records->getAsArray();
    if (!array) {
      emitDriverError(Twine("coverage '") + group + "' must be an array");
      return false;
    }
    for (const json::Value &entry : *array) {
      const json::Object *record = entry.getAsObject();
      if (!record) {
        emitDriverError(Twine("coverage '") + group +
                        "' entries must be objects");
        return false;
      }
      for (const auto &field : *record)
        if (field.first != "metrics" && field.first != "file" &&
            field.first != "hierarchy" && field.first != "reason") {
          emitDriverError(Twine("unknown coverage rule field '") +
                          field.first.str() + "'");
          return false;
        }
      if (const json::Value *metrics = record->get("metrics")) {
        const json::Array *list = metrics->getAsArray();
        if (!list || list->empty()) {
          emitDriverError("coverage rule metrics must be a nonempty array");
          return false;
        }
        llvm::StringSet<> seen;
        for (const json::Value &metricValue : *list) {
          std::optional<StringRef> metric = metricValue.getAsString();
          if (!metric ||
              (*metric != "line" && *metric != "toggle" &&
               *metric != "functional") ||
              !seen.insert(*metric).second) {
            emitDriverError(
                "coverage rule metrics must contain unique line, toggle, or "
                "functional names");
            return false;
          }
        }
      }
      for (StringRef selector : {"file", "hierarchy", "reason"})
        if (const json::Value *value = record->get(selector))
          if (!value->getAsString()) {
            emitDriverError(Twine("coverage rule '") + selector +
                            "' must be a string");
            return false;
          }
      if (group == "include" && record->get("reason")) {
        emitDriverError("coverage include rules cannot carry a reason");
        return false;
      }
      if (auto file = record->getString("file"); file && file->contains('\\')) {
        emitDriverError("coverage file globs must use '/' separators");
        return false;
      }
      if (auto hierarchy = record->getString("hierarchy");
          hierarchy && hierarchy->contains('/')) {
        emitDriverError("coverage hierarchy globs must use '.' separators");
        return false;
      }
    }
  }
  return true;
}

static std::string resolveSVLibraryPath(StringRef root, StringRef path,
                                        bool appendExtension) {
  SmallString<256> resolved;
  if (!sys::path::is_absolute(path) && !root.empty()) {
    resolved = root;
    sys::path::append(resolved, path);
  } else {
    resolved = path;
  }
  if (appendExtension)
    resolved += ".so";
  sys::path::remove_dots(resolved, true);
  return resolved.str().str();
}

static bool collectSVLibraryInputs(const InputArgList &args,
                                   SmallVectorImpl<std::string> &libraries) {
  SmallVector<std::string> bootstrapLibraries;
  SmallVector<std::string> directLibraries;
  std::string root;
  for (const Arg *arg : args) {
    if (arg->getOption().matches(OPT_sv_root)) {
      root = arg->getValue();
      continue;
    }
    if (arg->getOption().matches(OPT_sv_lib)) {
      directLibraries.push_back(
          resolveSVLibraryPath(root, arg->getValue(), true));
      continue;
    }
    if (!arg->getOption().matches(OPT_sv_liblist))
      continue;

    std::string bootstrapPath =
        resolveSVLibraryPath(root, arg->getValue(), false);
    ErrorOr<std::unique_ptr<MemoryBuffer>> buffer =
        MemoryBuffer::getFile(bootstrapPath, /*IsText=*/true);
    if (!buffer) {
      emitDriverError(Twine("could not read DPI bootstrap file '") +
                      bootstrapPath + "': " + buffer.getError().message());
      return false;
    }
    SmallVector<StringRef> lines;
    (*buffer)->getBuffer().split(lines, '\n');
    if (lines.empty() || lines.front().rtrim("\r") != "#!SV_LIBRARIES") {
      emitDriverError(Twine("DPI bootstrap file '") + bootstrapPath +
                      "' must begin with #!SV_LIBRARIES");
      return false;
    }
    for (auto [lineNumber, original] :
         llvm::enumerate(ArrayRef<StringRef>(lines).drop_front())) {
      StringRef line = original.rtrim("\r");
      StringRef trimmed = line.trim();
      if (trimmed.empty() || trimmed.starts_with("#"))
        continue;
      if (line.empty() || (line.front() != ' ' && line.front() != '\t')) {
        emitDriverError(Twine("DPI bootstrap file '") + bootstrapPath +
                        "' line " + Twine(lineNumber + 2) +
                        " must indent its library path");
        return false;
      }
      bootstrapLibraries.push_back(resolveSVLibraryPath(root, trimmed, true));
    }
  }
  llvm::StringSet<> seen;
  auto appendUnique = [&](ArrayRef<std::string> candidates) {
    for (const std::string &candidate : candidates)
      if (seen.insert(candidate).second)
        libraries.push_back(candidate);
  };
  // Annex J gives bootstrap entries precedence over direct -sv_lib entries,
  // independent of where their options occur on the command line.
  appendUnique(bootstrapLibraries);
  appendUnique(directLibraries);
  return true;
}

// Command files are the delivery format for real testbenches, and every other
// tool treats them as "my whole command line, in a file" rather than a
// frontend-only filelist. Expanding them into the driver's own argv before
// option parsing is what makes that true here: a `.f` may carry any obelisk
// option, not just the ones slang happens to understand. The frontend still
// receives everything, because buildSlangArguments() reconstructs slang's
// command line from the parsed options rather than forwarding the file.
//
// Tokenization mirrors slang's own command-file lexer (`#`, `//` and `/*`
// comments, `$VAR` expansion, backslash escapes, single and double quotes) so
// that a file accepted before is tokenized identically now.
static void tokenizeCommandFile(StringRef text, StringSaver &saver,
                                SmallVectorImpl<const char *> &tokens) {
  std::string current;
  bool pending = false;
  auto finish = [&]() {
    if (pending) {
      tokens.push_back(saver.save(StringRef(current)).data());
      current.clear();
      pending = false;
    }
  };

  const char *ptr = text.begin();
  const char *end = text.end();
  while (ptr != end) {
    char c = *ptr++;
    if (isSpace(static_cast<unsigned char>(c)) || c == '\0') {
      finish();
      continue;
    }

    // A '#' always starts a comment; '/' only when not already mid-argument,
    // so that path components like `a//b` survive.
    if (c == '#') {
      finish();
      while (ptr != end && *ptr != '\n' && *ptr != '\r')
        ++ptr;
      continue;
    }
    if (c == '/' && !pending && ptr != end) {
      if (*ptr == '/') {
        ++ptr;
        while (ptr != end && *ptr != '\n' && *ptr != '\r')
          ++ptr;
        continue;
      }
      if (*ptr == '*') {
        ++ptr;
        while (ptr != end) {
          char inner = *ptr++;
          if (inner == '*' && ptr != end && *ptr == '/') {
            ++ptr;
            break;
          }
        }
        continue;
      }
    }

    if (c == '$' && ptr != end) {
      // ${NAME} and $NAME both expand; an unset variable expands to nothing,
      // matching slang.
      const char *nameStart = ptr;
      bool braced = *ptr == '{';
      if (braced)
        ++nameStart;
      const char *scan = nameStart;
      while (scan != end &&
             (isAlnum(static_cast<unsigned char>(*scan)) || *scan == '_'))
        ++scan;
      if (scan != nameStart && (!braced || (scan != end && *scan == '}'))) {
        StringRef name(nameStart, scan - nameStart);
        ptr = braced ? scan + 1 : scan;
        if (std::optional<std::string> value = sys::Process::GetEnv(name)) {
          current.append(*value);
          pending = true;
        }
        continue;
      }
    }

    if (c == '\\') {
      if (ptr != end && *ptr != '\n' && *ptr != '\r') {
        current.push_back(*ptr++);
        pending = true;
      }
      continue;
    }

    // Any non-whitespace character starts an argument, so that a quoted empty
    // string still produces a token.
    pending = true;

    if (c == '\'') {
      while (ptr != end && *ptr != '\'')
        current.push_back(*ptr++);
      if (ptr != end)
        ++ptr;
      continue;
    }
    if (c == '"') {
      while (ptr != end && *ptr != '"') {
        char inner = *ptr++;
        if (inner == '\\' && ptr != end)
          inner = *ptr++;
        current.push_back(inner);
      }
      if (ptr != end)
        ++ptr;
      continue;
    }
    current.push_back(c);
  }
  finish();
}

/// Splices the contents of every `-f`/`--filelist` command file into `argv`,
/// recursively. Returns false after reporting a read error or a cycle.
///
/// This runs before option parsing, so it matches `-f` lexically rather than
/// semantically: a literal `-f` supplied as some other option's separate value
/// (`-D -f`) would be taken as a command file. Response-file expansion has the
/// same ambiguity everywhere it exists, and the alternative is parsing twice.
static bool expandCommandFiles(SmallVectorImpl<const char *> &argv,
                               StringSaver &saver,
                               SmallVectorImpl<std::string> &activeFiles) {
  // A testbench that includes a shared `.f` twice is legitimate; only a file
  // that (transitively) includes itself is an error, so track the active
  // chain rather than every file ever visited.
  static constexpr unsigned kMaxDepth = 64;
  if (activeFiles.size() > kMaxDepth) {
    emitDriverError("command files nested more than " + Twine(kMaxDepth) +
                    " levels deep");
    return false;
  }

  SmallVector<const char *> expanded;
  for (size_t index = 0, size = argv.size(); index != size; ++index) {
    StringRef argument(argv[index]);
    if (argument != "-f" && argument != "--filelist") {
      expanded.push_back(argv[index]);
      continue;
    }
    if (index + 1 == size) {
      emitDriverError("missing argument to '" + argument +
                      "' (expected a command file)");
      return false;
    }
    StringRef path(argv[++index]);

    SmallString<256> canonical(path);
    if (std::error_code error = sys::fs::real_path(path, canonical))
      canonical = path;
    if (llvm::is_contained(activeFiles, StringRef(canonical))) {
      emitDriverError("command file '" + path + "' includes itself");
      return false;
    }

    ErrorOr<std::unique_ptr<MemoryBuffer>> buffer =
        MemoryBuffer::getFile(path, /*IsText=*/true);
    if (!buffer) {
      emitDriverError("could not read command file '" + path +
                      "': " + buffer.getError().message());
      return false;
    }

    SmallVector<const char *> nested;
    tokenizeCommandFile((*buffer)->getBuffer(), saver, nested);
    activeFiles.emplace_back(canonical);
    bool expandedNested = expandCommandFiles(nested, saver, activeFiles);
    activeFiles.pop_back();
    if (!expandedNested)
      return false;
    expanded.append(nested.begin(), nested.end());
  }

  argv.assign(expanded.begin(), expanded.end());
  return true;
}

// The compute graph is deliberately operation-independent, but the schedule
// inspector can still relate a fragment to the source location retained by
// its control-flow boundary or owning code unit. Keep this provenance next to
// the diagnostic instead of adding it to the versioned runtime graph schema.
static void printScheduleSourceLocations(obelisk::sim::SimDesignOp design,
                                         obelisk::sim::ComputeGraphAttr graph,
                                         raw_ostream &output) {
  SymbolTable symbols(design);
  bool first = true;
  output << " source_locations = [";
  for (Attribute node : graph.getNodes()) {
    auto fragment = dyn_cast<obelisk::sim::ComputeFragmentAttr>(node);
    if (!fragment)
      continue;
    auto function = symbols.lookup<obelisk::sim::SimFuncOp>(
        fragment.getFunction().getValue());
    if (!function)
      continue;
    Block *block = obelisk::analysis::lookupComputeGraphBlock(
        function, fragment.getBlock());
    Location source =
        block ? block->getTerminator()->getLoc() : function.getLoc();
    auto location = source->findInstanceOf<FileLineColLoc>();
    if (!location)
      location = function.getLoc()->findInstanceOf<FileLineColLoc>();
    if (!location)
      continue;
    if (!first)
      output << ", ";
    first = false;
    output << '#' << fragment.getId() << " = ";
    location.getFilename().print(output);
    output << ':' << location.getLine() << ':' << location.getColumn();
  }
  output << ']';
}

static bool parseUnsignedOption(const ArgList &args, OptSpecifier option,
                                StringRef spelling,
                                std::optional<uint32_t> &result) {
  const Arg *arg = args.getLastArg(option);
  if (!arg)
    return true;
  uint32_t value;
  if (StringRef(arg->getValue()).getAsInteger(10, value)) {
    emitDriverError(Twine("invalid value '") + arg->getValue() + "' for " +
                    spelling);
    return false;
  }
  result = value;
  return true;
}

struct DPIHeaderType {
  std::string spelling;
  std::string suffix;
  bool vector = false;
  bool openArray = false;
  bool aggregate = false;
};

class DPIHeaderTypes {
public:
  void reserveIdentifier(StringRef identifier) {
    reservedIdentifiers.insert(identifier);
  }

  FailureOr<DPIHeaderType> get(mlir::Type type, Location location) {
    FailureOr<obelisk::DPIABIType> abi =
        obelisk::classifyDPIABIType(type, location);
    if (failed(abi))
      return failure();
    if (abi->kind != obelisk::DPIABIKind::UnpackedAggregate) {
      std::string suffix;
      if (abi->isVector())
        suffix = ("[" + Twine((uint64_t{abi->width} + 31) / 32) + "]").str();
      return DPIHeaderType{obelisk::getDPICTypeSpelling(*abi).str(), suffix,
                           abi->isVector(),
                           abi->kind == obelisk::DPIABIKind::OpenArray, false};
    }
    SmallVector<uint64_t> dimensions;
    while (true) {
      if (auto array = dyn_cast<obelisk::ir::RangedUnpackedArrayType>(type)) {
        uint64_t left = static_cast<uint64_t>(array.getLeft());
        uint64_t right = static_cast<uint64_t>(array.getRight());
        uint64_t distance =
            array.getLeft() >= array.getRight() ? left - right : right - left;
        if (distance == UINT64_MAX) {
          emitError(location) << "DPI array extent is not representable";
          return failure();
        }
        dimensions.push_back(distance + 1);
        type = array.getElementType();
        continue;
      }
      if (auto array = dyn_cast<obelisk::ir::UnpackedArrayType>(type)) {
        dimensions.push_back(array.getSize());
        type = array.getElementType();
        continue;
      }
      break;
    }
    if (!dimensions.empty()) {
      FailureOr<DPIHeaderType> element = get(type, location);
      if (failed(element) || element->openArray)
        return failure();
      std::string suffix;
      for (uint64_t dimension : dimensions)
        suffix += ("[" + Twine(dimension) + "]").str();
      suffix += element->suffix;
      return DPIHeaderType{element->spelling, suffix, false, false, true};
    }

    auto aggregate = dyn_cast<obelisk::ir::SourceAggregateType>(type);
    if (!aggregate || aggregate.getIsPacked() || aggregate.getIsUnion()) {
      emitError(location)
          << "DPI header generation requires a named unpacked struct";
      return failure();
    }
    auto found = names.find(type);
    if (found != names.end())
      return DPIHeaderType{found->second, {}, false, false, true};
    std::string name = sanitize(aggregate.getName().getValue());
    if (name.empty())
      name = ("obelisk_dpi_struct_" + Twine(names.size())).str();
    std::string baseName = name;
    for (uint64_t suffix = 1;; ++suffix) {
      auto collision = namedTypes.find(name);
      if ((collision == namedTypes.end() || collision->second == type) &&
          !reservedIdentifiers.contains(name))
        break;
      name = (baseName + "_" + Twine(suffix)).str();
    }
    names.try_emplace(type, name);
    namedTypes.try_emplace(name, type);
    std::string definition = "typedef struct " + name + " {\n";
    llvm::StringSet<> fieldNames;
    for (auto [fieldIndex, attribute] :
         llvm::enumerate(aggregate.getFields())) {
      auto field = dyn_cast<DictionaryAttr>(attribute);
      auto fieldType = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
      auto fieldName = field ? field.getAs<StringAttr>("name") : StringAttr{};
      if (!fieldType || !fieldName) {
        emitError(location) << "DPI struct has incomplete field metadata";
        return failure();
      }
      FailureOr<DPIHeaderType> cField = get(fieldType.getValue(), location);
      if (failed(cField) || cField->openArray) {
        emitError(location) << "DPI struct field has no concrete C type";
        return failure();
      }
      std::string cName = sanitize(fieldName.getValue());
      if (cName.empty())
        cName = ("obelisk_field_" + Twine(fieldIndex)).str();
      std::string base = cName;
      for (uint64_t suffix = 1; !fieldNames.insert(cName).second; ++suffix)
        cName = (base + "_" + Twine(suffix)).str();
      definition +=
          "  " + cField->spelling + " " + cName + cField->suffix + ";\n";
    }
    definition += "} " + name + ";";
    definitions.push_back(std::move(definition));
    return DPIHeaderType{name, {}, false, false, true};
  }

  ArrayRef<std::string> getDefinitions() const { return definitions; }

private:
  static std::string sanitize(StringRef value) {
    std::string result;
    for (char character : value) {
      bool valid = llvm::isAlnum(static_cast<unsigned char>(character)) ||
                   character == '_';
      result += valid ? character : '_';
    }
    if (!result.empty() && llvm::isDigit(result.front()))
      result.insert(result.begin(), '_');
    static constexpr StringLiteral keywords[] = {"alignas",
                                                 "alignof",
                                                 "and",
                                                 "and_eq",
                                                 "asm",
                                                 "atomic_cancel",
                                                 "atomic_commit",
                                                 "atomic_noexcept",
                                                 "auto",
                                                 "bitand",
                                                 "bitor",
                                                 "bool",
                                                 "break",
                                                 "case",
                                                 "catch",
                                                 "char",
                                                 "char8_t",
                                                 "char16_t",
                                                 "char32_t",
                                                 "class",
                                                 "co_await",
                                                 "co_return",
                                                 "co_yield",
                                                 "compl",
                                                 "concept",
                                                 "const",
                                                 "consteval",
                                                 "constexpr",
                                                 "constinit",
                                                 "const_cast",
                                                 "continue",
                                                 "contract_assert",
                                                 "decltype",
                                                 "default",
                                                 "delete",
                                                 "do",
                                                 "double",
                                                 "dynamic_cast",
                                                 "else",
                                                 "enum",
                                                 "explicit",
                                                 "export",
                                                 "extern",
                                                 "false",
                                                 "float",
                                                 "for",
                                                 "friend",
                                                 "goto",
                                                 "if",
                                                 "import",
                                                 "inline",
                                                 "int",
                                                 "long",
                                                 "module",
                                                 "mutable",
                                                 "namespace",
                                                 "new",
                                                 "noexcept",
                                                 "not",
                                                 "not_eq",
                                                 "nullptr",
                                                 "operator",
                                                 "or",
                                                 "or_eq",
                                                 "private",
                                                 "protected",
                                                 "public",
                                                 "reflexpr",
                                                 "register",
                                                 "reinterpret_cast",
                                                 "requires",
                                                 "restrict",
                                                 "return",
                                                 "short",
                                                 "signed",
                                                 "sizeof",
                                                 "static",
                                                 "static_assert",
                                                 "static_cast",
                                                 "struct",
                                                 "switch",
                                                 "synchronized",
                                                 "template",
                                                 "this",
                                                 "thread_local",
                                                 "throw",
                                                 "true",
                                                 "try",
                                                 "typedef",
                                                 "typeid",
                                                 "typename",
                                                 "typeof",
                                                 "typeof_unqual",
                                                 "union",
                                                 "unsigned",
                                                 "using",
                                                 "virtual",
                                                 "void",
                                                 "volatile",
                                                 "wchar_t",
                                                 "while",
                                                 "xor",
                                                 "xor_eq",
                                                 "_Alignas",
                                                 "_Alignof",
                                                 "_Atomic",
                                                 "_BitInt",
                                                 "_Bool",
                                                 "_Complex",
                                                 "_Decimal128",
                                                 "_Decimal32",
                                                 "_Decimal64",
                                                 "_Generic",
                                                 "_Imaginary",
                                                 "_Noreturn",
                                                 "_Static_assert",
                                                 "_Thread_local"};
    if (llvm::is_contained(keywords, result))
      result.insert(0, "obelisk_");
    return result;
  }

  llvm::DenseMap<mlir::Type, std::string> names;
  StringMap<mlir::Type> namedTypes;
  llvm::StringSet<> reservedIdentifiers;
  SmallVector<std::string> definitions;
};

static LogicalResult writeDPIHeader(ModuleOp module, raw_ostream &output) {
  llvm::StringMap<std::string> prototypes;
  DPIHeaderTypes headerTypes;
  module.walk([&](obelisk::ir::SVSubroutineSymbolOp op) {
    bool imported = op.getIsDpiImport().value_or(false);
    StringAttr identifier = imported ? op.getDpiCIdentifierAttr()
                                     : op.getDpiExportCIdentifierAttr();
    if (identifier)
      headerTypes.reserveIdentifier(identifier.getValue());
  });
  WalkResult walked = module.walk([&](obelisk::ir::SVSubroutineSymbolOp op) {
    bool imported = op.getIsDpiImport().value_or(false);
    StringAttr exported = op.getDpiExportCIdentifierAttr();
    if (!imported && !exported)
      return WalkResult::advance();
    StringAttr cIdentifier = imported ? op.getDpiCIdentifierAttr() : exported;
    if (!cIdentifier) {
      op.emitError("DPI subroutine has no resolved C identifier");
      return WalkResult::interrupt();
    }
    SmallVector<std::string> arguments;
    unsigned argumentIndex = 0;
    for (Operation &child : op.getRegion().front()) {
      auto formal = dyn_cast<obelisk::ir::SVFormalArgumentSymbolOp>(child);
      if (!formal)
        continue;
      std::optional<mlir::Type> semanticType = formal.getSemanticType();
      if (!semanticType) {
        formal.emitError("DPI formal has no semantic type");
        return WalkResult::interrupt();
      }
      FailureOr<DPIHeaderType> type =
          headerTypes.get(*semanticType, formal.getLoc());
      if (failed(type))
        return WalkResult::interrupt();
      auto direction = formal.getDirection();
      bool input = direction == obelisk::ir::SVArgumentDirection::In;
      bool pointer =
          !type->openArray && !type->aggregate && (!input || type->vector);
      std::string declaration;
      if (((input && (type->vector || type->aggregate)) || type->openArray) &&
          !StringRef(type->spelling).starts_with("const "))
        declaration += "const ";
      declaration += type->spelling;
      if (type->aggregate && type->suffix.empty())
        pointer = true;
      declaration += pointer ? " *" : " ";
      declaration += ("arg" + Twine(argumentIndex++)).str();
      if (type->aggregate)
        declaration += type->suffix;
      arguments.push_back(std::move(declaration));
    }

    bool task = op.getSubroutineKind() == obelisk::ir::SVSubroutineKind::Task;
    std::string returnType = task ? "int" : "";
    if (!task) {
      auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
      auto subroutine =
          semanticType
              ? dyn_cast<obelisk::ir::SubroutineType>(semanticType.getValue())
              : obelisk::ir::SubroutineType{};
      auto signature = subroutine
                           ? dyn_cast<FunctionType>(subroutine.getSignature())
                           : FunctionType{};
      if (!signature || signature.getNumResults() != 1) {
        op.emitError("DPI function has no resolved result signature");
        return WalkResult::interrupt();
      }
      if (isa<obelisk::ir::VoidType>(signature.getResult(0))) {
        returnType = "void";
      } else {
        FailureOr<DPIHeaderType> result =
            headerTypes.get(signature.getResult(0), op.getLoc());
        if (failed(result))
          return WalkResult::interrupt();
        if (result->vector) {
          returnType = "void";
          arguments.insert(arguments.begin(), result->spelling + " *result");
        } else {
          returnType = result->spelling;
        }
      }
    }
    std::string prototype =
        returnType + " " + cIdentifier.getValue().str() + "(";
    if (arguments.empty()) {
      prototype += "void";
    } else {
      for (auto [index, argument] : llvm::enumerate(arguments)) {
        if (index)
          prototype += ", ";
        prototype += argument;
      }
    }
    prototype += ");";
    auto inserted = prototypes.try_emplace(cIdentifier.getValue(), prototype);
    if (!inserted.second && inserted.first->second != prototype) {
      op.emitError() << "C identifier '" << cIdentifier.getValue()
                     << "' has incompatible DPI import/export signatures";
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();
  output << "#ifndef OBELISK_GENERATED_DPI_H\n"
            "#define OBELISK_GENERATED_DPI_H\n\n"
            "#include <stdint.h>\n"
            "#include <svdpi.h>\n\n";
  for (const std::string &definition : headerTypes.getDefinitions())
    output << definition << "\n\n";
  output << "#ifdef __cplusplus\n"
            "extern \"C\" {\n"
            "#endif\n\n";
  SmallVector<StringRef> names;
  names.reserve(prototypes.size());
  for (auto &entry : prototypes)
    names.push_back(entry.getKey());
  llvm::sort(names);
  for (StringRef name : names)
    output << prototypes.lookup(name) << '\n';
  output << "\n#ifdef __cplusplus\n"
            "}\n"
            "#endif\n\n"
            "#endif\n";
  return success();
}

static void writeBindingReport(ModuleOp module, raw_ostream &output) {
  SmallVector<Operation *> instances;
  module.walk([&](Operation *instance) {
    if (!isa<obelisk::slangir::InstanceSymbolOp,
             obelisk::slangir::CheckerInstanceSymbolOp>(instance))
      return;
    if (instance->hasAttr("configuration") ||
        instance->hasAttr("is_from_bind") ||
        instance->hasAttr("is_below_bind") ||
        instance->hasAttr("is_bind_target"))
      instances.push_back(instance);
  });
  llvm::sort(instances, [](Operation *lhs, Operation *rhs) {
    return lhs->getAttrOfType<StringAttr>("hierarchical_name").getValue() <
           rhs->getAttrOfType<StringAttr>("hierarchical_name").getValue();
  });

  for (Operation *instance : instances) {
    output
        << "binding "
        << instance->getAttrOfType<StringAttr>("hierarchical_name").getValue()
        << " -> "
        << instance->getAttrOfType<StringAttr>("selected_cell").getValue();
    if (instance->getAttrOfType<BoolAttr>("is_from_bind"))
      output << " from-bind";
    if (instance->getAttrOfType<BoolAttr>("is_below_bind"))
      output << " below-bind";
    if (instance->getAttrOfType<BoolAttr>("is_bind_target"))
      output << " bind-target";
    if (StringAttr config =
            instance->getAttrOfType<StringAttr>("configuration"))
      output << " config=" << config.getValue() << " root="
             << instance->getAttrOfType<StringAttr>("configuration_root")
                    .getValue();
    if (ArrayAttr liblist =
            instance->getAttrOfType<ArrayAttr>("configuration_liblist")) {
      output << " liblist=[";
      llvm::interleaveComma(liblist, output, [&](Attribute library) {
        output << cast<StringAttr>(library).getValue();
      });
      output << ']';
    }
    if (StringAttr kind =
            instance->getAttrOfType<StringAttr>("configuration_rule_kind")) {
      output << " rule=" << kind.getValue();
      auto range = cast<obelisk::slangir::SourceRangeType>(
          instance->getAttrOfType<TypeAttr>("configuration_rule_source_range")
              .getValue());
      output << '@' << sys::path::filename(range.getStartFile()) << ':'
             << range.getStartLine() << ':' << range.getStartColumn();
    }
    output << '\n';
  }
}

static obelisk::frontend::FrontendOptions buildFrontendOptions(
    const InputArgList &args, bool &valid,
    const obelisk::driver::ProtectedEnvelopeConfiguration &protectConfig) {
  obelisk::frontend::FrontendOptions options;
  options.includeDirs = args.getAllArgValues(OPT_I);
  options.includeSystemDirs = args.getAllArgValues(OPT_isystem);
  options.defines = args.getAllArgValues(OPT_D);
  options.undefines = args.getAllArgValues(OPT_U);
  options.libDirs = args.getAllArgValues(OPT_y);
  for (Arg *arg : args.filtered(OPT_Y)) {
    arg->claim();
    StringRef value = arg->getValue();

    // The conventional +libext+ spelling carries an ordered '+'-separated
    // list in its joined value. Keep that order when translating to slang's
    // canonical one-extension-per-argument form. A plain -Y value remains a
    // single extension, even if a platform permits '+' in a filename suffix.
    const Arg *alias = arg->getAlias();
    bool isPlusList = alias && alias->getSpelling() == "+libext+";
    SmallVector<StringRef> extensions;
    if (isPlusList)
      value.split(extensions, '+', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
    else
      extensions.push_back(value);

    for (StringRef extension : extensions) {
      if (extension.empty()) {
        emitDriverError(
            Twine("empty module library extension in '") +
            (isPlusList ? alias->getAsString(args) : arg->getAsString(args)) +
            "'");
        valid = false;
        continue;
      }
      options.libExts.emplace_back(extension);
    }
  }
  for (Arg *arg : args.filtered(OPT_v, OPT_libmap)) {
    arg->claim();
    options.libraryInputs.push_back(
        {arg->getOption().matches(OPT_v)
             ? obelisk::frontend::LibraryInputKind::File
             : obelisk::frontend::LibraryInputKind::Map,
         arg->getValue()});
  }
  options.topModules = args.getAllArgValues(OPT_top_EQ);
  options.paramOverrides = args.getAllArgValues(OPT_G);
  options.warningOptions = args.getAllArgValues(OPT_W);
  options.suppressWarningsPaths =
      args.getAllArgValues(OPT_suppress_warnings_EQ);

  options.singleUnit = args.hasArg(OPT_single_unit);
  options.librariesInheritMacros = args.hasArg(OPT_libraries_inherit_macros);
  options.allowUseBeforeDeclare = args.hasArg(OPT_allow_use_before_declare);
  options.ignoreUnknownModules = args.hasArg(OPT_ignore_unknown_modules);
  if (const Arg *arg = args.getLastArg(OPT_timescale_EQ))
    options.timeScale = arg->getValue();
  StringRef timing = args.getLastArgValue(OPT_timing_EQ, "typ");
  if (timing != "min" && timing != "typ" && timing != "max") {
    emitDriverError(Twine("unsupported min:typ:max selection '") + timing +
                    "'; expected min, typ, or max");
    valid = false;
  }
  options.minTypMax = timing == "min"   ? obelisk::frontend::MinTypMax::Min
                      : timing == "max" ? obelisk::frontend::MinTypMax::Max
                                        : obelisk::frontend::MinTypMax::Typ;

  valid &= parseUnsignedOption(args, OPT_max_include_depth_EQ,
                               "--max-include-depth", options.maxIncludeDepth);
  valid &= parseUnsignedOption(args, OPT_error_limit_EQ, "--error-limit",
                               options.errorLimit);

  StringRef standard = args.getLastArgValue(OPT_std_EQ, "1800-2023");
  if (standard != "1800-2017" && standard != "1800-2023") {
    emitDriverError(Twine("unsupported SystemVerilog revision '") + standard +
                    "'; expected 1800-2017 or 1800-2023");
    valid = false;
  }
  options.languageVersion =
      standard == "1800-2017"
          ? obelisk::frontend::LanguageVersion::IEEE1800_2017
          : obelisk::frontend::LanguageVersion::IEEE1800_2023;
  for (std::string argument : args.getAllArgValues(OPT_Xslang))
    options.slangArgs.push_back(std::move(argument));
  options.protectedEnvelopeProvider = protectConfig.provider;
  options.maxProtectedEnvelopeDepth = protectConfig.maxDepth;
  options.maxProtectedEnvelopeBytes = protectConfig.maxBytes;
  options.maxProtectedEnvelopeCount = protectConfig.maxCount;
  return options;
}

static int executeCompilation(
    const InputArgList &args,
    const obelisk::driver::ProtectedEnvelopeConfiguration &protectConfig) {
  SmallVector<std::string> inputs;
  for (const Arg *arg : args.filtered(OPT_INPUT))
    inputs.emplace_back(arg->getValue());
  for (const Arg *arg : args.filtered(OPT__DASH_DASH))
    for (const char *value : arg->getValues())
      inputs.emplace_back(value);

  bool valid = true;
  obelisk::frontend::FrontendOptions frontendOptions =
      buildFrontendOptions(args, valid, protectConfig);
  // Command files were already spliced into argv, so anything they contributed
  // is present here as an ordinary input.
  if (inputs.empty()) {
    emitDriverError("no input files");
    valid = false;
  }
  if (std::count(inputs.begin(), inputs.end(), "-") > 1) {
    emitDriverError("standard input may only appear once");
    valid = false;
  }
  if (!valid)
    return 1;

  std::optional<uint32_t> requestedWorkers;
  valid &=
      parseUnsignedOption(args, OPT_threads_EQ, "--threads", requestedWorkers);
  if (requestedWorkers && *requestedWorkers == 0) {
    emitDriverError("--threads must be greater than zero");
    valid = false;
  }
  if (requestedWorkers && *requestedWorkers > 65535) {
    emitDriverError("--threads exceeds the generated lane ID limit (65535)");
    valid = false;
  }
  std::optional<uint32_t> compilerThreads;
  valid &= parseUnsignedOption(args, OPT_compile_threads_EQ,
                               "--compile-threads", compilerThreads);
  if (compilerThreads && *compilerThreads == 0) {
    emitDriverError("--compile-threads must be greater than zero");
    valid = false;
  }
  std::string coverageConfigContents;
  if (StringRef path = args.getLastArgValue(OPT_coverage_config_EQ);
      !path.empty())
    valid &= validateCoverageConfiguration(path, coverageConfigContents);
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  if (compilerThreads && *compilerThreads != 1) {
    emitDriverError(
        "--compile-threads must be 1 in a single-threaded wasm compiler");
    valid = false;
  }
  // Construct MLIRContext with threading disabled. Its native default creates
  // a worker pool before any pass runs, which Emscripten cannot provide in a
  // module built without pthread support.
  compilerThreads = 1;
#endif
  StringRef vpiMode = args.getLastArgValue(OPT_vpi_EQ, "off");
  if (vpiMode != "off" && vpiMode != "read" && vpiMode != "full") {
    emitDriverError(Twine("unsupported VPI mode '") + vpiMode +
                    "'; expected off, read, or full");
    valid = false;
  }
  // The default is whichever target this build can produce; a wasm build of
  // the compiler has no native ELF backend linked in.
  StringRef targetName =
      args.getLastArgValue(OPT_target_EQ, OBELISK_DEFAULT_TARGET);
  if (targetName != "native" && targetName != "wasm32") {
    emitDriverError(Twine("unsupported target '") + targetName +
                    "'; expected native or wasm32");
    valid = false;
  }
  if (targetName != "wasm32" && args.hasArg(OPT_sysroot_EQ)) {
    emitDriverError("--sysroot is only valid with --target=wasm32");
    valid = false;
  }
  StringRef executionTier =
      args.getLastArgValue(OPT_execution_tier_EQ, "native");
  if (executionTier != "native" && executionTier != "bytecode") {
    emitDriverError(Twine("unsupported execution tier '") + executionTier +
                    "'; expected native or bytecode");
    valid = false;
  }
  StringRef nativeScheduler =
      args.getLastArgValue(OPT_native_scheduler_EQ, "auto");
  if (!obelisk::sim::symbolizeNativeSchedulerMode(nativeScheduler)) {
    emitDriverError(Twine("unsupported native scheduler '") + nativeScheduler +
                    "'; expected auto, generic, aot, or eval");
    valid = false;
  }
  StringRef staticSpecialization =
      args.getLastArgValue(OPT_static_specialization_EQ, "auto");
  if (staticSpecialization != "auto" && staticSpecialization != "off" &&
      staticSpecialization != "on") {
    emitDriverError(Twine("unsupported static specialization '") +
                    staticSpecialization + "'; expected auto, off, or on");
    valid = false;
  }
  if (const Arg *coverage = args.getLastArg(OPT_coverage, OPT_coverage_EQ)) {
    if (coverage->getOption().matches(OPT_coverage_EQ)) {
      SmallVector<StringRef> metrics;
      StringRef(coverage->getValue())
          .split(metrics, ',', -1,
                 /*KeepEmpty=*/true);
      llvm::StringSet<> seen;
      for (StringRef metric : metrics) {
        if ((metric != "line" && metric != "toggle" &&
             metric != "functional") ||
            !seen.insert(metric).second) {
          emitDriverError(Twine("invalid --coverage metric list '") +
                          coverage->getValue() +
                          "'; expected unique line,toggle,functional names");
          valid = false;
          break;
        }
      }
    }
  }
  (void)args.getLastArgValue(OPT_coverage_config_EQ);
  for (StringRef mapping : args.getAllArgValues(OPT_coverage_prefix_map_EQ)) {
    size_t equal = mapping.find('=');
    if (equal == StringRef::npos || equal == 0) {
      emitDriverError(Twine("invalid --coverage-prefix-map '") + mapping +
                      "'; expected <from>=<to>");
      valid = false;
    }
  }
  std::optional<uint32_t> pulseRejectPercent;
  std::optional<uint32_t> pulseErrorPercent;
  valid &= parseUnsignedOption(args, OPT_pulse_reject_percent_EQ,
                               "--pulse-reject-percent", pulseRejectPercent);
  valid &= parseUnsignedOption(args, OPT_pulse_error_percent_EQ,
                               "--pulse-error-percent", pulseErrorPercent);
  if ((pulseRejectPercent && *pulseRejectPercent > 100) ||
      (pulseErrorPercent && *pulseErrorPercent > 100)) {
    emitDriverError("global pulse percentages must be between 0 and 100");
    valid = false;
  }
  uint32_t effectivePulseReject = pulseRejectPercent.value_or(100);
  uint32_t effectivePulseError = pulseErrorPercent.value_or(100);
  if (effectivePulseError < effectivePulseReject) {
    // IEEE 1800-2017 30.7.2 requires a diagnostic and defines recovery by
    // raising the error percentage to the reject percentage. Command-line
    // compilation treats that required error as fatal, consistently with
    // source semantic errors, rather than silently changing pulse behavior.
    emitDriverError(
        "--pulse-error-percent cannot be less than --pulse-reject-percent");
    valid = false;
  }
  StringRef globalPulseStyle = args.getLastArgValue(OPT_pulse_style_EQ, "");
  StringRef globalCancelledPulses =
      args.getLastArgValue(OPT_cancelled_pulses_EQ, "");
  uint32_t optLevel = 3;
  if (const Arg *optimization =
          args.getLastArg(OPT_O0, OPT_O1, OPT_O2, OPT_O3)) {
    if (optimization->getOption().matches(OPT_O0))
      optLevel = 0;
    else if (optimization->getOption().matches(OPT_O1))
      optLevel = 1;
    else if (optimization->getOption().matches(OPT_O2))
      optLevel = 2;
  }
  if (!valid)
    return 1;
  uint32_t resolvedCompilerThreads = compilerThreads.value_or(
      std::max(1u, llvm::hardware_concurrency().compute_thread_count()));
  frontendOptions.numThreads = resolvedCompilerThreads;
  frontendOptions.collectCoverageSourceFiles =
      args.hasArg(OPT_coverage, OPT_coverage_EQ);

  const Arg *action =
      args.getLastArg(OPT_E, OPT_dump_tokens, OPT_emit_slang, OPT_emit_bindings,
                      OPT_emit_obelisk, OPT_emit_sim, OPT_emit_schedule, OPT_c,
                      OPT_emit_llvm, OPT_emit_dpi_header);
  bool preprocess = action && action->getOption().matches(OPT_E);
  bool dumpTokens = action && action->getOption().matches(OPT_dump_tokens);
  bool emitSlang = action && action->getOption().matches(OPT_emit_slang);
  bool emitBindings = action && action->getOption().matches(OPT_emit_bindings);
  bool emitSim = action && action->getOption().matches(OPT_emit_sim);
  bool emitSchedule = action && action->getOption().matches(OPT_emit_schedule);
  bool emitObject = action && action->getOption().matches(OPT_c);
  bool emitLLVM = action && action->getOption().matches(OPT_emit_llvm);
  bool emitDPIHeader =
      action && action->getOption().matches(OPT_emit_dpi_header);
  bool native = !action || emitObject || emitLLVM;
  if (native && requestedWorkers.value_or(1) != 1) {
    emitDriverError(
        "native executable generation currently requires --threads=1");
    valid = false;
  }
  if (!native && executionTier != "native") {
    emitDriverError(
        "--execution-tier is only valid for native executable generation");
    valid = false;
  }
  if (!native && args.hasArg(OPT_native_scheduler_EQ)) {
    emitDriverError(
        "--native-scheduler is only valid for native executable generation");
    valid = false;
  }
  if (!valid)
    return 1;

  SmallVector<std::string> svLibraries;
  if (targetName != "native" &&
      (args.hasArg(OPT_sv_lib) || args.hasArg(OPT_sv_liblist))) {
    emitDriverError("Annex J DPI libraries require --target=native");
    return 1;
  }
  if (!collectSVLibraryInputs(args, svLibraries))
    return 1;
  inputs.append(svLibraries);

  obelisk::driver::ClassifiedInputs classifiedInputs;
  if (failed(obelisk::driver::classifyDirectInputs(
          inputs, action == nullptr && targetName == "native", vpiMode,
          classifiedInputs)))
    return 1;
  if (classifiedInputs.systemVerilog.empty()) {
    emitDriverError(
        "at least one SystemVerilog input or command file is required");
    return 1;
  }
  inputs.assign(classifiedInputs.systemVerilog.begin(),
                classifiedInputs.systemVerilog.end());

  if (preprocess || dumpTokens) {
    FailureOr<std::string> text =
        preprocess ? obelisk::frontend::preprocessSystemVerilog(inputs,
                                                                frontendOptions)
                   : obelisk::frontend::listSystemVerilogTokens(
                         inputs, frontendOptions);
    if (failed(text))
      return 1;
    std::string outputFilename = args.getLastArgValue(OPT_o, "-").str();
    std::error_code error;
    ToolOutputFile output(outputFilename, error, sys::fs::OF_None);
    if (error) {
      emitDriverError(Twine("could not open output '") + outputFilename +
                      "': " + error.message());
      return 1;
    }
    output.os() << *text;
    output.keep();
    return 0;
  }

  DialectRegistry registry;
  registry.insert<obelisk::slangir::SlangDialect, obelisk::ir::ObeliskDialect,
                  obelisk::runtime::ObeliskRuntimeDialect,
                  obelisk::sim::ObeliskSimulationDialect,
                  mlir::LLVM::LLVMDialect>();
  // One explicitly sized pool is shared by all MLIR parallel pass adaptors.
  // Its lifetime encloses the context as required by MLIRContext.
  std::unique_ptr<llvm::DefaultThreadPool> compilerPool;
  MLIRContext context(registry, compilerThreads
                                    ? MLIRContext::Threading::DISABLED
                                    : MLIRContext::Threading::ENABLED);
  if (compilerThreads) {
    if (*compilerThreads > 1) {
      llvm::ThreadPoolStrategy strategy =
          llvm::hardware_concurrency(*compilerThreads);
      strategy.Limit = true;
      compilerPool = std::make_unique<llvm::DefaultThreadPool>(strategy);
      context.setThreadPool(*compilerPool);
    }
  }
  context.loadAllAvailableDialects();

  // MLIR only falls back to printing a diagnostic when it is an error and no
  // handler is registered, so without this every warning and remark a pass
  // emits -- and every note attached to an error -- is dropped on the floor.
  // Print all of them, in the format the fallback uses for errors.
  context.getDiagEngine().registerHandler([](Diagnostic &diagnostic) {
    std::function<void(Diagnostic &)> print = [&](Diagnostic &entry) {
      llvm::raw_ostream &os = llvm::errs();
      if (!llvm::isa<UnknownLoc>(entry.getLocation()))
        os << entry.getLocation() << ": ";
      switch (entry.getSeverity()) {
      case DiagnosticSeverity::Error:
        os << "error: ";
        break;
      case DiagnosticSeverity::Warning:
        os << "warning: ";
        break;
      case DiagnosticSeverity::Remark:
        os << "remark: ";
        break;
      case DiagnosticSeverity::Note:
        os << "note: ";
        break;
      }
      os << entry << '\n';
      for (Diagnostic &note : entry.getNotes())
        print(note);
      os.flush();
    };
    print(diagnostic);
    return success();
  });

  auto importedModule =
      obelisk::frontend::importSystemVerilog(inputs, context, frontendOptions);
  if (failed(importedModule))
    return 1;
  OwningOpRef<ModuleOp> module = std::move(*importedModule);

  if (pulseRejectPercent || pulseErrorPercent) {
    (*module)->setAttr(
        "obelisk.pulse_reject_percent",
        IntegerAttr::get(IntegerType::get(&context, 32), effectivePulseReject));
    (*module)->setAttr(
        "obelisk.pulse_error_percent",
        IntegerAttr::get(IntegerType::get(&context, 32), effectivePulseError));
  }
  if (!globalPulseStyle.empty())
    (*module)->setAttr("obelisk.pulse_on_detect",
                       BoolAttr::get(&context, globalPulseStyle == "ondetect"));
  if (!globalCancelledPulses.empty())
    (*module)->setAttr(
        "obelisk.pulse_show_cancelled",
        BoolAttr::get(&context, globalCancelledPulses == "show"));
  if (const Arg *coverage = args.getLastArg(OPT_coverage, OPT_coverage_EQ)) {
    SmallVector<Attribute> metrics;
    if (coverage->getOption().matches(OPT_coverage)) {
      for (StringRef metric : {"line", "toggle", "functional"})
        metrics.push_back(StringAttr::get(&context, metric));
    } else {
      SmallVector<StringRef> selected;
      StringRef(coverage->getValue()).split(selected, ',');
      for (StringRef metric : selected)
        metrics.push_back(StringAttr::get(&context, metric));
    }
    (*module)->setAttr("obelisk.coverage.metrics",
                       ArrayAttr::get(&context, metrics));
  }
  if (!coverageConfigContents.empty())
    (*module)->setAttr("obelisk.coverage.config",
                       StringAttr::get(&context, coverageConfigContents));
  SmallVector<Attribute> prefixMaps;
  for (StringRef mapping : args.getAllArgValues(OPT_coverage_prefix_map_EQ))
    prefixMaps.push_back(StringAttr::get(&context, mapping));
  if (!prefixMaps.empty())
    (*module)->setAttr("obelisk.coverage.prefix_maps",
                       ArrayAttr::get(&context, prefixMaps));

  if (native) {
    obelisk::sim::NativeSchedulerMode pipelineScheduler =
        *obelisk::sim::symbolizeNativeSchedulerMode(nativeScheduler);
    if (pipelineScheduler == obelisk::sim::NativeSchedulerMode::Auto) {
      if (!args.hasArg(OPT_execution_tier_EQ))
        (*module)->setAttr("obelisk.native_scheduler.auto_requested",
                           UnitAttr::get(&context));
      pipelineScheduler = executionTier == "bytecode"
                              ? obelisk::sim::NativeSchedulerMode::Generic
                              : obelisk::sim::NativeSchedulerMode::Eval;
    }
    (*module)->setAttr("obelisk.native_scheduler",
                       obelisk::sim::NativeSchedulerModeAttr::get(
                           &context, pipelineScheduler));
  }

  if (!emitSlang && !emitBindings) {
    PassManager passManager(&context);
    if (args.hasArg(OPT_mlir_timing)) {
      passManager.enableTiming();
      // Cohort fusion runs here, long before native lowering sets the same
      // marker. Body fusion is the one Tier-1 path that does not depend on
      // whole-design admission, so its planning decisions must be reportable
      // under the existing diagnostic flag rather than only its pass timing.
      (*module)->setAttr("obelisk.debug.native_timing", UnitAttr::get(&context));
    }
    passManager.addPass(obelisk::createConvertSlangToObeliskPass());
    if (emitSim || emitSchedule || native)
      obelisk::buildObeliskToSimulationPipeline(
          passManager, requestedWorkers.value_or(1), vpiMode, optLevel,
          // Exact eval selection needs the verified static-state/NBA and
          // superstep plans even at O0. Auto otherwise follows optimization
          // level; an explicit "off" remains authoritative.
          staticSpecialization == "auto" && nativeScheduler == "eval" &&
                  executionTier != "bytecode"
              ? "on"
              : staticSpecialization);
    if (failed(passManager.run(*module)))
      return 1;
  }

  bool hasDPI = false;
  (*module)->walk([&](obelisk::sim::SimCodeUnitDeclOp declaration) {
    hasDPI |= declaration->hasAttr("obelisk_sim.dpi_import") ||
              declaration->hasAttr("obelisk_sim.dpi_export");
  });
  if (native && targetName == "wasm32") {
    if (hasDPI) {
      emitDriverError(
          "DPI is unavailable for the wasm32 target; use --target=native");
      return 1;
    }
  }

  if (native) {
    obelisk::driver::NativeOutputOptions nativeOptions;
    nativeOptions.kind = emitObject ? obelisk::driver::NativeOutputKind::Object
                         : emitLLVM
                             ? obelisk::driver::NativeOutputKind::LLVMIR
                             : obelisk::driver::NativeOutputKind::Executable;
    nativeOptions.outputPath = args.getLastArgValue(OPT_o, emitObject ? "a.o"
                                                           : emitLLVM ? "-"
                                                                      : "a.out")
                                   .str();
    nativeOptions.explicitSysroot = args.getLastArgValue(OPT_sysroot_EQ).str();
    nativeOptions.executablePath = driverExecutablePath;
    nativeOptions.nativeLinkInputs =
        std::move(classifiedInputs.nativeLinkInputs);
    nativeOptions.sharedLibraryInputs =
        std::move(classifiedInputs.sharedLibraries);
    nativeOptions.dpi = hasDPI || !nativeOptions.sharedLibraryInputs.empty();
    nativeOptions.vpi = vpiMode.str();
    nativeOptions.nativeScheduler = nativeScheduler.str();
    nativeOptions.thinLTOCacheDir =
        args.getLastArgValue(OPT_thinlto_cache_dir_EQ).str();
    nativeOptions.bytecode = executionTier == "bytecode";
    nativeOptions.optLevel = optLevel;
    nativeOptions.noLTO = args.hasFlag(OPT_fno_lto, OPT_flto, false);
    nativeOptions.timing = args.hasArg(OPT_mlir_timing);
    nativeOptions.debugNativeExecutionCounts =
        args.hasArg(OPT_debug_native_execution_counts);
    nativeOptions.compileThreads = resolvedCompilerThreads;
    nativeOptions.target = targetName == "wasm32"
                               ? obelisk::driver::TargetKind::Wasm
                               : obelisk::driver::TargetKind::Native;
    return succeeded(obelisk::driver::emitTargetOutput(*module, nativeOptions))
               ? 0
               : 1;
  }

  std::string outputFilename = args.getLastArgValue(OPT_o, "-").str();
  std::error_code error;
  ToolOutputFile output(outputFilename, error, sys::fs::OF_None);
  if (error) {
    emitDriverError(Twine("could not open output '") + outputFilename +
                    "': " + error.message());
    return 1;
  }

  if (emitBindings) {
    writeBindingReport(*module, output.os());
  } else if (emitDPIHeader) {
    if (failed(writeDPIHeader(*module, output.os())))
      return 1;
  } else if (emitSchedule) {
    for (obelisk::sim::SimDesignOp design :
         module->getBody()->getOps<obelisk::sim::SimDesignOp>()) {
      output.os() << "schedule @" << design.getSymName() << ' ';
      obelisk::sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
      if (!graph) {
        emitDriverError("simulation lowering produced no compute graph");
        return 1;
      }
      Attribute(graph).print(output.os());
      if (args.hasArg(OPT_mlir_print_debuginfo))
        printScheduleSourceLocations(design, graph, output.os());
      output.os() << '\n';
    }
  } else {
    OpPrintingFlags printingFlags;
    if (args.hasArg(OPT_mlir_print_debuginfo))
      printingFlags.enableDebugInfo();
    module->print(output.os(), printingFlags);
    output.os() << '\n';
  }
  output.keep();
  return 0;
}

} // namespace

int obelisk::driver::runObeliskDriver(
    int argc, char **argv,
    const ProtectedEnvelopeConfiguration &protectConfig) {
  driverExecutablePath = sys::fs::getMainExecutable(
      argv[0], reinterpret_cast<void *>(&runObeliskDriver));
  const OptTable &optionTable = obelisk::driver::getDriverOptTable();

  BumpPtrAllocator allocator;
  StringSaver saver(allocator);

  SmallVector<const char *> arguments(argv, argv + argc);
  SmallVector<std::string> activeCommandFiles;
  if (!expandCommandFiles(arguments, saver, activeCommandFiles))
    return 1;

  bool parseFailed = false;
  InputArgList args =
      optionTable.parseArgs(static_cast<int>(arguments.size()),
                            const_cast<char *const *>(arguments.data()),
                            OPT_UNKNOWN, saver, [&](StringRef msg) {
                              emitDriverError(msg);
                              parseFailed = true;
                            });
  if (parseFailed)
    return 1;

  if (args.hasArg(OPT_help) || args.hasArg(OPT_help_hidden)) {
    optionTable.printHelp(outs(), "obelisk [options] <input files>",
                          "Obelisk ahead-of-time SystemVerilog compiler",
                          args.hasArg(OPT_help_hidden));
    return 0;
  }
  if (args.hasArg(OPT_version)) {
    outs() << "obelisk version " << OBELISK_VERSION_STRING << '\n'
           << obelisk::frontend::getSlangVersion() << '\n';
    return 0;
  }
  if (args.hasArg(OPT_print_resource_dir)) {
    outs() << OBELISK_RESOURCE_DIR << '\n';
    return 0;
  }
  if (args.hasArg(OPT_print_host_c_runtime)) {
#if OBELISK_HAS_NATIVE_BACKEND
    FailureOr<obelisk::driver::HostCRuntimeInputs> inputs =
        obelisk::driver::discoverHostCRuntime(OBELISK_NATIVE_TARGET_TRIPLE,
                                              driverExecutablePath);
    if (failed(inputs))
      return 1;
    outs() << "dynamic-linker=" << inputs->dynamicLinker << '\n'
           << "crt1=" << inputs->crt1 << '\n'
           << "crti=" << inputs->crti << '\n'
           << "crtn=" << inputs->crtn << '\n'
           << "libc=" << inputs->libc << '\n'
           << "libm=" << inputs->libm << '\n';
    return 0;
#else
    emitDriverError("host C-runtime discovery is unavailable in this build");
    return 1;
#endif
  }

  int status = executeCompilation(args, protectConfig);
  // A reusable Emscripten module does not exit after callMain(), so its libc
  // streams do not get the process-exit flush a native invocation receives.
  // Flush explicitly to deliver linker diagnostics to print/printErr.
  outs().flush();
  errs().flush();
  return status;
}
