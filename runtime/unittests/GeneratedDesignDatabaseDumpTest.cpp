//===- GeneratedDesignDatabaseDumpTest.cpp - Dump encoded design database -===//

#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"

#include "../lib/RuntimeInternal.h"

#include "gtest/gtest.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"

#include "sv_vpi_user.h"
#include "vpi_user.h"

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
  uint64_t semanticRootBindingOffset = 0, semanticRootBindingCount = 0;
  uint64_t relationIndexOffset = 0, relationIndexCount = 0;
  uint64_t relationIndexDimensionOffset = 0, relationIndexDimensionCount = 0;
  uint64_t relationIndexKeyOffset = 0, relationIndexKeyCount = 0;
  uint64_t relationIndexMemberOffset = 0, relationIndexMemberCount = 0;
  uint64_t fixedPropertyOffset = 0, fixedPropertyCount = 0;
  uint64_t resolvedNetRunOffset = 0, resolvedNetRunCount = 0;
  uint64_t netDelayRunOffset = 0, netDelayRunCount = 0;
  uint64_t staticObjectOffset = 0, staticObjectCount = 0;
  uint64_t definitionOffset = 0, definitionCount = 0;
  uint64_t definitionBindingOffset = 0, definitionBindingCount = 0;
  if (header.getReserved() != 0) {
    const uint64_t directoryOffset = header.getReserved();
    ASSERT_TRUE(validRange(directoryOffset, 1, SemanticDirectoryLayout.size,
                           imageSize));
    const SemanticDirectoryView directory(image + directoryOffset);
    semanticTypeOffset = directory.getSemanticTypeOffset();
    semanticTypeCount = directory.getSemanticTypeCount();
    semanticEdgeOffset = directory.getSemanticTypeEdgeOffset();
    semanticEdgeCount = directory.getSemanticTypeEdgeCount();
    semanticRootBindingOffset = directory.getSemanticRootBindingOffset();
    semanticRootBindingCount = directory.getSemanticRootBindingCount();
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
    netDelayRunOffset = directory.getNetDelayRunOffset();
    netDelayRunCount = directory.getNetDelayRunCount();
    staticObjectOffset = directory.getStaticObjectOffset();
    staticObjectCount = directory.getStaticObjectCount();
    definitionOffset = directory.getDefinitionOffset();
    definitionCount = directory.getDefinitionCount();
    definitionBindingOffset = directory.getDefinitionBindingOffset();
    definitionBindingCount = directory.getDefinitionBindingCount();
  }

