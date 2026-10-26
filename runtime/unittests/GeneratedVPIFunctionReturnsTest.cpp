//===- GeneratedVPIFunctionReturnsTest.cpp - Function return variables
//---------===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

TEST(GeneratedDesignDatabase, FunctionReturns) {
  using namespace obelisk;
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<sim::SimDesignOp>().begin();
  SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  using namespace reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  std::vector<uint64_t> returns;
  uint64_t voidProperty = UINT64_MAX;
  for (uint64_t index = 0; index != header.getRelationCount(); ++index) {
    uint64_t offset = header.getRelationOffset() + index * RelationLayout.size;
    if (RelationView(image.data() + offset).getSelector() == vpiReturn)
      returns.push_back(offset);
  }
  for (uint64_t index = 0; index != directory.getFixedPropertyCount();
       ++index) {
    uint64_t offset =
        directory.getFixedPropertyOffset() + index * FixedPropertyLayout.size;
    if (FixedPropertyView(image.data() + offset).getSelector() == vpiSize) {
      ASSERT_EQ(voidProperty, UINT64_MAX);
      voidProperty = offset;
    }
  }
  ASSERT_EQ(returns.size(), 8u);
  ASSERT_NE(voidProperty, UINT64_MAX);
  auto rejects = [&](auto mutate) {
    auto bytes = image;
    mutate(bytes);
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
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + returns[0])
        .setTargetIndexAndTable(
            RelationView(bytes.data() + returns[1]).getTargetIndexAndTable());
  });
  rejects([&](auto &bytes) {
    FixedPropertyWriter(bytes.data() + voidProperty).setPayload(1);
  });
  rejects([&](auto &bytes) {
    // A valid void fact is incompatible with any Return relation.
    auto relation = RelationView(bytes.data() + returns[0]);
    uint32_t source = 0;
    ASSERT_TRUE(tryPackTableIndex(
        unpackRelationSourceTable(relation.getSourceKindAndTable()),
        relation.getSourceIndex(), source));
    FixedPropertyWriter(bytes.data() + voidProperty)
        .setSourceIndexAndTable(source);
    using Row = std::array<uint8_t, FixedPropertyLayout.size>;
    std::vector<Row> rows(directory.getFixedPropertyCount());
    for (size_t i = 0; i != rows.size(); ++i)
      std::copy_n(bytes.data() + directory.getFixedPropertyOffset() +
                      i * FixedPropertyLayout.size,
                  FixedPropertyLayout.size, rows[i].data());
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
      FixedPropertyView lhs(a.data()), rhs(b.data());
      return std::make_pair(lhs.getSourceIndexAndTable(), lhs.getSelector()) <
             std::make_pair(rhs.getSourceIndexAndTable(), rhs.getSelector());
    });
    for (size_t i = 0; i != rows.size(); ++i)
      std::copy_n(rows[i].data(), FixedPropertyLayout.size,
                  bytes.data() + directory.getFixedPropertyOffset() +
                      i * FixedPropertyLayout.size);
  });
  // Re-encode a valid inventory-only baseline. Return facts add no variables,
  // types, or values, only one 16-byte edge per function and one void fact.
  auto baselineModule = mlir::cast<mlir::ModuleOp>(module->clone());
  auto baselineDesign = *baselineModule.getOps<sim::SimDesignOp>().begin();
  llvm::SmallVector<mlir::Operation *> erase;
  for (auto relation : baselineDesign.getOps<sim::SimVPIRelationDeclOp>())
    if (relation.getSelector() == vpiReturn)
      erase.push_back(relation);
  for (auto *operation : erase)
    operation->erase();
  for (auto unit : baselineDesign.getOps<sim::SimCodeUnitDeclOp>())
    if (auto properties =
            unit->getAttrOfType<sim::VPIPropertySetAttr>("vpi_properties")) {
      llvm::SmallVector<mlir::Attribute> kept;
      for (auto item : properties.getProperties())
        if (mlir::cast<sim::VPIPropertyAttr>(item).getSelector().getInt() !=
            vpiSize)
          kept.push_back(item);
      if (kept.empty())
        unit->removeAttr("vpi_properties");
      else
        unit->setAttr("vpi_properties",
                      sim::VPIPropertySetAttr::get(
                          &context, mlir::ArrayAttr::get(&context, kept)));
    }
  auto baseline = encodeSimulationDesign(baselineDesign, options);
  ASSERT_TRUE(mlir::succeeded(baseline));
  auto baselineExecution = execution;
  baselineExecution.design_database = baseline->designDatabase.data();
  baselineExecution.design_database_size = baseline->designDatabase.size();
  ASSERT_EQ(obelisk_rt_v1_design_validate(&baselineExecution), OBELISK_RT_OK);
  EXPECT_EQ(image.size() - baseline->designDatabase.size(),
            returns.size() * RelationLayout.size + FixedPropertyLayout.size);
  HeaderView baselineHeader(baseline->designDatabase.data());
  EXPECT_EQ(header.getObjectCount(), baselineHeader.getObjectCount());
  baselineModule.erase();
  SimulationBytecodeOptions offOptions;
  offOptions.vpi = "off";
  auto off = encodeSimulationDesign(design, offOptions);
  ASSERT_TRUE(mlir::succeeded(off));
  EXPECT_TRUE(off->designDatabase.empty());
  EXPECT_EQ(off->executionFlags & (OBELISK_RT_EXECUTION_VPI_READ |
                                   OBELISK_RT_EXECUTION_VPI_WRITE),
            0u);
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &raw),
            OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      runtime(raw, obelisk_rt_v1_context_destroy);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(raw, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(raw), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(raw), OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle h) {
    if (h)
      handles.push_back(h);
    return h;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto signedFunction = named("top.signed_result");
  auto staticFunction = named("top.static_result");
  auto vectorFunction = named("top.vector_result");
  auto textFunction = named("top.text_result");
  auto voidFunction = named("top.void_result");
  auto enumFunction = named("top.enum_result");
  ASSERT_NE(enumFunction, nullptr);
  auto missingFunction = named("top.unretained_result");
  ASSERT_NE(signedFunction, nullptr);
  ASSERT_NE(staticFunction, nullptr);
  ASSERT_NE(vectorFunction, nullptr);
  ASSERT_NE(textFunction, nullptr);
  ASSERT_NE(voidFunction, nullptr);
  ASSERT_NE(missingFunction, nullptr);
  auto result = keep(vpi_handle(vpiReturn, signedFunction));
  auto persistent = keep(vpi_handle(vpiReturn, staticFunction));
  auto vector = keep(vpi_handle(vpiReturn, vectorFunction));
  ASSERT_NE(result, nullptr);
  ASSERT_NE(persistent, nullptr);
  ASSERT_NE(vector, nullptr);
  EXPECT_EQ(vpi_get(vpiType, result), vpiReg);
  EXPECT_EQ(vpi_get(vpiSize, signedFunction), 9);
  EXPECT_EQ(vpi_get(vpiSigned, signedFunction), 1);
  EXPECT_EQ(vpi_get(vpiSize, staticFunction), 32);
  EXPECT_EQ(vpi_get(vpiSigned, staticFunction), 1);
  EXPECT_EQ(vpi_get(vpiSize, vectorFunction), 4);
  EXPECT_EQ(vpi_get(vpiSigned, vectorFunction), 0);
  auto enumeration = keep(vpi_handle(vpiReturn, enumFunction));
  ASSERT_NE(enumeration, nullptr);
  EXPECT_EQ(vpi_get(vpiType, enumeration), vpiEnumVar);
  EXPECT_EQ(vpi_get(vpiSize, enumFunction), 9);
  EXPECT_EQ(vpi_get(vpiSigned, enumFunction), 1);
  auto enumType = keep(vpi_handle(vpiTypespec, enumeration));
  ASSERT_NE(enumType, nullptr);
  EXPECT_EQ(vpi_get(vpiType, enumType), vpiEnumTypespec);
  auto declaredType = vpi_iterate(vpiTypedef, enumFunction);
  ASSERT_NE(declaredType, nullptr);
  EXPECT_TRUE(vpi_compare_objects(enumType, keep(vpi_scan(declaredType))));
  EXPECT_EQ(vpi_scan(declaredType), nullptr);
  for (auto expected : {std::make_pair("top.array_result", 6),
                        std::make_pair("top.record_result", 2),
                        std::make_pair("top.packed_result", 41)}) {
    auto function = named(expected.first);
    ASSERT_NE(function, nullptr);
    auto variable = keep(vpi_handle(vpiReturn, function));
    ASSERT_NE(variable, nullptr);
    EXPECT_EQ(vpi_get(vpiSize, function), expected.second);
    EXPECT_EQ(vpi_get(vpiSize, function), vpi_get(vpiSize, variable));
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  }
  EXPECT_EQ(vpi_get(vpiAutomatic, result), 1);
  EXPECT_EQ(vpi_get(vpiAutomatic, persistent), 0);
  EXPECT_TRUE(
      vpi_compare_objects(keep(vpi_handle(vpiScope, result)), signedFunction));
  EXPECT_TRUE(vpi_compare_objects(keep(vpi_handle(vpiScope, persistent)),
                                  staticFunction));
  auto variables = vpi_iterate(vpiVariables, signedFunction);
  ASSERT_NE(variables, nullptr);
  auto existing = keep(vpi_scan(variables));
  EXPECT_TRUE(vpi_compare_objects(existing, result));
  EXPECT_EQ(vpi_scan(variables), nullptr);
  EXPECT_TRUE(vpi_compare_objects(persistent,
                                  named("top.static_result.static_result")));
  auto integer = [&](vpiHandle object) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(object, &value);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), 0);
    return value.value.integer;
  };
  EXPECT_EQ(integer(keep(vpi_handle(vpiLeftRange, signedFunction))), 8);
  EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, signedFunction))), 0);
  EXPECT_EQ(integer(keep(vpi_handle(vpiLeftRange, vectorFunction))), 2);
  EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, vectorFunction))), 5);
  s_vpi_value write{};
  write.format = vpiIntVal;
  write.value.integer = 37;
  vpi_put_value(persistent, &write, nullptr, vpiNoDelay);
  EXPECT_EQ(integer(persistent), 37);
  EXPECT_EQ(vpi_get(vpiSize, voidFunction), 0);
  EXPECT_EQ(vpi_handle(vpiReturn, voidFunction), nullptr);
  s_vpi_error_info error{};
  EXPECT_EQ(vpi_chk_error(&error), 0);
  EXPECT_EQ(vpi_handle(vpiReturn, missingFunction), nullptr);
  EXPECT_EQ(vpi_get(vpiSize, missingFunction), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_EQ(vpi_get(vpiSize, textFunction), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
