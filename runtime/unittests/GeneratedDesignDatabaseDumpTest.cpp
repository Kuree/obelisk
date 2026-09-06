//===- GeneratedDesignDatabaseDumpTest.cpp - Dump encoded design database -===//

#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using namespace obelisk::reflection;

bool validRange(uint64_t offset, uint64_t count, uint64_t stride,
                uint64_t imageSize) {
  return offset <= imageSize && count <= (imageSize - offset) / stride;
}

bool isRecordOffset(uint64_t offset, uint64_t base, uint64_t count,
                    uint64_t stride) {
  return offset >= base && (offset - base) % stride == 0 &&
         (offset - base) / stride < count;
}

bool stringAt(const uint8_t *image, uint64_t stringOffset, uint64_t stringSize,
              uint64_t offset, std::string &result) {
  if (offset < stringOffset || offset - stringOffset >= stringSize)
    return false;
  const char *begin = reinterpret_cast<const char *>(image + offset);
  const void *end = std::memchr(begin, 0, stringSize - (offset - stringOffset));
  if (!end)
    return false;
  result.assign(begin, static_cast<const char *>(end));
  return true;
}

bool relativeStringAt(const uint8_t *image, uint64_t stringOffset,
                      uint64_t stringSize, uint64_t relative,
                      std::string &result) {
  if (relative > std::numeric_limits<uint64_t>::max() - stringOffset)
    return false;
  return stringAt(image, stringOffset, stringSize, stringOffset + relative,
                  result);
}

std::string hex(uint64_t value) {
  static constexpr char digits[] = "0123456789abcdef";
  if (value == 0)
    return "0";
  std::string result;
  while (value != 0) {
    result.push_back(digits[value & 0xf]);
    value >>= 4;
  }
  return std::string(result.rbegin(), result.rend());
}

} // namespace

