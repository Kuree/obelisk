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
#include <optional>
#include <tuple>

using namespace llvm;

namespace {

constexpr int64_t maxU32 = std::numeric_limits<uint32_t>::max();

struct Encoding {
  enum class Kind { Bytes, U16LE, U32LE, U64LE, I64LE } kind;
  StringRef name;
  uint32_t width;
};

StringRef getCppType(Encoding::Kind encoding) {
  if (encoding == Encoding::Kind::U16LE)
    return "uint16_t";
  if (encoding == Encoding::Kind::U32LE)
    return "uint32_t";
  if (encoding == Encoding::Kind::U64LE)
    return "uint64_t";
  if (encoding == Encoding::Kind::I64LE)
    return "int64_t";
  return "const uint8_t *";
}

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
  Encoding::Kind kind;
  uint32_t expectedWidth = width;
  if (name == "Bytes")
    kind = Encoding::Kind::Bytes;
  else if (name == "U16LE") {
    kind = Encoding::Kind::U16LE;
    expectedWidth = 2;
  } else if (name == "U32LE") {
    kind = Encoding::Kind::U32LE;
    expectedWidth = 4;
  } else if (name == "U64LE") {
    kind = Encoding::Kind::U64LE;
    expectedWidth = 8;
  } else if (name == "I64LE") {
    kind = Encoding::Kind::I64LE;
    expectedWidth = 8;
  } else {
    PrintError(encoding->getLoc(), "unsupported reflection encoding");
    return false;
  }
  if (width != expectedWidth) {
    PrintError(encoding->getLoc(), "reflection encoding has invalid width");
    return false;
  }
  result = {kind, name, width};
  return true;
}

