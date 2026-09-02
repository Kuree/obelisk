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
  auto objectSets = records.getAllDerivedDefinitions("VPIObjectSet");
  auto traversalModes = records.getAllDerivedDefinitions("VPITraversalMode");
  auto traversalOrders = records.getAllDerivedDefinitions("VPITraversalOrder");
  auto traversalEdges = records.getAllDerivedDefinitions("VPITraversalEdge");
  if (families.empty() || roles.empty() || objects.empty() ||
      relations.empty() || objectSets.empty() || traversalModes.empty() ||
      traversalOrders.empty() || traversalEdges.empty()) {
    PrintError("VPI object model needs families, roles, objects, relations, "
               "object sets, traversal modes, orders, and edges");
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
  if (!validateEnum(traversalModes, supportedModes, "VPI traversal mode") ||
      !validateEnum(traversalOrders, supportedOrders, "VPI traversal order"))
    return false;

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
    } else if (expanded.empty() && !set->getValueAsBit("nullRoot")) {
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

  StringMap<const Record *> edgeKeys;
  for (const Record *edge : traversalEdges) {
    const Record *sources = edge->getValueAsDef("sources");
    const Record *targets = edge->getValueAsDef("targets");
    const Record *selector = edge->getValueAsDef("selector");
    const Record *mode = edge->getValueAsDef("mode");
    const Record *order = edge->getValueAsDef("order");
    StringRef clause = edge->getValueAsString("clause");
    if (targets->getValueAsBit("nullRoot") || clause.empty()) {
      PrintError(edge->getLoc(),
                 "VPI traversal needs non-root targets and an LRM clause");
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

  struct EmittedTraversalEdge {
    uint32_t source;
    uint32_t selector;
    const Record *mode;
    const Record *order;
    const Record *targets;
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
        "  const char *selectorName;\n"
        "  const char *clause;\n"
        "};\n\n";
  os << "inline constexpr VPITraversalDescriptor vpiTraversals[] = {\n";
  for (const EmittedTraversalEdge &edge : emittedEdges) {
    os << formatv(
        "  {{{0}, {1}, VPITraversalMode::{2}, VPITraversalOrder::{3}, "
        "VPIObjectSetID::{4}, \"{5}\", \"{6}\"",
        edge.source, edge.selector, edge.mode->getValueAsString("cppName"),
        edge.order->getValueAsString("cppName"),
        edge.targets->getValueAsString("cppName"), edge.selectorName,
        edge.clause);
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
