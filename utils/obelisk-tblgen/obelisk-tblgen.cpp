//===- obelisk-tblgen.cpp - Obelisk TableGen backends -------------------===//

#include "mlir/TableGen/GenInfo.h"
#include "mlir/Tools/mlir-tblgen/MlirTblgenMain.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

#include <algorithm>
#include <cstdint>
#include <limits>

using namespace llvm;

namespace {

constexpr int64_t maxU32 = std::numeric_limits<uint32_t>::max();

struct Encoding {
  StringRef name;
  uint32_t width;
};

bool isCppIdentifier(StringRef name) {
  if (name.empty() || !(isAlpha(name.front()) || name.front() == '_'))
    return false;
  return llvm::all_of(name.drop_front(), [](char value) {
    return isAlnum(value) || value == '_';
  });
}

bool getCppName(const Record &record, StringRef description, StringRef &name) {
  name = record.getValueAsString("cppName");
  if (isCppIdentifier(name))
    return true;
  PrintError(record.getLoc(),
             Twine(description) + " cppName must be a C++ identifier");
  return false;
}

bool getU32(const Record &record, StringRef field, uint32_t minimum,
            uint32_t &result) {
  int64_t value = record.getValueAsInt(field);
  if (value < minimum || value > maxU32) {
    PrintError(record.getLoc(), Twine(field) + " must be in [" +
                                    Twine(minimum) + ", " + Twine(maxU32) +
                                    "]");
    return false;
  }
  result = static_cast<uint32_t>(value);
  return true;
}

bool getEncoding(const Record &field, Encoding &result) {
  const Record *encoding = field.getValueAsDef("encoding");
  uint32_t width = 0;
  StringRef name;
  if (!getCppName(*encoding, "reflection encoding", name) ||
      !getU32(*encoding, "width", 1, width))
    return false;
  result = {name, width};
  return true;
}

bool validateReflectionSchema(const RecordKeeper &records) {
  auto encodings = records.getAllDerivedDefinitions("ReflectionEncoding");
  auto layouts = records.getAllDerivedDefinitions("ReflectionLayout");
  auto kinds = records.getAllDerivedDefinitions("ReflectionRecordKind");
  if (encodings.empty() || layouts.empty() || kinds.empty()) {
    PrintError("reflection schema needs encodings, layouts, and record kinds");
    return false;
  }

  StringMap<const Record *> encodingNames;
  for (const Record *encoding : encodings) {
    StringRef name;
    uint32_t width = 0;
    if (!getCppName(*encoding, "reflection encoding", name) ||
        !getU32(*encoding, "width", 1, width))
      return false;
    if (!encodingNames.try_emplace(name, encoding).second) {
      PrintError(encoding->getLoc(), "duplicate reflection encoding name");
      return false;
    }
  }

  StringMap<const Record *> layoutNames;
  StringSet<> generatedFieldNames;
  for (const Record *layout : layouts) {
    StringRef layoutName;
    uint32_t size = 0;
    if (!getCppName(*layout, "reflection layout", layoutName) ||
        !getU32(*layout, "size", 1, size))
      return false;
    if (!layoutNames.try_emplace(layoutName, layout).second) {
      PrintError(layout->getLoc(), "duplicate reflection layout name");
      return false;
    }

    auto fields = layout->getValueAsListOfDefs("fields");
    if (fields.empty()) {
      PrintError(layout->getLoc(),
                 "reflection layout needs at least one field");
      return false;
    }

    SmallVector<std::pair<uint32_t, uint32_t>> ranges;
    StringMap<const Record *> fieldNames;
    for (const Record *field : fields) {
      StringRef fieldName;
      uint32_t offset = 0;
      Encoding encoding;
      if (!getCppName(*field, "reflection field", fieldName) ||
          !getU32(*field, "offset", 0, offset) ||
          !getEncoding(*field, encoding))
        return false;
      if (offset > size || encoding.width > size - offset) {
        PrintError(field->getLoc(), "reflection field is outside its layout");
        return false;
      }
      if (!fieldNames.try_emplace(fieldName, field).second) {
        PrintError(field->getLoc(), "duplicate field name in layout");
        return false;
      }

      std::string generatedName = (layoutName + fieldName).str();
      if (!generatedFieldNames.insert(generatedName).second) {
        PrintError(field->getLoc(),
                   "duplicate generated reflection field name");
        return false;
      }

      uint32_t end = offset + encoding.width;
      for (auto [otherBegin, otherEnd] : ranges) {
        if (offset < otherEnd && otherBegin < end) {
          PrintError(field->getLoc(), "overlapping reflection fields");
          return false;
        }
      }
      ranges.emplace_back(offset, end);
    }
  }

  DenseSet<uint32_t> kindValues;
  StringMap<const Record *> kindNames;
  for (const Record *kind : kinds) {
    StringRef kindName;
    uint32_t value = 0;
    if (!getCppName(*kind, "reflection record kind", kindName) ||
        !getU32(*kind, "value", 1, value))
      return false;
    if (!kindNames.try_emplace(kindName, kind).second ||
        !kindValues.insert(value).second) {
      PrintError(kind->getLoc(), "duplicate record kind name or value");
      return false;
    }
  }
  return true;
}

bool emitReflectionLayouts(const RecordKeeper &records, raw_ostream &os) {
  if (!validateReflectionSchema(records))
    return true;

  os << "//===- DesignReflectionLayout.h.inc - generated; do not edit -*- "
        "C++ -*-===//\n\n";
  os << "#ifndef OBELISK_REFLECTION_DESIGNREFLECTIONLAYOUT_H_INC\n";
  os << "#define OBELISK_REFLECTION_DESIGNREFLECTIONLAYOUT_H_INC\n\n";
  os << "#include <cstddef>\n#include <cstdint>\n\n";
  os << "namespace obelisk::reflection {\n\n";

  auto encodingRecords = records.getAllDerivedDefinitions("ReflectionEncoding");
  SmallVector<const Record *> encodings(encodingRecords.begin(),
                                        encodingRecords.end());
  llvm::sort(encodings, [](const Record *left, const Record *right) {
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  os << "enum class FieldEncoding : uint8_t {\n";
  for (const Record *encoding : encodings)
    os << formatv("  {0},\n", encoding->getValueAsString("cppName"));
  os << "};\n\n";

  os << "struct FieldDescriptor {\n"
        "  const char *name;\n"
        "  uint32_t offset;\n"
        "  uint32_t width;\n"
        "  FieldEncoding encoding;\n"
        "};\n\n";
  os << "struct LayoutDescriptor {\n"
        "  const char *name;\n"
        "  uint32_t size;\n"
        "  const FieldDescriptor *fields;\n"
        "  size_t fieldCount;\n"
        "};\n\n";

  auto layoutRecords = records.getAllDerivedDefinitions("ReflectionLayout");
  SmallVector<const Record *> layouts(layoutRecords.begin(),
                                      layoutRecords.end());
  llvm::sort(layouts, [](const Record *left, const Record *right) {
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  for (const Record *layout : layouts) {
    StringRef name = layout->getValueAsString("cppName");
    auto fields = layout->getValueAsListOfDefs("fields");
    os << "namespace field {\n";
    for (const Record *field : fields)
      os << formatv("inline constexpr uint32_t {0}{1} = {2};\n", name,
                    field->getValueAsString("cppName"),
                    field->getValueAsInt("offset"));
    os << "} // namespace field\n";
    os << formatv("inline constexpr FieldDescriptor {0}Fields[] = {{\n", name);
    for (const Record *field : fields) {
      Encoding encoding;
      if (!getEncoding(*field, encoding))
        return true;
      os << "  {";
      os << formatv("\"{0}\", {1}, {2}, FieldEncoding::{3}",
                    field->getValueAsString("cppName"),
                    field->getValueAsInt("offset"), encoding.width,
                    encoding.name);
      os << "},\n";
    }
    os << "};\n";
    os << formatv("inline constexpr LayoutDescriptor {0}Layout = ", name);
    os << "{";
    os << formatv("\"{0}\", {1}, {0}Fields, {2}", name,
                  layout->getValueAsInt("size"), fields.size());
    os << "};\n\n";
  }

  auto kindRecords = records.getAllDerivedDefinitions("ReflectionRecordKind");
  SmallVector<const Record *> kinds(kindRecords.begin(), kindRecords.end());
  llvm::sort(kinds, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class RecordKind : uint32_t {\n";
  for (const Record *kind : kinds)
    os << formatv("  {0} = {1},\n", kind->getValueAsString("cppName"),
                  kind->getValueAsInt("value"));
  os << "};\n\n";
  os << "struct RecordKindDescriptor {\n"
        "  RecordKind kind;\n"
        "  const char *name;\n"
        "  const LayoutDescriptor *layout;\n"
        "};\n\n";
  os << "inline constexpr RecordKindDescriptor recordKinds[] = {\n";
  for (const Record *kind : kinds) {
    const Record *layout = kind->getValueAsDef("layout");
    os << "  {";
    os << formatv("RecordKind::{0}, \"{0}\", &{1}Layout",
                  kind->getValueAsString("cppName"),
                  layout->getValueAsString("cppName"));
    os << "},\n";
  }
  os << "};\n\n";
  os << "} // namespace obelisk::reflection\n\n";
  os << "#endif // OBELISK_REFLECTION_DESIGNREFLECTIONLAYOUT_H_INC\n";
  return false;
}

mlir::GenRegistration reflectionLayoutGen(
    "gen-obelisk-reflection-layout",
    "Generate Obelisk design-reflection layouts and record kinds",
    emitReflectionLayouts);

} // namespace

int main(int argc, char **argv) { return mlir::MlirTblgenMain(argc, argv); }
