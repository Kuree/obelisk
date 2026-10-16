//===- GeneratedVPILexicalInheritanceTest.cpp - Source inheritance -------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace obelisk::reflection;

TEST(GeneratedDesignDatabase, LexicalInheritance) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<obelisk::sim::SimDesignOp>().begin();
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      checksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto lookup = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto notice = [] {
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
    EXPECT_EQ(error.level, vpiNotice);
  };
  auto base = lookup("Base"), mid = lookup("Mid"), derived = lookup("Derived"),
       explicitClass = lookup("Explicit"), unknown = lookup("Unknown"),
       constructor = lookup("Constructor");
  for (auto definition :
       {base, mid, derived, explicitClass, unknown, constructor}) {
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(vpi_get(vpiType, definition), vpiClassDefn);
    EXPECT_EQ(vpi_iterate(vpiClassTypespec, definition), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  }
  auto relation = [&](int selector, vpiHandle from) {
    return keep(vpi_handle(selector, from));
  };
  auto clause = relation(vpiExtends, mid);
  ASSERT_NE(clause, nullptr);
  EXPECT_EQ(vpi_get(vpiType, clause), vpiExtends);
  EXPECT_EQ(vpi_compare_objects(relation(vpiClassDefn, clause), mid), 1);
  auto lexical = relation(vpiClassTypespec, clause);
  ASSERT_NE(lexical, nullptr);
  EXPECT_EQ(vpi_get(vpiType, lexical), vpiClassTypespec);
  EXPECT_STREQ(vpi_get_str(vpiName, lexical), "Base");
  EXPECT_EQ(vpi_get(vpiClassType, lexical), vpiUserDefinedClass);
  EXPECT_EQ(vpi_compare_objects(relation(vpiClassDefn, lexical), base), 1);
  EXPECT_EQ(vpi_compare_objects(relation(vpiScope, lexical), mid), 1);
  for (int selector :
       {vpiVariables, vpiMethods, vpiConstraint, vpiNamedEvent,
        vpiNamedEventArray, vpiTypedef, vpiInternalScope, vpiParameter}) {
    EXPECT_EQ(vpi_iterate(selector, lexical), nullptr);
    notice();
  }
  auto derivedType = relation(vpiClassTypespec, relation(vpiExtends, derived));
  ASSERT_NE(derivedType, nullptr);
  EXPECT_EQ(vpi_compare_objects(relation(vpiClassDefn, derivedType), mid), 1);
  EXPECT_EQ(vpi_compare_objects(relation(vpiExtends, derivedType), lexical), 1);

  std::vector<std::string> derivedNames;
  auto iterator = vpi_iterate(vpiDerivedClasses, base);
  ASSERT_NE(iterator, nullptr);
  EXPECT_EQ(vpi_compare_objects(relation(vpiUse, iterator), base), 1);
  while (auto definition = vpi_scan(iterator)) {
    keep(definition);
    derivedNames.emplace_back(vpi_get_str(vpiName, definition));
  }
  EXPECT_EQ(derivedNames, (std::vector<std::string>{
                              "Mid", "Derived", "Explicit", "Constructor"}));

  std::map<std::string, vpiHandle> parameters;
  iterator = vpi_iterate(vpiParameter, base);
  ASSERT_NE(iterator, nullptr);
  while (auto parameter = vpi_scan(iterator)) {
    keep(parameter);
    parameters.emplace(vpi_get_str(vpiName, parameter), parameter);
  }
  ASSERT_EQ(parameters.size(), 5u);
  std::vector<vpiHandle> assignments;
  iterator = vpi_iterate(vpiParamAssign, lexical);
  ASSERT_NE(iterator, nullptr);
  EXPECT_EQ(vpi_compare_objects(relation(vpiUse, iterator), lexical), 1);
  for (const char *name : {"T", "P", "Unsupported", "Required"}) {
    auto assignment = keep(vpi_scan(iterator));
    ASSERT_NE(assignment, nullptr);
    assignments.push_back(assignment);
    EXPECT_EQ(vpi_get(vpiType, assignment), vpiParamAssign);
    EXPECT_EQ(vpi_get(vpiConnByName, assignment), 0);
    EXPECT_EQ(vpi_compare_objects(relation(vpiScope, assignment), lexical), 1);
    EXPECT_EQ(
        vpi_compare_objects(relation(vpiLhs, assignment), parameters.at(name)),
        1);
    if (std::string(name) == "Unsupported") {
      EXPECT_EQ(vpi_handle(vpiRhs, assignment), nullptr);
      notice();
    } else if (std::string(name) == "Required") {
      EXPECT_EQ(vpi_handle(vpiRhs, assignment), nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
    } else {
      auto original = relation(vpiExpr, parameters.at(name));
      ASSERT_NE(original, nullptr);
      EXPECT_EQ(vpi_compare_objects(relation(vpiRhs, assignment), original), 1);
    }
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  auto pDefault = relation(vpiRhs, assignments[1]);
  s_vpi_value value{};
  value.format = vpiIntVal;
  vpi_get_value(pDefault, &value);
  EXPECT_EQ(value.value.integer, 3);
  auto constructorClause = relation(vpiExtends, constructor);
  auto constructorType = relation(vpiClassTypespec, constructorClause);
  EXPECT_EQ(vpi_compare_objects(constructorType, lexical), 0);
  EXPECT_EQ(vpi_iterate(vpiArgument, constructorClause), nullptr);
  notice();
  iterator = vpi_iterate(vpiParamAssign, constructorType);
  ASSERT_NE(iterator, nullptr);
  auto first = keep(vpi_scan(iterator));
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(vpi_compare_objects(relation(vpiRhs, first),
                                relation(vpiRhs, assignments[0])),
            1);
  while (auto assignment = vpi_scan(iterator))
    keep(assignment);
  auto explicitType =
      relation(vpiClassTypespec, relation(vpiExtends, explicitClass));
  ASSERT_NE(explicitType, nullptr);
  EXPECT_EQ(vpi_iterate(vpiParamAssign, explicitType), nullptr);
  notice();
  auto unknownClause = relation(vpiExtends, unknown);
  ASSERT_NE(unknownClause, nullptr);
  EXPECT_EQ(vpi_handle(vpiClassTypespec, unknownClause), nullptr);
  notice();

  auto cursor = [&](const char *name) {
    obelisk_rt_design_cursor_v1 result{};
    EXPECT_EQ(obelisk_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(name),
                  std::char_traits<char>::length(name), &result),
              OBELISK_RT_OK);
    return result;
  };
  auto target = [&](obelisk_rt_design_cursor_v1 from, uint32_t selector,
                    bool iterate = false, uint64_t ordinal = 0) {
    VPIRelationRange range{};
    obelisk_rt_design_cursor_v1 result{};
    uint32_t kind = 0;
    bool statement = false;
    EXPECT_EQ(obelisk_rt_cached_vpi_relation_range(runtime, from, selector,
                                                   iterate, &range),
              OBELISK_RT_OK);
    EXPECT_LT(ordinal, range.count);
    EXPECT_EQ(obelisk_rt_cached_vpi_relation_target(
                  runtime, range.first + ordinal, &result, &kind, &statement),
              OBELISK_RT_OK);
    EXPECT_FALSE(statement);
    return result;
  };
  auto baseCursor = cursor("Base"), midCursor = cursor("Mid");
  auto clauseCursor = target(midCursor, vpiExtends);
  auto typeCursor = target(clauseCursor, vpiClassTypespec);
  auto assignmentCursor = target(typeCursor, vpiParamAssign, true, 1);
  auto otherAssignment = target(typeCursor, vpiParamAssign, true, 0);
  auto pCursor = cursor("Base::P"), tCursor = cursor("Base::T");
  auto originalCursor = target(pCursor, vpiExpr);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(runtime);

  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  auto packed = [&](obelisk_rt_design_cursor_v1 object) {
    uint32_t result = UINT32_MAX;
    if (object.offset >= header.getObjectOffset() &&
        object.offset < header.getObjectOffset() +
                            header.getObjectCount() * ObjectLayout.size)
      EXPECT_TRUE(tryPackTableIndex(TableKind::Object,
                                    (object.offset - header.getObjectOffset()) /
                                        ObjectLayout.size,
                                    result));
    else
      EXPECT_TRUE(tryPackTableIndex(
          TableKind::StaticObject,
          (object.offset - directory.getStaticObjectOffset()) /
              StaticObjectLayout.size,
          result));
    return result;
  };
  auto relationRow = [&](uint32_t source, uint32_t selector) {
    for (uint64_t index = 0; index != header.getRelationCount(); ++index) {
      uint64_t row = header.getRelationOffset() + index * RelationLayout.size;
      RelationView relation(image.data() + row);
      uint32_t packedSource = 0;
      if (tryPackTableIndex(
              unpackRelationSourceTable(relation.getSourceKindAndTable()),
              relation.getSourceIndex(), packedSource) &&
          packedSource == source && relation.getSelector() == selector)
        return row;
    }
    return uint64_t{0};
  };
  uint64_t typeNameRow = 0, otherName = 0;
  for (uint64_t index = 0; index != directory.getFixedPropertyCount();
       ++index) {
    uint64_t row =
        directory.getFixedPropertyOffset() + index * FixedPropertyLayout.size;
    FixedPropertyView property(image.data() + row);
    if (property.getSelector() != vpiName)
      continue;
    if (property.getSourceIndexAndTable() == packed(typeCursor))
      typeNameRow = row;
    if (property.getSourceIndexAndTable() == packed(midCursor))
      otherName = property.getPayload();
  }
  ASSERT_NE(typeNameRow, 0u);
  ASSERT_NE(otherName, 0u);
  auto rejects = [&](auto corrupt) {
    std::vector<uint8_t> bytes(image.begin(), image.end());
    corrupt(bytes);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != bytes.size(); ++index) {
      hash ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : bytes[index];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    auto invalid = execution;
    invalid.design_database = bytes.data();
    invalid.design_database_size = bytes.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  auto rhs = relationRow(packed(assignmentCursor), vpiRhs);
  auto lhs = relationRow(packed(assignmentCursor), vpiLhs);
  auto classDefinition = relationRow(packed(typeCursor), vpiClassDefn);
  ASSERT_NE(rhs, 0u);
  ASSERT_NE(lhs, 0u);
  ASSERT_NE(classDefinition, 0u);
  EXPECT_EQ(RelationView(image.data() + rhs).getTargetIndexAndTable(),
            packed(originalCursor));
  // Type-compatible but not the exact base parameter or original default.
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + rhs).setTargetIndexAndTable(packed(tCursor));
  });
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + lhs).setTargetIndexAndTable(packed(tCursor));
  });
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + classDefinition)
        .setTargetIndexAndTable(packed(midCursor));
  });
  // Matching record/property spellings must still name the referenced class.
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + typeCursor.offset).setName(otherName);
    FixedPropertyWriter(bytes.data() + typeNameRow).setPayload(otherName);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + typeCursor.offset).setCaps(0);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + typeCursor.offset).setWidth(64);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + assignmentCursor.offset).setStateOffset(8);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + typeCursor.offset)
        .setCaps(OBELISK_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC |
                 OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETERS);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + clauseCursor.offset)
        .setCaps(OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_BASE);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + otherAssignment.offset).setCaps(0);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + assignmentCursor.offset)
        .setCaps(OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT |
                 OBELISK_RT_DESIGN_CAP_PORT_OUTPUT);
  });
}
} // namespace
