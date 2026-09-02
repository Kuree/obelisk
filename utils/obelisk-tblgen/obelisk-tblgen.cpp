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
#include <tuple>

using namespace llvm;

namespace {

constexpr int64_t maxU32 = std::numeric_limits<uint32_t>::max();

struct Encoding {
  enum class Kind { Bytes, U32LE, U64LE, I64LE } kind;
  StringRef name;
  uint32_t width;
};

StringRef getCppType(Encoding::Kind encoding) {
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
  else if (name == "U32LE") {
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

  os << "namespace detail {\n"
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
  os << "} // namespace obelisk::reflection\n\n";
  os << "#endif // OBELISK_REFLECTION_DESIGNREFLECTIONLAYOUT_H_INC\n";
  return false;
}

bool validateVPIObjectModel(const RecordKeeper &records) {
  auto families = records.getAllDerivedDefinitions("VPIObjectFamily");
  auto roles = records.getAllDerivedDefinitions("VPIObjectRole");
  auto objects = records.getAllDerivedDefinitions("VPIObjectKind");
  auto relations = records.getAllDerivedDefinitions("VPIRelation");
  if (families.empty() || roles.empty() || objects.empty() ||
      relations.empty()) {
    PrintError(
        "VPI object model needs families, roles, objects, and relations");
    return false;
  }
  if (families.size() > 64) {
    PrintError("VPI object model supports at most 64 families");
    return false;
  }

  StringMap<const Record *> familyNames;
  for (const Record *family : families) {
    StringRef name;
    if (!getCppName(*family, "VPI object family", name))
      return false;
    if (!familyNames.try_emplace(name, family).second) {
      PrintError(family->getLoc(), "duplicate VPI object family name");
      return false;
    }
  }

  StringSet<> supportedRoles{"Concrete", "AbstractSelector", "RelationOnly",
                             "CompatibilitySelector"};
  StringMap<const Record *> roleNames;
  for (const Record *role : roles) {
    StringRef name;
    if (!getCppName(*role, "VPI object role", name))
      return false;
    if (!supportedRoles.contains(name)) {
      PrintError(role->getLoc(), "unsupported VPI object role");
      return false;
    }
    if (!roleNames.try_emplace(name, role).second) {
      PrintError(role->getLoc(), "duplicate VPI object role name");
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
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  DenseMap<const Record *, unsigned> familyBits;
  os << "enum class VPIObjectFamily : uint8_t {\n";
  for (auto [index, family] : llvm::enumerate(families)) {
    familyBits[family] = index;
    os << formatv("  {0} = {1},\n", family->getValueAsString("cppName"), index);
  }
  os << "};\n\n";
  os << "constexpr uint64_t vpiFamilyMask(VPIObjectFamily family) {\n"
        "  return uint64_t{1} << static_cast<unsigned>(family);\n"
        "}\n\n";

  auto roleRecords = records.getAllDerivedDefinitions("VPIObjectRole");
  SmallVector<const Record *> roles(roleRecords.begin(), roleRecords.end());
  llvm::sort(roles, [](const Record *left, const Record *right) {
    return left->getValueAsString("cppName") <
           right->getValueAsString("cppName");
  });
  os << "enum class VPIObjectRole : uint8_t {\n";
  for (const Record *role : roles)
    os << formatv("  {0},\n", role->getValueAsString("cppName"));
  os << "  Alias,\n};\n\n";

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
  os << "struct VPIObjectKindDescriptor {\n"
        "  const char *apiName;\n"
        "  uint32_t value;\n"
        "  uint64_t families;\n"
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
        "  {{\"{0}\", {1}, UINT64_C({2}), VPIObjectRole::{3}, ",
        object->getValueAsString("apiName"), object->getValueAsInt("value"),
        mask,
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

  os << "inline constexpr const VPIObjectKindDescriptor *\n"
        "findVPIObjectKind(uint32_t value) {\n"
        "  const auto *kind = findVPIObjectSelector(value);\n"
        "  return kind && kind->role == VPIObjectRole::Concrete ? kind\n"
        "                                                        : nullptr;\n"
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

} // namespace

int main(int argc, char **argv) { return mlir::MlirTblgenMain(argc, argv); }