#define ASSERT_SECTION_RANGE(Offset, Count, Layout)                            \
  ASSERT_TRUE(validRange((Offset), (Count), (Layout).size, imageSize))         \
      << "invalid " << (Layout).name << " section"
  ASSERT_SECTION_RANGE(semanticTypeOffset, semanticTypeCount,
                       SemanticTypeLayout);
  ASSERT_SECTION_RANGE(semanticEdgeOffset, semanticEdgeCount,
                       SemanticTypeEdgeLayout);
  ASSERT_SECTION_RANGE(semanticRootBindingOffset, semanticRootBindingCount,
                       SemanticRootBindingLayout);
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
  ASSERT_SECTION_RANGE(netDelayRunOffset, netDelayRunCount, NetDelayRunLayout);
  ASSERT_SECTION_RANGE(staticObjectOffset, staticObjectCount,
                       StaticObjectLayout);
  ASSERT_SECTION_RANGE(definitionOffset, definitionCount, DefinitionLayout);
  ASSERT_SECTION_RANGE(definitionBindingOffset, definitionBindingCount,
                       DefinitionBindingLayout);
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
           << static_cast<uint32_t>(unpackRecordKind(scope.getKindAndVPIKind()))
           << " vpi_kind=" << unpackRecordKindPayload(scope.getKindAndVPIKind())
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
           << unpackRecordKindPayload(object.getKindAndVPIKind()) << " caps=0x"
           << hex(object.getCaps()) << " id=" << object.getID()
           << " scope=" << ownerName << " width=" << object.getWidth()
           << " range=[" << object.getLeft() << ':' << object.getRight()
           << "] state=" << object.getStateOffset() << " type_kind=" << typeKind
           << " type_flags=0x" << hex(typeFlags) << " port_ordinal=" << ordinal
           << " element_kind=" << elementKind << " element_flags=0x"
           << hex(elementFlags) << " element_width=" << elementWidth
           << " element_range=[" << elementLeft << ':' << elementRight
           << "] child_kind=" << childKind << " child_flags=0x"
           << hex(childFlags) << " child_width=" << childWidth << '\n';
  }

  std::vector<std::string> staticObjectIndexNames;
  for (uint64_t index = 0; index != staticObjectCount; ++index) {
    const StaticObjectView object(image + staticObjectOffset +
                                  index * StaticObjectLayout.size);
    std::string name;
    if (object.getName() != 0) {
      ASSERT_TRUE(relativeStringAt(image, header.getStringOffset(),
                                   header.getStringSize(), object.getName(),
                                   name));
    } else {
      name = "static#" + std::to_string(object.getID());
    }
    staticObjectIndexNames.push_back(name);
    ASSERT_LT(object.getScopeIndex(), scopeIndexNames.size());
    output << "static_object name=" << name
           << " vpi_kind=" << object.getVPIKind() << " id=" << object.getID()
           << " scope=" << scopeIndexNames[object.getScopeIndex()] << '\n';
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
           << " alias=" << type.getAliasObjectIndexAndTable()
           << " identity=" << type.getIdentityTargetIndexAndTable()
           << " name=" << name << " modport=" << modport
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

  for (uint64_t index = 0; index != semanticRootBindingCount; ++index) {
    const SemanticRootBindingView root(image + semanticRootBindingOffset +
                                       index * SemanticRootBindingLayout.size);
    const uint32_t packed = root.getObjectIndexAndTable();
    const size_t table = static_cast<size_t>(unpackTableIndexKind(packed));
    const uint32_t object = unpackTableIndex(packed);
    ASSERT_TRUE(table == static_cast<size_t>(TableKind::Object) ||
                table == static_cast<size_t>(TableKind::StaticObject));
    const std::vector<std::string> &names =
        table == static_cast<size_t>(TableKind::Object)
            ? objectIndexNames
            : staticObjectIndexNames;
    ASSERT_LT(object, names.size());
    output << "semantic_root source_table=" << table << " object=" << object
           << " object_name=" << names[object]
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
           << " source=" << unpackTableIndex(property.getSourceIndexAndTable())
           << " selector=" << property.getSelector()
           << " kind=" << property.getKindAndFlags() << " value=" << value
           << '\n';
  }

  for (uint64_t index = 0; index != definitionCount; ++index) {
    const DefinitionView definition(image + definitionOffset +
                                    index * DefinitionLayout.size);
    std::string name, file;
    ASSERT_TRUE(relativeStringAt(image, header.getStringOffset(),
                                 header.getStringSize(), definition.getName(),
                                 name));
    if (definition.getFile() != 0) {
      ASSERT_TRUE(relativeStringAt(image, header.getStringOffset(),
                                   header.getStringSize(), definition.getFile(),
                                   file));
    }
    output << "definition index=" << index
           << " vpi_kind=" << definition.getVPIKind() << " name=" << name
           << " source=" << file << ':' << definition.getLine() << '\n';
  }
  for (uint64_t index = 0; index != definitionBindingCount; ++index) {
    const DefinitionBindingView binding(image + definitionBindingOffset +
                                        index * DefinitionBindingLayout.size);
    output << "definition_binding source_table="
           << static_cast<uint32_t>(
                  unpackTableIndexKind(binding.getSourceIndexAndTable()))
           << " source=" << unpackTableIndex(binding.getSourceIndexAndTable())
           << " definition=" << binding.getDefinition() << '\n';
  }

  for (uint64_t index = 0; index != netDelayRunCount; ++index) {
    const NetDelayRunView run(image + netDelayRunOffset +
                              index * NetDelayRunLayout.size);
    ASSERT_LT(run.getObjectIndex(), objectIndexNames.size());
    output << "net_delay_run object=" << run.getObjectIndex()
           << " object_name=" << objectIndexNames[run.getObjectIndex()]
           << " bits=[" << run.getFirstBit() << ':'
           << run.getFirstBit() + run.getBitCount() << ") delays=["
           << run.getRise() << ',' << run.getFall() << ',' << run.getThird()
           << "]\n";
  }

  for (uint64_t index = 0; index != header.getStatementSiteCount(); ++index) {
    const StatementSiteView site(image + header.getStatementSiteOffset() +
                                 index * StatementSiteLayout.size);
    output << "statement_site id=" << site.getID()
           << " statement=" << site.getStatementIndex()
           << " phase=" << site.getPhase() << " flags=0x"
           << hex(site.getFlags()) << '\n';
  }

  const std::array<const std::vector<std::string> *, 4> tableNames{
      &scopeIndexNames, &objectIndexNames, &statementIndexNames,
      &staticObjectIndexNames};
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

TEST(GeneratedDesignDatabase, CompactStaticQueries) {
  const char *inputPath = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(inputPath, nullptr) << "OBELISK_TEST_INPUT is required";

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
  options.vpi = "read";
  mlir::FailureOr<obelisk::EncodedSimulationDesign> encoded =
      obelisk::encodeSimulationDesign(designs.front(), options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  ASSERT_GE(encoded->bytecode.size(), 40u);
  uint64_t bytecodeChecksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    bytecodeChecksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      bytecodeChecksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);

  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);

  PLI_BYTE8 packageName[] = "pkg";
  PLI_BYTE8 topName[] = "top";
  PLI_BYTE8 clockingName[] = "cb";
  PLI_BYTE8 propertyName[] = "prop";
  PLI_BYTE8 sequenceName[] = "seq";
  PLI_BYTE8 generateName[] = "g";
  PLI_BYTE8 gateName[] = "gate";
  vpiHandle package = vpi_handle_by_name(packageName, nullptr);
  vpiHandle top = vpi_handle_by_name(topName, nullptr);
  ASSERT_NE(package, nullptr);
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(vpi_get(vpiType, package), vpiPackage);
  EXPECT_STREQ(vpi_get_str(vpiName, package), "pkg");
  EXPECT_STREQ(vpi_get_str(vpiFullName, package), "pkg::");
  EXPECT_STREQ(vpi_get_str(vpiFile, package), "compact_static.sv");
  EXPECT_EQ(vpi_get(vpiLineNo, package), 3);

  vpiHandle clocking = vpi_handle_by_name(clockingName, top);
  vpiHandle generated = vpi_handle_by_name(generateName, top);
  vpiHandle gate = vpi_handle_by_name(gateName, top);
  ASSERT_NE(clocking, nullptr);
  ASSERT_NE(generated, nullptr);
  ASSERT_NE(gate, nullptr);
  EXPECT_EQ(vpi_get(vpiType, clocking), vpiClockingBlock);
  EXPECT_EQ(vpi_get(vpiType, generated), vpiGenScope);
  EXPECT_EQ(vpi_get(vpiIsProtected, generated), 1);
  EXPECT_EQ(vpi_get(vpiType, gate), vpiGate);
  EXPECT_EQ(vpi_get(vpiSize, gate), 2);
  vpiHandle property = vpi_handle_by_name(propertyName, clocking);
  vpiHandle sequence = vpi_handle_by_name(sequenceName, clocking);
  ASSERT_NE(property, nullptr);
  ASSERT_NE(sequence, nullptr);
  EXPECT_EQ(vpi_get(vpiType, property), vpiPropertyDecl);
  EXPECT_EQ(vpi_get(vpiType, sequence), vpiSequenceDecl);

  vpiHandle properties = vpi_iterate(vpiPropertyDecl, clocking);
  vpiHandle sequences = vpi_iterate(vpiSequenceDecl, clocking);
  ASSERT_NE(properties, nullptr);
  ASSERT_NE(sequences, nullptr);
  vpiHandle traversedProperty = vpi_scan(properties);
  vpiHandle traversedSequence = vpi_scan(sequences);
  ASSERT_NE(traversedProperty, nullptr);
  ASSERT_NE(traversedSequence, nullptr);
  EXPECT_EQ(vpi_compare_objects(property, traversedProperty), 1);
  EXPECT_EQ(vpi_compare_objects(sequence, traversedSequence), 1);
  EXPECT_EQ(vpi_scan(properties), nullptr);
  EXPECT_EQ(vpi_scan(sequences), nullptr);

  vpiHandle propertyScope = vpi_handle(vpiScope, property);
  vpiHandle sequenceScope = vpi_handle(vpiScope, sequence);
  vpiHandle clockingScope = vpi_handle(vpiScope, clocking);
  vpiHandle generatedScope = vpi_handle(vpiScope, generated);
  ASSERT_NE(propertyScope, nullptr);
  ASSERT_NE(sequenceScope, nullptr);
  ASSERT_NE(clockingScope, nullptr);
  EXPECT_EQ(generatedScope, nullptr);
  EXPECT_EQ(vpi_compare_objects(clocking, propertyScope), 1);
  EXPECT_EQ(vpi_compare_objects(clocking, sequenceScope), 1);
  EXPECT_EQ(vpi_compare_objects(top, clockingScope), 1);

  EXPECT_EQ(vpi_release_handle(clockingScope), 1);
  EXPECT_EQ(vpi_release_handle(sequenceScope), 1);
  EXPECT_EQ(vpi_release_handle(propertyScope), 1);
  EXPECT_EQ(vpi_release_handle(traversedSequence), 1);
  EXPECT_EQ(vpi_release_handle(traversedProperty), 1);
  EXPECT_EQ(vpi_release_handle(sequence), 1);
  EXPECT_EQ(vpi_release_handle(property), 1);
  EXPECT_EQ(vpi_release_handle(gate), 1);
  EXPECT_EQ(vpi_release_handle(generated), 1);
  EXPECT_EQ(vpi_release_handle(clocking), 1);
  EXPECT_EQ(vpi_release_handle(top), 1);
  EXPECT_EQ(vpi_release_handle(package), 1);
  obelisk_rt_v1_context_destroy(runtime);
}

