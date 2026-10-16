//===- Shared type parameter original defaults ----------------------------===//
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
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, SharedTypeDefaults) {
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
  auto named = [&](const std::string &name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name.c_str()), nullptr));
  };
  auto related = [&](int selector, vpiHandle source) {
    return keep(vpi_handle(selector, source));
  };
  std::array<vpiHandle, 2> declarations{}, originals{};
  for (unsigned index = 0; index != 2; ++index) {
    std::string name = index ? "right" : "left";
    auto owner = named(name);
    auto first = named(name + ".T"), second = named(name + ".U");
    auto third = named(name + ".V"), text = named(name + ".S");
    for (auto handle : {owner, first, second, third, text})
      ASSERT_NE(handle, nullptr);
    EXPECT_EQ(vpi_get(vpiType, first), vpiTypeParameter);
    EXPECT_EQ(vpi_get(vpiLineNo, first), 10);
    auto original = related(vpiExpr, first),
         effective = related(vpiTypespec, first);
    ASSERT_NE(original, nullptr);
    ASSERT_NE(effective, nullptr);
    EXPECT_EQ(vpi_get(vpiType, original), vpiIntTypespec);
    EXPECT_EQ(vpi_get_str(vpiName, original), nullptr);
    EXPECT_EQ(vpi_get(vpiType, effective),
              index ? vpiIntTypespec : vpiBitTypespec);
    if (!index) {
      auto left = related(vpiLeftRange, effective);
      auto right = related(vpiRightRange, effective);
      ASSERT_NE(left, nullptr);
      ASSERT_NE(right, nullptr);
      s_vpi_value value{};
      value.format = vpiIntVal;
      vpi_get_value(left, &value);
      EXPECT_EQ(value.value.integer, 7);
      vpi_get_value(right, &value);
      EXPECT_EQ(value.value.integer, 0);
    }
    EXPECT_EQ(vpi_get(vpiLineNo, original), 30);
    EXPECT_STREQ(vpi_get_str(vpiFile, original), "original-types.sv");
    EXPECT_EQ(vpi_get(vpiLineNo, effective), 10);
    // Source provenance does not change equal underlying type identity.
    EXPECT_EQ(vpi_compare_objects(original, effective), index ? 1 : 0);
    EXPECT_EQ(vpi_compare_objects(related(vpiInstance, original), owner), 1);
    EXPECT_EQ(vpi_compare_objects(related(vpiExpr, second), first), 1);
    EXPECT_EQ(vpi_compare_objects(related(vpiExpr, third), second), 1);
    EXPECT_EQ(vpi_get(vpiType, related(vpiTypespec, second)),
              index ? vpiChandleTypespec : vpiStringTypespec);
    auto originalString = related(vpiExpr, text);
    ASSERT_NE(originalString, nullptr);
    EXPECT_EQ(vpi_get(vpiType, originalString), vpiStringTypespec);
    EXPECT_EQ(vpi_get(vpiLineNo, originalString), 40);
    EXPECT_EQ(vpi_get(vpiType, related(vpiTypespec, text)),
              index ? vpiStringTypespec : vpiChandleTypespec);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    auto absent = named(name + ".Absent");
    ASSERT_NE(absent, nullptr);
    EXPECT_EQ(related(vpiExpr, absent), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    auto unavailable = named(name + ".Unsupported");
    ASSERT_NE(unavailable, nullptr);
    EXPECT_EQ(related(vpiExpr, unavailable), nullptr);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
    declarations[index] = first;
    originals[index] = original;
  }
  EXPECT_EQ(vpi_compare_objects(declarations[0], declarations[1]), 0);
  EXPECT_EQ(vpi_compare_objects(originals[0], originals[1]), 0);
  for (auto handle : handles)
    EXPECT_EQ(vpi_free_object(handle), 1);
  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  EXPECT_EQ(directory.getDefinitionCount(), 1u);
  EXPECT_EQ(directory.getDefinitionMemberCount(), 6u);
  EXPECT_EQ(directory.getExpressionRootBindingCount(), 4u);
  EXPECT_EQ(directory.getExpressionTemplateCount(), 0u);
  EXPECT_EQ(directory.getExpressionNodeCount(), 0u);
  EXPECT_EQ(directory.getExpressionOperandCount(), 0u);
  EXPECT_EQ(directory.getExpressionLeafCount(), 2u);
  uint32_t concrete = UINT32_MAX, direct = UINT32_MAX;
  for (uint32_t index = 0; index != directory.getExpressionRootBindingCount();
       ++index) {
    ExpressionRootBindingView root(
        image.data() + directory.getExpressionRootBindingOffset() +
        uint64_t{index} * ExpressionRootBindingLayout.size);
    if (root.getMode() & expressionRootConcreteTypeDefault)
      concrete = index;
    else
      direct = index;
  }
  ASSERT_NE(concrete, UINT32_MAX);
  ASSERT_NE(direct, UINT32_MAX);
  auto copy = [&] { return std::vector<uint8_t>(image.begin(), image.end()); };
  auto rootAt = [&](std::vector<uint8_t> &bytes, uint32_t index) {
    return ExpressionRootBindingWriter(
        bytes.data() + directory.getExpressionRootBindingOffset() +
        uint64_t{index} * ExpressionRootBindingLayout.size);
  };
  auto reject = [&](std::vector<uint8_t> bytes) {
    uint64_t checksum = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != bytes.size(); ++index) {
      checksum ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : bytes[index];
      checksum *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(checksum);
    auto malformed = execution;
    malformed.design_database = bytes.data();
    malformed.design_database_size = bytes.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&malformed),
              OBELISK_RT_INVALID_DESIGN);
  };
  uint32_t rangedBit = UINT32_MAX;
  for (uint32_t index = 0; index != directory.getSemanticTypeCount(); ++index) {
    SemanticTypeView type(image.data() + directory.getSemanticTypeOffset() +
                          uint64_t{index} * SemanticTypeLayout.size);
    if ((type.getKindAndFlags() & 255) == OBELISK_RT_DESIGN_SEMANTIC_BIT &&
        type.getLeft() == 7 && type.getRight() == 0)
      rangedBit = index;
  }
  ASSERT_NE(rangedBit, UINT32_MAX);
  auto bytes = copy();
  rootAt(bytes, concrete).setTemplateIndex(rangedBit);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, concrete).setTemplateIndex(UINT32_MAX);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, concrete).setMode(expressionRootConcreteTypeDefault);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, concrete).setMode(expressionRootDefinitionMember);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, concrete)
      .setMode(expressionRootDefinitionMember |
               expressionRootConcreteTypeDefault | 1);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, concrete).setFirstLeaf(UINT32_MAX);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, concrete).setFirstLeaf(UINT32_MAX - 1);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, concrete).setOrdinal(0);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, concrete).setOrdinal(UINT32_MAX);
  reject(std::move(bytes));
  bytes = copy();
  rootAt(bytes, direct)
      .setMode(expressionRootDefinitionMember |
               expressionRootConcreteTypeDefault);
  reject(std::move(bytes));
  bytes = copy();
  // Absent is otherwise valid: isolate mandatory original-root completeness.
  DefinitionMemberWriter(bytes.data() + directory.getDefinitionMemberOffset() +
                         4 * DefinitionMemberLayout.size)
      .setFlags(1u << 11);
  reject(std::move(bytes));
}
TEST(GeneratedDesignDatabase, SharedTypeDefaultImageSize) {
  using namespace obelisk;
  using namespace obelisk::reflection;
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  for (unsigned count : {1u, 32u, 128u}) {
    std::string text = R"mlir(
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#bit = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @repeated {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T" {has_original_default}
    simulation.vpi_definition_member.decl @U of @cell type 609 ordinal 1 name "U" {has_original_default}
    simulation.vpi_definition_type_default @T type #int loc(unknown)
    simulation.vpi_relation.decl <kind = symbol, symbol = @U> selector 102 handle ordinal 0 to <kind = symbol, symbol = @T>
    simulation.vpi_definition_specialization.decl @same of @cell
    simulation.vpi_definition_member.specialize @same member @T type #bit
    simulation.vpi_definition_member.specialize @same member @U type #bit
)mlir";
    for (unsigned index = 0; index != count; ++index)
      text += "simulation.scope.decl " + std::to_string(index + 1) +
              " parent 0 hierarchy \"i" + std::to_string(index) +
              "\" vpi_kind 32 definition @cell specialization @same\n";
    text += R"mlir(
    simulation.code_unit.decl 1 in 1 initial hierarchy "i0.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} { simulation.return }
  } }
)mlir";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    ASSERT_TRUE(module);
    auto baseline = mlir::OwningOpRef<mlir::ModuleOp>(module->clone());
    auto stripped = *baseline->getOps<sim::SimDesignOp>().begin();
    for (auto member : stripped.getOps<sim::SimVPIDefinitionMemberDeclOp>())
      member.removeHasOriginalDefaultAttr();
    for (auto &operation :
         llvm::make_early_inc_range(stripped.getBody().front()))
      if (mlir::isa<sim::SimVPIDefinitionTypeDefaultOp,
                    sim::SimVPIRelationDeclOp>(operation))
        operation.erase();
    SimulationBytecodeOptions options;
    options.vpi = "read";
    auto shared = encodeSimulationDesign(
        *module->getOps<sim::SimDesignOp>().begin(), options);
    auto bare = encodeSimulationDesign(stripped, options);
    ASSERT_TRUE(mlir::succeeded(shared));
    ASSERT_TRUE(mlir::succeeded(bare));
    for (const auto *encoded : {&*shared, &*bare}) {
      obelisk_rt_execution_descriptor_v1 execution{};
      execution.version = OBELISK_RT_VERSION;
      execution.flags = encoded->executionFlags;
      execution.bytecode = encoded->bytecode.data();
      execution.bytecode_size = encoded->bytecode.size();
      execution.design_database = encoded->designDatabase.data();
      execution.design_database_size = encoded->designDatabase.size();
      execution.state_bit_count = encoded->stateBitCount;
      for (unsigned byte = 0; byte != 8; ++byte)
        execution.checksum |= uint64_t{encoded->bytecode[32 + byte]}
                              << (byte * 8);
      ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
    }
    const auto &bytes = shared->designDatabase;
    HeaderView header(bytes.data());
    SemanticDirectoryView directory(bytes.data() + header.getReserved());
    EXPECT_EQ(directory.getExpressionRootBindingCount(), 2u);
    EXPECT_EQ(directory.getExpressionLeafCount(), 1u);
    EXPECT_EQ(directory.getExpressionTemplateCount(), 0u);
    EXPECT_EQ(directory.getExpressionNodeCount(), 0u);
    EXPECT_EQ(directory.getDefinitionMemberCount(), 2u);
    EXPECT_EQ(directory.getDefinitionSpecializationCount(), 1u);
    uint64_t growth = bytes.size() - bare->designDatabase.size();
    HeaderView baselineHeader(bare->designDatabase.data());
    SemanticDirectoryView baselineDirectory(bare->designDatabase.data() +
                                            baselineHeader.getReserved());
    EXPECT_EQ(directory.getSemanticTypeCount(),
              baselineDirectory.getSemanticTypeCount() + 1);
    EXPECT_EQ(header.getStringSize(), baselineHeader.getStringSize());
    EXPECT_EQ(header.getIndexCount(), baselineHeader.getIndexCount());
    uint64_t padding = header.getIndexOffset() - header.getStringOffset() -
                       header.getStringSize();
    uint64_t baselinePadding = baselineHeader.getIndexOffset() -
                               baselineHeader.getStringOffset() -
                               baselineHeader.getStringSize();
    // Exactly two Root20 rows, one leaf word and one original semantic type;
    // final index alignment is independently measured from each valid image.
    EXPECT_EQ(growth + baselinePadding - padding,
              2 * ExpressionRootBindingLayout.size + ExpressionLeafLayout.size +
                  SemanticTypeLayout.size);
    RecordProperty("bytes_" + std::to_string(count), bytes.size());
    RecordProperty("original_type_growth_" + std::to_string(count), growth);
  }
}
} // namespace