bool validateReflectionSchema(const RecordKeeper &records) {
  auto encodings = records.getAllDerivedDefinitions("ReflectionEncoding");
  auto layouts = records.getAllDerivedDefinitions("ReflectionLayout");
  auto kinds = records.getAllDerivedDefinitions("ReflectionRecordKind");
  auto tableKinds = records.getAllDerivedDefinitions("ReflectionTableKind");
  if (encodings.empty() || layouts.empty() || kinds.empty() ||
      tableKinds.empty()) {
    PrintError("reflection schema needs encodings, layouts, record kinds, and "
               "table kinds");
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
  std::optional<uint32_t> recordKindPackedWidth;
  for (const Record *kind : kinds) {
    StringRef kindName;
    uint32_t value = 0;
    uint32_t packedWidth = 0;
    if (!getCppName(*kind, "reflection record kind", kindName) ||
        !getU32(*kind, "value", 1, value) ||
        !getU32(*kind, "packedWidth", 1, packedWidth))
      return false;
    if (packedWidth >= 32) {
      PrintError(kind->getLoc(),
                 "reflection record kind packed width must be below 32");
      return false;
    }
    if (!recordKindPackedWidth)
      recordKindPackedWidth = packedWidth;
    else if (*recordKindPackedWidth != packedWidth) {
      PrintError(kind->getLoc(),
                 "reflection record kinds must use one packed width");
      return false;
    }
    if (value >= (uint32_t{1} << packedWidth)) {
      PrintError(kind->getLoc(),
                 "reflection record kind does not fit its packed width");
      return false;
    }
    if (!kindNames.try_emplace(kindName, kind).second ||
        !kindValues.insert(value).second) {
      PrintError(kind->getLoc(), "duplicate record kind name or value");
      return false;
    }
  }
  DenseSet<uint32_t> tableKindValues;
  StringMap<const Record *> tableKindNames;
  std::optional<uint32_t> tableKindWidth;
  for (const Record *kind : tableKinds) {
    StringRef kindName;
    uint32_t value = 0;
    uint32_t packedWidth = 0;
    if (!getCppName(*kind, "reflection table kind", kindName) ||
        !getU32(*kind, "value", 0, value) ||
        !getU32(*kind, "packedWidth", 1, packedWidth))
      return false;
    if (packedWidth > 8) {
      PrintError(kind->getLoc(),
                 "reflection table kind packed width must be at most 8");
      return false;
    }
    if (!tableKindWidth)
      tableKindWidth = packedWidth;
    else if (*tableKindWidth != packedWidth) {
      PrintError(kind->getLoc(),
                 "reflection table kinds must use one packed width");
      return false;
    }
    if (value >= (uint32_t{1} << packedWidth)) {
      PrintError(kind->getLoc(),
                 "reflection table kind does not fit its packed width");
      return false;
    }
    if (!tableKindNames.try_emplace(kindName, kind).second ||
        !tableKindValues.insert(value).second) {
      PrintError(kind->getLoc(), "duplicate table kind name or value");
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

  os << "namespace detail {\n"
        "inline uint16_t readU16LE(const uint8_t *data) {\n"
        "  return uint16_t{data[0]} | (uint16_t{data[1]} << 8);\n"
        "}\n"
        "inline uint32_t readU32LE(const uint8_t *data) {\n"
        "  uint32_t value = 0;\n"
        "  for (unsigned byte = 0; byte != 4; ++byte)\n"
        "    value |= uint32_t{data[byte]} << (byte * 8);\n"
        "  return value;\n"
        "}\n"
        "inline uint64_t readU64LE(const uint8_t *data) {\n"
        "  uint64_t value = 0;\n"
        "  for (unsigned byte = 0; byte != 8; ++byte)\n"
        "    value |= uint64_t{data[byte]} << (byte * 8);\n"
        "  return value;\n"
        "}\n"
        "inline void writeU32LE(uint8_t *data, uint32_t value) {\n"
        "  for (unsigned byte = 0; byte != 4; ++byte)\n"
        "    data[byte] = static_cast<uint8_t>(value >> (byte * 8));\n"
        "}\n"
        "inline void writeU16LE(uint8_t *data, uint16_t value) {\n"
        "  data[0] = static_cast<uint8_t>(value);\n"
        "  data[1] = static_cast<uint8_t>(value >> 8);\n"
        "}\n"
        "inline void writeU64LE(uint8_t *data, uint64_t value) {\n"
        "  for (unsigned byte = 0; byte != 8; ++byte)\n"
        "    data[byte] = static_cast<uint8_t>(value >> (byte * 8));\n"
        "}\n"
        "} // namespace detail\n\n";

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

    os << formatv("class {0}View {{\n", name);
    os << "public:\n";
    os << formatv("  explicit {0}View(const uint8_t *data) : data(data)", name);
    os << " {}\n";
    os << "  const uint8_t *getData() const { return data; }\n";
    for (const Record *field : fields) {
      Encoding encoding;
      if (!getEncoding(*field, encoding))
        return true;
      StringRef fieldName = field->getValueAsString("cppName");
      os << formatv("  {0} get{1}() const {{ ", getCppType(encoding.kind),
                    fieldName);
      if (encoding.kind == Encoding::Kind::Bytes)
        os << formatv("return data + field::{0}{1};", name, fieldName);
      else if (encoding.kind == Encoding::Kind::I64LE)
        os << formatv("return static_cast<int64_t>(detail::readU64LE(data + "
                      "field::{0}{1}));",
                      name, fieldName);
      else
        os << formatv("return detail::read{0}(data + field::{1}{2});",
                      encoding.name, name, fieldName);
      os << " }\n";
    }
    os << "private:\n  const uint8_t *data;\n};\n\n";

    os << formatv("class {0}Writer {{\n", name);
    os << "public:\n";
    os << formatv("  explicit {0}Writer(uint8_t *data) : data(data)", name);
    os << " {}\n";
    os << "  uint8_t *getData() const { return data; }\n";
    for (const Record *field : fields) {
      Encoding encoding;
      if (!getEncoding(*field, encoding))
        return true;
      StringRef fieldName = field->getValueAsString("cppName");
      if (encoding.kind == Encoding::Kind::Bytes) {
        os << formatv("  void set{0}(const uint8_t *value) {{\n", fieldName);
        os << formatv("    for (uint32_t byte = 0; byte != {0}; ++byte)\n",
                      encoding.width);
        os << formatv("      data[field::{0}{1} + byte] = value[byte];\n", name,
                      fieldName);
        os << "  }\n";
        continue;
      }
      os << formatv("  void set{0}({1} value) {{ ", fieldName,
                    getCppType(encoding.kind));
      if (encoding.kind == Encoding::Kind::I64LE)
        os << formatv("detail::writeU64LE(data + field::{0}{1}, "
                      "static_cast<uint64_t>(value));",
                      name, fieldName);
      else
        os << formatv("detail::write{0}(data + field::{1}{2}, value);",
                      encoding.name, name, fieldName);
      os << " }\n";
    }
    os << "private:\n  uint8_t *data;\n};\n\n";
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
  os << "constexpr bool isValidRecordKind(RecordKind kind) {\n"
        "  switch (kind) {\n";
  for (const Record *kind : kinds)
    os << formatv("  case RecordKind::{0}:\n",
                  kind->getValueAsString("cppName"));
  os << "    return true;\n"
        "  }\n"
        "  return false;\n"
        "}\n\n";
  const uint32_t recordKindWidth = kinds.front()->getValueAsInt("packedWidth");
  os << "inline constexpr unsigned recordKindPackedWidth = " << recordKindWidth
     << ";\n"
        "inline constexpr unsigned recordKindPayloadShift = "
        "recordKindPackedWidth;\n"
        "inline constexpr uint32_t recordKindMask = "
        "(uint32_t{1} << recordKindPackedWidth) - 1;\n"
        "inline constexpr uint32_t recordKindPayloadMask = "
        "UINT32_MAX >> recordKindPayloadShift;\n\n"
        "constexpr bool canPackRecordKindPayload(uint32_t payload) {\n"
        "  return payload <= recordKindPayloadMask;\n"
        "}\n\n"
        "constexpr bool tryPackRecordKindPayload(RecordKind kind, "
        "uint32_t payload, uint32_t &packed) {\n"
        "  if (!isValidRecordKind(kind) || "
        "!canPackRecordKindPayload(payload))\n"
        "    return false;\n"
        "  packed = (payload << recordKindPayloadShift) | "
        "static_cast<uint32_t>(kind);\n"
        "  return true;\n"
        "}\n\n"
        "constexpr RecordKind unpackRecordKind(uint32_t value) {\n"
        "  return static_cast<RecordKind>(value & recordKindMask);\n"
        "}\n\n"
        "constexpr uint32_t unpackRecordKindPayload(uint32_t value) {\n"
        "  return value >> recordKindPayloadShift;\n"
        "}\n\n";

  auto tableKindRecords =
      records.getAllDerivedDefinitions("ReflectionTableKind");
  SmallVector<const Record *> tableKinds(tableKindRecords.begin(),
                                         tableKindRecords.end());
  llvm::sort(tableKinds, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class TableKind : uint8_t {\n";
  for (const Record *kind : tableKinds)
    os << formatv("  {0} = {1},\n", kind->getValueAsString("cppName"),
                  kind->getValueAsInt("value"));
  os << "};\n\n";
  os << "constexpr bool isValidTableKind(TableKind table) {\n"
        "  switch (table) {\n";
  for (const Record *kind : tableKinds)
    os << formatv("  case TableKind::{0}:\n",
                  kind->getValueAsString("cppName"));
  os << "    return true;\n"
        "  }\n"
        "  return false;\n"
        "}\n\n";
  const uint32_t packedWidth = tableKinds.front()->getValueAsInt("packedWidth");
  os << "inline constexpr unsigned tableKindPackedWidth = " << packedWidth
     << ";\n"
        "inline constexpr unsigned tableKindPackedShift = 16 - "
        "tableKindPackedWidth;\n"
        "inline constexpr uint16_t tableKindPayloadMask = "
        "(uint16_t{1} << tableKindPackedShift) - 1;\n\n"
        "constexpr bool canPackTableKindPayload(uint32_t payload) {\n"
        "  return payload <= tableKindPayloadMask;\n"
        "}\n\n"
        "constexpr bool tryPackTableKindPayload(TableKind table, "
        "uint32_t payload, uint16_t &packed) {\n"
        "  if (!isValidTableKind(table) || "
        "!canPackTableKindPayload(payload))\n"
        "    return false;\n"
        "  packed = (static_cast<uint16_t>(table) << "
        "tableKindPackedShift) | static_cast<uint16_t>(payload);\n"
        "  return true;\n"
        "}\n\n"
        "constexpr TableKind unpackTableKind(uint16_t value) {\n"
        "  return static_cast<TableKind>(value >> tableKindPackedShift);\n"
        "}\n\n"
        "constexpr uint16_t unpackTableKindPayload(uint16_t value) {\n"
        "  return value & tableKindPayloadMask;\n"
        "}\n\n"
        "inline constexpr uint16_t relationSourceKindMask =\n"
        "    tableKindPayloadMask >> 1;\n"
        "inline constexpr uint16_t relationSourceIterateBit =\n"
        "    relationSourceKindMask + 1;\n\n"
        "constexpr bool tryPackRelationSource(TableKind table, uint32_t kind,\n"
        "                                     bool iterate, uint16_t &packed) "
        "{\n"
        "  if (!isValidTableKind(table) || kind > relationSourceKindMask)\n"
        "    return false;\n"
        "  packed = (static_cast<uint16_t>(table) << tableKindPackedShift) |\n"
        "           (iterate ? relationSourceIterateBit : 0) |\n"
        "           static_cast<uint16_t>(kind);\n"
        "  return true;\n"
        "}\n\n"
        "constexpr TableKind unpackRelationSourceTable(uint16_t value) {\n"
        "  return unpackTableKind(value);\n"
        "}\n\n"
        "constexpr uint16_t unpackRelationSourceKind(uint16_t value) {\n"
        "  return value & relationSourceKindMask;\n"
        "}\n\n"
        "constexpr bool relationSourceIsIterate(uint16_t value) {\n"
        "  return (value & relationSourceIterateBit) != 0;\n"
        "}\n\n"
        "inline constexpr unsigned tableIndexPackedShift = 32 - "
        "tableKindPackedWidth;\n"
        "inline constexpr uint32_t tableIndexPayloadMask = "
        "    (uint32_t{1} << tableIndexPackedShift) - 1;\n\n"
        "constexpr bool canPackTableIndex(uint32_t index) {\n"
        "  return index <= tableIndexPayloadMask;\n"
        "}\n\n"
        "constexpr bool tryPackTableIndex(TableKind table, uint32_t index,\n"
        "                                 uint32_t &packed) {\n"
        "  if (!isValidTableKind(table) || !canPackTableIndex(index))\n"
        "    return false;\n"
        "  packed = (static_cast<uint32_t>(table) << tableIndexPackedShift) |\n"
        "           index;\n"
        "  return true;\n"
        "}\n\n"
        "constexpr TableKind unpackTableIndexKind(uint32_t value) {\n"
        "  return static_cast<TableKind>(value >> tableIndexPackedShift);\n"
        "}\n\n"
        "constexpr uint32_t unpackTableIndex(uint32_t value) {\n"
        "  return value & tableIndexPayloadMask;\n"
        "}\n\n";
  os << "} // namespace obelisk::reflection\n\n";
  os << "#endif // OBELISK_REFLECTION_DESIGNREFLECTIONLAYOUT_H_INC\n";
  return false;
}

bool validateVPIObjectModel(const RecordKeeper &records) {
  auto families = records.getAllDerivedDefinitions("VPIObjectFamily");
  auto roles = records.getAllDerivedDefinitions("VPIObjectRole");
  auto objects = records.getAllDerivedDefinitions("VPIObjectKind");
  auto relations = records.getAllDerivedDefinitions("VPIRelation");
  auto objectSets = records.getAllDerivedDefinitions("VPIObjectSet");
  auto objectRepresentations =
      records.getAllDerivedDefinitions("VPIObjectRepresentation");
  auto objectRepresentationSpecs =
      records.getAllDerivedDefinitions("VPIObjectRepresentationSpec");
  auto propertyValueKinds =
      records.getAllDerivedDefinitions("VPIPropertyValueKind");
  auto propertyStabilities =
      records.getAllDerivedDefinitions("VPIPropertyStability");
  auto propertyProtectedAccesses =
      records.getAllDerivedDefinitions("VPIPropertyProtectedAccess");
  auto propertyRealizations =
      records.getAllDerivedDefinitions("VPIPropertyRealization");
  auto properties = records.getAllDerivedDefinitions("VPIProperty");
  auto integerPropertyValues =
      records.getAllDerivedDefinitions("VPIIntegerPropertyValue");
  auto valueFormats = records.getAllDerivedDefinitions("VPIValueFormat");
  auto valueDefaults =
      records.getAllDerivedDefinitions("VPIValueDefaultFormat");
  auto valueReadSemantics =
      records.getAllDerivedDefinitions("VPIValueReadSemantics");
  auto valueRequirements =
      records.getAllDerivedDefinitions("VPIValueRequirement");
  auto valuePolicies = records.getAllDerivedDefinitions("VPIValuePolicy");
  auto arrayValueFormats =
      records.getAllDerivedDefinitions("VPIArrayValueFormat");
  auto arrayValuePolicies =
      records.getAllDerivedDefinitions("VPIArrayValuePolicy");
  auto traversalModes = records.getAllDerivedDefinitions("VPITraversalMode");
  auto traversalOrders = records.getAllDerivedDefinitions("VPITraversalOrder");
  auto automaticRelations =
      records.getAllDerivedDefinitions("VPIAutomaticRelation");
  auto traversalEdges = records.getAllDerivedDefinitions("VPITraversalEdge");
  auto indexedAccessKinds =
      records.getAllDerivedDefinitions("VPIIndexedAccessKind");
  auto indexedAccesses = records.getAllDerivedDefinitions("VPIIndexedAccess");
  auto indexedTypeResults =
      records.getAllDerivedDefinitions("VPIIndexedTypeResult");
  auto callbackPhases =
      records.getAllDerivedDefinitions("VPIStatementCallbackPhase");
  auto callbackPolicies =
      records.getAllDerivedDefinitions("VPIStatementCallbackPolicy");
  auto callbackSpecs =
      records.getAllDerivedDefinitions("VPIStatementCallbackSpec");
  if (families.empty() || roles.empty() || objects.empty() ||
      relations.empty() || objectSets.empty() ||
      objectRepresentations.empty() || objectRepresentationSpecs.empty() ||
      propertyValueKinds.empty() || propertyStabilities.empty() ||
      propertyProtectedAccesses.empty() || propertyRealizations.empty() ||
      properties.empty() || valueFormats.empty() || valueDefaults.empty() ||
      valueReadSemantics.empty() || valueRequirements.empty() ||
      valuePolicies.empty() || arrayValueFormats.empty() ||
      arrayValuePolicies.empty() || traversalModes.empty() ||
      traversalOrders.empty() || automaticRelations.empty() ||
      traversalEdges.empty() || indexedAccessKinds.empty() ||
      indexedAccesses.empty() || indexedTypeResults.empty() ||
      callbackPhases.empty() || callbackPolicies.empty() ||
      callbackSpecs.empty()) {
    PrintError("VPI object model needs families, roles, objects, relations, "
               "object sets, object representations, properties, value "
               "policies, traversal modes, "
               "orders, automatic relations, edges, indexed-access policies, "
               "and statement callback policies");
    return false;
  }
  if (families.size() > 64) {
    PrintError("VPI object model supports at most 64 families");
    return false;
  }
  if (objectSets.size() > std::numeric_limits<uint16_t>::max()) {
    PrintError("VPI object model supports at most 65535 object sets");
    return false;
  }

  StringMap<const Record *> familyNames;
  DenseMap<uint32_t, const Record *> familyValues;
  for (const Record *family : families) {
    StringRef name;
    uint32_t value = 0;
    if (!getCppName(*family, "VPI object family", name))
      return false;
    if (!getU32(*family, "value", 0, value))
      return false;
    if (value >= 64) {
      PrintError(family->getLoc(),
                 "VPI object family value must be in [0, 63]");
      return false;
    }
    if (!familyNames.try_emplace(name, family).second) {
      PrintError(family->getLoc(), "duplicate VPI object family name");
      return false;
    }
    if (!familyValues.try_emplace(value, family).second) {
      PrintError(family->getLoc(), "duplicate VPI object family value");
      return false;
    }
  }

  StringMap<uint32_t> supportedRoles{{"Concrete", 0},
                                     {"AbstractSelector", 1},
                                     {"RelationOnly", 2},
                                     {"CompatibilitySelector", 3}};
  StringMap<const Record *> roleNames;
  DenseMap<uint32_t, const Record *> roleValues;
  for (const Record *role : roles) {
    StringRef name;
    uint32_t value = 0;
    if (!getCppName(*role, "VPI object role", name))
      return false;
    if (!getU32(*role, "value", 0, value))
      return false;
    auto supported = supportedRoles.find(name);
    if (supported == supportedRoles.end() || supported->second != value ||
        role->getValueAsBit("concrete") != (name == "Concrete")) {
      PrintError(role->getLoc(), "unsupported VPI object role");
      return false;
    }
    if (!roleNames.try_emplace(name, role).second) {
      PrintError(role->getLoc(), "duplicate VPI object role name");
      return false;
    }
    if (!roleValues.try_emplace(value, role).second) {
      PrintError(role->getLoc(), "duplicate VPI object role value");
      return false;
    }
  }
  if (roleNames.size() != supportedRoles.size()) {
    PrintError("VPI object model must define every supported object role");
    return false;
  }

  auto validateInventory = [&](ArrayRef<const Record *> inventory,
                               StringRef description,
                               StringMap<const Record *> &apiNames) {
    DenseMap<uint32_t, const Record *> canonicalValues;
    for (const Record *record : inventory) {
      StringRef apiName = record->getValueAsString("apiName");
      uint32_t value = 0;
      if (!isCppIdentifier(apiName) || !apiName.starts_with("vpi")) {
        PrintError(record->getLoc(),
                   Twine(description) + " apiName must be a vpi C identifier");
        return false;
      }
      if (!getU32(*record, "value", 1, value))
        return false;
      if (!apiNames.try_emplace(apiName, record).second) {
        PrintError(record->getLoc(),
                   Twine("duplicate ") + description + " API name");
        return false;
      }
      StringRef aliasOf = record->getValueAsString("aliasOf");
      if (aliasOf.empty()) {
        if (!canonicalValues.try_emplace(value, record).second) {
          PrintError(record->getLoc(),
                     Twine("duplicate canonical ") + description + " value");
          return false;
        }
      }
    }
    return true;
  };

  StringMap<const Record *> objectApiNames;
  StringMap<const Record *> relationApiNames;
  if (!validateInventory(objects, "VPI object", objectApiNames) ||
      !validateInventory(relations, "VPI relation", relationApiNames))
    return false;

  auto validateAliases = [&](ArrayRef<const Record *> inventory,
                             StringRef description,
                             const StringMap<const Record *> &apiNames,
                             const StringMap<const Record *> *externalNames) {
    for (const Record *record : inventory) {
      StringRef aliasOf = record->getValueAsString("aliasOf");
      if (aliasOf.empty())
        continue;
      auto target = apiNames.find(aliasOf);
      const Record *targetRecord =
          target == apiNames.end() ? nullptr : target->second;
      if (!targetRecord && externalNames) {
        auto externalTarget = externalNames->find(aliasOf);
        if (externalTarget != externalNames->end())
          targetRecord = externalTarget->second;
      }
      if (!targetRecord || !targetRecord->getValueAsString("aliasOf").empty() ||
          targetRecord->getValueAsInt("value") !=
              record->getValueAsInt("value")) {
        PrintError(record->getLoc(), Twine(description) +
                                         " alias must name a canonical entry "
                                         "with the same value");
        return false;
      }
      if (record->isSubClassOf("VPIObjectKind")) {
        DenseSet<const Record *> aliasFamilies;
        DenseSet<const Record *> targetFamilies;
        auto recordFamilies = record->getValueAsListOfDefs("families");
        auto canonicalFamilies = targetRecord->getValueAsListOfDefs("families");
        aliasFamilies.insert(recordFamilies.begin(), recordFamilies.end());
        targetFamilies.insert(canonicalFamilies.begin(),
                              canonicalFamilies.end());
        if (aliasFamilies != targetFamilies) {
          PrintError(record->getLoc(),
                     "VPI object alias must preserve target families");
          return false;
        }
      }
      if (record->isSubClassOf("VPIRelation") &&
          targetRecord->isSubClassOf("VPIRelation") &&
          record->getValueAsDef("cardinality") !=
              targetRecord->getValueAsDef("cardinality")) {
        PrintError(record->getLoc(),
                   "VPI relation alias must preserve target cardinality");
        return false;
      }
    }
    return true;
  };

  if (!validateAliases(objects, "VPI object", objectApiNames, nullptr) ||
      !validateAliases(relations, "VPI relation", relationApiNames,
                       &objectApiNames))
    return false;

  const std::pair<StringRef, uint32_t> supportedCallbackPhases[] = {
      {"BeforeExecute", 0},
      {"BeforeForControls", 1},
      {"BeforeForIncrement", 2},
  };
  const std::pair<StringRef, uint32_t> supportedCallbackPolicies[] = {
      {"OnceBefore", 0},
      {"ConditionEachIteration", 1},
      {"RepeatEncounterAndIteration", 2},
      {"ForInitialAndIncrement", 3},
      {"ForeverEncounterAndIteration", 4},
      {"DelayEncounter", 5},
      {"EventEncounter", 6},
      {"CallBefore", 7},
  };

  for (const Record *object : objects) {
    const Record *role = object->getValueAsDef("role");
    if (!roleNames.contains(role->getValueAsString("cppName"))) {
      PrintError(object->getLoc(), "unknown role on VPI object");
      return false;
    }
    DenseSet<const Record *> seenFamilies;
    auto objectFamilies = object->getValueAsListOfDefs("families");
    if (objectFamilies.empty()) {
      PrintError(object->getLoc(), "VPI object needs at least one family");
      return false;
    }
    for (const Record *family : objectFamilies)
      if (!seenFamilies.insert(family).second) {
        PrintError(object->getLoc(), "duplicate family on VPI object");
        return false;
      }
  }

  for (const Record *relation : relations) {
    StringRef cardinality =
        relation->getValueAsDef("cardinality")->getValueAsString("cppName");
    if (cardinality != "One" && cardinality != "Many" &&
        cardinality != "OneOrMany") {
      PrintError(relation->getLoc(), "unsupported VPI relation cardinality");
      return false;
    }
  }

  auto validateEnum = [](ArrayRef<const Record *> values,
                         ArrayRef<std::pair<StringRef, uint32_t>> supported,
                         StringRef description) {
    StringMap<uint32_t> expected;
    for (auto [name, value] : supported)
      expected[name] = value;
    StringSet<> names;
    DenseSet<uint32_t> numbers;
    for (const Record *record : values) {
      StringRef name;
      uint32_t value = 0;
      if (!getCppName(*record, description, name) ||
          !getU32(*record, "value", 0, value))
        return false;
      auto found = expected.find(name);
      if (found == expected.end() || found->second != value ||
          !names.insert(name).second || !numbers.insert(value).second) {
        PrintError(record->getLoc(),
                   Twine("unsupported or duplicate ") + description);
        return false;
      }
    }
    if (names.size() != expected.size()) {
      PrintError(Twine("missing ") + description);
      return false;
    }
    return true;
  };
  const std::pair<StringRef, uint32_t> supportedModes[] = {{"Handle", 0},
                                                           {"Iterate", 1}};
  const std::pair<StringRef, uint32_t> supportedOrders[] = {{"None", 0},
                                                            {"Source", 1},
                                                            {"Declaration", 2},
                                                            {"Index", 3},
                                                            {"Time", 4}};
  const std::pair<StringRef, uint32_t> supportedAutomaticRelations[] = {
      {"None", 0},
      {"DirectChild", 1},
      {"ParentScope", 2},
      {"DirectPortConnection", 3},
      {"IndexedContainer", 4},
      {"DefinitionMember", 5},
      {"DefinitionMemberParent", 6},
      {"DefinitionMemberExpr", 7},
      {"DefinitionMemberInstanceRelation", 8}};
  const std::pair<StringRef, uint32_t> supportedIndexedAccessKinds[] = {
      {"PortElement", 0},
      {"NetElement", 1},
      {"VariableElement", 2},
      {"RelationElement", 3}};
  const std::pair<StringRef, uint32_t> supportedPropertyValueKinds[] = {
      {"Boolean", 0}, {"Integer", 1}, {"Int64", 2}, {"String", 3}};
  const std::pair<StringRef, uint32_t> supportedPropertyStabilities[] = {
      {"Static", 0}, {"Dynamic", 1}};
  const std::pair<StringRef, uint32_t> supportedPropertyProtectedAccesses[] = {
      {"Denied", 0}, {"Allowed", 1}};
  const std::pair<StringRef, uint32_t> supportedPropertyRealizations[] = {
      {"Derived", 0},
      {"FixedImage", 1},
      {"Runtime", 2},
      {"IndexedImage", 3},
      {"DefinitionImage", 4}};
  const std::pair<StringRef, uint32_t> supportedObjectRepresentations[] = {
      {"PhysicalScope", 0}, {"PhysicalObject", 1}, {"CodeUnit", 2},
      {"Statement", 3},     {"StaticImage", 4},    {"SemanticSynthetic", 5},
      {"Runtime", 6}};
  const std::pair<StringRef, uint32_t> supportedValueFormats[] = {
      {"BinStr", 1}, {"OctStr", 2},    {"DecStr", 3}, {"HexStr", 4},
      {"Scalar", 5}, {"Int", 6},       {"Real", 7},   {"String", 8},
      {"Vector", 9}, {"Strength", 10}, {"Time", 11},  {"ObjType", 12}};
  const std::pair<StringRef, uint32_t> supportedValueDefaults[] = {
      {"Semantic", 0}, {"ScalarOrVector", 1}, {"Integer", 2},
      {"Real", 3},     {"String", 4},         {"Time", 5}};
  const std::pair<StringRef, uint32_t> supportedValueReadSemantics[] = {
      {"Snapshot", 0}, {"Evaluate", 1}};
  const std::pair<StringRef, uint32_t> supportedValueRequirements[] = {
      {"RejectWholeUnpacked", 1},
      {"RejectClassDefinitionOrigin", 2},
      {"RejectNonStaticClassTypespecOrigin", 4},
      {"RestrictStringConstant", 8}};
  const std::pair<StringRef, uint32_t> supportedArrayValueFormats[] = {
      {"Int", 6},        {"Real", 7},         {"Vector", 9},
      {"Time", 11},      {"ShortInt", 14},    {"LongInt", 15},
      {"ShortReal", 16}, {"RawTwoState", 17}, {"RawFourState", 18}};
  if (!validateEnum(traversalModes, supportedModes, "VPI traversal mode") ||
      !validateEnum(traversalOrders, supportedOrders, "VPI traversal order") ||
      !validateEnum(automaticRelations, supportedAutomaticRelations,
                    "VPI automatic relation") ||
      !validateEnum(indexedAccessKinds, supportedIndexedAccessKinds,
                    "VPI indexed access kind") ||
      !validateEnum(propertyValueKinds, supportedPropertyValueKinds,
                    "VPI property value kind") ||
      !validateEnum(propertyStabilities, supportedPropertyStabilities,
                    "VPI property stability") ||
      !validateEnum(propertyProtectedAccesses,
                    supportedPropertyProtectedAccesses,
                    "VPI protected property access") ||
      !validateEnum(propertyRealizations, supportedPropertyRealizations,
                    "VPI property realization") ||
      !validateEnum(objectRepresentations, supportedObjectRepresentations,
                    "VPI object representation") ||
      !validateEnum(valueFormats, supportedValueFormats, "VPI value format") ||
      !validateEnum(valueDefaults, supportedValueDefaults,
                    "VPI value default format") ||
      !validateEnum(valueReadSemantics, supportedValueReadSemantics,
                    "VPI value read semantics") ||
      !validateEnum(valueRequirements, supportedValueRequirements,
                    "VPI value requirement") ||
      !validateEnum(arrayValueFormats, supportedArrayValueFormats,
                    "VPI array value format") ||
      !validateEnum(callbackPhases, supportedCallbackPhases,
                    "VPI statement callback phase") ||
      !validateEnum(callbackPolicies, supportedCallbackPolicies,
                    "VPI statement callback policy"))
    return false;

  DenseSet<const Record *> callbackObjects;
  for (const Record *policy : callbackPolicies) {
    DenseSet<const Record *> phases;
    for (const Record *phase : policy->getValueAsListOfDefs("phases"))
      if (!phases.insert(phase).second) {
        PrintError(policy->getLoc(),
                   "duplicate phase in VPI statement callback policy");
        return false;
      }
    if (phases.empty()) {
      PrintError(policy->getLoc(),
                 "VPI statement callback policy needs a phase");
      return false;
    }
  }
  for (const Record *spec : callbackSpecs) {
    const Record *object = spec->getValueAsDef("object");
    if (!object->getValueAsString("aliasOf").empty() ||
        !object->getValueAsDef("role")->getValueAsBit("concrete") ||
        !llvm::is_contained(object->getValueAsListOfDefs("families"),
                            familyNames.lookup("Statement")) ||
        !callbackObjects.insert(object).second ||
        spec->getValueAsString("clause").empty()) {
      PrintError(spec->getLoc(),
                 "VPI statement callback spec needs a unique canonical "
                 "concrete statement object and an LRM clause");
      return false;
    }
  }

  auto isConcrete = [](const Record *object) {
    return object->getValueAsString("aliasOf").empty() &&
           object->getValueAsDef("role")->getValueAsBit("concrete");
  };
  auto expandSet = [&](const Record *set,
                       SmallVectorImpl<const Record *> &expanded) {
    DenseSet<const Record *> selected;
    DenseSet<const Record *> requestedFamilies;
    for (const Record *family : set->getValueAsListOfDefs("families")) {
      if (!requestedFamilies.insert(family).second) {
        PrintError(set->getLoc(), "duplicate family in VPI object set");
        return false;
      }
    }
    for (const Record *object : objects) {
      if (!isConcrete(object))
        continue;
      for (const Record *family : object->getValueAsListOfDefs("families"))
        if (requestedFamilies.contains(family)) {
          selected.insert(object);
          break;
        }
    }
    for (const Record *object : set->getValueAsListOfDefs("objects")) {
      if (!isConcrete(object) || !selected.insert(object).second) {
        PrintError(set->getLoc(),
                   "VPI object set needs unique canonical concrete objects");
        return false;
      }
    }
    DenseSet<const Record *> exclusions;
    for (const Record *object : set->getValueAsListOfDefs("exclude")) {
      if (!isConcrete(object) || !exclusions.insert(object).second ||
          !selected.erase(object)) {
        PrintError(set->getLoc(),
                   "VPI object set exclusion must select a unique member");
        return false;
      }
    }
    llvm::append_range(expanded, selected);
    llvm::sort(expanded, [](const Record *left, const Record *right) {
      return left->getValueAsInt("value") < right->getValueAsInt("value");
    });
    return true;
  };

  StringMap<const Record *> setNames;
  DenseMap<const Record *, SmallVector<const Record *>> expandedSets;
  const Record *iterateSourcesSet = nullptr;
  for (const Record *set : objectSets) {
    StringRef name;
    if (!getCppName(*set, "VPI object set", name))
      return false;
    if (!setNames.try_emplace(name, set).second) {
      PrintError(set->getLoc(), "duplicate VPI object set name");
      return false;
    }
    auto &expanded = expandedSets[set];
    if (!expandSet(set, expanded))
      return false;
    if (set->getValueAsBit("iterateSources")) {
      if (iterateSourcesSet || !expanded.empty() ||
          set->getValueAsBit("nullRoot") ||
          !set->getValueAsListOfDefs("exclude").empty()) {
        PrintError(set->getLoc(),
                   "derived iterator-source set must be unique and empty");
        return false;
      }
      iterateSourcesSet = set;
    } else if (expanded.empty() && !set->getValueAsBit("nullRoot") &&
               !set->getValueAsBit("allowEmpty")) {
      PrintError(set->getLoc(), "VPI object set expands to no concrete kinds");
      return false;
    }
  }

  if (iterateSourcesSet) {
    DenseSet<const Record *> selected;
    for (const Record *edge : traversalEdges) {
      const Record *mode = edge->getValueAsDef("mode");
      const Record *sources = edge->getValueAsDef("sources");
      if (sources == iterateSourcesSet) {
        PrintError(edge->getLoc(),
                   "derived iterator-source set cannot be an edge source");
        return false;
      }
      if (mode->getValueAsInt("value") != 1 ||
          sources->getValueAsBit("nullRoot"))
        continue;
      const auto &sourceKinds = expandedSets.find(sources)->second;
      selected.insert(sourceKinds.begin(), sourceKinds.end());
    }
    auto &expanded = expandedSets[iterateSourcesSet];
    llvm::append_range(expanded, selected);
    llvm::sort(expanded, [](const Record *left, const Record *right) {
      return left->getValueAsInt("value") < right->getValueAsInt("value");
    });
    if (expanded.empty()) {
      PrintError(iterateSourcesSet->getLoc(),
                 "derived iterator-source set expands to no concrete kinds");
      return false;
    }
  }

  DenseMap<const Record *, uint8_t> representationMasks;
  DenseSet<const Record *> specifiedRepresentations;
  for (const Record *spec : objectRepresentationSpecs) {
    const Record *representation = spec->getValueAsDef("representation");
    const Record *set = spec->getValueAsDef("objects");
    if (!specifiedRepresentations.insert(representation).second) {
      PrintError(spec->getLoc(),
                 "duplicate VPI object representation specification");
      return false;
    }
    if (set->getValueAsBit("nullRoot") ||
        set->getValueAsBit("iterateSources") ||
        set->getValueAsBit("allowEmpty")) {
      PrintError(spec->getLoc(),
                 "VPI object representation requires an exact non-root "
                 "object set");
      return false;
    }
    uint32_t bit =
        static_cast<uint32_t>(representation->getValueAsInt("value"));
    for (const Record *object : expandedSets.lookup(set))
      representationMasks[object] |= uint8_t{1} << bit;
  }
  if (specifiedRepresentations.size() != objectRepresentations.size()) {
    PrintError("every VPI object representation requires one specification");
    return false;
  }
  for (const Record *object : objects) {
    if (!isConcrete(object))
      continue;
    if (representationMasks.lookup(object) == 0) {
      PrintError(
          object->getLoc(),
          Twine("canonical concrete VPI object has no representation: ") +
              object->getValueAsString("apiName"));
      return false;
    }
  }

  StringMap<const Record *> propertyNames;
  DenseMap<uint32_t, const Record *> propertyValues;
  for (const Record *property : properties) {
    StringRef apiName = property->getValueAsString("apiName");
    uint32_t value = 0;
    const Record *sources = property->getValueAsDef("sources");
    if (!isCppIdentifier(apiName) || !apiName.starts_with("vpi") ||
        !getU32(*property, "value", 1, value) ||
        property->getValueAsString("clause").empty() ||
        (expandedSets.lookup(sources).empty() &&
         !sources->getValueAsBit("nullRoot"))) {
      PrintError(property->getLoc(),
                 "VPI property needs a unique API name and value, concrete "
                 "or null-root sources, and an LRM clause");
      return false;
    }
    if (!propertyNames.try_emplace(apiName, property).second ||
        !propertyValues.try_emplace(value, property).second) {
      PrintError(property->getLoc(),
                 "duplicate VPI property API name or value");
      return false;
    }
    if (property->getValueAsBit("symbolicString") &&
        property->getValueAsDef("valueKind")->getValueAsString("cppName") !=
            "Integer") {
      PrintError(property->getLoc(),
                 "symbolic VPI property must have an integer value kind");
      return false;
    }
    if (property->getValueAsBit("symbolicString") &&
        property->getValueAsDef("valueKind")->getValueAsString("cppName") !=
            "Integer") {
      PrintError(property->getLoc(),
                 "symbolic VPI property must have an integer value kind");
      return false;
    }
    const auto &propertySources = expandedSets.lookup(sources);
    for (StringRef field :
         {"dynamicSources", "protectedSources", "fixedImageSources",
          "runtimeSources", "definitionImageSources"}) {
      const Record *overrides = property->getValueAsDef(field);
      if (overrides->getValueAsBit("nullRoot")) {
        PrintError(property->getLoc(), Twine("VPI property ") + field +
                                           " cannot select the null root");
        return false;
      }
      for (const Record *source : expandedSets.lookup(overrides))
        if (!llvm::is_contained(propertySources, source)) {
          PrintError(property->getLoc(),
                     Twine("VPI property ") + field +
                         " must be a subset of its sources");
          return false;
        }
    }
    const auto &fixedSources =
        expandedSets.lookup(property->getValueAsDef("fixedImageSources"));
    const auto &runtimeSources =
        expandedSets.lookup(property->getValueAsDef("runtimeSources"));
    const auto &definitionSources =
        expandedSets.lookup(property->getValueAsDef("definitionImageSources"));
    for (const Record *source : fixedSources)
      if (llvm::is_contained(runtimeSources, source) ||
          llvm::is_contained(definitionSources, source)) {
        PrintError(property->getLoc(),
                   "VPI property realization override sets overlap");
        return false;
      }
    for (const Record *source : runtimeSources)
      if (llvm::is_contained(definitionSources, source)) {
        PrintError(property->getLoc(),
                   "VPI property realization override sets overlap");
        return false;
      }
    const auto &dynamicSources =
        expandedSets.lookup(property->getValueAsDef("dynamicSources"));
    bool baseDynamic =
        property->getValueAsDef("stability")->getValueAsString("cppName") ==
        "Dynamic";
    bool baseRuntime =
        property->getValueAsDef("realization")->getValueAsString("cppName") ==
        "Runtime";
    bool baseFixed =
        property->getValueAsDef("realization")->getValueAsString("cppName") ==
        "FixedImage";
    for (const Record *source : propertySources) {
      bool dynamic = baseDynamic || llvm::is_contained(dynamicSources, source);
      bool runtime = baseRuntime || llvm::is_contained(runtimeSources, source);
      bool fixed = baseFixed || llvm::is_contained(fixedSources, source);
      if (dynamic != runtime || (fixed && runtime)) {
        PrintError(property->getLoc(),
                   "dynamic VPI property sources must use Runtime "
                   "realization, and only those sources may use Runtime");
        return false;
      }
    }
    if (sources->getValueAsBit("nullRoot") && baseDynamic != baseRuntime) {
      PrintError(property->getLoc(),
                 "dynamic null-root VPI property must use Runtime "
                 "realization");
      return false;
    }
  }

  DenseSet<uint64_t> integerPropertyKeys;
  StringSet<> integerPropertySymbolicNames;
  for (const Record *valueRecord : integerPropertyValues) {
    const Record *property = valueRecord->getValueAsDef("property");
    uint32_t propertyValue = 0;
    uint32_t value = 0;
    StringRef symbolicName = valueRecord->getValueAsString("symbolicName");
    if (!getU32(*property, "value", 1, propertyValue) ||
        !getU32(*valueRecord, "value", 0, value) ||
        property->getValueAsDef("valueKind")->getValueAsString("cppName") !=
            "Integer" ||
        property->getValueAsBit("symbolicString") != !symbolicName.empty() ||
        (!symbolicName.empty() && (!isCppIdentifier(symbolicName) ||
                                   !symbolicName.starts_with("vpi")))) {
      PrintError(valueRecord->getLoc(),
                 "VPI integer property value needs an integer property, "
                 "encodable value, and matching vpi symbolic-name policy");
      return false;
    }
    uint64_t key = (uint64_t{propertyValue} << 32) | value;
    if (!integerPropertyKeys.insert(key).second) {
      PrintError(valueRecord->getLoc(),
                 "duplicate VPI integer property domain value");
      return false;
    }
    if (!symbolicName.empty()) {
      std::string keyName = (Twine(propertyValue) + ":" + symbolicName).str();
      if (!integerPropertySymbolicNames.insert(keyName).second) {
        PrintError(valueRecord->getLoc(),
                   "duplicate symbolic name in VPI integer property domain");
        return false;
      }
    }
  }

  DenseMap<uint32_t, const Record *> valuePolicySources;
  for (const Record *policy : valuePolicies) {
    const Record *sources = policy->getValueAsDef("sources");
    if (sources->getValueAsBit("nullRoot") ||
        policy->getValueAsString("clause").empty()) {
      PrintError(policy->getLoc(),
                 "VPI value policy needs non-root sources and an LRM clause");
      return false;
    }
    DenseSet<const Record *> seenFormats;
    uint32_t formatMask = 0;
    for (const Record *format : policy->getValueAsListOfDefs("formats")) {
      uint32_t value = static_cast<uint32_t>(format->getValueAsInt("value"));
      if (!seenFormats.insert(format).second || value == 0 || value >= 16) {
        PrintError(policy->getLoc(),
                   "VPI value policy has duplicate or unencodable formats");
        return false;
      }
      formatMask |= uint32_t{1} << value;
    }
    if (formatMask == 0) {
      PrintError(policy->getLoc(), "VPI value policy needs a format");
      return false;
    }
    DenseSet<const Record *> seenRequirements;
    uint32_t requirementMask = 0;
    for (const Record *requirement :
         policy->getValueAsListOfDefs("requirements")) {
      uint32_t value =
          static_cast<uint32_t>(requirement->getValueAsInt("value"));
      if (!seenRequirements.insert(requirement).second || value == 0 ||
          (value & (value - 1)) != 0 || value > UINT8_MAX) {
        PrintError(policy->getLoc(),
                   "VPI value policy has duplicate or invalid requirements");
        return false;
      }
      requirementMask |= value;
    }
    for (const Record *source : expandedSets.lookup(sources)) {
      uint32_t value = static_cast<uint32_t>(source->getValueAsInt("value"));
      auto [existing, inserted] = valuePolicySources.try_emplace(value, policy);
      if (!inserted) {
        PrintError(policy->getLoc(),
                   Twine("duplicate VPI value policy for source ") +
                       Twine(value) + "; first declared by " +
                       existing->second->getName());
        return false;
      }
    }
  }

  DenseSet<const Record *> arrayPolicyFormats;
  size_t arrayPolicyElementCount = 0;
  for (const Record *policy : arrayValuePolicies) {
    const Record *format = policy->getValueAsDef("format");
    auto elements = policy->getValueAsListOfDefs("elementTypes");
    if (policy->getValueAsString("clause").empty() || elements.empty() ||
        !arrayPolicyFormats.insert(format).second) {
      PrintError(policy->getLoc(),
                 "VPI array value policy needs a unique format, element "
                 "types, and an LRM clause");
      return false;
    }
    DenseSet<const Record *> seenElements;
    if (elements.size() > UINT16_MAX ||
        arrayPolicyElementCount > UINT16_MAX - elements.size()) {
      PrintError(policy->getLoc(),
                 "VPI array value policies support at most 65535 element "
                 "entries");
      return false;
    }
    arrayPolicyElementCount += elements.size();
    for (const Record *element : elements) {
      if (!isConcrete(element) || !seenElements.insert(element).second) {
        PrintError(policy->getLoc(),
                   "VPI array value policy needs unique canonical concrete "
                   "element types");
        return false;
      }
    }
  }
  if (arrayPolicyFormats.size() != arrayValueFormats.size()) {
    PrintError("VPI array value model needs exactly one policy per format");
    return false;
  }

  DenseMap<uint32_t, const Record *> indexedAccessSources;
  DenseMap<const Record *, SmallVector<const Record *>> accessesByKind;
  for (const Record *access : indexedAccesses) {
    const Record *sources = access->getValueAsDef("sources");
    const Record *targets = access->getValueAsDef("targets");
    const Record *accessKind = access->getValueAsDef("accessKind");
    const bool relationBacked =
        accessKind->getValueAsString("cppName") == "RelationElement";
    if (sources->getValueAsBit("nullRoot") ||
        targets->getValueAsBit("nullRoot") ||
        expandedSets.lookup(sources).empty() ||
        expandedSets.lookup(targets).empty() ||
        access->getValueAsString("clause").empty()) {
      PrintError(access->getLoc(),
                 "VPI indexed access needs non-root concrete source and "
                 "target sets and an LRM clause");
      return false;
    }
    accessesByKind[accessKind].push_back(access);
    for (StringRef field :
         {"terminalResult", "unpackedFallback", "packedFallback"}) {
      const Record *result = access->getValueAsDef(field);
      if (!llvm::is_contained(expandedSets.lookup(targets), result)) {
        PrintError(access->getLoc(), Twine("VPI indexed-access ") + field +
                                         " must belong to its target set");
        return false;
      }
    }
    const Record *relationSelector = access->getValueAsDef("relationSelector");
    if (relationBacked) {
      if (access->getValueAsBit("mapSemanticType")) {
        PrintError(access->getLoc(),
                   "relation-backed indexed access cannot map semantic "
                   "value result types");
        return false;
      }
      bool foundEdge = false;
      for (const Record *edge : traversalEdges) {
        if (edge->getValueAsDef("selector") != relationSelector ||
            edge->getValueAsDef("mode")->getValueAsString("cppName") !=
                "Iterate" ||
            edge->getValueAsDef("order")->getValueAsString("cppName") !=
                "Index")
          continue;
        if (expandedSets.lookup(edge->getValueAsDef("sources")) !=
                expandedSets.lookup(sources) ||
            !llvm::is_contained(
                expandedSets.lookup(edge->getValueAsDef("targets")),
                access->getValueAsDef("terminalResult")))
          continue;
        foundEdge = true;
        break;
      }
      if (!foundEdge) {
        PrintError(access->getLoc(),
                   "relation-backed indexed access must reference its exact "
                   "IndexOrder iterate edge");
        return false;
      }
      const auto &sourceKinds = expandedSets.lookup(sources);
      if (sourceKinds.size() != 1 ||
          sourceKinds.front() != access->getValueAsDef("unpackedFallback")) {
        PrintError(access->getLoc(),
                   "relation-backed indexed access must have one array "
                   "source and use it as its partial result");
        return false;
      }
    } else if (relationSelector->getValueAsString("apiName") != "vpiIndex") {
      PrintError(access->getLoc(),
                 "physical indexed access cannot carry a relation selector");
      return false;
    }
    for (const Record *source : expandedSets.lookup(sources)) {
      uint32_t value = static_cast<uint32_t>(source->getValueAsInt("value"));
      auto [existing, inserted] =
          indexedAccessSources.try_emplace(value, access);
      if (!inserted) {
        PrintError(access->getLoc(),
                   Twine("duplicate VPI indexed-access policy for source ") +
                       Twine(value) + "; first declared by " +
                       existing->second->getName());
        return false;
      }
    }
  }

  StringMap<const Record *> indexedTypeResultKeys;
  const Record *typespecFamily = familyNames.lookup("Typespec");
  for (const Record *mapping : indexedTypeResults) {
    const Record *kind = mapping->getValueAsDef("accessKind");
    const Record *typespec = mapping->getValueAsDef("selectedTypespec");
    const Record *result = mapping->getValueAsDef("result");
    if (mapping->getValueAsString("clause").empty() || !isConcrete(typespec) ||
        !llvm::is_contained(typespec->getValueAsListOfDefs("families"),
                            typespecFamily) ||
        result->getValueAsDef("role")->getValueAsString("cppName") !=
            "Concrete") {
      PrintError(mapping->getLoc(),
                 "VPI indexed type result needs a typespec, concrete result, "
                 "and LRM clause");
      return false;
    }
    auto accesses = accessesByKind.lookup(kind);
    if (accesses.empty()) {
      PrintError(mapping->getLoc(),
                 "VPI indexed type result has no access policy");
      return false;
    }
    bool hasMappedAccess = false;
    for (const Record *access : accesses) {
      if (!access->getValueAsBit("mapSemanticType"))
        continue;
      hasMappedAccess = true;
      if (!llvm::is_contained(
              expandedSets.lookup(access->getValueAsDef("targets")), result)) {
        PrintError(mapping->getLoc(),
                   "VPI indexed type result is outside its access targets");
        return false;
      }
    }
    if (!hasMappedAccess) {
      PrintError(mapping->getLoc(),
                 "VPI indexed type result has no semantic-mapping access");
      return false;
    }
    std::string key = (Twine(kind->getValueAsInt("value")) + ":" +
                       Twine(typespec->getValueAsInt("value")))
                          .str();
    if (!indexedTypeResultKeys.try_emplace(key, mapping).second) {
      PrintError(mapping->getLoc(), "duplicate VPI indexed type result");
      return false;
    }
  }

  const Record *semanticSyntheticRepresentation = nullptr;
  for (const Record *representation : objectRepresentations)
    if (representation->getValueAsString("cppName") == "SemanticSynthetic") {
      semanticSyntheticRepresentation = representation;
      break;
    }
  assert(semanticSyntheticRepresentation &&
         "validated representation enumeration is incomplete");
  const uint8_t semanticSyntheticMask =
      uint8_t{1} << semanticSyntheticRepresentation->getValueAsInt("value");
  auto requireSemanticSynthetic = [&](const Record *object,
                                      const Record *source) {
    if ((representationMasks.lookup(object) & semanticSyntheticMask) != 0)
      return true;
    PrintError(source->getLoc(),
               Twine("VPI indexed result requires SemanticSynthetic "
                     "representation: ") +
                   object->getValueAsString("apiName"));
    return false;
  };
  for (const Record *access : indexedAccesses) {
    const bool relationBacked =
        access->getValueAsDef("accessKind")->getValueAsString("cppName") ==
        "RelationElement";
    if (!requireSemanticSynthetic(access->getValueAsDef("unpackedFallback"),
                                  access))
      return false;
    if (relationBacked)
      continue;
    if (!requireSemanticSynthetic(access->getValueAsDef("terminalResult"),
                                  access) ||
        !requireSemanticSynthetic(access->getValueAsDef("packedFallback"),
                                  access))
      return false;
    for (const Record *target :
         expandedSets.lookup(access->getValueAsDef("targets")))
      if (!requireSemanticSynthetic(target, access))
        return false;
  }
  for (const Record *mapping : indexedTypeResults)
    if (!requireSemanticSynthetic(mapping->getValueAsDef("result"), mapping))
      return false;

  const Record *runtimeRepresentation = nullptr;
  for (const Record *representation : objectRepresentations)
    if (representation->getValueAsString("cppName") == "Runtime") {
      runtimeRepresentation = representation;
      break;
    }
  assert(runtimeRepresentation &&
         "validated representation enumeration is incomplete");
  const uint8_t runtimeMask = uint8_t{1}
                              << runtimeRepresentation->getValueAsInt("value");
  const Record *frameObject = objectApiNames.lookup("vpiFrame");
  const Record *classObject = objectApiNames.lookup("vpiClassObj");
  assert(frameObject && classObject &&
         "validated object inventory is incomplete");
  auto sourceContains = [&](const Record *edge, const Record *object) {
    return llvm::is_contained(
        expandedSets.lookup(edge->getValueAsDef("sources")), object);
  };
  auto requiresTransientTargets = [&](const Record *edge) {
    StringRef selector =
        edge->getValueAsDef("selector")->getValueAsString("apiName");
    if (sourceContains(edge, frameObject))
      return selector == "vpiAutomatics";
    if (!sourceContains(edge, classObject))
      return false;
    return selector == "vpiVariables" || selector == "vpiMethods" ||
           selector == "vpiNamedEvent" || selector == "vpiNamedEventArray" ||
           selector == "vpiVirtualInterfaceVar" ||
           selector == "vpiConstraint" || selector == "vpiMessages";
  };
  for (const Record *edge : traversalEdges) {
    if (!requiresTransientTargets(edge))
      continue;
    for (const Record *target :
         expandedSets.lookup(edge->getValueAsDef("targets"))) {
      if ((representationMasks.lookup(target) & runtimeMask) != 0)
        continue;
      PrintError(edge->getLoc(),
                 Twine("VPI transient traversal target requires Runtime "
                       "representation: ") +
                     target->getValueAsString("apiName"));
      return false;
    }
  }

  StringMap<const Record *> edgeKeys;
  for (const Record *edge : traversalEdges) {
    const Record *sources = edge->getValueAsDef("sources");
    const Record *targets = edge->getValueAsDef("targets");
    const Record *selector = edge->getValueAsDef("selector");
    const Record *mode = edge->getValueAsDef("mode");
    const Record *order = edge->getValueAsDef("order");
    const Record *automatic = edge->getValueAsDef("automaticRelation");
    int64_t inverseSelector = edge->getValueAsInt("inverseSelector");
    StringRef clause = edge->getValueAsString("clause");
    if (targets->getValueAsBit("nullRoot") || clause.empty() ||
        inverseSelector < 0 || inverseSelector > UINT32_MAX) {
      PrintError(edge->getLoc(),
                 "VPI traversal needs non-root targets, an LRM clause, and a "
                 "32-bit inverse selector");
      return false;
    }
    if (edge->getValueAsBit("statementContainment")) {
      if (sources->getValueAsBit("nullRoot")) {
        PrintError(edge->getLoc(),
                   "statement containment cannot start at the null root");
        return false;
      }
      const Record *statementFamily = familyNames.lookup("Statement");
      for (const Record *target : expandedSets.lookup(targets))
        if (!llvm::is_contained(target->getValueAsListOfDefs("families"),
                                statementFamily)) {
          PrintError(edge->getLoc(),
                     "statement containment target is not a statement");
          return false;
        }
    }
    StringRef automaticName = automatic->getValueAsString("cppName");
    if (automaticName != "None") {
      if (edge->getValueAsBit("statementContainment") ||
          sources->getValueAsBit("nullRoot")) {
        PrintError(edge->getLoc(),
                   "automatic design relation cannot be statement "
                   "containment or start at the null root");
        return false;
      }
      StringRef requiredMode =
          automaticName == "DirectChild" ||
                  automaticName == "DefinitionMember" ||
                  automaticName == "DefinitionMemberInstanceRelation"
              ? "Iterate"
              : "Handle";
      if (mode->getValueAsString("cppName") != requiredMode) {
        PrintError(edge->getLoc(),
                   "automatic design relation has incompatible traversal "
                   "mode");
        return false;
      }
      if (automaticName == "DirectChild") {
        const Record *scopeFamily = familyNames.lookup("Scope");
        for (const Record *source : expandedSets.lookup(sources))
          if (!llvm::is_contained(source->getValueAsListOfDefs("families"),
                                  scopeFamily)) {
            PrintError(edge->getLoc(),
                       "automatic direct-child source is not a scope");
            return false;
          }
      }
      if (automaticName == "ParentScope") {
        const Record *scopeFamily = familyNames.lookup("Scope");
        bool acceptsScope = false;
        for (const Record *target : expandedSets.lookup(targets))
          acceptsScope |= llvm::is_contained(
              target->getValueAsListOfDefs("families"), scopeFamily);
        if (!acceptsScope) {
          PrintError(edge->getLoc(),
                     "automatic parent-scope edge cannot target a scope");
          return false;
        }
      }
      if (automaticName == "IndexedContainer") {
        const Record *arrayFamily = familyNames.lookup("Array");
        for (const Record *source : expandedSets.lookup(sources)) {
          bool hasIndexedContainer = false;
          for (const Record *access : indexedAccesses) {
            if (access->getValueAsDef("accessKind")
                        ->getValueAsString("cppName") != "RelationElement" ||
                (source != access->getValueAsDef("terminalResult") &&
                 source != access->getValueAsDef("unpackedFallback")))
              continue;

            bool acceptsEveryContainer = true;
            for (const Record *container :
                 expandedSets.lookup(access->getValueAsDef("sources"))) {
              acceptsEveryContainer &=
                  llvm::is_contained(expandedSets.lookup(targets), container);
              acceptsEveryContainer &= llvm::is_contained(
                  container->getValueAsListOfDefs("families"), arrayFamily);
            }
            if (acceptsEveryContainer) {
              hasIndexedContainer = true;
              break;
            }
          }
          if (!hasIndexedContainer) {
            PrintError(edge->getLoc(),
                       "automatic indexed-container source has no "
                       "relation-backed array container in its target set");
            return false;
          }
        }
      }
      if (automaticName == "DirectPortConnection") {
        for (const Record *source : expandedSets.lookup(sources))
          if (source->getValueAsString("apiName") != "vpiPort") {
            PrintError(edge->getLoc(),
                       "automatic direct-port connection source is not a "
                       "port");
            return false;
          }
        if (selector->getValueAsString("apiName") != "vpiLowConn") {
          PrintError(edge->getLoc(),
                     "automatic direct-port connection selector is not "
                     "vpiLowConn");
          return false;
        }
      }
      if (automaticName == "DefinitionMember") {
        for (const Record *source : expandedSets.lookup(sources))
          if (source->getValueAsString("apiName") != "vpiModule") {
            PrintError(edge->getLoc(),
                       "automatic definition-member source is not a module");
            return false;
          }
        for (const Record *target : expandedSets.lookup(targets))
          if (target->getValueAsString("apiName") != "vpiIODecl") {
            PrintError(edge->getLoc(),
                       "automatic definition-member target is not an io "
                       "declaration");
            return false;
          }
        if (selector->getValueAsString("apiName") != "vpiIODecl") {
          PrintError(edge->getLoc(),
                     "automatic definition-member selector is not "
                     "vpiIODecl");
          return false;
        }
      }
      if (automaticName == "DefinitionMemberInstanceRelation") {
        for (const Record *source : expandedSets.lookup(sources))
          if (source->getValueAsString("apiName") != "vpiRefObj") {
            PrintError(edge->getLoc(),
                       "automatic definition-member instance relation source "
                       "is not a reference object");
            return false;
          }
        for (const Record *target : expandedSets.lookup(targets))
          if (target->getValueAsString("apiName") != "vpiPort" &&
              target->getValueAsString("apiName") != "vpiPortBit") {
            PrintError(edge->getLoc(),
                       "automatic definition-member instance relation target "
                       "is not a port");
            return false;
          }
        StringRef selectorName = selector->getValueAsString("apiName");
        StringRef inverseName = selectorName == "vpiPort"       ? "vpiLowConn"
                                : selectorName == "vpiPortInst" ? "vpiHighConn"
                                                                : StringRef{};
        const Record *inverse = relationApiNames.lookup(inverseName);
        if (!inverse || inverseSelector != inverse->getValueAsInt("value")) {
          PrintError(edge->getLoc(),
                     "automatic definition-member instance relation has an "
                     "invalid inverse selector");
          return false;
        }
      }
      if (automaticName == "DefinitionMemberParent") {
        StringRef selectorName = selector->getValueAsString("apiName");
        for (const Record *source : expandedSets.lookup(sources)) {
          StringRef sourceName = source->getValueAsString("apiName");
          if (sourceName != "vpiIODecl" && sourceName != "vpiRefObj") {
            PrintError(edge->getLoc(),
                       "automatic definition-member parent source is not an "
                       "io declaration or reference object");
            return false;
          }
        }
        if (selectorName != "vpiInstance") {
          PrintError(edge->getLoc(),
                     "automatic definition-member parent selector is not "
                     "vpiInstance");
          return false;
        }
        for (StringRef required : {"vpiModule", "vpiInterface", "vpiProgram"}) {
          bool accepted = llvm::any_of(
              expandedSets.lookup(targets), [&](const Record *target) {
                return target->getValueAsString("apiName") == required;
              });
          if (!accepted) {
            PrintError(edge->getLoc(),
                       "automatic definition-member parent target does not "
                       "include " +
                           required);
            return false;
          }
        }
      }
      if (automaticName == "DefinitionMemberExpr") {
        StringRef selectorName = selector->getValueAsString("apiName");
        bool ioExpression = false;
        for (const Record *source : expandedSets.lookup(sources)) {
          StringRef sourceName = source->getValueAsString("apiName");
          if ((sourceName != "vpiIODecl" || selectorName != "vpiExpr") &&
              (sourceName != "vpiRefObj" || selectorName != "vpiActual")) {
            PrintError(edge->getLoc(),
                       "automatic definition-member endpoint is not an io "
                       "expression or reference actual");
            return false;
          }
          ioExpression |= sourceName == "vpiIODecl";
        }
        if (selectorName != "vpiExpr" && selectorName != "vpiActual") {
          PrintError(edge->getLoc(),
                     "automatic definition-member endpoint selector is not "
                     "vpiExpr or vpiActual");
          return false;
        }
        if (ioExpression &&
            !llvm::any_of(
                expandedSets.lookup(targets), [](const Record *target) {
                  return target->getValueAsString("apiName") == "vpiRefObj";
                })) {
          PrintError(edge->getLoc(),
                     "automatic definition-member expression target does "
                     "not include vpiRefObj");
          return false;
        }
      }
      if (automaticName != "DefinitionMemberInstanceRelation" &&
          inverseSelector != 0) {
        PrintError(edge->getLoc(),
                   "automatic VPI traversal category does not support an "
                   "inverse selector");
        return false;
      }
    } else if (inverseSelector != 0) {
      PrintError(edge->getLoc(),
                 "non-automatic VPI traversal has an inverse selector");
      return false;
    }
    if (!selector->getValueAsString("aliasOf").empty()) {
      PrintError(edge->getLoc(), "VPI traversal selector must be canonical");
      return false;
    }
    if (selector->isSubClassOf("VPIObjectKind") &&
        selector->getValueAsDef("role")->getValueAsString("cppName") ==
            "RelationOnly") {
      PrintError(edge->getLoc(),
                 "relation-only traversal must use its VPIRelation record");
      return false;
    }
    StringRef modeName = mode->getValueAsString("cppName");
    if (selector->isSubClassOf("VPIRelation")) {
      StringRef cardinality =
          selector->getValueAsDef("cardinality")->getValueAsString("cppName");
      if ((cardinality == "One" && modeName != "Handle") ||
          (cardinality == "Many" && modeName != "Iterate")) {
        PrintError(edge->getLoc(),
                   "VPI traversal mode disagrees with relation cardinality");
        return false;
      }
    }
    if (modeName == "Handle" && order->getValueAsString("cppName") != "None") {
      PrintError(edge->getLoc(), "one-to-one VPI traversal cannot be ordered");
      return false;
    }
    auto addKey = [&](uint32_t source) {
      std::string key =
          (Twine(source) + ":" + Twine(selector->getValueAsInt("value")) + ":" +
           Twine(mode->getValueAsInt("value")))
              .str();
      auto [existing, inserted] = edgeKeys.try_emplace(key, edge);
      if (!inserted) {
        PrintError(edge->getLoc(),
                   Twine("duplicate expanded VPI edge for source ") +
                       Twine(source) + ", selector " +
                       Twine(selector->getValueAsInt("value")) + ", mode " +
                       modeName + "; first declared by " +
                       existing->second->getName());
        return false;
      }
      return true;
    };
    for (const Record *source : expandedSets.lookup(sources))
      if (!addKey(static_cast<uint32_t>(source->getValueAsInt("value"))))
        return false;
    if (sources->getValueAsBit("nullRoot") && !addKey(0))
      return false;
  }
  return true;
}

bool emitVPIObjectModel(const RecordKeeper &records, raw_ostream &os) {
  if (!validateVPIObjectModel(records))
    return true;

  os << "//===- VPIObjectModel.h.inc - generated; do not edit -*- C++ "
        "-*-===//\n\n";
  os << "#ifndef OBELISK_REFLECTION_VPIOBJECTMODEL_H_INC\n";
  os << "#define OBELISK_REFLECTION_VPIOBJECTMODEL_H_INC\n\n";
  os << "#include <cstddef>\n#include <cstdint>\n\n";
  os << "namespace obelisk::reflection {\n\n";

  auto familyRecords = records.getAllDerivedDefinitions("VPIObjectFamily");
  SmallVector<const Record *> families(familyRecords.begin(),
                                       familyRecords.end());
  llvm::sort(families, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  DenseMap<const Record *, unsigned> familyBits;
  os << "enum class VPIObjectFamily : uint8_t {\n";
  for (const Record *family : families) {
    unsigned value = family->getValueAsInt("value");
    familyBits[family] = value;
    os << formatv("  {0} = {1},\n", family->getValueAsString("cppName"), value);
  }
  os << "};\n\n";
  os << "constexpr uint64_t vpiFamilyMask(VPIObjectFamily family) {\n"
        "  return uint64_t{1} << static_cast<unsigned>(family);\n"
        "}\n\n";

  auto roleRecords = records.getAllDerivedDefinitions("VPIObjectRole");
  SmallVector<const Record *> roles(roleRecords.begin(), roleRecords.end());
  llvm::sort(roles, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIObjectRole : uint8_t {\n";
  for (const Record *role : roles)
    os << formatv("  {0} = {1},\n", role->getValueAsString("cppName"),
                  role->getValueAsInt("value"));
  os << "  Alias = 4,\n};\n\n";

  auto objectRecords = records.getAllDerivedDefinitions("VPIObjectKind");
  SmallVector<const Record *> objects(objectRecords.begin(),
                                      objectRecords.end());
  llvm::sort(objects, [](const Record *left, const Record *right) {
    auto leftKey = std::make_tuple(left->getValueAsInt("value"),
                                   !left->getValueAsString("aliasOf").empty(),
                                   left->getValueAsString("apiName"));
    auto rightKey = std::make_tuple(right->getValueAsInt("value"),
                                    !right->getValueAsString("aliasOf").empty(),
                                    right->getValueAsString("apiName"));
    return leftKey < rightKey;
  });

  auto representationRecords =
      records.getAllDerivedDefinitions("VPIObjectRepresentation");
  SmallVector<const Record *> representations(representationRecords.begin(),
                                              representationRecords.end());
  llvm::sort(representations, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  DenseMap<uint32_t, uint8_t> representationMasks;
  for (const Record *spec :
       records.getAllDerivedDefinitions("VPIObjectRepresentationSpec")) {
    const Record *representation = spec->getValueAsDef("representation");
    const Record *set = spec->getValueAsDef("objects");
    uint8_t bit = uint8_t{1} << representation->getValueAsInt("value");
    DenseSet<const Record *> requestedFamilies;
    for (const Record *family : set->getValueAsListOfDefs("families"))
      requestedFamilies.insert(family);
    DenseSet<const Record *> explicitObjects;
    for (const Record *object : set->getValueAsListOfDefs("objects"))
      explicitObjects.insert(object);
    DenseSet<const Record *> exclusions;
    for (const Record *object : set->getValueAsListOfDefs("exclude"))
      exclusions.insert(object);
    for (const Record *object : objects) {
      if (!object->getValueAsString("aliasOf").empty() ||
          !object->getValueAsDef("role")->getValueAsBit("concrete") ||
          exclusions.contains(object))
        continue;
      bool selected = explicitObjects.contains(object);
      for (const Record *family : object->getValueAsListOfDefs("families"))
        selected |= requestedFamilies.contains(family);
      if (selected)
        representationMasks[static_cast<uint32_t>(
            object->getValueAsInt("value"))] |= bit;
    }
  }

  os << "enum class VPIObjectRepresentation : uint8_t {\n";
  for (const Record *representation : representations)
    os << formatv("  {0} = {1},\n", representation->getValueAsString("cppName"),
                  representation->getValueAsInt("value"));
  os << "};\n\n"
        "constexpr uint8_t vpiRepresentationMask(\n"
        "    VPIObjectRepresentation representation) {\n"
        "  return uint8_t{1} << static_cast<unsigned>(representation);\n"
        "}\n\n";

  os << "enum class VPIObjectKind : uint16_t {\n";
  for (const Record *object : objects)
    os << formatv("  {0} = {1},\n", object->getName(),
                  object->getValueAsInt("value"));
  os << "};\n\n";
  os << "struct VPIObjectKindDescriptor {\n"
        "  const char *apiName;\n"
        "  uint32_t value;\n"
        "  uint64_t families;\n"
        "  uint8_t representations;\n"
        "  VPIObjectRole role;\n"
        "  const char *aliasOf;\n"
        "};\n\n";
  os << "inline constexpr VPIObjectKindDescriptor vpiObjectKinds[] = {\n";
  for (const Record *object : objects) {
    uint64_t mask = 0;
    for (const Record *family : object->getValueAsListOfDefs("families"))
      mask |= uint64_t{1} << familyBits.lookup(family);
    StringRef aliasOf = object->getValueAsString("aliasOf");
    os << formatv(
        "  {{\"{0}\", {1}, UINT64_C({2}), {3}, VPIObjectRole::{4}, ",
        object->getValueAsString("apiName"), object->getValueAsInt("value"),
        mask,
        representationMasks.lookup(
            static_cast<uint32_t>(object->getValueAsInt("value"))),
        aliasOf.empty()
            ? object->getValueAsDef("role")->getValueAsString("cppName")
            : StringRef("Alias"));
    if (aliasOf.empty())
      os << "nullptr";
    else
      os << formatv("\"{0}\"", aliasOf);
    os << "},\n";
  }
  os << "};\n\n";

  os << "inline constexpr const VPIObjectKindDescriptor *\n"
        "findVPIObjectSelector(uint32_t value) {\n"
        "  for (const auto &kind : vpiObjectKinds)\n"
        "    if (kind.value == value && kind.aliasOf == nullptr)\n"
        "      return &kind;\n"
        "  return nullptr;\n"
        "}\n\n";

  auto callbackPhaseRecords =
      records.getAllDerivedDefinitions("VPIStatementCallbackPhase");
  SmallVector<const Record *> callbackPhases(callbackPhaseRecords.begin(),
                                             callbackPhaseRecords.end());
  llvm::sort(callbackPhases, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIStatementCallbackPhase : uint16_t {\n";
  for (const Record *phase : callbackPhases)
    os << formatv("  {0} = {1},\n", phase->getValueAsString("cppName"),
                  phase->getValueAsInt("value"));
  os << "};\n\n";

  auto callbackPolicyRecords =
      records.getAllDerivedDefinitions("VPIStatementCallbackPolicy");
  SmallVector<const Record *> callbackPolicies(callbackPolicyRecords.begin(),
                                               callbackPolicyRecords.end());
  llvm::sort(callbackPolicies, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIStatementCallbackPolicy : uint8_t {\n";
  for (const Record *policy : callbackPolicies)
    os << formatv("  {0} = {1},\n", policy->getValueAsString("cppName"),
                  policy->getValueAsInt("value"));
  os << "};\n\n";
  os << "struct VPIStatementCallbackDescriptor {\n"
        "  uint32_t objectType;\n"
        "  VPIStatementCallbackPolicy policy;\n"
        "  uint8_t phaseMask;\n"
        "  const char *clause;\n"
        "};\n\n";
  auto callbackSpecRecords =
      records.getAllDerivedDefinitions("VPIStatementCallbackSpec");
  SmallVector<const Record *> callbackSpecs(callbackSpecRecords.begin(),
                                            callbackSpecRecords.end());
  llvm::sort(callbackSpecs, [](const Record *left, const Record *right) {
    return left->getValueAsDef("object")->getValueAsInt("value") <
           right->getValueAsDef("object")->getValueAsInt("value");
  });
  os << "inline constexpr VPIStatementCallbackDescriptor "
        "vpiStatementCallbacks[] = {\n";
  for (const Record *spec : callbackSpecs) {
    const Record *policy = spec->getValueAsDef("policy");
    uint32_t phaseMask = 0;
    for (const Record *phase : policy->getValueAsListOfDefs("phases"))
      phaseMask |= uint32_t{1} << phase->getValueAsInt("value");
    os << formatv("  {{{0}, VPIStatementCallbackPolicy::{1}, {2}, "
                  "\"{3}\"",
                  spec->getValueAsDef("object")->getValueAsInt("value"),
                  policy->getValueAsString("cppName"), phaseMask,
                  spec->getValueAsString("clause"));
    os << "},\n";
  }
  os << "};\n\n"
        "inline constexpr const VPIStatementCallbackDescriptor *\n"
        "findVPIStatementCallback(uint32_t objectType) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiStatementCallbacks) /\n"
        "                sizeof(vpiStatementCallbacks[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    if (vpiStatementCallbacks[middle].objectType < objectType)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  return low != sizeof(vpiStatementCallbacks) /\n"
        "                    sizeof(vpiStatementCallbacks[0]) &&\n"
        "                 vpiStatementCallbacks[low].objectType == objectType\n"
        "             ? &vpiStatementCallbacks[low]\n"
        "             : nullptr;\n"
        "}\n\n"
        "inline constexpr bool isVPIStatementCallbackPhase(\n"
        "    uint32_t objectType, VPIStatementCallbackPhase phase) {\n"
        "  const auto *callback = findVPIStatementCallback(objectType);\n"
        "  unsigned value = static_cast<unsigned>(phase);\n"
        "  return callback && value < 8 &&\n"
        "         (callback->phaseMask & (uint8_t{1} << value)) != 0;\n"
        "}\n\n";

  os << "inline constexpr const VPIObjectKindDescriptor *\n"
        "findVPIObjectKind(uint32_t value) {\n"
        "  const auto *kind = findVPIObjectSelector(value);\n"
        "  return kind && kind->role == VPIObjectRole::Concrete ? kind\n"
        "                                                        : nullptr;\n"
        "}\n\n"
        "inline constexpr bool hasVPIObjectRepresentation(\n"
        "    uint32_t value, VPIObjectRepresentation representation) {\n"
        "  const auto *kind = findVPIObjectKind(value);\n"
        "  return kind &&\n"
        "         (kind->representations &\n"
        "          vpiRepresentationMask(representation)) != 0;\n"
        "}\n\n";

  os << "enum class VPIRelationCardinality : uint8_t {\n"
        "  One,\n  Many,\n  OneOrMany,\n};\n\n";
  os << "struct VPIRelationDescriptor {\n"
        "  const char *apiName;\n"
        "  uint32_t value;\n"
        "  VPIRelationCardinality cardinality;\n"
        "  const char *aliasOf;\n"
        "};\n\n";

  auto relationRecords = records.getAllDerivedDefinitions("VPIRelation");
  SmallVector<const Record *> relations(relationRecords.begin(),
                                        relationRecords.end());
  llvm::sort(relations, [](const Record *left, const Record *right) {
    auto leftKey = std::make_tuple(left->getValueAsInt("value"),
                                   !left->getValueAsString("aliasOf").empty(),
                                   left->getValueAsString("apiName"));
    auto rightKey = std::make_tuple(right->getValueAsInt("value"),
                                    !right->getValueAsString("aliasOf").empty(),
                                    right->getValueAsString("apiName"));
    return leftKey < rightKey;
  });
  os << "enum class VPIRelationKind : uint16_t {\n";
  for (const Record *relation : relations)
    os << formatv("  {0} = {1},\n", relation->getName(),
                  relation->getValueAsInt("value"));
  os << "};\n\n";
  os << "inline constexpr VPIRelationDescriptor vpiRelations[] = {\n";
  for (const Record *relation : relations) {
    StringRef aliasOf = relation->getValueAsString("aliasOf");
    os << formatv(
        "  {{\"{0}\", {1}, VPIRelationCardinality::{2}, ",
        relation->getValueAsString("apiName"), relation->getValueAsInt("value"),
        relation->getValueAsDef("cardinality")->getValueAsString("cppName"));
    if (aliasOf.empty())
      os << "nullptr";
    else
      os << formatv("\"{0}\"", aliasOf);
    os << "},\n";
  }
  os << "};\n\n";
  os << "inline constexpr const VPIRelationDescriptor *findVPIRelation(\n"
        "    uint32_t value) {\n"
        "  for (const auto &relation : vpiRelations)\n"
        "    if (relation.value == value && relation.aliasOf == nullptr)\n"
        "      return &relation;\n"
        "  return nullptr;\n"
        "}\n\n";

  auto setRecords = records.getAllDerivedDefinitions("VPIObjectSet");
  SmallVector<const Record *> objectSets(setRecords.begin(), setRecords.end());
  llvm::sort(objectSets, [](const Record *left, const Record *right) {
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  DenseMap<const Record *, SmallVector<uint32_t>> expandedSets;
  for (const Record *set : objectSets) {
    DenseSet<const Record *> selected;
    DenseSet<const Record *> requestedFamilies;
    for (const Record *family : set->getValueAsListOfDefs("families"))
      requestedFamilies.insert(family);
    for (const Record *object : objects) {
      if (!object->getValueAsString("aliasOf").empty() ||
          !object->getValueAsDef("role")->getValueAsBit("concrete"))
        continue;
      for (const Record *family : object->getValueAsListOfDefs("families"))
        if (requestedFamilies.contains(family)) {
          selected.insert(object);
          break;
        }
    }
    for (const Record *object : set->getValueAsListOfDefs("objects"))
      selected.insert(object);
    for (const Record *object : set->getValueAsListOfDefs("exclude"))
      selected.erase(object);
    auto &expanded = expandedSets[set];
    for (const Record *object : selected)
      expanded.push_back(static_cast<uint32_t>(object->getValueAsInt("value")));
    llvm::sort(expanded);
  }
  for (const Record *set : objectSets) {
    if (!set->getValueAsBit("iterateSources"))
      continue;
    DenseSet<uint32_t> selected;
    for (const Record *edge :
         records.getAllDerivedDefinitions("VPITraversalEdge")) {
      const Record *mode = edge->getValueAsDef("mode");
      const Record *sources = edge->getValueAsDef("sources");
      if (mode->getValueAsInt("value") != 1 ||
          sources->getValueAsBit("nullRoot"))
        continue;
      const auto &sourceKinds = expandedSets.find(sources)->second;
      selected.insert(sourceKinds.begin(), sourceKinds.end());
    }
    auto &expanded = expandedSets[set];
    llvm::append_range(expanded, selected);
    llvm::sort(expanded);
  }

  os << "enum class VPIObjectSetID : uint16_t {\n";
  for (auto [index, set] : llvm::enumerate(objectSets))
    os << formatv("  {0} = {1},\n", set->getValueAsString("cppName"), index);
  os << "};\n\n";
  os << "inline constexpr uint32_t vpiObjectSetKinds[] = {\n";
  for (const Record *set : objectSets)
    for (uint32_t value : expandedSets.lookup(set))
      os << formatv("  {0},\n", value);
  os << "};\n\n";
  os << "struct VPIObjectSetDescriptor {\n"
        "  const char *name;\n"
        "  uint32_t firstKind;\n"
        "  uint32_t kindCount;\n"
        "  bool includesNullRoot;\n"
        "};\n\n";
  os << "inline constexpr VPIObjectSetDescriptor vpiObjectSets[] = {\n";
  uint32_t firstKind = 0;
  for (const Record *set : objectSets) {
    uint32_t count = expandedSets.lookup(set).size();
    os << formatv("  {{\"{0}\", {1}, {2}, {3}",
                  set->getValueAsString("cppName"), firstKind, count,
                  set->getValueAsBit("nullRoot") ? "true" : "false");
    os << "},\n";
    firstKind += count;
  }
  os << "};\n\n";
  os << "inline constexpr bool vpiObjectSetContains(VPIObjectSetID id,\n"
        "                                          uint32_t kind) {\n"
        "  const auto &set =\n"
        "      vpiObjectSets[static_cast<uint16_t>(id)];\n"
        "  uint32_t low = set.firstKind;\n"
        "  uint32_t high = low + set.kindCount;\n"
        "  while (low != high) {\n"
        "    uint32_t middle = low + (high - low) / 2;\n"
        "    if (vpiObjectSetKinds[middle] < kind)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  return low != set.firstKind + set.kindCount &&\n"
        "         vpiObjectSetKinds[low] == kind;\n"
        "}\n\n";

  auto indexedAccessKindRecords =
      records.getAllDerivedDefinitions("VPIIndexedAccessKind");
  SmallVector<const Record *> indexedAccessKinds(
      indexedAccessKindRecords.begin(), indexedAccessKindRecords.end());
  llvm::sort(indexedAccessKinds, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIIndexedAccessKind : uint8_t {\n";
  for (const Record *kind : indexedAccessKinds)
    os << formatv("  {0} = {1},\n", kind->getValueAsString("cppName"),
                  kind->getValueAsInt("value"));
  os << "};\n\n";

  struct EmittedIndexedAccess {
    uint32_t source;
    const Record *accessKind;
    const Record *targets;
    const Record *terminalResult;
    const Record *unpackedFallback;
    const Record *packedFallback;
    bool mapSemanticType;
    const Record *relationSelector;
    StringRef clause;
  };
  SmallVector<EmittedIndexedAccess> emittedIndexedAccesses;
  for (const Record *access :
       records.getAllDerivedDefinitions("VPIIndexedAccess"))
    for (uint32_t source :
         expandedSets.lookup(access->getValueAsDef("sources")))
      emittedIndexedAccesses.push_back(
          {source, access->getValueAsDef("accessKind"),
           access->getValueAsDef("targets"),
           access->getValueAsDef("terminalResult"),
           access->getValueAsDef("unpackedFallback"),
           access->getValueAsDef("packedFallback"),
           access->getValueAsBit("mapSemanticType"),
           access->getValueAsDef("relationSelector"),
           access->getValueAsString("clause")});
  llvm::sort(emittedIndexedAccesses, [](const EmittedIndexedAccess &left,
                                        const EmittedIndexedAccess &right) {
    return left.source < right.source;
  });
  os << "struct VPIIndexedAccessDescriptor {\n"
        "  uint32_t sourceType;\n"
        "  VPIIndexedAccessKind accessKind;\n"
        "  VPIObjectSetID targets;\n"
        "  uint32_t terminalResult;\n"
        "  uint32_t unpackedFallback;\n"
        "  uint32_t packedFallback;\n"
        "  bool mapSemanticType;\n"
        "  uint32_t relationSelector;\n"
        "  const char *clause;\n"
        "};\n\n";
  os << "inline constexpr VPIIndexedAccessDescriptor "
        "vpiIndexedAccesses[] = {\n";
  for (const EmittedIndexedAccess &access : emittedIndexedAccesses) {
    os << formatv("  {{{0}, VPIIndexedAccessKind::{1}, VPIObjectSetID::{2}, "
                  "{3}, {4}, {5}, {6}, {7}, \"{8}\"",
                  access.source, access.accessKind->getValueAsString("cppName"),
                  access.targets->getValueAsString("cppName"),
                  access.terminalResult->getValueAsInt("value"),
                  access.unpackedFallback->getValueAsInt("value"),
                  access.packedFallback->getValueAsInt("value"),
                  access.mapSemanticType ? "true" : "false",
                  access.relationSelector->getValueAsInt("value"),
                  access.clause);
    os << "},\n";
  }
  os << "};\n\n"
        "inline constexpr const VPIIndexedAccessDescriptor *\n"
        "findVPIIndexedAccess(uint32_t sourceType) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiIndexedAccesses) /\n"
        "                sizeof(vpiIndexedAccesses[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    if (vpiIndexedAccesses[middle].sourceType < sourceType)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  return low != sizeof(vpiIndexedAccesses) /\n"
        "                    sizeof(vpiIndexedAccesses[0]) &&\n"
        "                 vpiIndexedAccesses[low].sourceType == sourceType\n"
        "             ? &vpiIndexedAccesses[low]\n"
        "             : nullptr;\n"
        "}\n\n"
        "inline constexpr bool indexedVPIResultAllowed(\n"
        "    const VPIIndexedAccessDescriptor &access, uint32_t resultType) {\n"
        "  return vpiObjectSetContains(access.targets, resultType);\n"
        "}\n\n";

  struct EmittedIndexedTypeResult {
    const Record *accessKind;
    uint32_t selectedTypespec;
    uint32_t result;
    StringRef clause;
  };
  SmallVector<EmittedIndexedTypeResult> emittedIndexedTypeResults;
  for (const Record *mapping :
       records.getAllDerivedDefinitions("VPIIndexedTypeResult"))
    emittedIndexedTypeResults.push_back(
        {mapping->getValueAsDef("accessKind"),
         static_cast<uint32_t>(mapping->getValueAsDef("selectedTypespec")
                                   ->getValueAsInt("value")),
         static_cast<uint32_t>(
             mapping->getValueAsDef("result")->getValueAsInt("value")),
         mapping->getValueAsString("clause")});
  llvm::sort(emittedIndexedTypeResults,
             [](const EmittedIndexedTypeResult &left,
                const EmittedIndexedTypeResult &right) {
               return std::tuple(left.accessKind->getValueAsInt("value"),
                                 left.selectedTypespec) <
                      std::tuple(right.accessKind->getValueAsInt("value"),
                                 right.selectedTypespec);
             });
  os << "struct VPIIndexedTypeResultDescriptor {\n"
        "  VPIIndexedAccessKind accessKind;\n"
        "  uint32_t selectedTypespec;\n"
        "  uint32_t resultType;\n"
        "  const char *clause;\n"
        "};\n\n"
        "inline constexpr VPIIndexedTypeResultDescriptor "
        "vpiIndexedTypeResults[] = {\n";
  for (const EmittedIndexedTypeResult &mapping : emittedIndexedTypeResults) {
    os << formatv("  {{VPIIndexedAccessKind::{0}, {1}, {2}, \"{3}\"",
                  mapping.accessKind->getValueAsString("cppName"),
                  mapping.selectedTypespec, mapping.result, mapping.clause);
    os << "},\n";
  }
  os << "};\n\n"
        "inline constexpr const VPIIndexedTypeResultDescriptor *\n"
        "findVPIIndexedTypeResult(VPIIndexedAccessKind accessKind,\n"
        "                         uint32_t selectedTypespec) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiIndexedTypeResults) /\n"
        "                sizeof(vpiIndexedTypeResults[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    const auto &entry = vpiIndexedTypeResults[middle];\n"
        "    bool less = static_cast<uint8_t>(entry.accessKind) <\n"
        "                    static_cast<uint8_t>(accessKind) ||\n"
        "                (entry.accessKind == accessKind &&\n"
        "                 entry.selectedTypespec < selectedTypespec);\n"
        "    if (less)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  if (low == sizeof(vpiIndexedTypeResults) /\n"
        "                 sizeof(vpiIndexedTypeResults[0]))\n"
        "    return nullptr;\n"
        "  const auto &entry = vpiIndexedTypeResults[low];\n"
        "  return entry.accessKind == accessKind &&\n"
        "                 entry.selectedTypespec == selectedTypespec\n"
        "             ? &entry\n"
        "             : nullptr;\n"
        "}\n\n";

  auto propertyValueKindRecords =
      records.getAllDerivedDefinitions("VPIPropertyValueKind");
  SmallVector<const Record *> propertyValueKinds(
      propertyValueKindRecords.begin(), propertyValueKindRecords.end());
  llvm::sort(propertyValueKinds, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIPropertyValueKind : uint8_t {\n";
  for (const Record *kind : propertyValueKinds)
    os << formatv("  {0} = {1},\n", kind->getValueAsString("cppName"),
                  kind->getValueAsInt("value"));
  os << "};\n\n";

  auto emitPropertyEnum = [&](StringRef recordClass, StringRef enumName) {
    SmallVector<const Record *> values(
        records.getAllDerivedDefinitions(recordClass));
    llvm::sort(values, [](const Record *left, const Record *right) {
      return left->getValueAsInt("value") < right->getValueAsInt("value");
    });
    os << "enum class " << enumName << " : uint8_t {\n";
    for (const Record *value : values)
      os << formatv("  {0} = {1},\n", value->getValueAsString("cppName"),
                    value->getValueAsInt("value"));
    os << "};\n\n";
  };
  emitPropertyEnum("VPIPropertyStability", "VPIPropertyStability");
  emitPropertyEnum("VPIPropertyProtectedAccess", "VPIPropertyProtectedAccess");
  emitPropertyEnum("VPIPropertyRealization", "VPIPropertyRealization");

  const Record *dynamicProperty = nullptr;
  for (const Record *value :
       records.getAllDerivedDefinitions("VPIPropertyStability"))
    if (value->getValueAsString("cppName") == "Dynamic")
      dynamicProperty = value;
  const Record *allowedWhenProtected = nullptr;
  for (const Record *value :
       records.getAllDerivedDefinitions("VPIPropertyProtectedAccess"))
    if (value->getValueAsString("cppName") == "Allowed")
      allowedWhenProtected = value;
  const Record *fixedImageProperty = nullptr;
  const Record *runtimeProperty = nullptr;
  const Record *definitionImageProperty = nullptr;
  for (const Record *value :
       records.getAllDerivedDefinitions("VPIPropertyRealization")) {
    if (value->getValueAsString("cppName") == "FixedImage")
      fixedImageProperty = value;
    if (value->getValueAsString("cppName") == "Runtime")
      runtimeProperty = value;
    if (value->getValueAsString("cppName") == "DefinitionImage")
      definitionImageProperty = value;
  }
  assert(dynamicProperty && allowedWhenProtected && fixedImageProperty &&
         runtimeProperty && definitionImageProperty &&
         "validated property policy enums must exist");

  struct EmittedProperty {
    uint32_t source;
    uint32_t property;
    const Record *valueKind;
    const Record *stability;
    const Record *protectedAccess;
    const Record *realization;
    bool symbolicString;
    StringRef apiName;
    StringRef clause;
  };
  SmallVector<EmittedProperty> emittedProperties;
  for (const Record *property :
       records.getAllDerivedDefinitions("VPIProperty")) {
    const auto &dynamicSources =
        expandedSets.lookup(property->getValueAsDef("dynamicSources"));
    const auto &protectedSources =
        expandedSets.lookup(property->getValueAsDef("protectedSources"));
    const auto &fixedImageSources =
        expandedSets.lookup(property->getValueAsDef("fixedImageSources"));
    const auto &runtimeSources =
        expandedSets.lookup(property->getValueAsDef("runtimeSources"));
    const auto &definitionImageSources =
        expandedSets.lookup(property->getValueAsDef("definitionImageSources"));
    for (uint32_t source :
         expandedSets.lookup(property->getValueAsDef("sources"))) {
      auto sourceSelected = [&](const auto &sources) {
        return llvm::is_contained(sources, source);
      };
      emittedProperties.push_back(
          {source, static_cast<uint32_t>(property->getValueAsInt("value")),
           property->getValueAsDef("valueKind"),
           sourceSelected(dynamicSources)
               ? dynamicProperty
               : property->getValueAsDef("stability"),
           sourceSelected(protectedSources)
               ? allowedWhenProtected
               : property->getValueAsDef("protectedAccess"),
           sourceSelected(definitionImageSources) ? definitionImageProperty
           : sourceSelected(fixedImageSources)    ? fixedImageProperty
           : sourceSelected(runtimeSources)
               ? runtimeProperty
               : property->getValueAsDef("realization"),
           property->getValueAsBit("symbolicString"),
           property->getValueAsString("apiName"),
           property->getValueAsString("clause")});
    }
    if (property->getValueAsDef("sources")->getValueAsBit("nullRoot"))
      emittedProperties.push_back(
          {0, static_cast<uint32_t>(property->getValueAsInt("value")),
           property->getValueAsDef("valueKind"),
           property->getValueAsDef("stability"),
           property->getValueAsDef("protectedAccess"),
           property->getValueAsDef("realization"),
           property->getValueAsBit("symbolicString"),
           property->getValueAsString("apiName"),
           property->getValueAsString("clause")});
  }
  llvm::sort(emittedProperties,
             [](const EmittedProperty &left, const EmittedProperty &right) {
               return std::tie(left.source, left.property) <
                      std::tie(right.source, right.property);
             });
  os << "struct VPIPropertyDescriptor {\n"
        "  uint32_t sourceType;\n"
        "  uint32_t property;\n"
        "  VPIPropertyValueKind valueKind;\n"
        "  VPIPropertyStability stability;\n"
        "  VPIPropertyProtectedAccess protectedAccess;\n"
        "  VPIPropertyRealization realization;\n"
        "  bool symbolicString;\n"
        "  const char *apiName;\n"
        "  const char *clause;\n"
        "};\n\n";
  os << "inline constexpr VPIPropertyDescriptor vpiProperties[] = {\n";
  for (const EmittedProperty &property : emittedProperties) {
    os << formatv("  {{{0}, {1}, VPIPropertyValueKind::{2}, "
                  "VPIPropertyStability::{3}, VPIPropertyProtectedAccess::{4}, "
                  "VPIPropertyRealization::{5}, {6}, \"{7}\", \"{8}\"",
                  property.source, property.property,
                  property.valueKind->getValueAsString("cppName"),
                  property.stability->getValueAsString("cppName"),
                  property.protectedAccess->getValueAsString("cppName"),
                  property.realization->getValueAsString("cppName"),
                  property.symbolicString, property.apiName, property.clause);
    os << "},\n";
  }
  os << "};\n\n"
        "inline constexpr const VPIPropertyDescriptor *findVPIProperty(\n"
        "    uint32_t sourceType, uint32_t property) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiProperties) / sizeof(vpiProperties[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    const auto &candidate = vpiProperties[middle];\n"
        "    if (candidate.sourceType < sourceType ||\n"
        "        (candidate.sourceType == sourceType &&\n"
        "         candidate.property < property))\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  if (low == sizeof(vpiProperties) / sizeof(vpiProperties[0]))\n"
        "    return nullptr;\n"
        "  const auto &candidate = vpiProperties[low];\n"
        "  return candidate.sourceType == sourceType &&\n"
        "                 candidate.property == property\n"
        "             ? &candidate\n"
        "             : nullptr;\n"
        "}\n\n";

  auto integerValueRecords =
      records.getAllDerivedDefinitions("VPIIntegerPropertyValue");
  SmallVector<const Record *> integerPropertyValues(integerValueRecords.begin(),
                                                    integerValueRecords.end());
  llvm::sort(
      integerPropertyValues, [](const Record *left, const Record *right) {
        return std::make_tuple(
                   left->getValueAsDef("property")->getValueAsInt("value"),
                   left->getValueAsInt("value")) <
               std::make_tuple(
                   right->getValueAsDef("property")->getValueAsInt("value"),
                   right->getValueAsInt("value"));
      });
  os << "struct VPIIntegerPropertyValueDescriptor {\n"
        "  uint32_t property;\n"
        "  uint32_t value;\n"
        "  const char *symbolicName;\n"
        "};\n\n"
        "inline constexpr VPIIntegerPropertyValueDescriptor "
        "vpiIntegerPropertyValues[] = {\n";
  for (const Record *value : integerPropertyValues)
    os << "  {" << value->getValueAsDef("property")->getValueAsInt("value")
       << ", " << value->getValueAsInt("value") << ", \""
       << value->getValueAsString("symbolicName") << "\"},\n";
  os << "};\n\n"
        "inline constexpr const VPIIntegerPropertyValueDescriptor *\n"
        "findVPIIntegerPropertyValue(uint32_t property, uint32_t value) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiIntegerPropertyValues) /\n"
        "                sizeof(vpiIntegerPropertyValues[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    const auto &candidate = vpiIntegerPropertyValues[middle];\n"
        "    if (candidate.property < property ||\n"
        "        (candidate.property == property && candidate.value < value))\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  if (low == sizeof(vpiIntegerPropertyValues) /\n"
        "                 sizeof(vpiIntegerPropertyValues[0]))\n"
        "    return nullptr;\n"
        "  const auto &candidate = vpiIntegerPropertyValues[low];\n"
        "  return candidate.property == property && candidate.value == value\n"
        "             ? &candidate\n"
        "             : nullptr;\n"
        "}\n\n"
        "inline constexpr bool hasVPIIntegerPropertyDomain(uint32_t property) "
        "{\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiIntegerPropertyValues) /\n"
        "                sizeof(vpiIntegerPropertyValues[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    if (vpiIntegerPropertyValues[middle].property < property)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  return low != sizeof(vpiIntegerPropertyValues) /\n"
        "                    sizeof(vpiIntegerPropertyValues[0]) &&\n"
        "         vpiIntegerPropertyValues[low].property == property;\n"
        "}\n\n";

  auto valueFormatRecords = records.getAllDerivedDefinitions("VPIValueFormat");
  SmallVector<const Record *> valueFormats(valueFormatRecords.begin(),
                                           valueFormatRecords.end());
  llvm::sort(valueFormats, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIValueFormat : uint8_t {\n";
  for (const Record *format : valueFormats)
    os << formatv("  {0} = {1},\n", format->getValueAsString("cppName"),
                  format->getValueAsInt("value"));
  os << "};\n\n";

  auto valueDefaultRecords =
      records.getAllDerivedDefinitions("VPIValueDefaultFormat");
  SmallVector<const Record *> valueDefaults(valueDefaultRecords.begin(),
                                            valueDefaultRecords.end());
  llvm::sort(valueDefaults, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIValueDefaultFormat : uint8_t {\n";
  for (const Record *format : valueDefaults)
    os << formatv("  {0} = {1},\n", format->getValueAsString("cppName"),
                  format->getValueAsInt("value"));
  os << "};\n\n";

  auto valueReadRecords =
      records.getAllDerivedDefinitions("VPIValueReadSemantics");
  SmallVector<const Record *> valueReads(valueReadRecords.begin(),
                                         valueReadRecords.end());
  llvm::sort(valueReads, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIValueReadSemantics : uint8_t {\n";
  for (const Record *read : valueReads)
    os << formatv("  {0} = {1},\n", read->getValueAsString("cppName"),
                  read->getValueAsInt("value"));
  os << "};\n\n";

  auto valueRequirementRecords =
      records.getAllDerivedDefinitions("VPIValueRequirement");
  SmallVector<const Record *> valueRequirements(valueRequirementRecords.begin(),
                                                valueRequirementRecords.end());
  llvm::sort(valueRequirements, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIValueRequirement : uint8_t {\n";
  for (const Record *requirement : valueRequirements)
    os << formatv("  {0} = {1},\n", requirement->getValueAsString("cppName"),
                  requirement->getValueAsInt("value"));
  os << "};\n\n";

  struct EmittedValuePolicy {
    uint32_t source;
    uint16_t formatMask;
    const Record *defaultFormat;
    const Record *readSemantics;
    uint8_t requirements;
    StringRef clause;
  };
  SmallVector<EmittedValuePolicy> emittedValuePolicies;
  for (const Record *policy :
       records.getAllDerivedDefinitions("VPIValuePolicy")) {
    uint16_t formatMask = 0;
    for (const Record *format : policy->getValueAsListOfDefs("formats"))
      formatMask |= uint16_t{1} << format->getValueAsInt("value");
    uint8_t requirements = 0;
    for (const Record *requirement :
         policy->getValueAsListOfDefs("requirements"))
      requirements |= static_cast<uint8_t>(requirement->getValueAsInt("value"));
    for (uint32_t source :
         expandedSets.lookup(policy->getValueAsDef("sources")))
      emittedValuePolicies.push_back(
          {source, formatMask, policy->getValueAsDef("defaultFormat"),
           policy->getValueAsDef("readSemantics"), requirements,
           policy->getValueAsString("clause")});
  }
  llvm::sort(emittedValuePolicies, [](const EmittedValuePolicy &left,
                                      const EmittedValuePolicy &right) {
    return left.source < right.source;
  });
  os << "struct VPIValuePolicyDescriptor {\n"
        "  uint32_t sourceType;\n"
        "  uint16_t formatMask;\n"
        "  VPIValueDefaultFormat defaultFormat;\n"
        "  VPIValueReadSemantics readSemantics;\n"
        "  uint8_t requirements;\n"
        "  const char *clause;\n"
        "};\n\n";
  os << "inline constexpr VPIValuePolicyDescriptor vpiValuePolicies[] = {\n";
  for (const EmittedValuePolicy &policy : emittedValuePolicies) {
    os << formatv("  {{{0}, {1}, VPIValueDefaultFormat::{2}, "
                  "VPIValueReadSemantics::{3}, {4}, \"{5}\"",
                  policy.source, policy.formatMask,
                  policy.defaultFormat->getValueAsString("cppName"),
                  policy.readSemantics->getValueAsString("cppName"),
                  policy.requirements, policy.clause);
    os << "},\n";
  }
  os << "};\n\n"
        "inline constexpr const VPIValuePolicyDescriptor *\n"
        "findVPIValuePolicy(uint32_t sourceType) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiValuePolicies) /\n"
        "                sizeof(vpiValuePolicies[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    if (vpiValuePolicies[middle].sourceType < sourceType)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  return low != sizeof(vpiValuePolicies) /\n"
        "                    sizeof(vpiValuePolicies[0]) &&\n"
        "                 vpiValuePolicies[low].sourceType == sourceType\n"
        "             ? &vpiValuePolicies[low]\n"
        "             : nullptr;\n"
        "}\n\n"
        "inline constexpr bool acceptsVPIValueFormat(\n"
        "    const VPIValuePolicyDescriptor &policy, uint32_t format) {\n"
        "  return format < 16 &&\n"
        "         (policy.formatMask & (uint16_t{1} << format)) != 0;\n"
        "}\n\n";

  auto arrayValueFormatRecords =
      records.getAllDerivedDefinitions("VPIArrayValueFormat");
  SmallVector<const Record *> arrayValueFormats(arrayValueFormatRecords.begin(),
                                                arrayValueFormatRecords.end());
  llvm::sort(arrayValueFormats, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIArrayValueFormat : uint8_t {\n";
  for (const Record *format : arrayValueFormats)
    os << formatv("  {0} = {1},\n", format->getValueAsString("cppName"),
                  format->getValueAsInt("value"));
  os << "};\n\n";

  auto arrayValuePolicyRecords =
      records.getAllDerivedDefinitions("VPIArrayValuePolicy");
  SmallVector<const Record *> arrayValuePolicies(
      arrayValuePolicyRecords.begin(), arrayValuePolicyRecords.end());
  llvm::sort(arrayValuePolicies, [](const Record *left, const Record *right) {
    return left->getValueAsDef("format")->getValueAsInt("value") <
           right->getValueAsDef("format")->getValueAsInt("value");
  });
  SmallVector<std::pair<uint32_t, uint32_t>> emittedArrayValuePolicies;
  os << "inline constexpr uint32_t vpiArrayValueElementTypes[] = {\n";
  for (const Record *policy : arrayValuePolicies) {
    auto elements = policy->getValueAsListOfDefs("elementTypes");
    llvm::sort(elements, [](const Record *left, const Record *right) {
      return left->getValueAsInt("value") < right->getValueAsInt("value");
    });
    for (const Record *element : elements) {
      os << formatv("  {0},\n", element->getValueAsInt("value"));
      emittedArrayValuePolicies.emplace_back(
          policy->getValueAsDef("format")->getValueAsInt("value"),
          element->getValueAsInt("value"));
    }
  }
  llvm::sort(emittedArrayValuePolicies);
  os << "};\n\n"
        "struct VPIArrayValuePolicyDescriptor {\n"
        "  uint32_t format;\n"
        "  uint16_t firstElementType;\n"
        "  uint16_t elementTypeCount;\n"
        "  const char *clause;\n"
        "};\n\n"
        "inline constexpr VPIArrayValuePolicyDescriptor "
        "vpiArrayValuePolicies[] = {\n";
  uint32_t firstArrayElementType = 0;
  for (const Record *policy : arrayValuePolicies) {
    auto elements = policy->getValueAsListOfDefs("elementTypes");
    os << formatv("  {{{0}, {1}, {2}, \"{3}\"",
                  policy->getValueAsDef("format")->getValueAsInt("value"),
                  firstArrayElementType, elements.size(),
                  policy->getValueAsString("clause"));
    os << "},\n";
    firstArrayElementType += elements.size();
  }
  os << "};\n\n"
        "inline constexpr const VPIArrayValuePolicyDescriptor *\n"
        "findVPIArrayValuePolicy(uint32_t format) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiArrayValuePolicies) /\n"
        "                sizeof(vpiArrayValuePolicies[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    if (vpiArrayValuePolicies[middle].format < format)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  return low != sizeof(vpiArrayValuePolicies) /\n"
        "                    sizeof(vpiArrayValuePolicies[0]) &&\n"
        "                 vpiArrayValuePolicies[low].format == format\n"
        "             ? &vpiArrayValuePolicies[low]\n"
        "             : nullptr;\n"
        "}\n\n"
        "inline constexpr bool acceptsVPIArrayValueFormat(\n"
        "    uint32_t format, uint32_t elementType) {\n"
        "  const auto *policy = findVPIArrayValuePolicy(format);\n"
        "  if (!policy)\n"
        "    return false;\n"
        "  size_t low = policy->firstElementType;\n"
        "  size_t high = low + policy->elementTypeCount;\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    if (vpiArrayValueElementTypes[middle] < elementType)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  return low != policy->firstElementType + policy->elementTypeCount "
        "&&\n"
        "         vpiArrayValueElementTypes[low] == elementType;\n"
        "}\n\n";

  auto modeRecords = records.getAllDerivedDefinitions("VPITraversalMode");
  SmallVector<const Record *> modes(modeRecords.begin(), modeRecords.end());
  llvm::sort(modes, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPITraversalMode : uint8_t {\n";
  for (const Record *mode : modes)
    os << formatv("  {0} = {1},\n", mode->getValueAsString("cppName"),
                  mode->getValueAsInt("value"));
  os << "};\n\n";

  auto orderRecords = records.getAllDerivedDefinitions("VPITraversalOrder");
  SmallVector<const Record *> orders(orderRecords.begin(), orderRecords.end());
  llvm::sort(orders, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPITraversalOrder : uint8_t {\n";
  for (const Record *order : orders)
    os << formatv("  {0} = {1},\n", order->getValueAsString("cppName"),
                  order->getValueAsInt("value"));
  os << "};\n\n";

  auto automaticRelationRecords =
      records.getAllDerivedDefinitions("VPIAutomaticRelation");
  SmallVector<const Record *> automaticRelations(
      automaticRelationRecords.begin(), automaticRelationRecords.end());
  llvm::sort(automaticRelations, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "enum class VPIAutomaticRelation : uint8_t {\n";
  for (const Record *automatic : automaticRelations)
    os << formatv("  {0} = {1},\n", automatic->getValueAsString("cppName"),
                  automatic->getValueAsInt("value"));
  os << "};\n\n";

  struct EmittedTraversalEdge {
    uint32_t source;
    uint32_t selector;
    const Record *mode;
    const Record *order;
    const Record *targets;
    bool statementContainment;
    const Record *automaticRelation;
    uint32_t inverseSelector;
    StringRef selectorName;
    StringRef clause;
  };
  SmallVector<EmittedTraversalEdge> emittedEdges;
  for (const Record *edge :
       records.getAllDerivedDefinitions("VPITraversalEdge")) {
    const Record *sources = edge->getValueAsDef("sources");
    const Record *selector = edge->getValueAsDef("selector");
    auto addEdge = [&](uint32_t source) {
      emittedEdges.push_back(
          {source, static_cast<uint32_t>(selector->getValueAsInt("value")),
           edge->getValueAsDef("mode"), edge->getValueAsDef("order"),
           edge->getValueAsDef("targets"),
           edge->getValueAsBit("statementContainment"),
           edge->getValueAsDef("automaticRelation"),
           static_cast<uint32_t>(edge->getValueAsInt("inverseSelector")),
           selector->getValueAsString("apiName"),
           edge->getValueAsString("clause")});
    };
    for (uint32_t source : expandedSets.lookup(sources))
      addEdge(source);
    if (sources->getValueAsBit("nullRoot"))
      addEdge(0);
  }
  llvm::sort(emittedEdges, [](const EmittedTraversalEdge &left,
                              const EmittedTraversalEdge &right) {
    return std::make_tuple(left.source, left.selector,
                           left.mode->getValueAsInt("value")) <
           std::make_tuple(right.source, right.selector,
                           right.mode->getValueAsInt("value"));
  });
  os << "struct VPITraversalDescriptor {\n"
        "  uint32_t sourceType;\n"
        "  uint32_t selector;\n"
        "  VPITraversalMode mode;\n"
        "  VPITraversalOrder order;\n"
        "  VPIObjectSetID targets;\n"
        "  bool statementContainment;\n"
        "  VPIAutomaticRelation automaticRelation;\n"
        "  uint32_t inverseSelector;\n"
        "  const char *selectorName;\n"
        "  const char *clause;\n"
        "};\n\n";
  os << "inline constexpr VPITraversalDescriptor vpiTraversals[] = {\n";
  for (const EmittedTraversalEdge &edge : emittedEdges) {
    os << formatv(
        "  {{{0}, {1}, VPITraversalMode::{2}, VPITraversalOrder::{3}, "
        "VPIObjectSetID::{4}, {5}, VPIAutomaticRelation::{6}, {7}, \"{8}\", "
        "\"{9}\"",
        edge.source, edge.selector, edge.mode->getValueAsString("cppName"),
        edge.order->getValueAsString("cppName"),
        edge.targets->getValueAsString("cppName"), edge.statementContainment,
        edge.automaticRelation->getValueAsString("cppName"),
        edge.inverseSelector, edge.selectorName, edge.clause);
    os << "},\n";
  }
  os << "};\n\n";
  os << "inline constexpr const VPITraversalDescriptor *findVPITraversal(\n"
        "    uint32_t sourceType, uint32_t selector, VPITraversalMode mode) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(vpiTraversals) / sizeof(vpiTraversals[0]);\n"
        "  while (low != high) {\n"
        "    size_t middle = low + (high - low) / 2;\n"
        "    const auto &edge = vpiTraversals[middle];\n"
        "    bool less = edge.sourceType < sourceType ||\n"
        "                (edge.sourceType == sourceType &&\n"
        "                 (edge.selector < selector ||\n"
        "                  (edge.selector == selector &&\n"
        "                   static_cast<uint8_t>(edge.mode) <\n"
        "                       static_cast<uint8_t>(mode))));\n"
        "    if (less)\n"
        "      low = middle + 1;\n"
        "    else\n"
        "      high = middle;\n"
        "  }\n"
        "  if (low == sizeof(vpiTraversals) / sizeof(vpiTraversals[0]))\n"
        "    return nullptr;\n"
        "  const auto &edge = vpiTraversals[low];\n"
        "  return edge.sourceType == sourceType && edge.selector == selector "
        "&&\n"
        "                 edge.mode == mode\n"
        "             ? &edge\n"
        "             : nullptr;\n"
        "}\n\n"
        "inline constexpr bool hasVPITraversal(\n"
        "    uint32_t sourceType, uint32_t selector, VPITraversalMode mode) {\n"
        "  return findVPITraversal(sourceType, selector, mode) != nullptr;\n"
        "}\n\n"
        "inline constexpr bool isVPIStatementContainment(\n"
        "    uint32_t sourceType, uint32_t selector, VPITraversalMode mode) {\n"
        "  const auto *edge = findVPITraversal(sourceType, selector, mode);\n"
        "  return edge && edge->statementContainment;\n"
        "}\n\n";

  // Emit the same model as a compact, architecture-independent wire image.
  // Only target sets are retained: source-set expansion has already produced
  // the sorted dispatch table above and need not occupy the final ELF image.
  SmallVector<const Record *> imageSets;
  DenseSet<const Record *> selectedImageSets;
  for (const EmittedTraversalEdge &edge : emittedEdges)
    if (selectedImageSets.insert(edge.targets).second)
      imageSets.push_back(edge.targets);
  for (const EmittedIndexedAccess &access : emittedIndexedAccesses)
    if (selectedImageSets.insert(access.targets).second)
      imageSets.push_back(access.targets);
  llvm::sort(imageSets, [](const Record *left, const Record *right) {
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  DenseMap<const Record *, uint16_t> imageSetIDs;
  for (auto [index, set] : llvm::enumerate(imageSets)) {
    if (index > std::numeric_limits<uint16_t>::max()) {
      PrintError(set->getLoc(), "too many VPI image target sets");
      return true;
    }
    imageSetIDs[set] = static_cast<uint16_t>(index);
  }

  auto requireU16 = [](const Record *record, uint64_t value,
                       StringRef description) {
    if (value <= std::numeric_limits<uint16_t>::max())
      return true;
    PrintError(record->getLoc(), Twine(description) + " exceeds 16 bits");
    return false;
  };
  for (const Record *family : families)
    if (!requireU16(family, family->getValueAsInt("value"),
                    "VPI family ordinal"))
      return true;
  for (const Record *object : objects)
    if (!requireU16(object, object->getValueAsInt("value"), "VPI object value"))
      return true;
  for (const Record *relation : relations)
    if (!requireU16(relation, relation->getValueAsInt("value"),
                    "VPI relation value"))
      return true;
  for (const EmittedTraversalEdge &edge : emittedEdges)
    if (edge.source > std::numeric_limits<uint16_t>::max() ||
        edge.selector > std::numeric_limits<uint16_t>::max()) {
      PrintError("expanded VPI traversal key exceeds 16 bits");
      return true;
    }
  for (const EmittedProperty &property : emittedProperties)
    if (property.source > std::numeric_limits<uint16_t>::max() ||
        property.property > std::numeric_limits<uint16_t>::max()) {
      PrintError("expanded VPI property key exceeds 16 bits");
      return true;
    }
  for (const Record *value : integerPropertyValues)
    if (value->getValueAsDef("property")->getValueAsInt("value") > UINT16_MAX) {
      PrintError(value->getLoc(),
                 "VPI integer property domain selector exceeds 16 bits");
      return true;
    }
  for (const EmittedValuePolicy &policy : emittedValuePolicies)
    if (policy.source > std::numeric_limits<uint16_t>::max()) {
      PrintError("expanded VPI value-policy key exceeds 16 bits");
      return true;
    }
  for (auto [format, element] : emittedArrayValuePolicies)
    if (format > UINT16_MAX || element > UINT16_MAX) {
      PrintError("expanded VPI array-value policy exceeds 16 bits");
      return true;
    }
  for (const EmittedIndexedAccess &access : emittedIndexedAccesses)
    if (access.source > std::numeric_limits<uint16_t>::max() ||
        access.terminalResult->getValueAsInt("value") > UINT16_MAX ||
        access.unpackedFallback->getValueAsInt("value") > UINT16_MAX ||
        access.packedFallback->getValueAsInt("value") > UINT16_MAX ||
        access.relationSelector->getValueAsInt("value") > UINT16_MAX) {
      PrintError("expanded VPI indexed-access key exceeds 16 bits");
      return true;
    }
  for (const EmittedIndexedTypeResult &mapping : emittedIndexedTypeResults)
    if (mapping.selectedTypespec > UINT16_MAX || mapping.result > UINT16_MAX) {
      PrintError("expanded VPI indexed type-result key exceeds 16 bits");
      return true;
    }

  constexpr uint32_t imageHeaderSize = 120;
  constexpr uint32_t imageObjectSize = 12;
  constexpr uint32_t imageRelationSize = 4;
  constexpr uint32_t imageSetSize = 4;
  constexpr uint32_t imageTraversalSize = 12;
  constexpr uint32_t imagePropertySize = 6;
  constexpr uint32_t imageValuePolicySize = 8;
  constexpr uint32_t imageArrayValuePolicySize = 4;
  constexpr uint32_t imageIndexedAccessSize = 12;
  constexpr uint32_t imageIndexedTypeResultSize = 8;
  constexpr uint32_t imageIntegerPropertyValueSize = 12;
  SmallVector<uint8_t> image(imageHeaderSize, 0);
  auto append16 = [&](uint16_t value) {
    image.push_back(static_cast<uint8_t>(value));
    image.push_back(static_cast<uint8_t>(value >> 8));
  };
  auto append32 = [&](uint32_t value) {
    for (unsigned byte = 0; byte != 4; ++byte)
      image.push_back(static_cast<uint8_t>(value >> (byte * 8)));
  };
  auto append64 = [&](uint64_t value) {
    for (unsigned byte = 0; byte != 8; ++byte)
      image.push_back(static_cast<uint8_t>(value >> (byte * 8)));
  };
  auto write16 = [&](uint32_t offset, uint16_t value) {
    image[offset] = static_cast<uint8_t>(value);
    image[offset + 1] = static_cast<uint8_t>(value >> 8);
  };
  auto write32 = [&](uint32_t offset, uint32_t value) {
    for (unsigned byte = 0; byte != 4; ++byte)
      image[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
  };
  auto write64 = [&](uint32_t offset, uint64_t value) {
    for (unsigned byte = 0; byte != 8; ++byte)
      image[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
  };
  const uint8_t magic[8] = {'O', 'B', 'V', 'P', 'I', 0, 0, 0};
  llvm::copy(magic, image.begin());
  write16(12, imageHeaderSize);

  size_t objectOffset = image.size();
  uint32_t objectCount = 0;
  for (const Record *object : objects) {
    if (!object->getValueAsString("aliasOf").empty())
      continue;
    uint64_t mask = 0;
    for (const Record *family : object->getValueAsListOfDefs("families"))
      mask |= uint64_t{1} << familyBits.lookup(family);
    append16(static_cast<uint16_t>(object->getValueAsInt("value")));
    image.push_back(static_cast<uint8_t>(
        object->getValueAsDef("role")->getValueAsInt("value")));
    image.push_back(representationMasks.lookup(
        static_cast<uint32_t>(object->getValueAsInt("value"))));
    append64(mask);
    ++objectCount;
  }

  size_t relationOffset = image.size();
  uint32_t relationCount = 0;
  for (const Record *relation : relations) {
    if (!relation->getValueAsString("aliasOf").empty())
      continue;
    append16(static_cast<uint16_t>(relation->getValueAsInt("value")));
    StringRef cardinality =
        relation->getValueAsDef("cardinality")->getValueAsString("cppName");
    image.push_back(cardinality == "One" ? 0 : cardinality == "Many" ? 1 : 2);
    image.push_back(0);
    ++relationCount;
  }

  size_t setOffset = image.size();
  uint32_t imageFirstKind = 0;
  for (const Record *set : imageSets) {
    ArrayRef<uint32_t> kinds = expandedSets.lookup(set);
    if (imageFirstKind > std::numeric_limits<uint16_t>::max() ||
        kinds.size() > std::numeric_limits<uint16_t>::max()) {
      PrintError(set->getLoc(), "VPI image target set exceeds 16 bits");
      return true;
    }
    append16(static_cast<uint16_t>(imageFirstKind));
    append16(static_cast<uint16_t>(kinds.size()));
    imageFirstKind += kinds.size();
  }

  size_t kindOffset = image.size();
  for (const Record *set : imageSets)
    for (uint32_t kind : expandedSets.lookup(set))
      append16(static_cast<uint16_t>(kind));

  size_t traversalOffset = image.size();
  for (const EmittedTraversalEdge &edge : emittedEdges) {
    append16(static_cast<uint16_t>(edge.source));
    append16(static_cast<uint16_t>(edge.selector));
    append16(imageSetIDs.lookup(edge.targets));
    image.push_back(static_cast<uint8_t>(edge.mode->getValueAsInt("value")));
    image.push_back(static_cast<uint8_t>(
        edge.order->getValueAsInt("value") |
        (edge.statementContainment ? 0x08 : 0) |
        (edge.automaticRelation->getValueAsInt("value") << 4)));
    append32(edge.inverseSelector);
  }

  size_t propertyOffset = image.size();
  for (const EmittedProperty &property : emittedProperties) {
    append16(static_cast<uint16_t>(property.source));
    append16(static_cast<uint16_t>(property.property));
    image.push_back(
        static_cast<uint8_t>(property.valueKind->getValueAsInt("value")));
    image.push_back(static_cast<uint8_t>(
        property.stability->getValueAsInt("value") |
        (property.protectedAccess->getValueAsInt("value") << 1) |
        (property.symbolicString ? 4 : 0) |
        (property.realization->getValueAsInt("value") << 3)));
  }

  size_t valuePolicyOffset = image.size();
  for (const EmittedValuePolicy &policy : emittedValuePolicies) {
    append16(static_cast<uint16_t>(policy.source));
    append16(policy.formatMask);
    image.push_back(
        static_cast<uint8_t>(policy.defaultFormat->getValueAsInt("value")));
    image.push_back(
        static_cast<uint8_t>(policy.readSemantics->getValueAsInt("value")));
    image.push_back(policy.requirements);
    image.push_back(0);
  }

  size_t arrayValuePolicyOffset = image.size();
  for (auto [format, element] : emittedArrayValuePolicies) {
    append16(static_cast<uint16_t>(format));
    append16(static_cast<uint16_t>(element));
  }

  size_t indexedAccessOffset = image.size();
  for (const EmittedIndexedAccess &access : emittedIndexedAccesses) {
    append16(static_cast<uint16_t>(access.source));
    append16(imageSetIDs.lookup(access.targets));
    image.push_back(
        static_cast<uint8_t>(access.accessKind->getValueAsInt("value")));
    const bool relationBacked =
        access.accessKind->getValueAsString("cppName") == "RelationElement";
    image.push_back(access.mapSemanticType ? 1 : 0);
    append16(
        static_cast<uint16_t>(access.terminalResult->getValueAsInt("value")));
    append16(
        static_cast<uint16_t>(access.unpackedFallback->getValueAsInt("value")));
    append16(static_cast<uint16_t>(
        relationBacked ? access.relationSelector->getValueAsInt("value")
                       : access.packedFallback->getValueAsInt("value")));
  }

  size_t indexedTypeResultOffset = image.size();
  for (const EmittedIndexedTypeResult &mapping : emittedIndexedTypeResults) {
    image.push_back(
        static_cast<uint8_t>(mapping.accessKind->getValueAsInt("value")));
    image.push_back(0);
    append16(static_cast<uint16_t>(mapping.selectedTypespec));
    append16(static_cast<uint16_t>(mapping.result));
    append16(0);
  }

  SmallVector<uint8_t> integerPropertyStrings;
  StringMap<uint32_t> integerPropertyStringOffsets;
  SmallVector<uint32_t> integerPropertyNames;
  integerPropertyNames.reserve(integerPropertyValues.size());
  for (const Record *value : integerPropertyValues) {
    StringRef name = value->getValueAsString("symbolicName");
    if (name.empty()) {
      integerPropertyNames.push_back(UINT32_MAX);
      continue;
    }
    auto [entry, inserted] = integerPropertyStringOffsets.try_emplace(
        name, static_cast<uint32_t>(integerPropertyStrings.size()));
    if (inserted) {
      llvm::append_range(integerPropertyStrings, name.bytes());
      integerPropertyStrings.push_back(0);
    }
    integerPropertyNames.push_back(entry->second);
  }
  size_t integerPropertyValueOffset = image.size();
  for (auto [index, value] : llvm::enumerate(integerPropertyValues)) {
    append16(static_cast<uint16_t>(
        value->getValueAsDef("property")->getValueAsInt("value")));
    append16(0);
    append32(static_cast<uint32_t>(value->getValueAsInt("value")));
    append32(integerPropertyNames[index]);
  }
  size_t integerPropertyStringOffset = image.size();
  llvm::append_range(image, integerPropertyStrings);

  if (image.size() > std::numeric_limits<uint32_t>::max()) {
    PrintError("VPI object model image exceeds 32 bits");
    return true;
  }
  uint32_t imageSize = static_cast<uint32_t>(image.size());
  write32(8, imageSize);
  write32(24, static_cast<uint32_t>(objectOffset));
  write32(28, objectCount);
  write32(32, static_cast<uint32_t>(relationOffset));
  write32(36, relationCount);
  write32(40, static_cast<uint32_t>(setOffset));
  write32(44, static_cast<uint32_t>(imageSets.size()));
  write32(48, static_cast<uint32_t>(kindOffset));
  write32(52, imageFirstKind);
  write32(56, static_cast<uint32_t>(traversalOffset));
  write32(60, static_cast<uint32_t>(emittedEdges.size()));
  write32(64, static_cast<uint32_t>(propertyOffset));
  write32(68, static_cast<uint32_t>(emittedProperties.size()));
  write32(72, static_cast<uint32_t>(valuePolicyOffset));
  write32(76, static_cast<uint32_t>(emittedValuePolicies.size()));
  write32(80, static_cast<uint32_t>(indexedAccessOffset));
  write32(84, static_cast<uint32_t>(emittedIndexedAccesses.size()));
  write32(88, static_cast<uint32_t>(indexedTypeResultOffset));
  write32(92, static_cast<uint32_t>(emittedIndexedTypeResults.size()));
  write32(96, static_cast<uint32_t>(arrayValuePolicyOffset));
  write32(100, static_cast<uint32_t>(emittedArrayValuePolicies.size()));
  write32(104, static_cast<uint32_t>(integerPropertyValueOffset));
  write32(108, static_cast<uint32_t>(integerPropertyValues.size()));
  write32(112, static_cast<uint32_t>(integerPropertyStringOffset));
  write32(116, static_cast<uint32_t>(integerPropertyStrings.size()));
  uint64_t imageChecksum = UINT64_C(14695981039346656037);
  for (uint8_t byte : image) {
    imageChecksum ^= byte;
    imageChecksum *= UINT64_C(1099511628211);
  }
  write64(16, imageChecksum);

  os << "inline constexpr uint32_t vpiObjectModelImageHeaderSize = "
     << imageHeaderSize << ";\n";
  os << "inline constexpr uint32_t vpiObjectModelImageObjectSize = "
     << imageObjectSize << ";\n";
  os << "inline constexpr uint32_t vpiObjectModelImageRelationSize = "
     << imageRelationSize << ";\n";
  os << "inline constexpr uint32_t vpiObjectModelImageSetSize = "
     << imageSetSize << ";\n";
  os << "inline constexpr uint32_t vpiObjectModelImageTraversalSize = "
     << imageTraversalSize << ";\n\n";
  os << "inline constexpr uint32_t vpiObjectModelImagePropertySize = "
     << imagePropertySize << ";\n\n";
  os << "inline constexpr uint32_t vpiObjectModelImageValuePolicySize = "
     << imageValuePolicySize << ";\n\n";
  os << "inline constexpr uint32_t vpiObjectModelImageArrayValuePolicySize = "
     << imageArrayValuePolicySize << ";\n\n";
  os << "inline constexpr uint32_t vpiObjectModelImageIndexedAccessSize = "
     << imageIndexedAccessSize << ";\n\n";
  os << "inline constexpr uint32_t vpiObjectModelImageIndexedTypeResultSize = "
     << imageIndexedTypeResultSize << ";\n\n";
  os << "inline constexpr uint32_t vpiObjectModelImageIntegerPropertyValueSize "
        "= "
     << imageIntegerPropertyValueSize << ";\n\n";
  os << "inline constexpr uint8_t "
        "vpiObjectModelImageOrderMask = 0x07;\n"
        "inline constexpr uint8_t "
        "vpiObjectModelImageAutomaticRelationMask = 0xf0;\n"
        "inline constexpr uint8_t "
        "vpiObjectModelImageAutomaticRelationShift = 4;\n"
        "inline constexpr uint8_t "
        "vpiObjectModelImageStatementContainment = 0x08;\n\n";
  os << formatv("inline constexpr uint64_t "
                "vpiObjectModelImageFingerprint = UINT64_C({0});\n\n",
                imageChecksum);
  os << "inline constexpr uint8_t vpiObjectModelImage[] = {\n";
  for (auto [index, byte] : llvm::enumerate(image)) {
    if (index % 12 == 0)
      os << "  ";
    os << formatv("0x{0:X-2}", byte);
    os << (index + 1 == image.size() ? "\n" : ", ");
    if (index % 12 == 11)
      os << "\n";
  }
  os << "};\n\n";
  os << R"cpp(inline constexpr uint16_t readVPIObjectModelImage16(
    const uint8_t *data, size_t offset) {
  return uint16_t{data[offset]} | uint16_t{data[offset + 1]} << 8;
}

inline constexpr uint32_t readVPIObjectModelImage32(const uint8_t *data,
                                                     size_t offset) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte != 4; ++byte)
    value |= uint32_t{data[offset + byte]} << (byte * 8);
  return value;
}

inline constexpr uint64_t readVPIObjectModelImage64(const uint8_t *data,
                                                     size_t offset) {
  uint64_t value = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    value |= uint64_t{data[offset + byte]} << (byte * 8);
  return value;
}

inline constexpr uint64_t checksumVPIObjectModelImage(const uint8_t *data,
                                                       size_t size) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index != size; ++index) {
    uint8_t byte = index >= 16 && index < 24 ? 0 : data[index];
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

inline constexpr bool validateVPIObjectModelImageStructure(const uint8_t *data,
                                                            size_t size) {
  if (!data || size < vpiObjectModelImageHeaderSize || data[0] != 'O' ||
      data[1] != 'B' || data[2] != 'V' || data[3] != 'P' ||
      data[4] != 'I' || data[5] != 0 || data[6] != 0 || data[7] != 0 ||
      readVPIObjectModelImage32(data, 8) != size ||
      readVPIObjectModelImage16(data, 12) !=
          vpiObjectModelImageHeaderSize ||
      readVPIObjectModelImage16(data, 14) != 0 ||
      readVPIObjectModelImage64(data, 16) == 0 ||
      readVPIObjectModelImage64(data, 16) !=
          checksumVPIObjectModelImage(data, size))
    return false;
  uint64_t objectOffset = readVPIObjectModelImage32(data, 24);
  uint64_t objectCount = readVPIObjectModelImage32(data, 28);
  uint64_t relationOffset = readVPIObjectModelImage32(data, 32);
  uint64_t relationCount = readVPIObjectModelImage32(data, 36);
  uint64_t setOffset = readVPIObjectModelImage32(data, 40);
  uint64_t setCount = readVPIObjectModelImage32(data, 44);
  uint64_t kindOffset = readVPIObjectModelImage32(data, 48);
  uint64_t kindCount = readVPIObjectModelImage32(data, 52);
  uint64_t traversalOffset = readVPIObjectModelImage32(data, 56);
  uint64_t traversalCount = readVPIObjectModelImage32(data, 60);
  uint64_t propertyOffset = readVPIObjectModelImage32(data, 64);
  uint64_t propertyCount = readVPIObjectModelImage32(data, 68);
  uint64_t valuePolicyOffset = readVPIObjectModelImage32(data, 72);
  uint64_t valuePolicyCount = readVPIObjectModelImage32(data, 76);
  uint64_t indexedAccessOffset = readVPIObjectModelImage32(data, 80);
  uint64_t indexedAccessCount = readVPIObjectModelImage32(data, 84);
  uint64_t indexedTypeResultOffset = readVPIObjectModelImage32(data, 88);
  uint64_t indexedTypeResultCount = readVPIObjectModelImage32(data, 92);
  uint64_t arrayValuePolicyOffset = readVPIObjectModelImage32(data, 96);
  uint64_t arrayValuePolicyCount = readVPIObjectModelImage32(data, 100);
  uint64_t integerPropertyValueOffset = readVPIObjectModelImage32(data, 104);
  uint64_t integerPropertyValueCount = readVPIObjectModelImage32(data, 108);
  uint64_t integerPropertyStringOffset = readVPIObjectModelImage32(data, 112);
  uint64_t integerPropertyStringSize = readVPIObjectModelImage32(data, 116);
  if (objectOffset != vpiObjectModelImageHeaderSize ||
      objectCount > (size - objectOffset) / vpiObjectModelImageObjectSize ||
      relationOffset !=
          objectOffset + objectCount * vpiObjectModelImageObjectSize ||
      relationCount >
          (size - relationOffset) / vpiObjectModelImageRelationSize ||
      setOffset != relationOffset +
                       relationCount * vpiObjectModelImageRelationSize ||
      setCount > (size - setOffset) / vpiObjectModelImageSetSize ||
      kindOffset != setOffset + setCount * vpiObjectModelImageSetSize ||
      kindCount > (size - kindOffset) / 2 ||
      traversalOffset != kindOffset + kindCount * 2 ||
      traversalCount >
          (size - traversalOffset) / vpiObjectModelImageTraversalSize ||
      propertyOffset !=
          traversalOffset +
              traversalCount * vpiObjectModelImageTraversalSize ||
      propertyCount >
          (size - propertyOffset) / vpiObjectModelImagePropertySize ||
      valuePolicyOffset !=
          propertyOffset +
              propertyCount * vpiObjectModelImagePropertySize ||
      valuePolicyCount >
          (size - valuePolicyOffset) / vpiObjectModelImageValuePolicySize ||
      arrayValuePolicyOffset !=
          valuePolicyOffset +
              valuePolicyCount * vpiObjectModelImageValuePolicySize ||
      arrayValuePolicyCount >
          (size - arrayValuePolicyOffset) /
              vpiObjectModelImageArrayValuePolicySize ||
      indexedAccessOffset !=
          arrayValuePolicyOffset +
              arrayValuePolicyCount *
                  vpiObjectModelImageArrayValuePolicySize ||
      indexedAccessCount >
          (size - indexedAccessOffset) /
              vpiObjectModelImageIndexedAccessSize ||
      indexedAccessOffset +
              indexedAccessCount * vpiObjectModelImageIndexedAccessSize !=
          indexedTypeResultOffset ||
      indexedTypeResultCount >
          (size - indexedTypeResultOffset) /
              vpiObjectModelImageIndexedTypeResultSize ||
      indexedTypeResultOffset +
              indexedTypeResultCount *
                  vpiObjectModelImageIndexedTypeResultSize !=
          integerPropertyValueOffset ||
      integerPropertyValueCount >
          (size - integerPropertyValueOffset) /
              vpiObjectModelImageIntegerPropertyValueSize ||
      integerPropertyValueOffset +
              integerPropertyValueCount *
                  vpiObjectModelImageIntegerPropertyValueSize !=
          integerPropertyStringOffset ||
      integerPropertyStringSize > size - integerPropertyStringOffset ||
      integerPropertyStringOffset + integerPropertyStringSize != size)
    return false;

  uint16_t previousValue = 0;
  for (uint32_t index = 0; index != objectCount; ++index) {
    const uint8_t *record =
        data + objectOffset + index * vpiObjectModelImageObjectSize;
    uint16_t value = readVPIObjectModelImage16(record, 0);
    uint64_t families = readVPIObjectModelImage64(record, 4);
    const auto *descriptor = findVPIObjectSelector(value);
    bool concrete =
        record[2] == static_cast<uint8_t>(VPIObjectRole::Concrete);
    constexpr uint8_t representationMask =
        (uint8_t{1} << (static_cast<uint8_t>(VPIObjectRepresentation::Runtime) +
                       1)) -
        1;
    if ((index != 0 && value <= previousValue) || !descriptor ||
        record[2] != static_cast<uint8_t>(descriptor->role) ||
        record[3] != descriptor->representations ||
        (record[3] & ~representationMask) != 0 ||
        (concrete ? record[3] == 0 : record[3] != 0) ||
        families != descriptor->families || families == 0)
      return false;
    previousValue = value;
  }
  previousValue = 0;
  for (uint32_t index = 0; index != relationCount; ++index) {
    const uint8_t *record =
        data + relationOffset + index * vpiObjectModelImageRelationSize;
    uint16_t value = readVPIObjectModelImage16(record, 0);
    if ((index != 0 && value <= previousValue) || record[2] > 2 ||
        record[3] != 0)
      return false;
    previousValue = value;
  }
  uint32_t expectedFirst = 0;
  for (uint32_t index = 0; index != setCount; ++index) {
    const uint8_t *record =
        data + setOffset + index * vpiObjectModelImageSetSize;
    uint16_t first = readVPIObjectModelImage16(record, 0);
    uint16_t count = readVPIObjectModelImage16(record, 2);
    if (first != expectedFirst || first > kindCount || count == 0 ||
        count > kindCount - first)
      return false;
    uint16_t previousKind = 0;
    for (uint32_t kindIndex = 0; kindIndex != count; ++kindIndex) {
      uint16_t kind = readVPIObjectModelImage16(
          data, kindOffset + (uint32_t{first} + kindIndex) * 2);
      if (kindIndex != 0 && kind <= previousKind)
        return false;
      previousKind = kind;
    }
    expectedFirst += count;
  }
  if (expectedFirst != kindCount)
    return false;
  auto objectHasFamily = [&](uint16_t value, uint64_t family) {
    for (uint32_t objectIndex = 0; objectIndex != objectCount; ++objectIndex) {
      const uint8_t *object =
          data + objectOffset +
          objectIndex * vpiObjectModelImageObjectSize;
      uint16_t objectValue = readVPIObjectModelImage16(object, 0);
      if (objectValue == value)
        return (readVPIObjectModelImage64(object, 4) & family) != 0;
      if (objectValue > value)
        break;
    }
    return false;
  };
  auto hasRelationIndexedAccess = [&](uint16_t array,
                                      uint16_t member) constexpr {
    for (uint32_t accessIndex = 0; accessIndex != indexedAccessCount;
         ++accessIndex) {
      const uint8_t *access =
          data + indexedAccessOffset +
          accessIndex * vpiObjectModelImageIndexedAccessSize;
      if (readVPIObjectModelImage16(access, 0) == array &&
          access[4] == static_cast<uint8_t>(
                           VPIIndexedAccessKind::RelationElement) &&
          (readVPIObjectModelImage16(access, 6) == member ||
           readVPIObjectModelImage16(access, 8) == member))
        return true;
    }
    return false;
  };
  uint16_t previousSource = 0;
  uint16_t previousSelector = 0;
  uint8_t previousMode = 0;
  for (uint32_t index = 0; index != traversalCount; ++index) {
    const uint8_t *record =
        data + traversalOffset + index * vpiObjectModelImageTraversalSize;
    uint16_t source = readVPIObjectModelImage16(record, 0);
    uint16_t selector = readVPIObjectModelImage16(record, 2);
    uint16_t targets = readVPIObjectModelImage16(record, 4);
    uint8_t mode = record[6];
    uint8_t flagsAndOrder = record[7];
    uint8_t order = flagsAndOrder & vpiObjectModelImageOrderMask;
    uint8_t automaticRelation =
        (flagsAndOrder & vpiObjectModelImageAutomaticRelationMask) >>
        vpiObjectModelImageAutomaticRelationShift;
    uint32_t inverseSelector = readVPIObjectModelImage32(record, 8);
    const VPITraversalDescriptor *descriptor = findVPITraversal(
        source, selector, static_cast<VPITraversalMode>(mode));
    bool automaticTargetsValid = true;
    if (automaticRelation ==
        static_cast<uint8_t>(VPIAutomaticRelation::IndexedContainer)) {
      if (targets >= setCount) {
        automaticTargetsValid = false;
      } else {
        const uint8_t *set =
            data + setOffset + targets * vpiObjectModelImageSetSize;
        uint16_t first = readVPIObjectModelImage16(set, 0);
        uint16_t count = readVPIObjectModelImage16(set, 2);
        automaticTargetsValid = false;
        for (uint32_t targetIndex = 0; targetIndex != count; ++targetIndex) {
          uint16_t kind = readVPIObjectModelImage16(
              data, kindOffset + (uint32_t{first} + targetIndex) * 2);
          if (objectHasFamily(kind, vpiFamilyMask(VPIObjectFamily::Array)) &&
              hasRelationIndexedAccess(kind, source)) {
            automaticTargetsValid = true;
            break;
          }
        }
      }
    }
    bool ordered = index == 0 || previousSource < source ||
                   (previousSource == source &&
                    (previousSelector < selector ||
                     (previousSelector == selector && previousMode < mode)));
    if (!ordered || targets >= setCount || mode > 1 || order > 4 ||
        !descriptor || descriptor->inverseSelector != inverseSelector ||
        !automaticTargetsValid ||
        automaticRelation > static_cast<uint8_t>(
            VPIAutomaticRelation::DefinitionMemberInstanceRelation) ||
        (automaticRelation !=
             static_cast<uint8_t>(VPIAutomaticRelation::None) &&
         ((flagsAndOrder & vpiObjectModelImageStatementContainment) != 0 ||
          mode != ((automaticRelation == static_cast<uint8_t>(
                                             VPIAutomaticRelation::DirectChild) ||
                    automaticRelation == static_cast<uint8_t>(
                                             VPIAutomaticRelation::DefinitionMember) ||
                    automaticRelation == static_cast<uint8_t>(
                        VPIAutomaticRelation::DefinitionMemberInstanceRelation))
                       ? 1
                       : 0))) ||
        (mode == 0 && order != 0))
      return false;
    previousSource = source;
    previousSelector = selector;
    previousMode = mode;
  }
  previousSource = 0;
  uint16_t previousProperty = 0;
  for (uint32_t index = 0; index != propertyCount; ++index) {
    const uint8_t *record =
        data + propertyOffset + index * vpiObjectModelImagePropertySize;
    uint16_t source = readVPIObjectModelImage16(record, 0);
    uint16_t property = readVPIObjectModelImage16(record, 2);
    const auto *descriptor = findVPIProperty(source, property);
    bool ordered = index == 0 || previousSource < source ||
                   (previousSource == source && previousProperty < property);
    if (!ordered || property == 0 || !descriptor ||
        record[4] > static_cast<uint8_t>(
                                    VPIPropertyValueKind::String) ||
        ((record[5] >> 3) & 7) >
            static_cast<uint8_t>(VPIPropertyRealization::DefinitionImage) ||
        (record[5] & ~UINT8_C(63)) != 0)
      return false;
    if (record[4] != static_cast<uint8_t>(descriptor->valueKind) ||
        (record[5] & 1) != static_cast<uint8_t>(descriptor->stability) ||
        ((record[5] >> 1) & 1) !=
            static_cast<uint8_t>(descriptor->protectedAccess) ||
        ((record[5] >> 3) & 7) !=
            static_cast<uint8_t>(descriptor->realization) ||
        ((record[5] & 4) != 0) != descriptor->symbolicString)
      return false;
    previousSource = source;
    previousProperty = property;
  }
  previousSource = 0;
  constexpr uint16_t validValueFormats = 0x1ffe;
  constexpr uint8_t validValueRequirements = 0x0f;
  for (uint32_t index = 0; index != valuePolicyCount; ++index) {
    const uint8_t *record =
        data + valuePolicyOffset +
        index * vpiObjectModelImageValuePolicySize;
    uint16_t source = readVPIObjectModelImage16(record, 0);
    uint16_t formats = readVPIObjectModelImage16(record, 2);
    bool concrete = false;
    for (uint32_t objectIndex = 0; objectIndex != objectCount;
         ++objectIndex) {
      const uint8_t *object =
          data + objectOffset +
          objectIndex * vpiObjectModelImageObjectSize;
      if (readVPIObjectModelImage16(object, 0) == source) {
        concrete = object[2] == static_cast<uint8_t>(VPIObjectRole::Concrete);
        break;
      }
    }
    if ((index != 0 && source <= previousSource) || !concrete ||
        formats == 0 || (formats & ~validValueFormats) != 0 ||
        record[4] > static_cast<uint8_t>(VPIValueDefaultFormat::Time) ||
        record[5] > static_cast<uint8_t>(VPIValueReadSemantics::Evaluate) ||
        (record[6] & ~validValueRequirements) != 0 || record[7] != 0)
      return false;
    previousSource = source;
  }
  previousSource = 0;
  auto concreteObject = [&](uint16_t value) constexpr {
    for (uint32_t objectIndex = 0; objectIndex != objectCount;
         ++objectIndex) {
      const uint8_t *object =
          data + objectOffset +
          objectIndex * vpiObjectModelImageObjectSize;
      if (readVPIObjectModelImage16(object, 0) == value)
        return object[2] == static_cast<uint8_t>(VPIObjectRole::Concrete);
    }
    return false;
  };
  uint16_t previousArrayFormat = 0;
  uint16_t previousArrayElement = 0;
  for (uint32_t index = 0; index != arrayValuePolicyCount; ++index) {
    const uint8_t *record =
        data + arrayValuePolicyOffset +
        index * vpiObjectModelImageArrayValuePolicySize;
    uint16_t format = readVPIObjectModelImage16(record, 0);
    uint16_t element = readVPIObjectModelImage16(record, 2);
    bool validFormat = format == static_cast<uint8_t>(VPIArrayValueFormat::Int) ||
                       format == static_cast<uint8_t>(VPIArrayValueFormat::Real) ||
                       format == static_cast<uint8_t>(VPIArrayValueFormat::Vector) ||
                       format == static_cast<uint8_t>(VPIArrayValueFormat::Time) ||
                       format == static_cast<uint8_t>(VPIArrayValueFormat::ShortInt) ||
                       format == static_cast<uint8_t>(VPIArrayValueFormat::LongInt) ||
                       format == static_cast<uint8_t>(VPIArrayValueFormat::ShortReal) ||
                       format == static_cast<uint8_t>(VPIArrayValueFormat::RawTwoState) ||
                       format == static_cast<uint8_t>(VPIArrayValueFormat::RawFourState);
    bool ordered = index == 0 || previousArrayFormat < format ||
                   (previousArrayFormat == format &&
                    previousArrayElement < element);
    if (!validFormat || !ordered || !concreteObject(element) ||
        !acceptsVPIArrayValueFormat(format, element))
      return false;
    previousArrayFormat = format;
    previousArrayElement = element;
  }
  auto targetContains = [&](uint16_t setID, uint16_t value) constexpr {
    if (setID >= setCount)
      return false;
    const uint8_t *set =
        data + setOffset + setID * vpiObjectModelImageSetSize;
    uint16_t first = readVPIObjectModelImage16(set, 0);
    uint16_t count = readVPIObjectModelImage16(set, 2);
    for (uint32_t targetIndex = 0; targetIndex != count; ++targetIndex)
      if (readVPIObjectModelImage16(
              data, kindOffset + (uint32_t{first} + targetIndex) * 2) == value)
        return true;
    return false;
  };
  for (uint32_t index = 0; index != indexedAccessCount; ++index) {
    const uint8_t *record =
        data + indexedAccessOffset +
        index * vpiObjectModelImageIndexedAccessSize;
    uint16_t source = readVPIObjectModelImage16(record, 0);
    uint16_t targets = readVPIObjectModelImage16(record, 2);
    uint16_t terminalResult = readVPIObjectModelImage16(record, 6);
    uint16_t unpackedFallback = readVPIObjectModelImage16(record, 8);
    uint16_t packedFallback = readVPIObjectModelImage16(record, 10);
    auto accessKind = static_cast<VPIIndexedAccessKind>(record[4]);
    bool relationBacked =
        accessKind == VPIIndexedAccessKind::RelationElement;
    bool validPayload = false;
    if (relationBacked) {
      for (uint32_t traversalIndex = 0; traversalIndex != traversalCount;
           ++traversalIndex) {
        const uint8_t *traversal =
            data + traversalOffset +
            traversalIndex * vpiObjectModelImageTraversalSize;
        if (readVPIObjectModelImage16(traversal, 0) == source &&
            readVPIObjectModelImage16(traversal, 2) == packedFallback &&
            targetContains(readVPIObjectModelImage16(traversal, 4),
                           terminalResult) &&
            traversal[6] == 1 &&
            (traversal[7] & vpiObjectModelImageOrderMask) ==
                static_cast<uint8_t>(VPITraversalOrder::Index)) {
          validPayload = true;
          break;
        }
      }
      validPayload &= record[5] == 0 && unpackedFallback == source &&
                      concreteObject(unpackedFallback) &&
                      targetContains(targets, unpackedFallback);
    } else {
      validPayload = record[5] <= 1 && concreteObject(unpackedFallback) &&
                     concreteObject(packedFallback) &&
                     targetContains(targets, unpackedFallback) &&
                     targetContains(targets, packedFallback);
    }
    if ((index != 0 && source <= previousSource) || !concreteObject(source) ||
        targets >= setCount ||
        record[4] >
            static_cast<uint8_t>(VPIIndexedAccessKind::RelationElement) ||
        !validPayload || !concreteObject(terminalResult) ||
        !targetContains(targets, terminalResult) ||
        (relationBacked && record[5] != 0))
      return false;
    previousSource = source;
  }
  uint8_t previousAccessKind = 0;
  uint16_t previousTypespec = 0;
  for (uint32_t index = 0; index != indexedTypeResultCount; ++index) {
    const uint8_t *record =
        data + indexedTypeResultOffset +
        index * vpiObjectModelImageIndexedTypeResultSize;
    uint8_t accessKind = record[0];
    uint16_t typespec = readVPIObjectModelImage16(record, 2);
    uint16_t result = readVPIObjectModelImage16(record, 4);
    bool ordered = index == 0 || previousAccessKind < accessKind ||
                   (previousAccessKind == accessKind &&
                    previousTypespec < typespec);
    bool hasMappedAccess = false;
    bool allowed = true;
    for (uint32_t accessIndex = 0; accessIndex != indexedAccessCount;
         ++accessIndex) {
      const uint8_t *access =
          data + indexedAccessOffset +
          accessIndex * vpiObjectModelImageIndexedAccessSize;
      if (access[4] != accessKind || access[5] == 0)
        continue;
      hasMappedAccess = true;
      if (!targetContains(readVPIObjectModelImage16(access, 2), result))
        allowed = false;
    }
    bool typespecObject = false;
    for (uint32_t objectIndex = 0; objectIndex != objectCount;
         ++objectIndex) {
      const uint8_t *object =
          data + objectOffset +
          objectIndex * vpiObjectModelImageObjectSize;
      if (readVPIObjectModelImage16(object, 0) == typespec) {
        typespecObject =
            object[2] == static_cast<uint8_t>(VPIObjectRole::Concrete) &&
            (readVPIObjectModelImage64(object, 4) &
             vpiFamilyMask(VPIObjectFamily::Typespec)) != 0;
        break;
      }
    }
    if (!ordered || accessKind > static_cast<uint8_t>(
                                      VPIIndexedAccessKind::VariableElement) ||
        record[1] != 0 || readVPIObjectModelImage16(record, 6) != 0 ||
        !typespecObject || !concreteObject(result) || !hasMappedAccess ||
        !allowed)
      return false;
    previousAccessKind = accessKind;
    previousTypespec = typespec;
  }
  uint16_t previousIntegerProperty = 0;
  uint32_t previousIntegerValue = 0;
  for (uint32_t index = 0; index != integerPropertyValueCount; ++index) {
    const uint8_t *record =
        data + integerPropertyValueOffset +
        index * vpiObjectModelImageIntegerPropertyValueSize;
    uint16_t property = readVPIObjectModelImage16(record, 0);
    uint32_t value = readVPIObjectModelImage32(record, 4);
    uint32_t nameOffset = readVPIObjectModelImage32(record, 8);
    bool ordered = index == 0 || previousIntegerProperty < property ||
                   (previousIntegerProperty == property &&
                    previousIntegerValue < value);
    const auto *expected = findVPIIntegerPropertyValue(property, value);
    if (!ordered || readVPIObjectModelImage16(record, 2) != 0 || !expected)
      return false;
    if (expected->symbolicName[0] == '\0') {
      if (nameOffset != UINT32_MAX)
        return false;
    } else {
      if (nameOffset >= integerPropertyStringSize ||
          (nameOffset != 0 &&
           data[integerPropertyStringOffset + nameOffset - 1] != 0))
        return false;
      size_t character = 0;
      for (;; ++character) {
        if (nameOffset + character >= integerPropertyStringSize)
          return false;
        char actual = static_cast<char>(
            data[integerPropertyStringOffset + nameOffset + character]);
        char wanted = expected->symbolicName[character];
        if (actual != wanted)
          return false;
        if (wanted == '\0')
          break;
      }
    }
    previousIntegerProperty = property;
    previousIntegerValue = value;
  }
  for (uint32_t string = 0; string != integerPropertyStringSize;) {
    bool referenced = false;
    for (uint32_t index = 0; index != integerPropertyValueCount; ++index) {
      const uint8_t *record =
          data + integerPropertyValueOffset +
          index * vpiObjectModelImageIntegerPropertyValueSize;
      if (readVPIObjectModelImage32(record, 8) == string) {
        referenced = true;
        break;
      }
    }
    if (!referenced)
      return false;
    do {
      if (string >= integerPropertyStringSize)
        return false;
    } while (data[integerPropertyStringOffset + string++] != 0);
  }
  return true;
}

inline constexpr bool validateVPIObjectModelImage(const uint8_t *data,
                                                   size_t size) {
  return validateVPIObjectModelImageStructure(data, size) &&
         readVPIObjectModelImage64(data, 16) ==
             vpiObjectModelImageFingerprint;
}

inline constexpr bool findVPIObjectModelImageRepresentations(
    const uint8_t *data, uint32_t objectType, uint8_t &representations) {
  if (objectType > UINT16_MAX)
    return false;
  uint32_t offset = readVPIObjectModelImage32(data, 24);
  uint32_t low = 0;
  uint32_t high = readVPIObjectModelImage32(data, 28);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        data + offset + middle * vpiObjectModelImageObjectSize;
    if (readVPIObjectModelImage16(record, 0) < objectType)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == readVPIObjectModelImage32(data, 28))
    return false;
  const uint8_t *record =
      data + offset + low * vpiObjectModelImageObjectSize;
  if (readVPIObjectModelImage16(record, 0) != objectType)
    return false;
  representations = record[3];
  return true;
}

struct VPIObjectModelImageTraversal {
  uint16_t sourceType;
  uint16_t selector;
  uint16_t targets;
  VPITraversalMode mode;
  VPITraversalOrder order;
  bool statementContainment;
  VPIAutomaticRelation automaticRelation;
  uint32_t inverseSelector;
};

inline constexpr bool findVPIObjectModelImageTraversal(
    const uint8_t *data, uint32_t sourceType, uint32_t selector,
    VPITraversalMode mode, VPIObjectModelImageTraversal &result) {
  if (sourceType > UINT16_MAX || selector > UINT16_MAX)
    return false;
  uint32_t offset = readVPIObjectModelImage32(data, 56);
  uint32_t low = 0;
  uint32_t high = readVPIObjectModelImage32(data, 60);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        data + offset + middle * vpiObjectModelImageTraversalSize;
    uint16_t recordSource = readVPIObjectModelImage16(record, 0);
    uint16_t recordSelector = readVPIObjectModelImage16(record, 2);
    auto recordMode = static_cast<VPITraversalMode>(record[6]);
    bool less = recordSource < sourceType ||
                (recordSource == sourceType &&
                 (recordSelector < selector ||
                  (recordSelector == selector &&
                   static_cast<uint8_t>(recordMode) <
                       static_cast<uint8_t>(mode))));
    if (less)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == readVPIObjectModelImage32(data, 60))
    return false;
  const uint8_t *record =
      data + offset + low * vpiObjectModelImageTraversalSize;
  result = {readVPIObjectModelImage16(record, 0),
            readVPIObjectModelImage16(record, 2),
            readVPIObjectModelImage16(record, 4),
            static_cast<VPITraversalMode>(record[6]),
            static_cast<VPITraversalOrder>(
                record[7] & vpiObjectModelImageOrderMask),
            (record[7] & vpiObjectModelImageStatementContainment) != 0,
            static_cast<VPIAutomaticRelation>(
                (record[7] & vpiObjectModelImageAutomaticRelationMask) >>
                vpiObjectModelImageAutomaticRelationShift),
            readVPIObjectModelImage32(record, 8)};
  return result.sourceType == sourceType && result.selector == selector &&
         result.mode == mode;
}

struct VPIObjectModelImageProperty {
  uint16_t sourceType;
  uint16_t property;
  VPIPropertyValueKind valueKind;
  VPIPropertyStability stability;
  VPIPropertyProtectedAccess protectedAccess;
  VPIPropertyRealization realization;
  bool symbolicString;
};

inline constexpr bool findVPIObjectModelImageProperty(
    const uint8_t *data, uint32_t sourceType, uint32_t property,
    VPIObjectModelImageProperty &result) {
  if (sourceType > UINT16_MAX || property > UINT16_MAX)
    return false;
  uint32_t offset = readVPIObjectModelImage32(data, 64);
  uint32_t low = 0;
  uint32_t high = readVPIObjectModelImage32(data, 68);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        data + offset + middle * vpiObjectModelImagePropertySize;
    uint16_t recordSource = readVPIObjectModelImage16(record, 0);
    uint16_t recordProperty = readVPIObjectModelImage16(record, 2);
    if (recordSource < sourceType ||
        (recordSource == sourceType && recordProperty < property))
      low = middle + 1;
    else
      high = middle;
  }
  if (low == readVPIObjectModelImage32(data, 68))
    return false;
  const uint8_t *record =
      data + offset + low * vpiObjectModelImagePropertySize;
  result = {readVPIObjectModelImage16(record, 0),
            readVPIObjectModelImage16(record, 2),
            static_cast<VPIPropertyValueKind>(record[4]),
            static_cast<VPIPropertyStability>(record[5] & 1),
            static_cast<VPIPropertyProtectedAccess>((record[5] >> 1) & 1),
            static_cast<VPIPropertyRealization>((record[5] >> 3) & 7),
            (record[5] & 4) != 0};
  return result.sourceType == sourceType && result.property == property;
}

struct VPIObjectModelImageIntegerPropertyValue {
  uint16_t property;
  uint32_t value;
  const uint8_t *symbolicName;
};

inline constexpr bool findVPIObjectModelImageIntegerPropertyValue(
    const uint8_t *data, uint32_t property, uint32_t value,
    VPIObjectModelImageIntegerPropertyValue &result) {
  if (property > UINT16_MAX)
    return false;
  uint32_t offset = readVPIObjectModelImage32(data, 104);
  uint32_t low = 0;
  uint32_t high = readVPIObjectModelImage32(data, 108);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        data + offset + middle * vpiObjectModelImageIntegerPropertyValueSize;
    uint16_t recordProperty = readVPIObjectModelImage16(record, 0);
    uint32_t recordValue = readVPIObjectModelImage32(record, 4);
    if (recordProperty < property ||
        (recordProperty == property && recordValue < value))
      low = middle + 1;
    else
      high = middle;
  }
  if (low == readVPIObjectModelImage32(data, 108))
    return false;
  const uint8_t *record =
      data + offset + low * vpiObjectModelImageIntegerPropertyValueSize;
  uint32_t nameOffset = readVPIObjectModelImage32(record, 8);
  result = {readVPIObjectModelImage16(record, 0),
            readVPIObjectModelImage32(record, 4),
            nameOffset == UINT32_MAX
                ? nullptr
                : data + readVPIObjectModelImage32(data, 112) + nameOffset};
  return result.property == property && result.value == value;
}

struct VPIObjectModelImageValuePolicy {
  uint16_t sourceType;
  uint16_t formatMask;
  VPIValueDefaultFormat defaultFormat;
  VPIValueReadSemantics readSemantics;
  uint8_t requirements;
};

inline constexpr bool findVPIObjectModelImageValuePolicy(
    const uint8_t *data, uint32_t sourceType,
    VPIObjectModelImageValuePolicy &result) {
  if (sourceType > UINT16_MAX)
    return false;
  uint32_t offset = readVPIObjectModelImage32(data, 72);
  uint32_t low = 0;
  uint32_t high = readVPIObjectModelImage32(data, 76);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        data + offset + middle * vpiObjectModelImageValuePolicySize;
    if (readVPIObjectModelImage16(record, 0) < sourceType)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == readVPIObjectModelImage32(data, 76))
    return false;
  const uint8_t *record =
      data + offset + low * vpiObjectModelImageValuePolicySize;
  result = {readVPIObjectModelImage16(record, 0),
            readVPIObjectModelImage16(record, 2),
            static_cast<VPIValueDefaultFormat>(record[4]),
            static_cast<VPIValueReadSemantics>(record[5]), record[6]};
  return result.sourceType == sourceType;
}

struct VPIObjectModelImageArrayValuePolicy {
  uint16_t format;
  uint16_t elementType;
};

inline constexpr bool findVPIObjectModelImageArrayValuePolicy(
    const uint8_t *data, uint32_t format, uint32_t elementType,
    VPIObjectModelImageArrayValuePolicy &result) {
  if (format > UINT16_MAX || elementType > UINT16_MAX)
    return false;
  uint32_t offset = readVPIObjectModelImage32(data, 96);
  uint32_t low = 0;
  uint32_t high = readVPIObjectModelImage32(data, 100);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        data + offset + middle * vpiObjectModelImageArrayValuePolicySize;
    uint16_t recordFormat = readVPIObjectModelImage16(record, 0);
    uint16_t recordElement = readVPIObjectModelImage16(record, 2);
    if (recordFormat < format ||
        (recordFormat == format && recordElement < elementType))
      low = middle + 1;
    else
      high = middle;
  }
  if (low == readVPIObjectModelImage32(data, 100))
    return false;
  const uint8_t *record =
      data + offset + low * vpiObjectModelImageArrayValuePolicySize;
  result = {readVPIObjectModelImage16(record, 0),
            readVPIObjectModelImage16(record, 2)};
  return result.format == format && result.elementType == elementType;
}

struct VPIObjectModelImageIndexedAccess {
  uint16_t sourceType;
  uint16_t targets;
  VPIIndexedAccessKind accessKind;
  bool mapSemanticType;
  uint16_t terminalResult;
  uint16_t unpackedFallback;
  uint16_t packedFallback;
  uint16_t relationSelector;
};

inline constexpr bool findVPIObjectModelImageIndexedAccess(
    const uint8_t *data, uint32_t sourceType,
    VPIObjectModelImageIndexedAccess &result) {
  if (sourceType > UINT16_MAX)
    return false;
  uint32_t offset = readVPIObjectModelImage32(data, 80);
  uint32_t low = 0;
  uint32_t high = readVPIObjectModelImage32(data, 84);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        data + offset + middle * vpiObjectModelImageIndexedAccessSize;
    if (readVPIObjectModelImage16(record, 0) < sourceType)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == readVPIObjectModelImage32(data, 84))
    return false;
  const uint8_t *record =
      data + offset + low * vpiObjectModelImageIndexedAccessSize;
  auto accessKind = static_cast<VPIIndexedAccessKind>(record[4]);
  bool relationBacked =
      accessKind == VPIIndexedAccessKind::RelationElement;
  result = {readVPIObjectModelImage16(record, 0),
            readVPIObjectModelImage16(record, 2), accessKind,
            !relationBacked && record[5] != 0,
            readVPIObjectModelImage16(record, 6),
            readVPIObjectModelImage16(record, 8),
            relationBacked ? readVPIObjectModelImage16(record, 6)
                           : readVPIObjectModelImage16(record, 10),
            relationBacked ? readVPIObjectModelImage16(record, 10)
                           : uint16_t{0}};
  return result.sourceType == sourceType;
}

struct VPIObjectModelImageIndexedTypeResult {
  VPIIndexedAccessKind accessKind;
  uint16_t selectedTypespec;
  uint16_t resultType;
};

inline constexpr bool findVPIObjectModelImageIndexedTypeResult(
    const uint8_t *data, VPIIndexedAccessKind accessKind,
    uint32_t selectedTypespec,
    VPIObjectModelImageIndexedTypeResult &result) {
  if (selectedTypespec > UINT16_MAX)
    return false;
  uint32_t offset = readVPIObjectModelImage32(data, 88);
  uint32_t low = 0;
  uint32_t high = readVPIObjectModelImage32(data, 92);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        data + offset + middle * vpiObjectModelImageIndexedTypeResultSize;
    auto recordKind = static_cast<VPIIndexedAccessKind>(record[0]);
    uint16_t recordTypespec = readVPIObjectModelImage16(record, 2);
    bool less = static_cast<uint8_t>(recordKind) <
                    static_cast<uint8_t>(accessKind) ||
                (recordKind == accessKind &&
                 recordTypespec < selectedTypespec);
    if (less)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == readVPIObjectModelImage32(data, 92))
    return false;
  const uint8_t *record =
      data + offset + low * vpiObjectModelImageIndexedTypeResultSize;
  result = {static_cast<VPIIndexedAccessKind>(record[0]),
            readVPIObjectModelImage16(record, 2),
            readVPIObjectModelImage16(record, 4)};
  return result.accessKind == accessKind &&
         result.selectedTypespec == selectedTypespec;
}

inline constexpr bool vpiObjectModelImageTargetContains(
    const uint8_t *data, uint16_t setID, uint32_t kind) {
  uint32_t setCount = readVPIObjectModelImage32(data, 44);
  if (setID >= setCount || kind > UINT16_MAX)
    return false;
  uint32_t setOffset = readVPIObjectModelImage32(data, 40) +
                       setID * vpiObjectModelImageSetSize;
  uint16_t first = readVPIObjectModelImage16(data, setOffset);
  uint16_t count = readVPIObjectModelImage16(data, setOffset + 2);
  uint32_t kindOffset = readVPIObjectModelImage32(data, 48);
  uint32_t low = 0;
  uint32_t high = count;
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    uint16_t value = readVPIObjectModelImage16(
        data, kindOffset + (uint32_t{first} + middle) * 2);
    if (value < kind)
      low = middle + 1;
    else
      high = middle;
  }
  return low != count &&
         readVPIObjectModelImage16(
             data, kindOffset + (uint32_t{first} + low) * 2) == kind;
}

)cpp";

  os << "} // namespace obelisk::reflection\n\n";
  os << "#define OBELISK_FOR_EACH_VPI_OBJECT_KIND(M) \\\n";
  for (auto [index, object] : llvm::enumerate(objects)) {
    os << formatv("  M({0}, {1})", object->getValueAsString("apiName"),
                  object->getValueAsInt("value"));
    os << (index + 1 == objects.size() ? "\n\n" : " \\\n");
  }
  os << "#define OBELISK_FOR_EACH_VPI_RELATION(M) \\\n";
  for (auto [index, relation] : llvm::enumerate(relations)) {
    os << formatv("  M({0}, {1})", relation->getValueAsString("apiName"),
                  relation->getValueAsInt("value"));
    os << (index + 1 == relations.size() ? "\n\n" : " \\\n");
  }
  auto propertyRecords = records.getAllDerivedDefinitions("VPIProperty");
  SmallVector<const Record *> properties(propertyRecords.begin(),
                                         propertyRecords.end());
  llvm::sort(properties, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  os << "#define OBELISK_FOR_EACH_VPI_PROPERTY(M) \\\n";
  for (auto [index, property] : llvm::enumerate(properties)) {
    os << formatv("  M({0}, {1})", property->getValueAsString("apiName"),
                  property->getValueAsInt("value"));
    os << (index + 1 == properties.size() ? "\n\n" : " \\\n");
  }
  os << "#endif // OBELISK_REFLECTION_VPIOBJECTMODEL_H_INC\n";
  return false;
}

mlir::GenRegistration reflectionLayoutGen(
    "gen-obelisk-reflection-layout",
    "Generate Obelisk design-reflection layouts and record kinds",
    emitReflectionLayouts);

mlir::GenRegistration vpiObjectModelGen(
    "gen-obelisk-vpi-object-model",
    "Generate Obelisk VPI object and relationship descriptors",
    emitVPIObjectModel);

bool validateCoverageSchema(const RecordKeeper &records) {
  auto metrics = records.getAllDerivedDefinitions("CoverageMetric");
  auto sections = records.getAllDerivedDefinitions("CoverageSection");
  auto dimensions =
      records.getAllDerivedDefinitions("CoverageToggleDimensionKind");
  auto functionalItems =
      records.getAllDerivedDefinitions("CoverageFunctionalItemKind");
  auto functionalBins =
      records.getAllDerivedDefinitions("CoverageFunctionalBinKind");
  auto functionalSources =
      records.getAllDerivedDefinitions("CoverageFunctionalSourceRole");
  auto repetitions =
      records.getAllDerivedDefinitions("CoverageTransitionRepetitionKind");
  auto valueSets = records.getAllDerivedDefinitions("CoverageValueSetKind");
  auto valueAtoms = records.getAllDerivedDefinitions("CoverageValueAtomKind");
  auto retainPolicies =
      records.getAllDerivedDefinitions("CoverageCrossRetainAutoPolicy");
  auto selectors =
      records.getAllDerivedDefinitions("CoverageCrossSelectorKind");
  auto expressionOwners =
      records.getAllDerivedDefinitions("CoverageExpressionOwnerKind");
  auto expressionRoles =
      records.getAllDerivedDefinitions("CoverageExpressionRole");
  auto formalKinds =
      records.getAllDerivedDefinitions("CoverageFunctionalFormalKind");
  auto formalDirections =
      records.getAllDerivedDefinitions("CoverageFunctionalFormalDirection");
  auto expressionResults =
      records.getAllDerivedDefinitions("CoverageExpressionResultKind");
  auto signedness = records.getAllDerivedDefinitions("CoverageSignedness");
  auto arrayModes =
      records.getAllDerivedDefinitions("CoverageBinArrayMode");
  auto distributions =
      records.getAllDerivedDefinitions("CoverageBinDistributionKind");
  auto matchesPolicies =
      records.getAllDerivedDefinitions("CoverageCrossMatchesPolicy");
  auto optionScopes =
      records.getAllDerivedDefinitions("CoverageOptionScopeKind");
  auto optionKinds =
      records.getAllDerivedDefinitions("CoverageConfigurationOptionKind");
  auto optionOwners = records.getAllDerivedDefinitions(
      "CoverageConfigurationOptionOwnerKind");
  auto optionValues =
      records.getAllDerivedDefinitions("CoverageConfigurationValueKind");
  auto expressionPhases = records.getAllDerivedDefinitions(
      "CoverageExpressionEvaluationPhase");
  auto tupleModes =
      records.getAllDerivedDefinitions("CoverageTupleElementMode");
  auto resolvedValueSetRoles =
      records.getAllDerivedDefinitions("CoverageResolvedValueSetRole");
  if (metrics.empty() || sections.empty() || dimensions.empty() ||
      functionalItems.empty() || functionalBins.empty() ||
      functionalSources.empty() || repetitions.empty() || valueSets.empty() ||
      valueAtoms.empty() || retainPolicies.empty() || selectors.empty() ||
      expressionOwners.empty() || expressionRoles.empty() ||
      formalKinds.empty() || formalDirections.empty() ||
      expressionResults.empty() || signedness.empty() || arrayModes.empty() ||
      distributions.empty() || matchesPolicies.empty() ||
      optionScopes.empty() || optionKinds.empty() || optionOwners.empty() ||
      optionValues.empty() || expressionPhases.empty() || tupleModes.empty() ||
      resolvedValueSetRoles.empty()) {
    PrintError("coverage schema needs metrics, toggle dimensions, functional "
               "kinds, and sections");
    return false;
  }
  auto validateKinds = [&](ArrayRef<const Record *> kinds,
                           StringRef description) {
    DenseSet<uint32_t> values;
    StringSet<> names;
    for (const Record *kind : kinds) {
      StringRef name;
      uint32_t value = 0;
      if (!getCppName(*kind, description, name) ||
          !getU32(*kind, "value", 1, value))
        return false;
      if (!names.insert(name).second || !values.insert(value).second) {
        PrintError(kind->getLoc(),
                   Twine("duplicate ") + description + " name or value");
        return false;
      }
    }
    return true;
  };
  if (!validateKinds(functionalItems, "functional item kind") ||
      !validateKinds(functionalBins, "functional bin kind") ||
      !validateKinds(functionalSources, "functional source role") ||
      !validateKinds(repetitions, "transition repetition kind") ||
      !validateKinds(valueSets, "value-set kind") ||
      !validateKinds(valueAtoms, "value-atom kind") ||
      !validateKinds(retainPolicies, "cross retain-auto policy") ||
      !validateKinds(selectors, "cross selector kind") ||
      !validateKinds(expressionOwners, "expression owner kind") ||
      !validateKinds(expressionRoles, "expression role") ||
      !validateKinds(formalKinds, "functional formal kind") ||
      !validateKinds(formalDirections, "functional formal direction") ||
      !validateKinds(expressionResults, "expression result kind") ||
      !validateKinds(signedness, "coverage signedness") ||
      !validateKinds(arrayModes, "bin array mode") ||
      !validateKinds(distributions, "bin distribution kind") ||
      !validateKinds(matchesPolicies, "cross matches policy") ||
      !validateKinds(optionScopes, "functional option scope") ||
      !validateKinds(optionKinds, "configuration option kind") ||
      !validateKinds(optionOwners, "configuration option owner kind") ||
      !validateKinds(optionValues, "configuration value kind") ||
      !validateKinds(expressionPhases, "expression evaluation phase") ||
      !validateKinds(tupleModes, "tuple element mode") ||
      !validateKinds(resolvedValueSetRoles, "resolved value-set role"))
    return false;
  DenseSet<uint32_t> dimensionValues;
  StringSet<> dimensionNames;
  for (const Record *dimension : dimensions) {
    StringRef name;
    uint32_t value = 0;
    if (!getCppName(*dimension, "toggle dimension", name) ||
        !getU32(*dimension, "value", 1, value))
      return false;
    if (!dimensionNames.insert(name).second ||
        !dimensionValues.insert(value).second) {
      PrintError(dimension->getLoc(),
                 "duplicate toggle dimension name or value");
      return false;
    }
  }
  DenseSet<uint32_t> metricValues;
  StringSet<> metricNames;
  for (const Record *metric : metrics) {
    StringRef name;
    uint32_t value = 0;
    if (!getCppName(*metric, "coverage metric", name) ||
        !getU32(*metric, "value", 1, value))
      return false;
    if (!metricNames.insert(name).second ||
        !metricValues.insert(value).second) {
      PrintError(metric->getLoc(), "duplicate coverage metric name or value");
      return false;
    }
  }
  DenseSet<uint32_t> sectionValues;
  StringSet<> sectionNames;
  for (const Record *section : sections) {
    StringRef name;
    uint32_t value = 0;
    uint32_t recordSize = 0;
    if (!getCppName(*section, "coverage section", name) ||
        !getU32(*section, "value", 1, value) ||
        !getU32(*section, "recordSize", 0, recordSize))
      return false;
    if (!sectionNames.insert(name).second ||
        !sectionValues.insert(value).second) {
      PrintError(section->getLoc(), "duplicate coverage section name or value");
      return false;
    }
    if (recordSize && recordSize % 8 != 0) {
      PrintError(section->getLoc(),
                 "fixed coverage record size must be eight-byte aligned");
      return false;
    }
  }

  auto encodings =
      records.getAllDerivedDefinitions("CoverageFieldEncoding");
  auto domainDefs =
      records.getAllDerivedDefinitions("CoverageFieldDomain");
  SmallVector<const Record *> domains(domainDefs.begin(), domainDefs.end());
  auto physicalRecords = records.getAllDerivedDefinitions("CoverageRecord");
  if (encodings.empty() || domains.empty() || physicalRecords.empty()) {
    PrintError("coverage schema needs physical field encodings, domains, and "
               "records");
    return false;
  }
  StringSet<> domainNames;
  DenseSet<const Record *> domainRecords;
  DenseSet<uint32_t> knownSectionValues;
  for (const Record *section : sections)
    knownSectionValues.insert(
        static_cast<uint32_t>(section->getValueAsInt("value")));
  for (const Record *domain : domains) {
    StringRef name;
    if (!getCppName(*domain, "coverage field domain", name))
      return false;
    StringRef category = domain->getValueAsString("category");
    static constexpr StringLiteral categories[] = {
        "None", "Enum", "String", "StableRef", "Range", "UUIDRef",
        "Relational"};
    uint32_t targetSection = 0;
    if (!llvm::is_contained(categories, category) ||
        !getU32(*domain, "targetSection", 0, targetSection) ||
        (targetSection && !knownSectionValues.count(targetSection)) ||
        !domainNames.insert(name).second || !domainRecords.insert(domain).second) {
      PrintError(domain->getLoc(), "invalid coverage field domain");
      return false;
    }
    auto values = domain->getValueAsListOfDefs("enumValues");
    DenseSet<int64_t> uniqueValues;
    for (const Record *valueRecord : values) {
      int64_t value = valueRecord->getValueAsInt("value");
      if (value < 0 || value > maxU32 || !uniqueValues.insert(value).second) {
        PrintError(domain->getLoc(), "invalid coverage enum domain value");
        return false;
      }
    }
    if ((category == "Enum") != !values.empty() ||
        ((category == "String" || category == "StableRef" ||
          category == "Range" || category == "UUIDRef") !=
         (targetSection != 0))) {
      PrintError(domain->getLoc(),
                 "coverage field domain category has inconsistent metadata");
      return false;
    }
  }
  auto validateEnumDomain = [&](StringRef domainName,
                                ArrayRef<const Record *> expected) {
    const Record *matchingDomain = nullptr;
    for (const Record *domain : domains)
      if (domain->getValueAsString("cppName") == domainName) {
        matchingDomain = domain;
        break;
      }
    if (!matchingDomain) {
      PrintError(Twine("missing coverage enum domain ") + domainName);
      return false;
    }
    auto actual = matchingDomain->getValueAsListOfDefs("enumValues");
    DenseSet<const Record *> expectedSet(expected.begin(), expected.end());
    if (actual.size() != expectedSet.size()) {
      PrintError(matchingDomain->getLoc(),
                 "coverage enum domain must list its complete enum class");
      return false;
    }
    for (const Record *value : actual)
      if (!expectedSet.erase(value)) {
        PrintError(matchingDomain->getLoc(),
                   "coverage enum domain contains a value from another enum");
        return false;
      }
    return expectedSet.empty();
  };
  if (!validateEnumDomain("MetricKind", metrics) ||
      !validateEnumDomain("ToggleDimensionKind", dimensions) ||
      !validateEnumDomain("FunctionalSourceRole", functionalSources) ||
      !validateEnumDomain("FunctionalItemKind", functionalItems) ||
      !validateEnumDomain("FunctionalBinKind", functionalBins) ||
      !validateEnumDomain("TransitionRepetitionKind", repetitions) ||
      !validateEnumDomain("FunctionalValueSetKind", valueSets) ||
      !validateEnumDomain("FunctionalValueAtomKind", valueAtoms) ||
      !validateEnumDomain("CrossRetainAutoPolicy", retainPolicies) ||
      !validateEnumDomain("CrossSelectorKind", selectors) ||
      !validateEnumDomain("FunctionalExpressionOwnerKind", expressionOwners) ||
      !validateEnumDomain("FunctionalExpressionRole", expressionRoles) ||
      !validateEnumDomain("FunctionalFormalKind", formalKinds) ||
      !validateEnumDomain("FunctionalFormalDirection", formalDirections) ||
      !validateEnumDomain("FunctionalExpressionResultKind", expressionResults) ||
      !validateEnumDomain("CoverageSignedness", signedness) ||
      !validateEnumDomain("FunctionalBinArrayMode", arrayModes) ||
      !validateEnumDomain("FunctionalBinDistributionKind", distributions) ||
      !validateEnumDomain("CrossMatchesPolicy", matchesPolicies) ||
      !validateEnumDomain("FunctionalOptionScopeKind", optionScopes) ||
      !validateEnumDomain("FunctionalConfigurationOptionKind", optionKinds) ||
      !validateEnumDomain("FunctionalConfigurationOptionOwnerKind",
                          optionOwners) ||
      !validateEnumDomain("FunctionalConfigurationValueKind", optionValues) ||
      !validateEnumDomain("FunctionalExpressionEvaluationPhase",
                          expressionPhases) ||
      !validateEnumDomain("FunctionalTupleElementMode", tupleModes) ||
      !validateEnumDomain("ResolvedFunctionalValueSetRole",
                          resolvedValueSetRoles))
    return false;
  StringMap<uint32_t> encodingWidths;
  for (const Record *encoding : encodings) {
    StringRef name;
    uint32_t width = 0;
    if (!getCppName(*encoding, "coverage field encoding", name) ||
        !getU32(*encoding, "width", 1, width))
      return false;
    if (!encodingWidths.try_emplace(name, width).second) {
      PrintError(encoding->getLoc(),
                 "duplicate coverage field encoding name");
      return false;
    }
  }
  StringSet<> recordNames;
  DenseSet<const Record *> recordSections;
  DenseMap<const Record *, std::pair<uint32_t, uint32_t>> identityCounts;
  for (const Record *physicalRecord : physicalRecords) {
    StringRef recordName;
    if (!getCppName(*physicalRecord, "coverage physical record", recordName))
      return false;
    if (!recordNames.insert(recordName).second) {
      PrintError(physicalRecord->getLoc(),
                 "duplicate coverage physical record name");
      return false;
    }
    const Record *section = physicalRecord->getValueAsDef("section");
    if (!recordSections.insert(section).second) {
      PrintError(physicalRecord->getLoc(),
                 "coverage section has more than one physical record");
      return false;
    }
    uint32_t recordSize = 0;
    if (!getU32(*section, "recordSize", 1, recordSize))
      return false;
    auto fields = physicalRecord->getValueAsListOfDefs("fields");
    if (fields.empty()) {
      PrintError(physicalRecord->getLoc(),
                 "coverage physical record needs fields");
      return false;
    }
    StringSet<> fieldNames;
    StringMap<const Record *> fieldRecords;
    StringSet<> diagnosticNames;
    SmallVector<std::pair<uint32_t, uint32_t>> ranges;
    uint64_t coveredBytes = 0;
    for (const Record *field : fields) {
      StringRef fieldName;
      uint32_t offset = 0;
      if (!getCppName(*field, "coverage field", fieldName) ||
          !getU32(*field, "offset", 0, offset))
        return false;
      StringRef diagnosticName = field->getValueAsString("diagnosticName");
      StringRef semantic = field->getValueAsString("semantic");
      static constexpr StringLiteral validSemantics[] = {
          "Scalar",      "Enum",        "StableID",    "StableIDRef",
          "RangeStart",  "RangeCount",  "StringIndex", "Flags",
          "ReservedZero", "UUIDID",     "UUIDRef",    "Digest"};
      if (!llvm::is_contained(validSemantics, semantic)) {
        PrintError(field->getLoc(), "unknown coverage field semantic role");
        return false;
      }
      const Record *domainRecord = field->getValueAsDef("domain");
      StringRef domainCategory = domainRecord->getValueAsString("category");
      StringRef pairedField = field->getValueAsString("pairedField");
      StringRef expectedCategory =
          semantic == "Enum"          ? "Enum"
          : semantic == "StringIndex" ? "String"
          : semantic == "RangeStart" || semantic == "RangeCount"
              ? "Range"
          : semantic == "UUIDID" || semantic == "UUIDRef" ? "UUIDRef"
          : semantic == "StableID" || semantic == "StableIDRef"
              ? (domainCategory == "Relational" ? "Relational" : "StableRef")
              : "None";
      if (domainCategory != expectedCategory) {
        PrintError(field->getLoc(),
                   "coverage field semantic has incompatible typed domain");
        return false;
      }
      bool isRange = semantic == "RangeStart" || semantic == "RangeCount";
      if (isRange == pairedField.empty()) {
        PrintError(field->getLoc(),
                   "coverage range fields need a paired field name");
        return false;
      }
      const Record *encoding = field->getValueAsDef("encoding");
      StringRef encodingName;
      if (!getCppName(*encoding, "coverage field encoding", encodingName))
        return false;
      if (((semantic == "Enum" || semantic == "RangeStart" ||
            semantic == "RangeCount" ||
            semantic == "StringIndex") &&
           encodingName != "U32") ||
          (semantic == "Flags" && encodingName != "U32" &&
           encodingName != "U64") ||
          ((semantic == "StableID" || semantic == "StableIDRef") &&
           encodingName != "U64") ||
          ((semantic == "UUIDID" || semantic == "UUIDRef") &&
           encodingName != "UUID") ||
          (semantic == "Digest" && encodingName != "Digest")) {
        PrintError(field->getLoc(),
                   "coverage field semantic has incompatible encoding");
        return false;
      }
      auto width = encodingWidths.find(encodingName);
      if (diagnosticName.empty() || width == encodingWidths.end() ||
          offset > recordSize || width->second > recordSize - offset) {
        PrintError(field->getLoc(),
                   "coverage field has invalid name, encoding, or range");
        return false;
      }
      if (!fieldNames.insert(fieldName).second ||
          !diagnosticNames.insert(diagnosticName).second) {
        PrintError(field->getLoc(),
                   "duplicate coverage field or diagnostic name");
        return false;
      }
      fieldRecords.try_emplace(fieldName, field);
      uint32_t end = offset + width->second;
      for (auto [otherBegin, otherEnd] : ranges)
        if (offset < otherEnd && otherBegin < end) {
          PrintError(field->getLoc(), "overlapping coverage fields");
          return false;
        }
      ranges.emplace_back(offset, end);
      coveredBytes += width->second;
      if (semantic == "StableID")
        ++identityCounts[section].first;
      else if (semantic == "UUIDID")
        ++identityCounts[section].second;
    }
    for (const Record *field : fields) {
      StringRef pairedField = field->getValueAsString("pairedField");
      auto paired = fieldRecords.find(pairedField);
      if (!pairedField.empty() && paired == fieldRecords.end()) {
        PrintError(field->getLoc(),
                   "coverage range field names a missing paired field");
        return false;
      }
      if (!pairedField.empty()) {
        const Record *other = paired->second;
        StringRef semantic = field->getValueAsString("semantic");
        StringRef otherSemantic = other->getValueAsString("semantic");
        bool complementary =
            (semantic == "RangeStart" && otherSemantic == "RangeCount") ||
            (semantic == "RangeCount" && otherSemantic == "RangeStart");
        if (!complementary ||
            other->getValueAsString("pairedField") !=
                field->getValueAsString("cppName") ||
            other->getValueAsDef("domain") != field->getValueAsDef("domain") ||
            other->getValueAsDef("encoding") !=
                field->getValueAsDef("encoding") ||
            field->getValueAsDef("encoding")->getValueAsString("cppName") !=
                "U32") {
          PrintError(field->getLoc(),
                     "coverage range pair must be reciprocal and use one "
                     "typed target domain and U32 encoding");
          return false;
        }
      }
    }
    if (coveredBytes != recordSize) {
      PrintError(physicalRecord->getLoc(),
                 "coverage physical record must describe every wire byte");
      return false;
    }
  }
  for (const Record *section : sections) {
    uint32_t recordSize = 0;
    if (!getU32(*section, "recordSize", 0, recordSize))
      return false;
    if (recordSize && !recordSections.count(section)) {
      PrintError(section->getLoc(),
                 "fixed coverage section needs exactly one physical record");
      return false;
    }
  }
  for (const Record *domain : domains) {
    StringRef category = domain->getValueAsString("category");
    uint32_t targetSection =
        static_cast<uint32_t>(domain->getValueAsInt("targetSection"));
    if (category != "StableRef" && category != "UUIDRef")
      continue;
    const Record *target = nullptr;
    for (const Record *section : sections)
      if (section->getValueAsInt("value") == targetSection) {
        target = section;
        break;
      }
    if (!target)
      return false;
    const auto counts = identityCounts.lookup(target);
    const uint32_t identityCount =
        category == "StableRef" ? counts.first : counts.second;
    if (identityCount != 1) {
      PrintError(domain->getLoc(),
                 "coverage reference target must expose exactly one typed "
                 "identity field");
      return false;
    }
  }
  return true;
}

SmallVector<const Record *> sortedCoverageRecords(const RecordKeeper &records,
                                                  StringRef base) {
  auto derived = records.getAllDerivedDefinitions(base);
  SmallVector<const Record *> result(derived.begin(), derived.end());
  llvm::sort(result, [](const Record *left, const Record *right) {
    return left->getValueAsInt("value") < right->getValueAsInt("value");
  });
  return result;
}

bool emitCoverageDecls(const RecordKeeper &records, raw_ostream &os) {
  if (!validateCoverageSchema(records))
    return true;
  os << "// Generated from CoverageDatabase.td. Do not edit.\n";
  os << "#ifndef OBELISK_COVERAGE_FORMAT_DECLS_H_INC\n";
  os << "#define OBELISK_COVERAGE_FORMAT_DECLS_H_INC\n\n";
  os << "namespace obelisk::coverage {\n";
  os << "inline constexpr uint32_t CodecVersion = 1;\n";
  os << "inline constexpr uint32_t HeaderSize = 104;\n";
  os << "inline constexpr uint32_t DirectoryRecordSize = 32;\n";
  os << "enum class MetricKind : uint32_t {\n";
  for (const Record *metric : sortedCoverageRecords(records, "CoverageMetric"))
    os << "  " << metric->getValueAsString("cppName") << " = "
       << metric->getValueAsInt("value") << ",\n";
  os << "};\n";
  os << "enum class ToggleDimensionKind : uint32_t {\n";
  for (const Record *dimension :
       sortedCoverageRecords(records, "CoverageToggleDimensionKind"))
    os << "  " << dimension->getValueAsString("cppName") << " = "
       << dimension->getValueAsInt("value") << ",\n";
  os << "};\n";
  os << "enum class FunctionalItemKind : uint32_t {\n";
  for (const Record *kind :
       sortedCoverageRecords(records, "CoverageFunctionalItemKind"))
    os << "  " << kind->getValueAsString("cppName") << " = "
       << kind->getValueAsInt("value") << ",\n";
  os << "};\n";
  os << "enum class FunctionalBinKind : uint32_t {\n";
  for (const Record *kind :
       sortedCoverageRecords(records, "CoverageFunctionalBinKind"))
    os << "  " << kind->getValueAsString("cppName") << " = "
       << kind->getValueAsInt("value") << ",\n";
  os << "};\n";
  os << "enum class FunctionalSourceRole : uint32_t {\n";
  for (const Record *kind :
       sortedCoverageRecords(records, "CoverageFunctionalSourceRole"))
    os << "  " << kind->getValueAsString("cppName") << " = "
       << kind->getValueAsInt("value") << ",\n";
  os << "};\n";
  auto emitEnum = [&](StringRef cppName, StringRef base) {
    os << "enum class " << cppName << " : uint32_t {\n";
    for (const Record *kind : sortedCoverageRecords(records, base))
      os << "  " << kind->getValueAsString("cppName") << " = "
         << kind->getValueAsInt("value") << ",\n";
    os << "};\n";
  };
  emitEnum("TransitionRepetitionKind",
           "CoverageTransitionRepetitionKind");
  emitEnum("FunctionalValueSetKind", "CoverageValueSetKind");
  emitEnum("FunctionalValueAtomKind", "CoverageValueAtomKind");
  emitEnum("CrossRetainAutoPolicy", "CoverageCrossRetainAutoPolicy");
  emitEnum("CrossSelectorKind", "CoverageCrossSelectorKind");
  emitEnum("FunctionalExpressionOwnerKind", "CoverageExpressionOwnerKind");
  emitEnum("FunctionalExpressionRole", "CoverageExpressionRole");
  emitEnum("FunctionalFormalKind", "CoverageFunctionalFormalKind");
  emitEnum("FunctionalFormalDirection",
           "CoverageFunctionalFormalDirection");
  emitEnum("FunctionalExpressionResultKind",
           "CoverageExpressionResultKind");
  emitEnum("CoverageSignedness", "CoverageSignedness");
  emitEnum("FunctionalBinArrayMode", "CoverageBinArrayMode");
  emitEnum("FunctionalBinDistributionKind",
           "CoverageBinDistributionKind");
  emitEnum("CrossMatchesPolicy", "CoverageCrossMatchesPolicy");
  emitEnum("FunctionalOptionScopeKind", "CoverageOptionScopeKind");
  emitEnum("FunctionalConfigurationOptionKind",
           "CoverageConfigurationOptionKind");
  emitEnum("FunctionalConfigurationOptionOwnerKind",
           "CoverageConfigurationOptionOwnerKind");
  emitEnum("FunctionalConfigurationValueKind",
           "CoverageConfigurationValueKind");
  emitEnum("FunctionalExpressionEvaluationPhase",
           "CoverageExpressionEvaluationPhase");
  emitEnum("FunctionalTupleElementMode", "CoverageTupleElementMode");
  emitEnum("ResolvedFunctionalValueSetRole", "CoverageResolvedValueSetRole");
  os << "enum class CoverageFieldSemantic : uint32_t {\n"
        "  Scalar, Enum, StableID, StableIDRef, RangeStart, RangeCount,\n"
        "  StringIndex, Flags, ReservedZero, UUIDID, UUIDRef, Digest,\n"
        "};\n";
  os << "enum class CoverageFieldDomainKind : uint32_t {\n"
        "  None, Enum, String, StableRef, Range, UUIDRef, Relational,\n"
        "};\n";
  os << "enum class CoverageFieldDomain : uint32_t {\n";
  auto declarationDomainRecords =
      records.getAllDerivedDefinitions("CoverageFieldDomain");
  SmallVector<const Record *> domains(declarationDomainRecords.begin(),
                                      declarationDomainRecords.end());
  llvm::sort(domains, [](const Record *left, const Record *right) {
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  for (auto [index, domain] : llvm::enumerate(domains))
    os << "  " << domain->getValueAsString("cppName") << " = " << index
       << ",\n";
  os << "};\n";
  os << "enum class CoverageFieldEncoding : uint32_t {\n";
  uint32_t encodingValue = 1;
  auto encodingRecords =
      records.getAllDerivedDefinitions("CoverageFieldEncoding");
  SmallVector<const Record *> encodings(encodingRecords.begin(),
                                        encodingRecords.end());
  llvm::sort(encodings, [](const Record *left, const Record *right) {
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  for (const Record *encoding : encodings)
    os << "  " << encoding->getValueAsString("cppName") << " = "
       << encodingValue++ << ",\n";
  os << "};\n";
  os << "enum class SectionKind : uint32_t {\n";
  for (const Record *section :
       sortedCoverageRecords(records, "CoverageSection"))
    os << "  " << section->getValueAsString("cppName") << " = "
       << section->getValueAsInt("value") << ",\n";
  os << "};\n";
  auto physicalRecordDefs = records.getAllDerivedDefinitions("CoverageRecord");
  SmallVector<const Record *> physicalRecords(physicalRecordDefs.begin(),
                                              physicalRecordDefs.end());
  llvm::sort(physicalRecords, [](const Record *left, const Record *right) {
    const Record *leftSection = left->getValueAsDef("section");
    const Record *rightSection = right->getValueAsDef("section");
    return leftSection->getValueAsInt("value") <
           rightSection->getValueAsInt("value");
  });
  for (const Record *physicalRecord : physicalRecords) {
    StringRef name = physicalRecord->getValueAsString("cppName");
    const Record *section = physicalRecord->getValueAsDef("section");
    os << "struct " << name << " {\n";
    os << "  static constexpr SectionKind Section = SectionKind::"
       << section->getValueAsString("cppName") << ";\n";
    os << "  static constexpr uint32_t Size = "
       << section->getValueAsInt("recordSize") << ";\n";
    for (const Record *field :
         physicalRecord->getValueAsListOfDefs("fields")) {
      StringRef fieldName = field->getValueAsString("cppName");
      const Record *encoding = field->getValueAsDef("encoding");
      os << "  static constexpr uint32_t " << fieldName << "Offset = "
         << field->getValueAsInt("offset") << ";\n";
      os << "  static constexpr uint32_t " << fieldName << "Width = "
         << encoding->getValueAsInt("width") << ";\n";
      os << "  static constexpr const char *" << fieldName << "Name = \""
         << field->getValueAsString("diagnosticName") << "\";\n";
      os << "  static constexpr CoverageFieldSemantic " << fieldName
         << "Semantic = CoverageFieldSemantic::"
         << field->getValueAsString("semantic") << ";\n";
      StringRef encodingName = encoding->getValueAsString("cppName");
      uint32_t width = static_cast<uint32_t>(encoding->getValueAsInt("width"));
      if (encodingName == "U32" || encodingName == "U64" ||
          encodingName == "S64") {
        StringRef type = encodingName == "U32" ? "uint32_t"
                         : encodingName == "S64" ? "int64_t"
                                                   : "uint64_t";
        StringRef unsignedType =
            encodingName == "U32" ? "uint32_t" : "uint64_t";
        os << "  static " << type << " get" << fieldName
           << "(const uint8_t *data) {\n"
              "    " << unsignedType << " value = 0;\n"
              "    for (uint32_t i = 0; i != "
           << width << "; ++i)\n"
           << "      value |= " << unsignedType << "(data[" << fieldName
           << "Offset + i]) << (i * 8);\n"
              "    return static_cast<" << type << ">(value);\n"
              "  }\n"
              "  static void set"
           << fieldName << "(uint8_t *data, " << type << " value) {\n"
           << "    const auto bits = static_cast<" << unsignedType
           << ">(value);\n"
           << "    for (uint32_t i = 0; i != " << width << "; ++i)\n"
           << "      data[" << fieldName
           << "Offset + i] = static_cast<uint8_t>(bits >> (i * 8));\n"
              "  }\n";
      } else {
        os << "  static const uint8_t *get" << fieldName
           << "(const uint8_t *data) { return data + " << fieldName
           << "Offset; }\n"
              "  static void set"
           << fieldName << "(uint8_t *data, const uint8_t *value) {\n"
           << "    for (uint32_t i = 0; i != " << width << "; ++i)\n"
           << "      data[" << fieldName << "Offset + i] = value[i];\n"
              "  }\n";
      }
    }
    os << "};\n";
  }
  os << "} // namespace obelisk::coverage\n\n#endif\n";
  return false;
}

bool emitCoverageRecordDescriptors(const RecordKeeper &records,
                                   raw_ostream &os, StringRef prefix) {
  auto domainRecords =
      records.getAllDerivedDefinitions("CoverageFieldDomain");
  SmallVector<const Record *> domains(domainRecords.begin(),
                                      domainRecords.end());
  llvm::sort(domains, [](const Record *left, const Record *right) {
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  for (const Record *domain : domains) {
    auto values = domain->getValueAsListOfDefs("enumValues");
    if (values.empty())
      continue;
    os << "inline constexpr uint32_t " << prefix << "Domain"
       << domain->getValueAsString("cppName") << "Values[] = {";
    for (auto [index, value] : llvm::enumerate(values))
      os << (index ? ", " : "") << value->getValueAsInt("value");
    os << "};\n";
  }
  os << "inline constexpr DomainDescriptor " << prefix
     << "DomainDescriptors[] = {\n";
  for (const Record *domain : domains) {
    auto values = domain->getValueAsListOfDefs("enumValues");
    os << "  {CoverageFieldDomain::" << domain->getValueAsString("cppName")
       << ", CoverageFieldDomainKind::"
       << domain->getValueAsString("category") << ", "
       << domain->getValueAsInt("targetSection") << ", ";
    if (values.empty())
      os << "nullptr, 0";
    else
      os << prefix << "Domain" << domain->getValueAsString("cppName")
         << "Values, " << values.size();
    os << "},\n";
  }
  os << "};\n";
  auto physicalRecordDefs = records.getAllDerivedDefinitions("CoverageRecord");
  SmallVector<const Record *> physicalRecords(physicalRecordDefs.begin(),
                                              physicalRecordDefs.end());
  llvm::sort(physicalRecords, [](const Record *left, const Record *right) {
    return left->getValueAsDef("section")->getValueAsInt("value") <
           right->getValueAsDef("section")->getValueAsInt("value");
  });
  for (const Record *physicalRecord : physicalRecords) {
    StringRef recordName = physicalRecord->getValueAsString("cppName");
    os << "inline constexpr FieldDescriptor " << prefix << recordName
       << "Fields[] = {\n";
    for (const Record *field :
         physicalRecord->getValueAsListOfDefs("fields")) {
      const Record *encoding = field->getValueAsDef("encoding");
      const Record *domain = field->getValueAsDef("domain");
      StringRef pairedDiagnostic;
      StringRef pairedName = field->getValueAsString("pairedField");
      if (!pairedName.empty())
        for (const Record *candidate :
             physicalRecord->getValueAsListOfDefs("fields"))
          if (candidate->getValueAsString("cppName") == pairedName) {
            pairedDiagnostic =
                candidate->getValueAsString("diagnosticName");
            break;
          }
      os << "  {\"" << field->getValueAsString("diagnosticName") << "\", "
         << field->getValueAsInt("offset") << ", "
         << encoding->getValueAsInt("width")
         << ", CoverageFieldEncoding::"
         << encoding->getValueAsString("cppName")
         << ", CoverageFieldSemantic::"
         << field->getValueAsString("semantic")
         << ", CoverageFieldDomain::"
         << domain->getValueAsString("cppName") << ", "
         << (field->getValueAsBit("nullable") ? "true" : "false") << ", "
         << field->getValueAsInt("validMask") << ", \""
         << pairedDiagnostic << "\"},\n";
    }
    os << "};\n";
  }
  os << "inline constexpr RecordDescriptor " << prefix
     << "RecordDescriptors[] = {\n";
  for (const Record *physicalRecord : physicalRecords) {
    StringRef recordName = physicalRecord->getValueAsString("cppName");
    const Record *section = physicalRecord->getValueAsDef("section");
    os << "  {SectionKind::" << section->getValueAsString("cppName")
       << ", \"" << recordName << "\", "
       << section->getValueAsInt("recordSize") << ", " << prefix
       << recordName << "Fields, "
       << physicalRecord->getValueAsListOfDefs("fields").size() << "},\n";
  }
  os << "};\n";
  os << "inline constexpr const DomainDescriptor *" << prefix
     << "FindDomain(CoverageFieldDomain domain) {\n"
        "  for (const auto &entry : "
     << prefix
     << "DomainDescriptors)\n"
        "    if (entry.domain == domain)\n"
        "      return &entry;\n"
        "  return nullptr;\n"
        "}\n";
  os << "template <typename Validate> bool " << prefix
     << "ValidatePhysicalRecord(const RecordDescriptor &record,\n"
        "    const uint8_t *data, Validate &&validate,\n"
        "    const FieldDescriptor *&invalidField) {\n"
        "  for (uint32_t fieldIndex = 0; fieldIndex != record.fieldCount; "
        "++fieldIndex) {\n"
        "    const FieldDescriptor &field = record.fields[fieldIndex];\n"
        "    uint64_t value = 0;\n"
        "    const uint32_t scalarBytes = field.width > 8 ? 8 : field.width;\n"
        "    for (uint32_t i = 0; i != scalarBytes; ++i)\n"
        "      value |= uint64_t(data[field.offset + i]) << (i * 8);\n"
        "    bool valid = true;\n"
        "    if (field.semantic == CoverageFieldSemantic::ReservedZero) {\n"
        "      for (uint32_t i = 0; i != field.width; ++i)\n"
        "        valid = valid && data[field.offset + i] == 0;\n"
        "    } else if (field.semantic == CoverageFieldSemantic::Flags) {\n"
        "      valid = field.validMask >= 0 &&\n"
        "              (value & ~uint64_t(field.validMask)) == 0;\n"
        "    } else if (field.semantic == CoverageFieldSemantic::Enum) {\n"
        "      valid = false;\n"
        "      if (const DomainDescriptor *domain = "
     << prefix
     << "FindDomain(field.domain))\n"
        "        for (uint32_t i = 0; i != domain->valueCount; ++i)\n"
        "          valid = valid || value == domain->values[i];\n"
        "    } else if (!field.nullable &&\n"
        "               (field.semantic == CoverageFieldSemantic::StableID ||\n"
        "                field.semantic == CoverageFieldSemantic::StableIDRef "
        "||\n"
        "                field.semantic == CoverageFieldSemantic::UUIDID ||\n"
        "                field.semantic == CoverageFieldSemantic::UUIDRef)) {\n"
        "      valid = false;\n"
        "      for (uint32_t i = 0; i != field.width; ++i)\n"
        "        valid = valid || data[field.offset + i] != 0;\n"
        "    }\n"
        "    if (valid)\n"
        "      valid = validate(field, value, data + field.offset);\n"
        "    if (!valid) { invalidField = &field; return false; }\n"
        "  }\n"
        "  invalidField = nullptr;\n"
        "  return true;\n"
        "}\n";
  return false;
}

bool emitCoverageDescriptors(const RecordKeeper &records, raw_ostream &os,
                             StringRef arrayName) {
  if (!validateCoverageSchema(records))
    return true;
  os << "// Generated from CoverageDatabase.td. Do not edit.\n";
  os << "inline constexpr SectionDescriptor " << arrayName << "[] = {\n";
  for (const Record *section :
       sortedCoverageRecords(records, "CoverageSection")) {
    os << "  {SectionKind::" << section->getValueAsString("cppName") << ", "
       << section->getValueAsInt("recordSize") << ", "
       << (section->getValueAsBit("required") ? "true" : "false") << ", "
       << (section->getValueAsBit("staticSchema") ? "true" : "false") << "},\n";
  }
  os << "};\n";
  return false;
}

bool emitCoverageParser(const RecordKeeper &records, raw_ostream &os) {
  if (emitCoverageDescriptors(records, os, "ParserSectionDescriptors"))
    return true;
  return emitCoverageRecordDescriptors(records, os, "Parser");
}

bool emitCoverageSerializer(const RecordKeeper &records, raw_ostream &os) {
  if (emitCoverageDescriptors(records, os, "SerializerSectionDescriptors"))
    return true;
  return emitCoverageRecordDescriptors(records, os, "Serializer");
}

mlir::GenRegistration
    coverageDeclsGen("gen-obelisk-coverage-format-decls",
                     "Generate Obelisk coverage enums and wire constants",
                     emitCoverageDecls);
mlir::GenRegistration
    coverageParserGen("gen-obelisk-coverage-format-parser",
                      "Generate Obelisk coverage parser validation descriptors",
                      emitCoverageParser);
mlir::GenRegistration coverageSerializerGen(
    "gen-obelisk-coverage-format-serializer",
    "Generate Obelisk coverage serializer ordering descriptors",
    emitCoverageSerializer);

} // namespace

int main(int argc, char **argv) { return mlir::MlirTblgenMain(argc, argv); }