TEST(GeneratedDesignDatabase, InterModPathQueries) {
  const char *inputPath = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(inputPath, nullptr) << "OBELISK_TEST_INPUT is required";

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
  options.vpi = "read";
  mlir::FailureOr<obelisk::EncodedSimulationDesign> encoded =
      obelisk::encodeSimulationDesign(designs.front(), options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  ASSERT_GE(encoded->bytecode.size(), 40u);
  uint64_t bytecodeChecksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    bytecodeChecksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      bytecodeChecksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);

  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);

  PLI_BYTE8 outputName[] = "top.output";
  PLI_BYTE8 inputName[] = "top.child.input";
  PLI_BYTE8 otherName[] = "top.other";
  PLI_BYTE8 inoutName[] = "top.inout";
  vpiHandle output = vpi_handle_by_name(outputName, nullptr);
  vpiHandle input = vpi_handle_by_name(inputName, nullptr);
  vpiHandle other = vpi_handle_by_name(otherName, nullptr);
  vpiHandle inout = vpi_handle_by_name(inoutName, nullptr);
  vpiHandle sameInout = vpi_handle_by_name(inoutName, nullptr);
  ASSERT_NE(output, nullptr);
  ASSERT_NE(input, nullptr);
  ASSERT_NE(other, nullptr);
  ASSERT_NE(inout, nullptr);
  ASSERT_NE(sameInout, nullptr);

  vpiHandle path = vpi_handle_multi(vpiInterModPath, output, input);
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(vpi_get(vpiType, path), vpiInterModPath);
  EXPECT_STREQ(vpi_get_str(vpiType, path), "vpiInterModPath");
  EXPECT_EQ(vpi_get(vpiIsProtected, path), 0);
  EXPECT_EQ(vpi_handle_multi(vpiInterModPath, input, output), nullptr);
  vpiHandle otherPath = vpi_handle_multi(vpiInterModPath, output, other);
  ASSERT_NE(otherPath, nullptr);
  EXPECT_EQ(vpi_handle_multi(vpiInterModPath, inout, sameInout), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);

  vpiHandle outputBit = vpi_handle_by_index(output, 0);
  vpiHandle inputBit = vpi_handle_by_index(input, 0);
  ASSERT_NE(outputBit, nullptr);
  ASSERT_NE(inputBit, nullptr);
  ASSERT_EQ(vpi_get(vpiType, outputBit), vpiPortBit);
  ASSERT_EQ(vpi_get(vpiType, inputBit), vpiPortBit);
  vpiHandle bitPath = vpi_handle_multi(vpiInterModPath, outputBit, inputBit);
  ASSERT_NE(bitPath, nullptr);
  vpiHandle bitPorts = vpi_iterate(vpiPorts, bitPath);
  ASSERT_NE(bitPorts, nullptr);
  vpiHandle bitUse = vpi_handle(vpiUse, bitPorts);
  ASSERT_NE(bitUse, nullptr);
  EXPECT_EQ(vpi_compare_objects(bitPath, bitUse), 1);
  vpiHandle scannedOutputBit = vpi_scan(bitPorts);
  vpiHandle scannedInputBit = vpi_scan(bitPorts);
  ASSERT_NE(scannedOutputBit, nullptr);
  ASSERT_NE(scannedInputBit, nullptr);
  EXPECT_EQ(vpi_compare_objects(outputBit, scannedOutputBit), 1);
  EXPECT_EQ(vpi_compare_objects(inputBit, scannedInputBit), 1);
  EXPECT_EQ(vpi_scan(bitPorts), nullptr);

  vpiHandle equivalent = vpi_handle_multi(vpiInterModPath, output, input);
  ASSERT_NE(equivalent, nullptr);
  EXPECT_EQ(vpi_compare_objects(path, equivalent), 1);

  vpiHandle ports = vpi_iterate(vpiPorts, path);
  ASSERT_NE(ports, nullptr);
  EXPECT_EQ(vpi_get(vpiIteratorType, ports), vpiPorts);
  vpiHandle use = vpi_handle(vpiUse, ports);
  ASSERT_NE(use, nullptr);
  EXPECT_EQ(vpi_compare_objects(path, use), 1);
  vpiHandle scannedOutput = vpi_scan(ports);
  vpiHandle scannedInput = vpi_scan(ports);
  ASSERT_NE(scannedOutput, nullptr);
  ASSERT_NE(scannedInput, nullptr);
  EXPECT_EQ(vpi_scan(ports), nullptr);
  EXPECT_EQ(vpi_compare_objects(output, scannedOutput), 1);
  EXPECT_EQ(vpi_compare_objects(input, scannedInput), 1);

  std::array<s_vpi_time, 2> delayValues{};
  s_vpi_delay delays{};
  delays.da = delayValues.data();
  delays.no_of_delays = 2;
  delays.time_type = vpiSimTime;
  vpi_get_delays(path, &delays);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  for (const s_vpi_time &delay : delayValues) {
    EXPECT_EQ(delay.type, vpiSimTime);
    EXPECT_EQ(delay.high, 0u);
    EXPECT_EQ(delay.low, 0u);
  }
  delays.no_of_delays = 1;
  vpi_get_delays(path, &delays);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);

  std::array<s_vpi_time, 27> expandedDelays{};
  for (s_vpi_time &delay : expandedDelays) {
    delay.type = -1;
    delay.high = 1;
    delay.low = 1;
    delay.real = 1.0;
  }
  delays.da = expandedDelays.data();
  delays.no_of_delays = 3;
  delays.time_type = vpiSuppressTime;
  delays.mtm_flag = 1;
  delays.pulsere_flag = 1;
  vpi_get_delays(path, &delays);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  for (const s_vpi_time &delay : expandedDelays) {
    EXPECT_EQ(delay.type, vpiSuppressTime);
    EXPECT_EQ(delay.high, 0u);
    EXPECT_EQ(delay.low, 0u);
    EXPECT_DOUBLE_EQ(delay.real, 0.0);
  }

  EXPECT_EQ(vpi_release_handle(scannedInput), 1);
  EXPECT_EQ(vpi_release_handle(scannedOutput), 1);
  EXPECT_EQ(vpi_release_handle(use), 1);
  EXPECT_EQ(vpi_release_handle(equivalent), 1);
  EXPECT_EQ(vpi_release_handle(path), 1);
  EXPECT_EQ(vpi_release_handle(scannedInputBit), 1);
  EXPECT_EQ(vpi_release_handle(scannedOutputBit), 1);
  EXPECT_EQ(vpi_release_handle(bitUse), 1);
  EXPECT_EQ(vpi_release_handle(bitPath), 1);
  EXPECT_EQ(vpi_release_handle(inputBit), 1);
  EXPECT_EQ(vpi_release_handle(outputBit), 1);
  EXPECT_EQ(vpi_release_handle(otherPath), 1);
  EXPECT_EQ(vpi_release_handle(sameInout), 1);
  EXPECT_EQ(vpi_release_handle(inout), 1);
  EXPECT_EQ(vpi_release_handle(other), 1);
  EXPECT_EQ(vpi_release_handle(input), 1);
  EXPECT_EQ(vpi_release_handle(output), 1);
  obelisk_rt_v1_context_destroy(runtime);
}

