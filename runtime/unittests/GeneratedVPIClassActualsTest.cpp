//===- GeneratedVPIClassActualsTest.cpp - Source class actuals -------===//

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

TEST(GeneratedDesignDatabase, ClassActuals) {
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
  auto relation = [&](int selector, vpiHandle from) {
    return keep(vpi_handle(selector, from));
  };
  auto classType = [&](const char *name) {
    auto definition = lookup(name);
    EXPECT_NE(definition, nullptr);
    EXPECT_EQ(vpi_iterate(vpiClassTypespec, definition), nullptr);
    return relation(vpiClassTypespec, relation(vpiExtends, definition));
  };
  auto base = lookup("Base");
  ASSERT_NE(base, nullptr);
  std::map<std::string, vpiHandle> formals;
  auto iterator = vpi_iterate(vpiParameter, base);
  ASSERT_NE(iterator, nullptr);
  while (auto formal = vpi_scan(iterator)) {
    keep(formal);
    formals.emplace(vpi_get_str(vpiName, formal), formal);
  }
  ASSERT_EQ(formals.size(), 3u);
  auto assignments = [&](vpiHandle type) {
    std::vector<vpiHandle> result;
    auto iter = vpi_iterate(vpiParamAssign, type);
    EXPECT_NE(iter, nullptr);
    if (!iter)
      return result;
    EXPECT_EQ(vpi_compare_objects(relation(vpiUse, iter), type), 1);
    while (auto actual = vpi_scan(iter)) {
      result.push_back(keep(actual));
      EXPECT_EQ(vpi_compare_objects(relation(vpiScope, actual), type), 1);
    }
    return result;
  };
  auto namedType = classType("Derived"), orderedType = classType("Ordered"),
       unsupportedType = classType("Unsupported");
  ASSERT_NE(namedType, nullptr);
  ASSERT_NE(orderedType, nullptr);
  ASSERT_NE(unsupportedType, nullptr);
  auto named = assignments(namedType), ordered = assignments(orderedType),
       unsupported = assignments(unsupportedType);
  ASSERT_EQ(named.size(), 3u);
  ASSERT_EQ(ordered.size(), 3u);
  ASSERT_EQ(unsupported.size(), 3u);
  const char *names[] = {"T", "P", "Q"};
  for (size_t index = 0; index != 3; ++index) {
    for (auto actual : {named[index], ordered[index], unsupported[index]}) {
      EXPECT_EQ(vpi_get(vpiType, actual), vpiParamAssign);
      EXPECT_EQ(vpi_compare_objects(relation(vpiLhs, actual),
                                    formals.at(names[index])),
                1);
    }
    EXPECT_EQ(vpi_get(vpiConnByName, named[index]), 1);
    EXPECT_EQ(vpi_get(vpiConnByName, ordered[index]), 0);
    EXPECT_EQ(vpi_get(vpiConnByName, unsupported[index]), index == 1);
  }
  auto u = lookup("Derived::U"), n = lookup("Derived::N");
  ASSERT_NE(u, nullptr);
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(vpi_compare_objects(relation(vpiRhs, named[0]), u), 1);
  auto operation = relation(vpiRhs, named[1]);
  ASSERT_NE(operation, nullptr);
  EXPECT_EQ(vpi_get(vpiType, operation), vpiOperation);
  EXPECT_EQ(vpi_get(vpiOpType, operation), vpiAddOp);
  iterator = vpi_iterate(vpiOperand, operation);
  ASSERT_NE(iterator, nullptr);
  EXPECT_EQ(vpi_compare_objects(relation(vpiUse, iterator), operation), 1);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_scan(iterator)), n), 1);
  auto two = keep(vpi_scan(iterator));
  ASSERT_NE(two, nullptr);
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  auto read = [&](vpiHandle value, int expected) {
    s_vpi_value result{};
    result.format = vpiIntVal;
    vpi_get_value(value, &result);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(result.value.integer, expected);
  };
  read(two, 2);
  read(relation(vpiRhs, ordered[1]), 7);
  read(relation(vpiExpr, formals.at("P")), 3);
  EXPECT_STREQ(vpi_get_str(vpiDecompile, operation),
               "\\N + 32'sb00000000000000000000000000000010");
  EXPECT_EQ(vpi_get(vpiType, relation(vpiRhs, ordered[0])), vpiBitTypespec);
  auto originalQ = relation(vpiExpr, formals.at("Q"));
  ASSERT_NE(originalQ, nullptr);
  for (auto actual : {named[2], ordered[2], unsupported[2]})
    EXPECT_EQ(vpi_compare_objects(relation(vpiRhs, actual), originalQ), 1);
  EXPECT_EQ(relation(vpiRhs, unsupported[1]), nullptr);
  notice();
  // A known original value reference remains a declaration without any
  // specialization-supplied value. Its identity is not replaced by a constant.
  s_vpi_value unresolved{};
  unresolved.format = vpiIntVal;
  vpi_get_value(n, &unresolved);
  notice();

  auto cursor = [&](const char *name) {
    obelisk_rt_design_cursor_v1 result{};
    EXPECT_EQ(obelisk_rt_v1_design_lookup(
                  &execution, reinterpret_cast<const uint8_t *>(name),
                  std::char_traits<char>::length(name), &result),
              OBELISK_RT_OK);
    return result;
  };
  auto target = [&](obelisk_rt_design_cursor_v1 source, uint32_t selector,
                    bool iterate = false, uint64_t ordinal = 0) {
    obelisk_rt_design_cursor_v1 result{};
    VPIRelationRange range{};
    uint32_t kind = 0;
    bool statement = false;
    EXPECT_EQ(obelisk_rt_cached_vpi_relation_range(runtime, source, selector,
                                                   iterate, &range),
              OBELISK_RT_OK);
    EXPECT_LT(ordinal, range.count);
    EXPECT_EQ(obelisk_rt_cached_vpi_relation_target(
                  runtime, range.first + ordinal, &result, &kind, &statement),
              OBELISK_RT_OK);
    EXPECT_FALSE(statement);
    return result;
  };
  auto derivedCursor =
      target(target(cursor("Derived"), vpiExtends), vpiClassTypespec);
  auto pActual = target(derivedCursor, vpiParamAssign, true, 1);
  auto tActual = target(derivedCursor, vpiParamAssign, true, 0);
  auto qActual = target(derivedCursor, vpiParamAssign, true, 2);
  auto operationCursor = target(pActual, vpiRhs);
  auto literalCursor = target(operationCursor, vpiOperand, true, 1);
  auto unrelatedScope = cursor("unrelated");
  auto baseP = cursor("Base::P"), derivedN = cursor("Derived::N");
  auto defaultP = target(baseP, vpiExpr);
  auto orderedCursor =
      target(target(cursor("Ordered"), vpiExtends), vpiClassTypespec);
  auto orderedT = target(orderedCursor, vpiParamAssign, true, 0);
  auto actualType = target(orderedT, vpiRhs);
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
  auto relationRow = [&](uint32_t source, uint32_t selector,
                         uint32_t ordinal = 0) {
    for (uint64_t index = 0; index != header.getRelationCount(); ++index) {
      uint64_t offset =
          header.getRelationOffset() + index * RelationLayout.size;
      RelationView relation(image.data() + offset);
      uint32_t packedSource = 0;
      if (tryPackTableIndex(
              unpackRelationSourceTable(relation.getSourceKindAndTable()),
              relation.getSourceIndex(), packedSource) &&
          packedSource == source && relation.getSelector() == selector &&
          relation.getOrdinal() == ordinal)
        return offset;
    }
    return uint64_t{0};
  };
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
  uint64_t operand = relationRow(packed(operationCursor), vpiOperand);
  uint64_t rhs = relationRow(packed(pActual), vpiRhs);
  uint64_t typeRhs = relationRow(packed(tActual), vpiRhs);
  ASSERT_NE(operand, 0u);
  ASSERT_NE(rhs, 0u);
  ASSERT_NE(typeRhs, 0u);
  EXPECT_EQ(RelationView(image.data() + operand).getTargetIndexAndTable(),
            packed(derivedN));
  // A source actual references the derived declaration, not a same-kind base
  // formal or the default of the overridden declaration.
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + operand)
        .setTargetIndexAndTable(packed(baseP));
  });
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + rhs).setTargetIndexAndTable(packed(defaultP));
  });
  // An otherwise valid physical scope cannot replace the exact occurrence
  // parent's scope, including original-default and actual operand trees.
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + literalCursor.offset)
        .setOwner(unrelatedScope.offset);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + defaultP.offset)
        .setOwner(unrelatedScope.offset);
  });
  // Concrete type occurrences cannot be shared between explicit assignments.
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + typeRhs)
        .setTargetIndexAndTable(packed(actualType));
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + pActual.offset)
        .setCaps(OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT |
                 OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + pActual.offset)
        .setCaps(OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT |
                 OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETER_RHS);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + qActual.offset)
        .setCaps(OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT);
  });
}
} // namespace
