//===- GeneratedVPIIntegralOperatorsTest.cpp - Operator values -----------===//

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

TEST(GeneratedDesignDatabase, IntegralExpressionOperators) {
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
  auto expression = [&](const char *name) {
    std::string path = "top.";
    path += name;
    return get(vpiRhs, get(vpiStmt, named(path.c_str())));
  };
  auto put = [&](const char *name, uint32_t value, uint32_t unknown = 0) {
    std::string path = "top.";
    path += name;
    auto object = named(path.c_str());
    EXPECT_NE(object, nullptr);
    s_vpi_vecval vector{value ^ unknown, unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto bits = [&](vpiHandle object) {
    s_vpi_value data{};
    data.format = vpiBinStrVal;
    vpi_get_value(object, &data);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return data.value.str ? std::string(data.value.str) : std::string{};
  };
  put("a", 6);
  put("b", 3);
  put("negative", 250);
  put("signed_three", 3);
  put("mixed", 128, 1);
  put("wild", 128, 127);
  put("zero", 0);
  put("unknown", 0, 255);
  put("minus_one", 255);
  put("minus_three", 253);
  struct Expected {
    const char *name;
    int operation, width;
    uint32_t value, unknown;
  };
  const Expected expected[] = {
      {"minus", 1, 8, 250, 0},
      {"plus", 2, 8, 6, 0},
      {"logical_not", 3, 1, 0, 0},
      {"bitwise_not", 4, 8, 249, 0},
      {"reduce_and", 5, 1, 0, 0},
      {"reduce_nand", 6, 1, 1, 0},
      {"reduce_or", 7, 1, 1, 0},
      {"reduce_nor", 8, 1, 0, 0},
      {"reduce_xor", 9, 1, 0, 0},
      {"reduce_xnor", 10, 1, 1, 0},
      {"subtract", 11, 8, 3, 0},
      {"divide", 12, 8, 2, 0},
      {"modulo", 13, 8, 0, 0},
      {"equal", 14, 1, 0, 0},
      {"not_equal", 15, 1, 1, 0},
      {"case_equal", 16, 1, 0, 0},
      {"case_not_equal", 17, 1, 1, 0},
      {"greater", 18, 1, 1, 0},
      {"greater_equal", 19, 1, 1, 0},
      {"less", 20, 1, 0, 0},
      {"less_equal", 21, 1, 0, 0},
      {"shift_left", 22, 8, 48, 0},
      {"shift_right", 23, 8, 0, 0},
      {"add", 24, 8, 9, 0},
      {"multiply", 25, 8, 18, 0},
      {"logical_and", 26, 1, 1, 0},
      {"logical_or", 27, 1, 1, 0},
      {"and", 28, 8, 2, 0},
      {"or", 29, 8, 7, 0},
      {"xor", 30, 8, 5, 0},
      {"xnor", 31, 8, 250, 0},
      {"conditional", 32, 8, 6, 0},
      {"arithmetic_left", 41, 8, 208, 0},
      {"arithmetic_right", 42, 8, 255, 0},
      {"power", 43, 8, 216, 0},
      {"implies", 50, 1, 1, 0},
      {"wild_equal", 69, 1, 1, 0},
      {"wild_not_equal", 70, 1, 0, 0},
      {"signed_divide", 12, 8, 254, 0},
      {"signed_remainder", 13, 8, 0, 0},
      {"signed_less", 20, 1, 1, 0},
      {"unknown_known_mismatch", 14, 1, 0, 0},
      {"unknown_equal", 14, 1, 0, 1},
      {"unknown_and_zero", 26, 1, 0, 0},
      {"known_one_dominates", 32, 8, 6, 0},
      {"ambiguous_merge", 32, 8, 128, 127},
      {"zero_divisor", 12, 8, 0, 255},
      {"unknown_shift", 22, 8, 0, 255},
      {"negative_power", 43, 8, 255, 0},
      {"zero_negative_power", 43, 8, 0, 255},
      {"positive_negative_power", 43, 8, 0, 0},
      {"bit_known_zero", 28, 8, 0, 0},
      {"unknown_reduce_and", 5, 1, 0, 1},
  };
  for (const auto &item : expected) {
    SCOPED_TRACE(item.name);
    auto object = expression(item.name);
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(vpi_get(vpiOpType, object), item.operation);
    EXPECT_EQ(vpi_get(vpiSize, object), item.width);
    ASSERT_NE(get(vpiTypespec, object), nullptr);
    std::string expectedBits;
    for (unsigned bit = item.width; bit != 0;) {
      uint32_t mask = uint32_t{1} << --bit;
      expectedBits += item.unknown & mask ? (item.value & mask ? 'z' : 'x')
                                          : (item.value & mask ? '1' : '0');
    }
    EXPECT_EQ(bits(object), expectedBits);
  }
  put("negative", 249);
  EXPECT_EQ(bits(expression("signed_remainder")), "11111111");
  put("negative", 250);
  auto widened = expression("widened");
  ASSERT_NE(widened, nullptr);
  EXPECT_EQ(bits(widened), "0000000000010010");
  put("a", 250);
  put("b", 10);
  EXPECT_EQ(bits(widened), "0000001000001000"); // (250+10)<<1, no 8-bit wrap.
  EXPECT_EQ(bits(expression("unsigned_context")), "1111111100000111");
  EXPECT_STREQ(vpi_get_str(vpiDecompile, widened), "\\a + \\b << 8'b00000001");
  auto operands = vpi_iterate(vpiOperand, widened);
  ASSERT_NE(operands, nullptr);
  auto sum = keep(vpi_scan(operands));
  auto shift = keep(vpi_scan(operands));
  ASSERT_NE(sum, nullptr);
  ASSERT_NE(shift, nullptr);
  EXPECT_EQ(vpi_scan(operands), nullptr);
  EXPECT_EQ(vpi_get(vpiSize, sum), 16);
  EXPECT_EQ(bits(sum), "0000000100000100");
  EXPECT_EQ(bits(expression("wide")), "01" + std::string(127, '0') + "1");
  EXPECT_EQ(bits(expression("wide_unknown")), std::string(130, 'x'));
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  uint32_t u8 = UINT32_MAX, s8 = UINT32_MAX, u16 = UINT32_MAX;
  for (uint32_t index = 0; index != directory.getExpressionLeafShapeCount();
       ++index) {
    ExpressionLeafShapeView leaf(image.data() +
                                 directory.getExpressionLeafShapeOffset() +
                                 index * ExpressionLeafShapeLayout.size);
    SemanticTypeView type(image.data() + directory.getSemanticTypeOffset() +
                          leaf.getSemanticTypeIndex() *
                              SemanticTypeLayout.size);
    if (type.getLeft() == 7 && !(type.getKindAndFlags() & 0x200)) {
      if (type.getKindAndFlags() & 0x100)
        s8 = leaf.getSemanticTypeIndex();
      else
        u8 = leaf.getSemanticTypeIndex();
    }
  }
  uint32_t nested = UINT32_MAX, division = UINT32_MAX, unsignedAdd = UINT32_MAX;
  for (uint32_t index = 0; index != directory.getExpressionNodeCount();
       ++index) {
    ExpressionNodeView node(image.data() + directory.getExpressionNodeOffset() +
                            index * ExpressionNodeLayout.size);
    SemanticTypeView type(image.data() + directory.getSemanticTypeOffset() +
                          node.getSemanticTypeIndex() *
                              SemanticTypeLayout.size);
    if (nested == UINT32_MAX && node.getCode() == vpiAddOp &&
        type.getLeft() == 15) {
      nested = index;
      u16 = node.getSemanticTypeIndex();
    }
    if (node.getCode() == vpiAddOp && type.getLeft() == 7 &&
        !(type.getKindAndFlags() & 0x300))
      unsignedAdd = index;
    if (node.getCode() == vpiDivOp && !(type.getKindAndFlags() & 0x100))
      division = index;
  }
  ASSERT_NE(u8, UINT32_MAX);
  ASSERT_NE(s8, UINT32_MAX);
  ASSERT_NE(u16, UINT32_MAX);
  ASSERT_NE(nested, UINT32_MAX);
  ASSERT_NE(division, UINT32_MAX);
  ASSERT_NE(unsignedAdd, UINT32_MAX);
  auto rejects = [&](uint32_t index, uint32_t semantic) {
    auto bytes = image;
    ExpressionNodeWriter(bytes.data() + directory.getExpressionNodeOffset() +
                         index * ExpressionNodeLayout.size)
        .setSemanticTypeIndex(semantic);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t byte = 0; byte != bytes.size(); ++byte) {
      hash ^= byte >= field::HeaderChecksum && byte < field::HeaderChecksum + 8
                  ? 0
                  : bytes[byte];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejects(nested, u8);
  rejects(unsignedAdd, s8);
  rejects(division, u8);
  for (auto handle : handles)
    vpi_release_handle(handle);
}

TEST(GeneratedDesignDatabase, SourceIntegralOperators) {
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
    auto iterator = vpi_iterate(selector, parent);
    if (!iterator)
      return result;
    EXPECT_EQ(vpi_compare_objects(get(vpiUse, iterator), parent), 1);
    while (auto child = vpi_scan(iterator))
      result.push_back(keep(child));
    return result;
  };
  auto top = named("top");
  ASSERT_NE(top, nullptr);
  std::map<std::string, vpiHandle> expressions, owners;
  for (auto process : children(vpiProcess, top)) {
    auto statement = get(vpiStmt, process);
    ASSERT_NE(statement, nullptr);
    ASSERT_EQ(vpi_get(vpiType, statement), vpiAssignment);
    auto lhs = get(vpiLhs, statement);
    ASSERT_NE(lhs, nullptr);
    std::string name = vpi_get_str(vpiName, lhs);
    ASSERT_TRUE(owners.emplace(name, statement).second);
    expressions[name] = get(vpiRhs, statement);
  }
  ASSERT_EQ(owners.size(), 8u);
  EXPECT_EQ(expressions.at("unsupported_cast"), nullptr);
  auto put = [&](const char *name, uint32_t value, uint32_t unknown = 0) {
    std::string path = std::string("top.") + name;
    auto object = named(path.c_str());
    ASSERT_NE(object, nullptr);
    s_vpi_vecval vector{value ^ unknown, unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto bits = [&](const char *name) {
    auto expression = expressions.at(name);
    EXPECT_NE(expression, nullptr);
    s_vpi_value data{};
    data.format = vpiBinStrVal;
    vpi_get_value(expression, &data);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return data.value.str ? std::string(data.value.str) : std::string();
  };
  put("a", 250);
  put("b", 10);
  put("negative", 250);
  put("condition", 0, 255);
  EXPECT_EQ(bits("sum"), "00000100");
  EXPECT_EQ(bits("widened"), "0000001000001000");
  EXPECT_EQ(bits("unsigned_context"), "1111111100000111");
  EXPECT_EQ(bits("less"), "0");
  EXPECT_EQ(bits("choice"), "xxxx1010");
  EXPECT_EQ(bits("inverted"), "00000101");
  EXPECT_EQ(bits("quotient"), "00011001");
  auto widened = expressions.at("widened");
  EXPECT_EQ(vpi_get(vpiSize, widened), 16);
  EXPECT_EQ(vpi_get(vpiSigned, widened), 0);
  EXPECT_STREQ(vpi_get_str(vpiDecompile, widened), "\\a + \\b << 8'b00000001");
  auto operands = children(vpiOperand, widened);
  ASSERT_EQ(operands.size(), 2u);
  EXPECT_EQ(vpi_get(vpiOpType, operands[0]), vpiAddOp);
  EXPECT_EQ(vpi_get(vpiSize, operands[0]), 16);
  auto leaves = children(vpiOperand, operands[0]);
  ASSERT_EQ(leaves.size(), 2u);
  EXPECT_EQ(vpi_compare_objects(leaves[0], named("top.a")), 1);
  EXPECT_EQ(vpi_compare_objects(leaves[1], named("top.b")), 1);
  EXPECT_EQ(vpi_get(vpiSize, leaves[0]), 8);
  for (const auto &[name, expression] : expressions) {
    if (!expression)
      continue;
    EXPECT_EQ(vpi_compare_objects(get(vpiModule, expression), top), 1);
    EXPECT_EQ(vpi_compare_objects(get(vpiRhs, owners.at(name)), expression), 1);
  }
  auto uses = children(vpiUse, named("top.a"));
  const char *expected[] = {"sum",    "widened",  "less",
                            "choice", "inverted", "quotient"};
  ASSERT_EQ(uses.size(), std::size(expected));
  for (auto name : expected)
    EXPECT_EQ(std::count_if(uses.begin(), uses.end(),
                            [&](vpiHandle use) {
                              return vpi_compare_objects(use,
                                                         owners.at(name)) == 1;
                            }),
              1);
  put("a", 6);
  put("b", 3);
  put("condition", 128, 1);
  EXPECT_EQ(bits("sum"), "00001001");
  EXPECT_EQ(bits("widened"), "0000000000010010");
  EXPECT_EQ(bits("choice"), "00000110"); // Known one dominates unknown.
  put("b", 0);
  EXPECT_EQ(bits("quotient"), "xxxxxxxx");
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