TEST(GeneratedDesignDatabase, ScopeOwnedStatementQueries) {
  const char *inputPath = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(inputPath, nullptr) << "OBELISK_TEST_INPUT is required";

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
  options.vpi = "read";
  mlir::FailureOr<obelisk::EncodedSimulationDesign> encoded =
      obelisk::encodeSimulationDesign(designs.front(), options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  ASSERT_GE(encoded->bytecode.size(), 40u);
  uint64_t bytecodeChecksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    bytecodeChecksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      bytecodeChecksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);

  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);

  PLI_BYTE8 topName[] = "top";
  PLI_BYTE8 generateName[] = "top.g";
  PLI_BYTE8 interfaceName[] = "iface";
  PLI_BYTE8 programName[] = "prog";
  vpiHandle top = vpi_handle_by_name(topName, nullptr);
  vpiHandle generated = vpi_handle_by_name(generateName, nullptr);
  vpiHandle interface = vpi_handle_by_name(interfaceName, nullptr);
  vpiHandle program = vpi_handle_by_name(programName, nullptr);
  ASSERT_NE(top, nullptr);
  ASSERT_NE(generated, nullptr);
  ASSERT_NE(interface, nullptr);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(vpi_get(vpiType, top), vpiModule);
  EXPECT_EQ(vpi_get(vpiType, generated), vpiGenScope);
  EXPECT_EQ(vpi_get(vpiType, interface), vpiInterface);
  EXPECT_EQ(vpi_get(vpiType, program), vpiProgram);

  vpiHandle topAssignments = vpi_iterate(vpiContAssign, top);
  ASSERT_NE(topAssignments, nullptr);
  vpiHandle directAssignment = vpi_scan(topAssignments);
  ASSERT_NE(directAssignment, nullptr);
  EXPECT_EQ(vpi_get(vpiType, directAssignment), vpiContAssign);
  EXPECT_STREQ(vpi_get_str(vpiFile, directAssignment), "scope_owned.sv");
  EXPECT_EQ(vpi_get(vpiLineNo, directAssignment), 10);
  vpiHandle vectorAssignment = vpi_scan(topAssignments);
  ASSERT_NE(vectorAssignment, nullptr);
  EXPECT_EQ(vpi_get(vpiType, vectorAssignment), vpiContAssign);
  EXPECT_EQ(vpi_get(vpiLineNo, vectorAssignment), 12);
  vpiHandle variableAssignment = vpi_scan(topAssignments);
  ASSERT_NE(variableAssignment, nullptr);
  EXPECT_EQ(vpi_get(vpiType, variableAssignment), vpiContAssign);
  EXPECT_EQ(vpi_get(vpiLineNo, variableAssignment), 13);
  EXPECT_EQ(vpi_scan(topAssignments), nullptr);

  PLI_BYTE8 lhsName[] = "top.direct_lhs";
  PLI_BYTE8 rhsName[] = "top.d";
  vpiHandle expectedLhs = vpi_handle_by_name(lhsName, nullptr);
  vpiHandle expectedRhs = vpi_handle_by_name(rhsName, nullptr);
  vpiHandle assignmentLhs = vpi_handle(vpiLhs, directAssignment);
  vpiHandle assignmentRhs = vpi_handle(vpiRhs, directAssignment);
  ASSERT_NE(expectedLhs, nullptr);
  ASSERT_NE(expectedRhs, nullptr);
  ASSERT_NE(assignmentLhs, nullptr);
  ASSERT_NE(assignmentRhs, nullptr);
  EXPECT_EQ(vpi_compare_objects(expectedLhs, assignmentLhs), 1);
  EXPECT_EQ(vpi_compare_objects(expectedRhs, assignmentRhs), 1);
  vpiHandle directSimNet = vpi_handle(vpiSimNet, assignmentLhs);
  ASSERT_NE(directSimNet, nullptr);
  EXPECT_EQ(vpi_compare_objects(assignmentLhs, directSimNet), 1);

  auto expectOnlyAssignment = [&](PLI_INT32 relation, vpiHandle source,
                                  vpiHandle expected) {
    vpiHandle iterator = vpi_iterate(relation, source);
    ASSERT_NE(iterator, nullptr) << "missing relation " << relation;
    vpiHandle target = vpi_scan(iterator);
    ASSERT_NE(target, nullptr) << "empty relation " << relation;
    EXPECT_EQ(vpi_compare_objects(expected, target), 1);
    EXPECT_EQ(vpi_scan(iterator), nullptr);
    EXPECT_EQ(vpi_release_handle(target), 1);
  };
  expectOnlyAssignment(vpiContAssign, assignmentLhs, directAssignment);
  expectOnlyAssignment(vpiDriver, assignmentLhs, directAssignment);
  expectOnlyAssignment(vpiLocalDriver, assignmentLhs, directAssignment);
  expectOnlyAssignment(vpiUse, assignmentLhs, directAssignment);
  expectOnlyAssignment(vpiLoad, assignmentRhs, directAssignment);
  expectOnlyAssignment(vpiLocalLoad, assignmentRhs, directAssignment);
  expectOnlyAssignment(vpiUse, assignmentRhs, directAssignment);

  PLI_BYTE8 vectorLhsName[] = "top.vector_lhs";
  PLI_BYTE8 vectorRhsName[] = "top.vector_rhs";
  vpiHandle expectedVectorLhs = vpi_handle_by_name(vectorLhsName, nullptr);
  vpiHandle expectedVectorRhs = vpi_handle_by_name(vectorRhsName, nullptr);
  vpiHandle vectorLhs = vpi_handle(vpiLhs, vectorAssignment);
  vpiHandle vectorRhs = vpi_handle(vpiRhs, vectorAssignment);
  ASSERT_NE(expectedVectorLhs, nullptr);
  ASSERT_NE(expectedVectorRhs, nullptr);
  ASSERT_NE(vectorLhs, nullptr);
  ASSERT_NE(vectorRhs, nullptr);
  EXPECT_EQ(vpi_compare_objects(expectedVectorLhs, vectorLhs), 1);
  EXPECT_EQ(vpi_compare_objects(expectedVectorRhs, vectorRhs), 1);
  // IEEE 1800-2023 37.16 limits vpiContAssign iteration to scalar nets and
  // bit-selects, while whole-vector driver/load iteration returns the driving
  // or loading assignment exactly once.
  EXPECT_EQ(vpi_iterate(vpiContAssign, vectorLhs), nullptr);
  expectOnlyAssignment(vpiDriver, vectorLhs, vectorAssignment);
  expectOnlyAssignment(vpiLocalDriver, vectorLhs, vectorAssignment);
  expectOnlyAssignment(vpiUse, vectorLhs, vectorAssignment);
  expectOnlyAssignment(vpiLoad, vectorRhs, vectorAssignment);
  expectOnlyAssignment(vpiLocalLoad, vectorRhs, vectorAssignment);
  expectOnlyAssignment(vpiUse, vectorRhs, vectorAssignment);

  PLI_BYTE8 variableLhsName[] = "top.variable_lhs";
  PLI_BYTE8 variableRhsName[] = "top.variable_rhs";
  vpiHandle expectedVariableLhs = vpi_handle_by_name(variableLhsName, nullptr);
  vpiHandle expectedVariableRhs = vpi_handle_by_name(variableRhsName, nullptr);
  vpiHandle variableLhs = vpi_handle(vpiLhs, variableAssignment);
  vpiHandle variableRhs = vpi_handle(vpiRhs, variableAssignment);
  ASSERT_NE(expectedVariableLhs, nullptr);
  ASSERT_NE(expectedVariableRhs, nullptr);
  ASSERT_NE(variableLhs, nullptr);
  ASSERT_NE(variableRhs, nullptr);
  EXPECT_EQ(vpi_compare_objects(expectedVariableLhs, variableLhs), 1);
  EXPECT_EQ(vpi_compare_objects(expectedVariableRhs, variableRhs), 1);
  expectOnlyAssignment(vpiContAssign, variableLhs, variableAssignment);
  expectOnlyAssignment(vpiDriver, variableLhs, variableAssignment);
  EXPECT_EQ(vpi_iterate(vpiLocalDriver, variableLhs), nullptr);
  expectOnlyAssignment(vpiUse, variableLhs, variableAssignment);
  expectOnlyAssignment(vpiLoad, variableRhs, variableAssignment);
  EXPECT_EQ(vpi_iterate(vpiLocalLoad, variableRhs), nullptr);
  expectOnlyAssignment(vpiUse, variableRhs, variableAssignment);

  vpiHandle topAliases = vpi_iterate(vpiAliasStmt, top);
  ASSERT_NE(topAliases, nullptr);
  std::array<vpiHandle, 3> directAliases{};
  std::unordered_map<std::string, unsigned> directAliasPairs;
  for (vpiHandle &alias : directAliases) {
    alias = vpi_scan(topAliases);
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(vpi_get(vpiType, alias), vpiAliasStmt);
    EXPECT_STREQ(vpi_get_str(vpiFile, alias), "scope_owned.sv");
    EXPECT_EQ(vpi_get(vpiLineNo, alias), 11);
    vpiHandle lhs = vpi_handle(vpiLhs, alias);
    vpiHandle rhs = vpi_handle(vpiRhs, alias);
    ASSERT_NE(lhs, nullptr);
    ASSERT_NE(rhs, nullptr);
    const char *lhsText = vpi_get_str(vpiFullName, lhs);
    ASSERT_NE(lhsText, nullptr);
    std::string lhsName(lhsText);
    const char *rhsText = vpi_get_str(vpiFullName, rhs);
    ASSERT_NE(rhsText, nullptr);
    ++directAliasPairs[lhsName + "=" + rhsText];
    EXPECT_EQ(vpi_release_handle(rhs), 1);
    EXPECT_EQ(vpi_release_handle(lhs), 1);
  }
  EXPECT_EQ(vpi_scan(topAliases), nullptr);
  EXPECT_EQ(directAliasPairs["top.a=top.d"], 1u);
  EXPECT_EQ(directAliasPairs["top.b=top.d"], 1u);
  EXPECT_EQ(directAliasPairs["top.c=top.d"], 1u);

  PLI_BYTE8 aliasAName[] = "top.a";
  PLI_BYTE8 aliasBName[] = "top.b";
  PLI_BYTE8 aliasCName[] = "top.c";
  PLI_BYTE8 aliasDName[] = "top.d";
  std::array<vpiHandle, 4> declaredAliases{
      vpi_handle_by_name(aliasAName, nullptr),
      vpi_handle_by_name(aliasBName, nullptr),
      vpi_handle_by_name(aliasCName, nullptr),
      vpi_handle_by_name(aliasDName, nullptr)};
  for (vpiHandle declared : declaredAliases)
    ASSERT_NE(declared, nullptr);
  for (size_t left = 0; left != declaredAliases.size(); ++left)
    for (size_t right = left + 1; right != declaredAliases.size(); ++right)
      EXPECT_EQ(
          vpi_compare_objects(declaredAliases[left], declaredAliases[right]),
          0);
  std::array<vpiHandle, 4> simulatedAliases{};
  for (size_t index = 0; index != declaredAliases.size(); ++index) {
    EXPECT_EQ(vpi_get(vpiResolvedNetType, declaredAliases[index]), vpiWire);
    simulatedAliases[index] = vpi_handle(vpiSimNet, declaredAliases[index]);
    ASSERT_NE(simulatedAliases[index], nullptr);
    EXPECT_EQ(
        vpi_compare_objects(declaredAliases.front(), simulatedAliases[index]),
        1);
    expectOnlyAssignment(vpiLoad, declaredAliases[index], directAssignment);
    expectOnlyAssignment(vpiLocalLoad, declaredAliases[index],
                         directAssignment);
    if (index + 1 != declaredAliases.size()) {
      EXPECT_EQ(vpi_iterate(vpiUse, declaredAliases[index]), nullptr);
    }
  }

  vpiHandle generatedAssignments = vpi_iterate(vpiContAssign, generated);
  ASSERT_NE(generatedAssignments, nullptr);
  vpiHandle generatedAssignment = vpi_scan(generatedAssignments);
  ASSERT_NE(generatedAssignment, nullptr);
  EXPECT_EQ(vpi_get(vpiType, generatedAssignment), vpiContAssign);
  EXPECT_EQ(vpi_get(vpiLineNo, generatedAssignment), 20);
  EXPECT_EQ(vpi_scan(generatedAssignments), nullptr);
  PLI_BYTE8 generatedAName[] = "top.g.a";
  PLI_BYTE8 generatedBName[] = "top.g.b";
  vpiHandle generatedA = vpi_handle_by_name(generatedAName, nullptr);
  vpiHandle generatedB = vpi_handle_by_name(generatedBName, nullptr);
  vpiHandle generatedAssignmentLhs = vpi_handle(vpiLhs, generatedAssignment);
  ASSERT_NE(generatedA, nullptr);
  ASSERT_NE(generatedB, nullptr);
  ASSERT_NE(generatedAssignmentLhs, nullptr);
  EXPECT_EQ(vpi_compare_objects(generatedA, generatedAssignmentLhs), 1);
  EXPECT_EQ(vpi_compare_objects(generatedA, generatedB), 0);
  expectOnlyAssignment(vpiContAssign, generatedB, generatedAssignment);
  expectOnlyAssignment(vpiDriver, generatedB, generatedAssignment);
  expectOnlyAssignment(vpiLocalDriver, generatedB, generatedAssignment);

  vpiHandle generatedAliases = vpi_iterate(vpiAliasStmt, generated);
  ASSERT_NE(generatedAliases, nullptr);
  vpiHandle generatedAlias = vpi_scan(generatedAliases);
  ASSERT_NE(generatedAlias, nullptr);
  EXPECT_EQ(vpi_get(vpiType, generatedAlias), vpiAliasStmt);
  EXPECT_EQ(vpi_get(vpiLineNo, generatedAlias), 21);
  EXPECT_EQ(vpi_scan(generatedAliases), nullptr);
  vpiHandle generatedAliasLhs = vpi_handle(vpiLhs, generatedAlias);
  vpiHandle generatedAliasRhs = vpi_handle(vpiRhs, generatedAlias);
  ASSERT_NE(generatedAliasLhs, nullptr);
  ASSERT_NE(generatedAliasRhs, nullptr);
  EXPECT_EQ(vpi_compare_objects(generatedA, generatedAliasLhs), 1);
  EXPECT_EQ(vpi_compare_objects(generatedB, generatedAliasRhs), 1);

  vpiHandle interfaceAssignments = vpi_iterate(vpiContAssign, interface);
  vpiHandle programAssignments = vpi_iterate(vpiContAssign, program);
  ASSERT_NE(interfaceAssignments, nullptr);
  ASSERT_NE(programAssignments, nullptr);
  vpiHandle interfaceAssignment = vpi_scan(interfaceAssignments);
  vpiHandle programAssignment = vpi_scan(programAssignments);
  ASSERT_NE(interfaceAssignment, nullptr);
  ASSERT_NE(programAssignment, nullptr);
  EXPECT_EQ(vpi_get(vpiLineNo, interfaceAssignment), 40);
  EXPECT_EQ(vpi_get(vpiLineNo, programAssignment), 50);
  EXPECT_EQ(vpi_scan(interfaceAssignments), nullptr);
  EXPECT_EQ(vpi_scan(programAssignments), nullptr);
  vpiHandle interfaceAssignmentInstance =
      vpi_handle(vpiInstance, interfaceAssignment);
  vpiHandle programAssignmentInstance =
      vpi_handle(vpiInstance, programAssignment);
  ASSERT_NE(interfaceAssignmentInstance, nullptr);
  ASSERT_NE(programAssignmentInstance, nullptr);
  EXPECT_EQ(vpi_compare_objects(interface, interfaceAssignmentInstance), 1);
  EXPECT_EQ(vpi_compare_objects(program, programAssignmentInstance), 1);

  vpiHandle directAssignmentModule = vpi_handle(vpiModule, directAssignment);
  vpiHandle generatedAssignmentModule =
      vpi_handle(vpiModule, generatedAssignment);
  vpiHandle directAliasInstance =
      vpi_handle(vpiInstance, directAliases.front());
  vpiHandle generatedAliasInstance = vpi_handle(vpiInstance, generatedAlias);
  ASSERT_NE(directAssignmentModule, nullptr);
  ASSERT_NE(generatedAssignmentModule, nullptr);
  ASSERT_NE(directAliasInstance, nullptr);
  ASSERT_NE(generatedAliasInstance, nullptr);
  EXPECT_EQ(vpi_compare_objects(top, directAssignmentModule), 1);
  EXPECT_EQ(vpi_compare_objects(top, generatedAssignmentModule), 1);
  EXPECT_EQ(vpi_compare_objects(top, directAliasInstance), 1);
  EXPECT_EQ(vpi_compare_objects(top, generatedAliasInstance), 1);

  EXPECT_EQ(vpi_release_handle(generatedAliasInstance), 1);
  EXPECT_EQ(vpi_release_handle(directAliasInstance), 1);
  EXPECT_EQ(vpi_release_handle(generatedAssignmentModule), 1);
  EXPECT_EQ(vpi_release_handle(directAssignmentModule), 1);
  EXPECT_EQ(vpi_release_handle(programAssignmentInstance), 1);
  EXPECT_EQ(vpi_release_handle(interfaceAssignmentInstance), 1);
  EXPECT_EQ(vpi_release_handle(programAssignment), 1);
  EXPECT_EQ(vpi_release_handle(interfaceAssignment), 1);
  EXPECT_EQ(vpi_release_handle(generatedAlias), 1);
  EXPECT_EQ(vpi_release_handle(generatedAliasRhs), 1);
  EXPECT_EQ(vpi_release_handle(generatedAliasLhs), 1);
  EXPECT_EQ(vpi_release_handle(generatedAssignmentLhs), 1);
  EXPECT_EQ(vpi_release_handle(generatedB), 1);
  EXPECT_EQ(vpi_release_handle(generatedA), 1);
  EXPECT_EQ(vpi_release_handle(generatedAssignment), 1);
  for (vpiHandle simulated : simulatedAliases)
    EXPECT_EQ(vpi_release_handle(simulated), 1);
  for (vpiHandle declared : declaredAliases)
    EXPECT_EQ(vpi_release_handle(declared), 1);
  for (vpiHandle alias : directAliases)
    EXPECT_EQ(vpi_release_handle(alias), 1);
  EXPECT_EQ(vpi_release_handle(vectorRhs), 1);
  EXPECT_EQ(vpi_release_handle(vectorLhs), 1);
  EXPECT_EQ(vpi_release_handle(expectedVectorRhs), 1);
  EXPECT_EQ(vpi_release_handle(expectedVectorLhs), 1);
  EXPECT_EQ(vpi_release_handle(vectorAssignment), 1);
  EXPECT_EQ(vpi_release_handle(variableRhs), 1);
  EXPECT_EQ(vpi_release_handle(variableLhs), 1);
  EXPECT_EQ(vpi_release_handle(expectedVariableRhs), 1);
  EXPECT_EQ(vpi_release_handle(expectedVariableLhs), 1);
  EXPECT_EQ(vpi_release_handle(variableAssignment), 1);
  EXPECT_EQ(vpi_release_handle(assignmentRhs), 1);
  EXPECT_EQ(vpi_release_handle(assignmentLhs), 1);
  EXPECT_EQ(vpi_release_handle(directSimNet), 1);
  EXPECT_EQ(vpi_release_handle(expectedRhs), 1);
  EXPECT_EQ(vpi_release_handle(expectedLhs), 1);
  EXPECT_EQ(vpi_release_handle(directAssignment), 1);
  EXPECT_EQ(vpi_release_handle(generated), 1);
  EXPECT_EQ(vpi_release_handle(program), 1);
  EXPECT_EQ(vpi_release_handle(interface), 1);
  EXPECT_EQ(vpi_release_handle(top), 1);
  obelisk_rt_v1_context_destroy(runtime);
}