TEST(GeneratedDesignDatabase, Dump) {
  const char *inputPath = std::getenv("OBELISK_TEST_INPUT");
  const char *outputPath = std::getenv("OBELISK_TEST_OUTPUT");
  const char *vpiProfile = std::getenv("OBELISK_TEST_VPI");
  ASSERT_NE(inputPath, nullptr) << "OBELISK_TEST_INPUT is required";
  ASSERT_NE(outputPath, nullptr) << "OBELISK_TEST_OUTPUT is required";

  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  mlir::OwningOpRef<mlir::ModuleOp> module =
      mlir::parseSourceFile<mlir::ModuleOp>(inputPath, &context);
  ASSERT_TRUE(module) << "failed to parse " << inputPath;

  llvm::SmallVector<obelisk::sim::SimDesignOp> designs;
  module->walk(
      [&](obelisk::sim::SimDesignOp design) { designs.push_back(design); });
  ASSERT_EQ(designs.size(), 1u);
  obelisk::SimulationBytecodeOptions options;
  options.vpi = vpiProfile ? vpiProfile : "read";
  mlir::FailureOr<obelisk::EncodedSimulationDesign> encoded =
      obelisk::encodeSimulationDesign(designs.front(), options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  ASSERT_FALSE(encoded->designDatabase.empty());

  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      0};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  ASSERT_NE(execution.design_database, nullptr);
  ASSERT_GE(execution.design_database_size,
            static_cast<uint64_t>(HeaderLayout.size));

  const uint8_t *image = execution.design_database;
  const uint64_t imageSize = execution.design_database_size;
  const HeaderView header(image);
  std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
  ASSERT_TRUE(output) << "failed to open " << outputPath;

  ASSERT_TRUE(validRange(header.getScopeOffset(), header.getScopeCount(),
                         ScopeLayout.size, imageSize));
  ASSERT_TRUE(validRange(header.getObjectOffset(), header.getObjectCount(),
                         ObjectLayout.size, imageSize));
  ASSERT_TRUE(validRange(header.getTypeOffset(), header.getTypeCount(),
                         TypeLayout.size, imageSize));
  ASSERT_TRUE(validRange(header.getStringOffset(), header.getStringSize(), 1,
                         imageSize));
  ASSERT_TRUE(validRange(header.getStatementOffset(),
                         header.getStatementCount(), StatementLayout.size,
                         imageSize));
  ASSERT_TRUE(validRange(header.getStatementSiteOffset(),
                         header.getStatementSiteCount(),
                         StatementSiteLayout.size, imageSize));
  ASSERT_TRUE(validRange(header.getRelationOffset(), header.getRelationCount(),
                         RelationLayout.size, imageSize));

  uint64_t semanticTypeOffset = 0, semanticTypeCount = 0;
  uint64_t semanticEdgeOffset = 0, semanticEdgeCount = 0;
  uint64_t semanticRootOffset = 0, semanticRootCount = 0;
  uint64_t relationIndexOffset = 0, relationIndexCount = 0;
  uint64_t relationIndexDimensionOffset = 0, relationIndexDimensionCount = 0;
  uint64_t relationIndexKeyOffset = 0, relationIndexKeyCount = 0;
  uint64_t relationIndexMemberOffset = 0, relationIndexMemberCount = 0;
  uint64_t fixedPropertyOffset = 0, fixedPropertyCount = 0;
  uint64_t resolvedNetRunOffset = 0, resolvedNetRunCount = 0;
  if (header.getReserved() != 0) {
    const uint64_t directoryOffset = header.getReserved();
    ASSERT_TRUE(validRange(directoryOffset, 1, SemanticDirectoryLayout.size,
                           imageSize));
    const SemanticDirectoryView directory(image + directoryOffset);
    semanticTypeOffset = directory.getSemanticTypeOffset();
    semanticTypeCount = directory.getSemanticTypeCount();
    semanticEdgeOffset = directory.getSemanticTypeEdgeOffset();
    semanticEdgeCount = directory.getSemanticTypeEdgeCount();
    semanticRootOffset = directory.getObjectSemanticRootOffset();
    semanticRootCount = directory.getObjectSemanticRootCount();
    relationIndexOffset = directory.getRelationIndexOffset();
    relationIndexCount = directory.getRelationIndexCount();
    relationIndexDimensionOffset = directory.getRelationIndexDimensionOffset();
    relationIndexDimensionCount = directory.getRelationIndexDimensionCount();
    relationIndexKeyOffset = directory.getRelationIndexKeyOffset();
    relationIndexKeyCount = directory.getRelationIndexKeyCount();
    relationIndexMemberOffset = directory.getRelationIndexMemberOffset();
    relationIndexMemberCount = directory.getRelationIndexMemberCount();
    fixedPropertyOffset = directory.getFixedPropertyOffset();
    fixedPropertyCount = directory.getFixedPropertyCount();
    resolvedNetRunOffset = directory.getResolvedNetRunOffset();
    resolvedNetRunCount = directory.getResolvedNetRunCount();
  }

#define ASSERT_SECTION_RANGE(Offset, Count, Layout)                            \
  ASSERT_TRUE(validRange((Offset), (Count), (Layout).size, imageSize))         \
      << "invalid " << (Layout).name << " section"
  ASSERT_SECTION_RANGE(semanticTypeOffset, semanticTypeCount,
                       SemanticTypeLayout);
  ASSERT_SECTION_RANGE(semanticEdgeOffset, semanticEdgeCount,
                       SemanticTypeEdgeLayout);
  ASSERT_SECTION_RANGE(semanticRootOffset, semanticRootCount,
                       ObjectSemanticRootLayout);
  ASSERT_SECTION_RANGE(relationIndexOffset, relationIndexCount,
                       RelationIndexLayout);
  ASSERT_SECTION_RANGE(relationIndexDimensionOffset,
                       relationIndexDimensionCount,
                       RelationIndexDimensionLayout);
  ASSERT_SECTION_RANGE(relationIndexKeyOffset, relationIndexKeyCount,
                       RelationIndexKeyLayout);
  ASSERT_SECTION_RANGE(relationIndexMemberOffset, relationIndexMemberCount,
                       RelationIndexMemberLayout);
  ASSERT_SECTION_RANGE(fixedPropertyOffset, fixedPropertyCount,
                       FixedPropertyLayout);
  ASSERT_SECTION_RANGE(resolvedNetRunOffset, resolvedNetRunCount,
                       ResolvedNetRunLayout);
#undef ASSERT_SECTION_RANGE

  std::unordered_map<uint64_t, std::string> scopeNames;
  std::vector<std::string> scopeIndexNames;
  for (uint64_t index = 0; index != header.getScopeCount(); ++index) {
    const uint64_t offset = header.getScopeOffset() + index * ScopeLayout.size;
    const ScopeView scope(image + offset);
    std::string name;
    ASSERT_TRUE(stringAt(image, header.getStringOffset(),
                         header.getStringSize(), scope.getName(), name));
    scopeNames.emplace(offset, name);
    scopeIndexNames.push_back(name);
    output << "scope name=" << name << " kind="
              << static_cast<uint32_t>(
                     unpackRecordKind(scope.getKindAndVPIKind()))
              << " vpi_kind="
              << unpackRecordKindPayload(scope.getKindAndVPIKind())
              << " caps=0x" << hex(scope.getCaps()) << " id=" << scope.getID()
              << '\n';
  }

  std::vector<std::string> objectIndexNames;
  for (uint64_t index = 0; index != header.getObjectCount(); ++index) {
    const ObjectView object(image + header.getObjectOffset() +
                            index * ObjectLayout.size);
    std::string name;
    ASSERT_TRUE(stringAt(image, header.getStringOffset(),
                         header.getStringSize(), object.getName(), name));
    objectIndexNames.push_back(name);

    uint32_t typeKind = 0, typeFlags = 0;
    uint32_t elementKind = 0, elementFlags = 0;
    uint64_t elementWidth = 0;
    int64_t elementLeft = 0, elementRight = 0;
    uint32_t childKind = 0, childFlags = 0;
    uint64_t childWidth = 0;
    if (object.getType() != 0) {
      ASSERT_TRUE(isRecordOffset(object.getType(), header.getTypeOffset(),
                                 header.getTypeCount(), TypeLayout.size));
      const TypeView type(image + object.getType());
      typeKind = type.getKindAndFlags() & UINT32_C(0xff);
      typeFlags = type.getKindAndFlags() >> 8;
      if (type.getElement() != 0) {
        ASSERT_TRUE(isRecordOffset(type.getElement(), header.getTypeOffset(),
                                   header.getTypeCount(), TypeLayout.size));
        const TypeView element(image + type.getElement());
        elementKind = element.getKindAndFlags() & UINT32_C(0xff);
        elementFlags = element.getKindAndFlags() >> 8;
        elementWidth = element.getWidth();
        elementLeft = element.getLeft();
        elementRight = element.getRight();
        if (element.getElement() != 0) {
          ASSERT_TRUE(isRecordOffset(element.getElement(),
                                     header.getTypeOffset(),
                                     header.getTypeCount(), TypeLayout.size));
          const TypeView child(image + element.getElement());
          childKind = child.getKindAndFlags() & UINT32_C(0xff);
          childFlags = child.getKindAndFlags() >> 8;
          childWidth = child.getWidth();
        }
      }
    }
    auto owner = scopeNames.find(object.getOwner());
    const std::string_view ownerName =
        owner == scopeNames.end() ? std::string_view("?") : owner->second;
    const uint32_t ordinal =
        (object.getCaps() & OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_MASK) >>
        OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_SHIFT;
    output << "object name=" << name << " kind="
              << static_cast<uint32_t>(
                     unpackRecordKind(object.getKindAndVPIKind()))
              << " vpi_kind="
              << unpackRecordKindPayload(object.getKindAndVPIKind())
              << " caps=0x" << hex(object.getCaps()) << " id=" << object.getID()
              << " scope=" << ownerName << " width=" << object.getWidth()
              << " range=[" << object.getLeft() << ':' << object.getRight()
              << "] state=" << object.getStateOffset()
              << " type_kind=" << typeKind << " type_flags=0x" << hex(typeFlags)
              << " port_ordinal=" << ordinal << " element_kind=" << elementKind
              << " element_flags=0x" << hex(elementFlags)
              << " element_width=" << elementWidth << " element_range=["
              << elementLeft << ':' << elementRight
              << "] child_kind=" << childKind << " child_flags=0x"
              << hex(childFlags) << " child_width=" << childWidth << '\n';
  }

  constexpr uint32_t semanticFlagsMask =
      OBELISK_RT_DESIGN_SEMANTIC_SIGNED |
      OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE |
      OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE | OBELISK_RT_DESIGN_SEMANTIC_TAGGED |
      OBELISK_RT_DESIGN_SEMANTIC_SOFT |
      OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX;
  for (uint64_t index = 0; index != semanticTypeCount; ++index) {
    const SemanticTypeView type(image + semanticTypeOffset +
                                index * SemanticTypeLayout.size);
    std::string name, modport;
    if (type.getName() != 0) {
      ASSERT_TRUE(relativeStringAt(image, header.getStringOffset(),
                                   header.getStringSize(), type.getName(),
                                   name));
    }
    if (type.getModport() != 0) {
      ASSERT_TRUE(relativeStringAt(image, header.getStringOffset(),
                                   header.getStringSize(), type.getModport(),
                                   modport));
    }
    output << "semantic_type index=" << index
              << " kind=" << (type.getKindAndFlags() & UINT32_C(0xff))
              << " flags=0x" << hex(type.getKindAndFlags() & semanticFlagsMask)
              << " public_vpi_kind="
              << ((type.getKindAndFlags() &
                   OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
                  OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT)
              << " first_edge=" << type.getFirstEdge()
              << " edge_count=" << type.getEdgeCount()
              << " alias=" << type.getAliasObject()
              << " identity=" << type.getIdentityTarget() << " name=" << name
              << " modport=" << modport
              << " queue_bound=" << type.getQueueBound() << " range=["
              << type.getLeft() << ':' << type.getRight()
              << "] bit_width=" << type.getBitWidth()
              << " tag_bits=" << type.getTagBits() << '\n';
  }

  for (uint64_t index = 0; index != semanticEdgeCount; ++index) {
    const SemanticTypeEdgeView edge(image + semanticEdgeOffset +
                                    index * SemanticTypeEdgeLayout.size);
    std::string name;
    if (edge.getName() != 0) {
      ASSERT_TRUE(relativeStringAt(image, header.getStringOffset(),
                                   header.getStringSize(), edge.getName(),
                                   name));
    }
    output << "semantic_edge index=" << index << " child=" << edge.getChild()
              << " role=" << (edge.getRoleAndFlags() & UINT32_C(0xff))
              << " flags=0x" << hex(edge.getRoleAndFlags() >> 8)
              << " ordinal=" << edge.getOrdinal() << " name=" << name
              << " packed_offset=" << edge.getPackedOffset() << '\n';
  }

  for (uint64_t index = 0; index != semanticRootCount; ++index) {
    ASSERT_LT(index, objectIndexNames.size());
    const ObjectSemanticRootView root(image + semanticRootOffset +
                                      index * ObjectSemanticRootLayout.size);
    output << "semantic_root object=" << index
              << " object_name=" << objectIndexNames[index]
              << " semantic_type=" << root.getSemanticType() << '\n';
  }

  for (uint64_t index = 0; index != relationIndexCount; ++index) {
    const RelationIndexView relationIndex(image + relationIndexOffset +
                                          index * RelationIndexLayout.size);
    ASSERT_LT(relationIndex.getObjectIndex(), objectIndexNames.size());
    output << "relation_index index=" << index
              << " object=" << relationIndex.getObjectIndex() << " object_name="
              << objectIndexNames[relationIndex.getObjectIndex()]
              << " first_dimension=" << relationIndex.getFirstDimension()
              << " dimension_count=" << relationIndex.getDimensionCount()
              << " flags=0x" << hex(relationIndex.getFlags())
              << " first_key=" << relationIndex.getFirstKey()
              << " first_ordinal_key=" << relationIndex.getFirstOrdinalKey()
              << '\n';
  }

  for (uint64_t index = 0; index != relationIndexDimensionCount; ++index) {
    const RelationIndexDimensionView dimension(
        image + relationIndexDimensionOffset +
        index * RelationIndexDimensionLayout.size);
    output << "relation_index_dimension index=" << index << " range=["
              << dimension.getLeft() << ':' << dimension.getRight() << "]\n";
  }

  for (uint64_t index = 0; index != relationIndexKeyCount; ++index) {
    const RelationIndexKeyView key(image + relationIndexKeyOffset +
                                   index * RelationIndexKeyLayout.size);
    output << "relation_index_key index=" << index
              << " value=" << key.getIndex() << " ordinal=" << key.getOrdinal()
              << '\n';
  }

  for (uint64_t index = 0; index != relationIndexMemberCount; ++index) {
    const RelationIndexMemberView member(image + relationIndexMemberOffset +
                                         index *
                                             RelationIndexMemberLayout.size);
    output << "relation_index_member index=" << index << " target_table="
              << static_cast<uint32_t>(
                     unpackTableIndexKind(member.getTargetIndexAndTable()))
              << " target=" << unpackTableIndex(member.getTargetIndexAndTable())
              << " relation_index=" << member.getRelationIndex()
              << " ordinal=" << member.getOrdinal() << '\n';
  }

  std::vector<std::string> statementIndexNames;
  for (uint64_t index = 0; index != header.getStatementCount(); ++index) {
    const StatementView statement(image + header.getStatementOffset() +
                                  index * StatementLayout.size);
    std::string source, name;
    if (statement.getSourceFile() != 0) {
      ASSERT_TRUE(relativeStringAt(image, header.getStringOffset(),
                                   header.getStringSize(),
                                   statement.getSourceFile(), source));
    }
    if (statement.getName() != 0) {
      ASSERT_TRUE(relativeStringAt(image, header.getStringOffset(),
                                   header.getStringSize(), statement.getName(),
                                   name));
    }
    statementIndexNames.push_back(
        name.empty() ? "statement#" + std::to_string(statement.getID()) : name);
    output << "statement id=" << statement.getID()
              << " owner=" << statement.getOwnerObjectIndex()
              << " scope=" << statement.getScopeIndex()
              << " parent=" << statement.getParentIndex()
              << " type=" << statement.getVPIKind() << " flags=0x"
              << hex(statement.getFlags()) << " source=" << source << ':'
              << statement.getSourceLine() << ':' << statement.getSourceColumn()
              << " name=" << name << '\n';
  }

  for (uint64_t index = 0; index != fixedPropertyCount; ++index) {
    const FixedPropertyView property(image + fixedPropertyOffset +
                                     index * FixedPropertyLayout.size);
    std::string value;
    switch (property.getKindAndFlags()) {
    case 0:
      value = property.getPayload() ? "true" : "false";
      break;
    case 1:
    case 2:
      value = std::to_string(static_cast<int64_t>(property.getPayload()));
      break;
    case 3:
      ASSERT_TRUE(stringAt(image, header.getStringOffset(),
                           header.getStringSize(), property.getPayload(),
                           value));
      break;
    default:
      value = "invalid(" + std::to_string(property.getPayload()) + ')';
      break;
    }
    output << "fixed_property source_table="
              << static_cast<uint32_t>(
                     unpackTableIndexKind(property.getSourceIndexAndTable()))
              << " source="
              << unpackTableIndex(property.getSourceIndexAndTable())
              << " selector=" << property.getSelector()
              << " kind=" << property.getKindAndFlags() << " value=" << value
              << '\n';
  }

  for (uint64_t index = 0; index != header.getStatementSiteCount(); ++index) {
    const StatementSiteView site(image + header.getStatementSiteOffset() +
                                 index * StatementSiteLayout.size);
    output << "statement_site id=" << site.getID()
              << " statement=" << site.getStatementIndex()
              << " phase=" << site.getPhase() << " flags=0x"
              << hex(site.getFlags()) << '\n';
  }

  const std::array<const std::vector<std::string> *, 3> tableNames{
      &scopeIndexNames, &objectIndexNames, &statementIndexNames};
  for (uint64_t index = 0; index != header.getRelationCount(); ++index) {
    const RelationView relation(image + header.getRelationOffset() +
                                index * RelationLayout.size);
    const size_t sourceTable = static_cast<size_t>(
        unpackRelationSourceTable(relation.getSourceKindAndTable()));
    const size_t targetTable = static_cast<size_t>(
        unpackTableIndexKind(relation.getTargetIndexAndTable()));
    ASSERT_LT(sourceTable, tableNames.size());
    ASSERT_LT(targetTable, tableNames.size());
    ASSERT_LT(relation.getSourceIndex(), tableNames[sourceTable]->size());
    const uint32_t target = unpackTableIndex(relation.getTargetIndexAndTable());
    ASSERT_LT(target, tableNames[targetTable]->size());
    output << "relation source_table=" << sourceTable
              << " source=" << relation.getSourceIndex() << " source_type="
              << unpackRelationSourceKind(relation.getSourceKindAndTable())
              << " mode="
              << (relationSourceIsIterate(relation.getSourceKindAndTable())
                      ? "iterate"
                      : "handle")
              << " selector=" << relation.getSelector()
              << " ordinal=" << relation.getOrdinal()
              << " target_table=" << targetTable << " target=" << target
              << " source_name="
              << (*tableNames[sourceTable])[relation.getSourceIndex()]
              << " target_name=" << (*tableNames[targetTable])[target] << '\n';
  }
  output.close();
  ASSERT_TRUE(output) << "failed to write " << outputPath;
}
