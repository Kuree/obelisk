//===- GeneratedVPIIntegralCastsTest.cpp - Integral cast queries
//-----------===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

TEST(GeneratedDesignDatabase, IntegralCasts) {
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
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
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
  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &raw),
            OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      runtime(raw, obelisk_rt_v1_context_destroy);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(raw, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(raw), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(raw), OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto get = [&](int selector, vpiHandle source) {
    return keep(vpi_handle(selector, source));
  };
  auto children = [&](int selector, vpiHandle parent) {
    std::vector<vpiHandle> result;
    if (auto iterator = vpi_iterate(selector, parent))
      while (auto child = keep(vpi_scan(iterator)))
        result.push_back(child);
    return result;
  };
  auto put = [&](const char *name, uint32_t value, uint32_t unknown = 0) {
    std::string path = std::string("top.") + name;
    auto object = named(path.c_str());
    ASSERT_NE(object, nullptr);
    s_vpi_vecval vector{value, unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  std::map<std::string, vpiHandle> expressions, owners;
  auto top = named("top");
  for (auto process : children(vpiProcess, top)) {
    auto statement = get(vpiStmt, process);
    ASSERT_NE(statement, nullptr);
    auto lhs = get(vpiLhs, statement);
    ASSERT_NE(lhs, nullptr);
    std::string name = vpi_get_str(vpiName, lhs);
    owners[name] = statement;
    expressions[name] = get(vpiRhs, statement);
  }
  ASSERT_EQ(expressions.size(), 11u);
  EXPECT_EQ(expressions.at("const_cast"), nullptr);
  auto bits = [&](const char *name) {
    auto object = expressions.at(name);
    EXPECT_NE(object, nullptr);
    s_vpi_value value{};
    value.format = vpiBinStrVal;
    vpi_get_value(object, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.str ? std::string(value.value.str) : std::string{};
  };
  put("a", 250);
  put("b", 10);
  put("s", 250);
  put("x", 260);
  put("f", 0xa4, 0x81);
  EXPECT_EQ(bits("widen"), "00000000000000000000000011111010");
  EXPECT_EQ(bits("sign_extend"), "11111111111111111111111111111010");
  EXPECT_EQ(bits("nested_widen"), "00000000000000000000000100000100");
  EXPECT_EQ(bits("nested_narrow"), "10000010"); // 260/2, then truncate.
  EXPECT_EQ(bits("narrow"), "00000100");
  EXPECT_EQ(bits("to_two"), "00100100");
  EXPECT_EQ(bits("to_four"), "11111111111111111111111111111010");
  EXPECT_EQ(bits("wide"), std::string(122, '1') + "11111010");
  EXPECT_EQ(bits("aliased"), "x010010z");
  EXPECT_EQ(bits("nested_boundary"), "00000000000000000000000000000101");
  for (auto &[name, expression] : expressions) {
    SCOPED_TRACE(name);
    if (!expression)
      continue;
    EXPECT_EQ(vpi_get(vpiType, expression), vpiOperation);
    EXPECT_EQ(vpi_compare_objects(get(vpiModule, expression), top), 1);
    EXPECT_EQ(vpi_compare_objects(get(vpiRhs, owners.at(name)), expression), 1);
    if (name == "nested_boundary")
      continue;
    EXPECT_EQ(vpi_get(vpiOpType, expression), vpiCastOp);
    EXPECT_EQ(children(vpiOperand, expression).size(), 1u);
    auto type = get(vpiTypespec, expression);
    ASSERT_NE(type, nullptr);
    int expectedType =
        name == "aliased"   ? vpiLogicTypespec
        : name == "wide"    ? vpiBitTypespec
        : name == "to_four" ? vpiLogicTypespec
        : name == "narrow" || name == "nested_narrow" || name == "to_two"
            ? vpiByteTypespec
            : vpiIntTypespec;
    EXPECT_EQ(vpi_get(vpiType, type), expectedType);
  }
  auto aliasedType = get(vpiTypespec, expressions.at("aliased"));
  EXPECT_EQ(
      vpi_compare_objects(aliasedType, get(vpiTypespec, named("top.aliased"))),
      1);
  auto operand = children(vpiOperand, expressions.at("widen"));
  ASSERT_EQ(operand.size(), 1u);
  EXPECT_EQ(vpi_compare_objects(operand[0], named("top.a")), 1);
  EXPECT_EQ(vpi_get(vpiSize, operand[0]), 8);
  operand = children(vpiOperand, expressions.at("nested_widen"));
  ASSERT_EQ(operand.size(), 1u);
  EXPECT_EQ(vpi_get(vpiOpType, operand[0]), vpiAddOp);
  EXPECT_EQ(vpi_get(vpiSize, operand[0]), 32);
  EXPECT_EQ(vpi_get(vpiSigned, operand[0]), 0);
  EXPECT_STREQ(vpi_get_str(vpiDecompile, expressions.at("widen")),
               "signed'(int'(\\a ))");
  EXPECT_STREQ(vpi_get_str(vpiDecompile, expressions.at("aliased")),
               "signed'(8'(\\f ))");
  // Re-reading the occurrence observes storage; no evaluated value is cached.
  put("a", 6);
  put("s", 1);
  EXPECT_EQ(bits("widen"), "00000000000000000000000000000110");
  EXPECT_EQ(bits("wide"), std::string(129, '0') + "1");
  std::vector<uint64_t> casts;
  uint32_t wideType = UINT32_MAX;
  for (uint32_t index = 0; index != directory.getExpressionNodeCount();
       ++index) {
    uint64_t offset = directory.getExpressionNodeOffset() +
                      uint64_t(index) * ExpressionNodeLayout.size;
    ExpressionNodeView node(image.data() + offset);
    if (node.getKind() == vpiOperation && node.getCode() == vpiCastOp) {
      casts.push_back(offset);
      EXPECT_NE(node.getSemanticTypeIndex(), UINT32_MAX);
      EXPECT_EQ(node.getFrozenValueIndex(), UINT32_MAX);
      EXPECT_EQ(node.getOperandCount(), 1u);
      SemanticTypeView type(image.data() + directory.getSemanticTypeOffset() +
                            uint64_t(node.getSemanticTypeIndex()) *
                                SemanticTypeLayout.size);
      if (type.getLeft() == 129)
        wideType = node.getSemanticTypeIndex();
    }
  }
  ASSERT_EQ(casts.size(), 10u);
  ASSERT_NE(wideType, UINT32_MAX);
  uint64_t contextualCast = UINT64_MAX;
  for (uint32_t index = 0; index != directory.getExpressionTemplateCount();
       ++index) {
    ExpressionTemplateView recipe(
        image.data() + directory.getExpressionTemplateOffset() +
        uint64_t(index) * ExpressionTemplateLayout.size);
    uint64_t offset =
        directory.getExpressionNodeOffset() +
        uint64_t(recipe.getFirstNode()) * ExpressionNodeLayout.size;
    ExpressionNodeView root(image.data() + offset);
    if (root.getKind() != vpiOperation || root.getCode() != vpiCastOp)
      continue;
    uint32_t child =
        ExpressionOperandView(
            image.data() + directory.getExpressionOperandOffset() +
            uint64_t(recipe.getFirstOperand() + root.getFirstOperand()) *
                ExpressionOperandLayout.size)
            .getTarget();
    if (!(child & UINT32_C(0x80000000)) &&
        ExpressionNodeView(image.data() + offset +
                           uint64_t(child) * ExpressionNodeLayout.size)
                .getCode() == vpiAddOp)
      contextualCast = offset;
  }
  ASSERT_NE(contextualCast, UINT64_MAX);
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
    ExpressionNodeWriter(bytes.data() + casts.front())
        .setSemanticTypeIndex(UINT32_MAX);
  });
  rejects([&](auto &bytes) {
    ExpressionNodeWriter(bytes.data() + casts.front()).setCode(vpiAddOp);
  });
  rejects([&](auto &bytes) {
    ExpressionNodeWriter(bytes.data() + casts.front()).setOperandCount(0);
  });
  rejects([&](auto &bytes) {
    // Both types are valid, but the addition was not evaluated in the wider
    // assignment context required by this cast target.
    ExpressionNodeWriter(bytes.data() + contextualCast)
        .setSemanticTypeIndex(wideType);
  });
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