TEST(GeneratedDesignDatabase, NetIdentityQueries) {
  const char *inputPath = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(inputPath, nullptr) << "OBELISK_TEST_INPUT is required";
  const bool aliasShape = std::getenv("OBELISK_TEST_ALIAS_SHAPE") != nullptr;

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
  options.vpi = "read";
  mlir::FailureOr<obelisk::EncodedSimulationDesign> encoded =
      obelisk::encodeSimulationDesign(designs.front(), options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  ASSERT_GE(encoded->bytecode.size(), 40u);
  uint64_t bytecodeChecksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    bytecodeChecksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      bytecodeChecksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);

  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);

  std::string physicalName = aliasShape ? "top.a" : "top.uniform";
  std::string aliasName = aliasShape ? "top.b" : "top.uniform_alias";
  vpiHandle physical = vpi_handle_by_name(
      reinterpret_cast<PLI_BYTE8 *>(physicalName.data()), nullptr);
  vpiHandle alias = vpi_handle_by_name(
      reinterpret_cast<PLI_BYTE8 *>(aliasName.data()), nullptr);
  ASSERT_NE(physical, nullptr);
  ASSERT_NE(alias, nullptr);
  EXPECT_EQ(vpi_compare_objects(physical, alias), 0);
  EXPECT_EQ(vpi_get(vpiType, physical), vpiNet);
  EXPECT_EQ(vpi_get(vpiType, alias), vpiNet);
  EXPECT_EQ(vpi_get(vpiSize, alias), 4);
  EXPECT_EQ(vpi_get(vpiResolvedNetType, physical), vpiWire);
  EXPECT_EQ(vpi_get(vpiResolvedNetType, alias), vpiWire);

  obelisk_rt_design_cursor_v1 physicalCursor{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &execution,
                reinterpret_cast<const uint8_t *>(physicalName.data()),
                physicalName.size(), &physicalCursor),
            OBELISK_RT_OK);
  uint64_t stateOffset = 0;
  ASSERT_EQ(
      obelisk_rt_design_state_offset(runtime, physicalCursor, 0, &stateOffset),
      OBELISK_RT_OK);
  for (uint64_t bit = 0; bit != 4; ++bit) {
    const uint64_t absolute = stateOffset + bit;
    const uint64_t mask = uint64_t{1} << (absolute % 64);
    if ((UINT64_C(0xa) & (uint64_t{1} << bit)) != 0)
      runtime->stateValue[absolute / 64] |= mask;
    else
      runtime->stateValue[absolute / 64] &= ~mask;
    runtime->stateUnknown[absolute / 64] &= ~mask;
  }
  s_vpi_value wholeValue{};
  wholeValue.format = vpiBinStrVal;
  vpi_get_value(physical, &wholeValue);
  ASSERT_NE(wholeValue.value.str, nullptr);
  std::string physicalValue(wholeValue.value.str);
  wholeValue = {};
  wholeValue.format = vpiBinStrVal;
  vpi_get_value(alias, &wholeValue);
  ASSERT_NE(wholeValue.value.str, nullptr);
  EXPECT_EQ(physicalValue, "1010");
  EXPECT_STREQ(wholeValue.value.str, physicalValue.c_str());

  auto expectDelays = [](vpiHandle net) {
    std::array<s_vpi_time, 3> values{};
    s_vpi_delay delays{};
    delays.da = values.data();
    delays.no_of_delays = 3;
    delays.time_type = vpiSimTime;
    vpi_get_delays(net, &delays);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(values[0].low, 7u);
    EXPECT_EQ(values[1].low, 11u);
    EXPECT_EQ(values[2].low, 13u);
    for (const s_vpi_time &value : values) {
      EXPECT_EQ(value.type, vpiSimTime);
      EXPECT_EQ(value.high, 0u);
    }
  };
  if (!aliasShape) {
    expectDelays(physical);
    expectDelays(alias);
  }

  vpiHandle physicalSimNet = vpi_handle(vpiSimNet, physical);
  vpiHandle aliasSimNet = vpi_handle(vpiSimNet, alias);
  ASSERT_NE(physicalSimNet, nullptr);
  ASSERT_NE(aliasSimNet, nullptr);
  EXPECT_EQ(vpi_compare_objects(physical, physicalSimNet), 1);
  EXPECT_EQ(vpi_compare_objects(physical, aliasSimNet), 1);

  std::array<vpiHandle, 4> aliasBits{};
  std::array<vpiHandle, 4> physicalBits{};
  std::array<vpiHandle, 4> simulatedAliasBits{};
  std::array<vpiHandle, 4> simulatedPhysicalBits{};
  for (int index = 0; index != 4; ++index) {
    aliasBits[index] = vpi_handle_by_index(alias, index);
    physicalBits[index] = vpi_handle_by_index(physical, 3 - index);
    ASSERT_NE(aliasBits[index], nullptr);
    ASSERT_NE(physicalBits[index], nullptr);
    simulatedAliasBits[index] = vpi_handle(vpiSimNet, aliasBits[index]);
    simulatedPhysicalBits[index] = vpi_handle(vpiSimNet, physicalBits[index]);
    ASSERT_NE(simulatedAliasBits[index], nullptr);
    ASSERT_NE(simulatedPhysicalBits[index], nullptr);
    EXPECT_EQ(vpi_get(vpiType, simulatedAliasBits[index]), vpiNetBit);
    EXPECT_EQ(vpi_compare_objects(simulatedAliasBits[index],
                                  simulatedPhysicalBits[index]),
              1);
  }
  for (int index = 0; index != 3; ++index)
    EXPECT_EQ(vpi_compare_objects(simulatedAliasBits[index],
                                  simulatedPhysicalBits[index + 1]),
              0);
  for (int index = 0; index != 4; ++index) {
    s_vpi_value aliasValue{};
    aliasValue.format = vpiScalarVal;
    vpi_get_value(aliasBits[index], &aliasValue);
    s_vpi_value physicalValue{};
    physicalValue.format = vpiScalarVal;
    vpi_get_value(physicalBits[index], &physicalValue);
    EXPECT_EQ(aliasValue.value.scalar, physicalValue.value.scalar);
    if (index != 3) {
      s_vpi_value mismatchedValue{};
      mismatchedValue.format = vpiScalarVal;
      vpi_get_value(physicalBits[index + 1], &mismatchedValue);
      EXPECT_NE(aliasValue.value.scalar, mismatchedValue.value.scalar);
    }
  }
  vpiHandle simulatedIndex = vpi_handle(vpiIndex, simulatedAliasBits[0]);
  ASSERT_NE(simulatedIndex, nullptr);
  s_vpi_value indexValue{};
  indexValue.format = vpiIntVal;
  vpi_get_value(simulatedIndex, &indexValue);
  EXPECT_EQ(indexValue.value.integer, 3);
  vpiHandle simulatedBitParent = vpi_handle(vpiParent, simulatedAliasBits[0]);
  ASSERT_NE(simulatedBitParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(physical, simulatedBitParent), 1);

  vpiHandle netScope = nullptr;
  if (aliasShape) {
    PLI_BYTE8 topName[] = "top";
    netScope = vpi_handle_by_name(topName, nullptr);
    ASSERT_NE(netScope, nullptr);
  }
  vpiHandle aliasTypespec = vpi_handle(vpiTypespec, alias);
  ASSERT_NE(aliasTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, aliasTypespec), vpiLogicTypespec);
  vpiHandle typedefAlias = nullptr;
  vpiHandle declaredTypedef = nullptr;
  if (aliasShape) {
    typedefAlias = vpi_handle(vpiTypedefAlias, aliasTypespec);
    ASSERT_NE(typedefAlias, nullptr);
    EXPECT_EQ(vpi_get(vpiType, typedefAlias), vpiLogicTypespec);
    vpiHandle typedefs = vpi_iterate(vpiTypedef, netScope);
    ASSERT_NE(typedefs, nullptr);
    declaredTypedef = vpi_scan(typedefs);
    ASSERT_NE(declaredTypedef, nullptr);
    EXPECT_EQ(vpi_scan(typedefs), nullptr);
    EXPECT_EQ(vpi_compare_objects(aliasTypespec, declaredTypedef), 1);
    EXPECT_EQ(vpi_compare_objects(typedefAlias, declaredTypedef), 0);
  }
  vpiHandle aliasLeft = vpi_handle(vpiLeftRange, alias);
  vpiHandle aliasRight = vpi_handle(vpiRightRange, alias);
  ASSERT_NE(aliasLeft, nullptr);
  ASSERT_NE(aliasRight, nullptr);
  indexValue = {};
  indexValue.format = vpiIntVal;
  vpi_get_value(aliasLeft, &indexValue);
  EXPECT_EQ(indexValue.value.integer, 0);
  indexValue = {};
  indexValue.format = vpiIntVal;
  vpi_get_value(aliasRight, &indexValue);
  EXPECT_EQ(indexValue.value.integer, 3);

  std::unordered_map<std::string, unsigned> netNames;
  vpiHandle nets = vpi_iterate(vpiNet, netScope);
  ASSERT_NE(nets, nullptr);
  while (vpiHandle net = vpi_scan(nets)) {
    const char *name = vpi_get_str(vpiFullName, net);
    ASSERT_NE(name, nullptr);
    ++netNames[name];
    EXPECT_EQ(vpi_release_handle(net), 1);
  }
  EXPECT_EQ(netNames[physicalName], 1u);
  EXPECT_EQ(netNames[aliasName], 1u);
  if (netScope) {
    EXPECT_EQ(vpi_release_handle(netScope), 1);
  }

  EXPECT_EQ(vpi_release_handle(aliasRight), 1);
  EXPECT_EQ(vpi_release_handle(aliasLeft), 1);
  if (declaredTypedef) {
    EXPECT_EQ(vpi_release_handle(declaredTypedef), 1);
  }
  if (typedefAlias) {
    EXPECT_EQ(vpi_release_handle(typedefAlias), 1);
  }
  EXPECT_EQ(vpi_release_handle(aliasTypespec), 1);
  EXPECT_EQ(vpi_release_handle(simulatedBitParent), 1);
  EXPECT_EQ(vpi_release_handle(simulatedIndex), 1);
  for (vpiHandle bit : simulatedPhysicalBits)
    EXPECT_EQ(vpi_release_handle(bit), 1);
  for (vpiHandle bit : simulatedAliasBits)
    EXPECT_EQ(vpi_release_handle(bit), 1);
  for (vpiHandle bit : physicalBits)
    EXPECT_EQ(vpi_release_handle(bit), 1);
  for (vpiHandle bit : aliasBits)
    EXPECT_EQ(vpi_release_handle(bit), 1);
  EXPECT_EQ(vpi_release_handle(aliasSimNet), 1);
  EXPECT_EQ(vpi_release_handle(physicalSimNet), 1);
  EXPECT_EQ(vpi_release_handle(alias), 1);
  EXPECT_EQ(vpi_release_handle(physical), 1);
  obelisk_rt_v1_context_destroy(runtime);
}
