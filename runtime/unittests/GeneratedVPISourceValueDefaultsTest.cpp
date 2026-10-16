//===- GeneratedVPISourceValueDefaultsTest.cpp - Parameter source defaults
//--===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace obelisk::reflection;

uint64_t imageChecksum(const std::vector<uint8_t> &bytes) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index != bytes.size(); ++index) {
    hash ^= index >= 32 && index < 40 ? uint8_t{0} : bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

TEST(GeneratedDesignDatabase, SourceValueDefaults) {
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
    EXPECT_NE(handle, nullptr);
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto integer = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.integer;
  };
  auto operands = [&](vpiHandle operation) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(vpiOperand, operation);
    EXPECT_NE(iterator, nullptr);
    if (!iterator)
      return result;
    auto use = keep(vpi_handle(vpiUse, iterator));
    EXPECT_EQ(vpi_compare_objects(use, operation), 1);
    while (auto operand = vpi_scan(iterator))
      result.push_back(keep(operand));
    return result;
  };
  auto binary = [](uint32_t value) {
    std::string result = "32'sb";
    for (unsigned bit = 32; bit != 0; --bit)
      result += value & (uint32_t{1} << (bit - 1)) ? '1' : '0';
    return result;
  };
  auto decompile = [&](vpiHandle expression) {
    const char *value = vpi_get_str(vpiDecompile, expression);
    EXPECT_NE(value, nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value ? std::string(value) : std::string{};
  };
  auto generic = named("G");
  EXPECT_EQ(vpi_iterate(vpiClassTypespec, generic), nullptr);
  auto iterator = vpi_iterate(vpiParameter, generic);
  ASSERT_NE(iterator, nullptr);
  std::array<vpiHandle, 3> source{};
  for (auto &parameter : source)
    parameter = keep(vpi_scan(iterator));
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  for (size_t index = 0; index != source.size(); ++index) {
    EXPECT_STREQ(vpi_get_str(vpiName, source[index]),
                 (std::array<const char *, 3>{"N", "T", "M"})[index]);
    EXPECT_EQ(vpi_get(vpiType, source[index]),
              index == 1 ? vpiTypeParameter : vpiParameter);
    auto scope = keep(vpi_handle(vpiScope, source[index]));
    EXPECT_EQ(vpi_compare_objects(scope, generic), 1);
  }
  for (auto parameter : {source[0], source[2]}) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    value.value.integer = 456;
    vpi_get_value(parameter, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    for (PLI_INT32 property : {vpiSize, vpiSigned, vpiConstType}) {
      EXPECT_EQ(vpi_get(property, parameter), vpiUndefined);
      EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    }
    EXPECT_EQ(vpi_handle(vpiTypespec, parameter), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  }
  auto sourceLiteral = keep(vpi_handle(vpiExpr, source[0]));
  EXPECT_EQ(vpi_get(vpiType, sourceLiteral), vpiConstant);
  EXPECT_EQ(integer(sourceLiteral), 4);
  EXPECT_EQ(vpi_get(vpiSigned, sourceLiteral), 1);
  EXPECT_EQ(vpi_get(vpiSize, sourceLiteral), 32);
  EXPECT_EQ(vpi_get(vpiConstType, sourceLiteral), vpiIntConst);
  EXPECT_EQ(decompile(sourceLiteral), binary(4));
  auto sourceAdd = keep(vpi_handle(vpiExpr, source[2]));
  EXPECT_EQ(vpi_get(vpiOpType, sourceAdd), vpiAddOp);
  auto sourceOperands = operands(sourceAdd);
  ASSERT_EQ(sourceOperands.size(), 2u);
  EXPECT_EQ(vpi_get(vpiType, sourceOperands[0]), vpiParameter);
  EXPECT_EQ(vpi_compare_objects(sourceOperands[0], source[0]), 1);
  EXPECT_EQ(integer(sourceOperands[1]), 2);
  EXPECT_EQ(decompile(sourceAdd), "\\N + " + binary(2));

  auto definition = named("C");
  iterator = vpi_iterate(vpiClassTypespec, definition);
  ASSERT_NE(iterator, nullptr);
  auto canonical = keep(vpi_scan(iterator));
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  for (auto owner : {definition, canonical}) {
    SCOPED_TRACE(owner == definition ? "lexical" : "specialization");
    iterator = vpi_iterate(vpiParameter, owner);
    ASSERT_NE(iterator, nullptr);
    std::array<vpiHandle, 7> parameters{};
    for (auto &parameter : parameters)
      parameter = keep(vpi_scan(iterator));
    EXPECT_EQ(vpi_scan(iterator), nullptr);
    const std::array<const char *, 7> names{"P",   "Q",          "R",       "S",
                                            "E.p", "UseEscaped", "Repeated"};
    const std::array<int, 7> effective{9, 11, 33, -1, 6, 8, 18};
    for (size_t index = 0; index != parameters.size(); ++index) {
      EXPECT_STREQ(vpi_get_str(vpiName, parameters[index]), names[index]);
      EXPECT_EQ(integer(parameters[index]), effective[index]);
      auto scope = keep(vpi_handle(vpiScope, parameters[index]));
      EXPECT_EQ(vpi_compare_objects(scope, owner), 1);
    }
    auto original = keep(vpi_handle(vpiExpr, parameters[0]));
    EXPECT_EQ(integer(original), 4);
    EXPECT_EQ(decompile(parameters[0]), "\\P ");
    EXPECT_EQ(vpi_compare_objects(original, parameters[0]), 0);
    auto repeated = keep(vpi_handle(vpiExpr, parameters[0]));
    EXPECT_EQ(vpi_compare_objects(repeated, original), 1);
    auto add = keep(vpi_handle(vpiExpr, parameters[1]));
    auto children = operands(add);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(vpi_compare_objects(children[0], parameters[0]), 1);
    EXPECT_EQ(integer(children[0]), 9);
    EXPECT_EQ(integer(children[1]), 2);
    EXPECT_EQ(decompile(add), "\\P + " + binary(2));
    s_vpi_value unavailable{};
    unavailable.format = vpiIntVal;
    vpi_get_value(add, &unavailable);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    EXPECT_EQ(vpi_get(vpiSize, add), vpiUndefined);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    auto multiply = keep(vpi_handle(vpiExpr, parameters[2]));
    EXPECT_EQ(vpi_get(vpiOpType, multiply), vpiMultOp);
    EXPECT_EQ(decompile(multiply), "(\\P + " + binary(2) + ") * " + binary(3));
    auto negative = keep(vpi_handle(vpiExpr, parameters[3]));
    EXPECT_EQ(integer(negative), -1);
    EXPECT_EQ(vpi_get(vpiSigned, negative), 1);
    EXPECT_EQ(decompile(negative), binary(UINT32_MAX));
    auto escaped = keep(vpi_handle(vpiExpr, parameters[5]));
    children = operands(escaped);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(vpi_compare_objects(children[0], parameters[4]), 1);
    EXPECT_EQ(decompile(escaped), "\\E.p + " + binary(2));
    auto repeatedParameter = keep(vpi_handle(vpiExpr, parameters[6]));
    children = operands(repeatedParameter);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(vpi_compare_objects(children[0], parameters[0]), 1);
    EXPECT_EQ(vpi_compare_objects(children[1], parameters[0]), 1);
    EXPECT_EQ(vpi_compare_objects(children[0], children[1]), 1);
    EXPECT_EQ(decompile(repeatedParameter), "\\P + \\P ");
  }

  auto cursor = [&](const char *name) {
    obelisk_rt_design_cursor_v1 result{};
    EXPECT_EQ(obelisk_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(name),
                  std::strlen(name), &result),
              OBELISK_RT_OK);
    return result;
  };
  auto expression = [&](obelisk_rt_design_cursor_v1 parameter) {
    VPIRelationRange range{};
    obelisk_rt_design_cursor_v1 result{};
    uint32_t type = 0;
    bool statement = false;
    EXPECT_EQ(obelisk_rt_cached_vpi_relation_range(runtime, parameter, vpiExpr,
                                                   false, &range),
              OBELISK_RT_OK);
    EXPECT_EQ(range.count, 1u);
    EXPECT_EQ(obelisk_rt_cached_vpi_relation_target(runtime, range.first,
                                                    &result, &type, &statement),
              OBELISK_RT_OK);
    return result;
  };
  const auto n = cursor("G::N");
  const auto m = cursor("G::M");
  const auto g = cursor("G");
  const auto literal = expression(n);
  const auto add = expression(m);
  obelisk_rt_design_cursor_v1 semantic{};
  ASSERT_EQ(obelisk_rt_cached_design_semantic_root(runtime, literal, &semantic),
            OBELISK_RT_OK);
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
                            header.getObjectCount() * ObjectLayout.size) {
      EXPECT_TRUE(tryPackTableIndex(TableKind::Object,
                                    (object.offset - header.getObjectOffset()) /
                                        ObjectLayout.size,
                                    result));
    } else {
      EXPECT_GE(object.offset, directory.getStaticObjectOffset());
      EXPECT_LT(object.offset,
                directory.getStaticObjectOffset() +
                    directory.getStaticObjectCount() * StaticObjectLayout.size);
      EXPECT_TRUE(tryPackTableIndex(
          TableKind::StaticObject,
          (object.offset - directory.getStaticObjectOffset()) /
              StaticObjectLayout.size,
          result));
    }
    return result;
  };
  const uint32_t nPacked = packed(n), addPacked = packed(add),
                 literalPacked = packed(literal);
  auto setFlags = [&](std::vector<uint8_t> &bytes,
                      obelisk_rt_design_cursor_v1 object, uint32_t flags) {
    if (unpackTableIndexKind(packed(object)) == TableKind::Object)
      ObjectWriter(bytes.data() + object.offset).setCaps(flags);
    else
      StaticObjectWriter(bytes.data() + object.offset).setFlags(flags);
  };
  auto rejects = [&](auto corrupt) {
    std::vector<uint8_t> bytes(image.begin(), image.end());
    corrupt(bytes);
    HeaderWriter(bytes.data()).setChecksum(imageChecksum(bytes));
    auto invalid = execution;
    invalid.design_database = bytes.data();
    invalid.design_database_size = bytes.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejects([&](auto &bytes) { setFlags(bytes, n, 0); });
  rejects([&](auto &bytes) {
    setFlags(bytes, g, OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER);
  });
  rejects([&](auto &bytes) {
    setFlags(bytes, n,
             OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER |
                 OBELISK_RT_DESIGN_CAP_UNAVAILABLE_PARAMETER_DEFAULT);
  });
  auto operandRow = [&] {
    for (uint64_t index = 0; index != header.getRelationCount(); ++index) {
      const uint64_t row =
          header.getRelationOffset() + index * RelationLayout.size;
      RelationView relation(image.data() + row);
      uint32_t source = 0;
      if (tryPackTableIndex(
              unpackRelationSourceTable(relation.getSourceKindAndTable()),
              relation.getSourceIndex(), source) &&
          source == addPacked && relation.getSelector() == vpiOperand &&
          relation.getOrdinal() == 0)
        return row;
    }
    return uint64_t{0};
  }();
  ASSERT_NE(operandRow, 0u);
  EXPECT_EQ(RelationView(image.data() + operandRow).getTargetIndexAndTable(),
            nPacked);
  rejects(
      [&](auto &bytes) { // An occurrence cannot have two owning expressions.
        RelationWriter(bytes.data() + operandRow)
            .setTargetIndexAndTable(literalPacked);
      });
  rejects([&](auto &bytes) { // A source operation cannot be its own operand.
    RelationWriter(bytes.data() + operandRow).setTargetIndexAndTable(addPacked);
  });
  rejects([&](auto &bytes) { // Shared leaves do not permit sparse ordinals.
    RelationWriter(bytes.data() + operandRow).setOrdinal(2);
  });
  uint64_t opProperty = 0;
  for (uint64_t index = 0; index != directory.getFixedPropertyCount();
       ++index) {
    uint64_t row =
        directory.getFixedPropertyOffset() + index * FixedPropertyLayout.size;
    FixedPropertyView property(image.data() + row);
    if (property.getSourceIndexAndTable() == addPacked &&
        property.getSelector() == vpiOpType)
      opProperty = row;
  }
  ASSERT_NE(opProperty, 0u);
  rejects([&](auto &bytes) {
    FixedPropertyWriter(bytes.data() + opProperty).setPayload(vpiMinusOp);
  });
  ASSERT_NE(semantic.offset, 0u);
  ASSERT_LE(semantic.offset, directory.getSemanticTypeCount());
  uint32_t valueIndex = UINT32_MAX;
  for (uint64_t index = 0; index != directory.getFrozenValueBindingCount();
       ++index) {
    FrozenValueBindingView binding(image.data() +
                                   directory.getFrozenValueBindingOffset() +
                                   index * FrozenValueBindingLayout.size);
    if (binding.getSourceIndexAndTable() == literalPacked)
      valueIndex = binding.getValue();
  }
  ASSERT_LT(valueIndex, directory.getFrozenValueCount());
  std::vector<uint32_t> literalSources;
  for (uint64_t index = 0; index != directory.getFrozenValueBindingCount();
       ++index) {
    FrozenValueBindingView binding(image.data() +
                                   directory.getFrozenValueBindingOffset() +
                                   index * FrozenValueBindingLayout.size);
    if (binding.getValue() != valueIndex)
      continue;
    const uint32_t source = binding.getSourceIndexAndTable();
    ASSERT_EQ(unpackTableIndexKind(source), TableKind::Object);
    ObjectView object(image.data() + header.getObjectOffset() +
                      unpackTableIndex(source) * ObjectLayout.size);
    ASSERT_EQ(object.getKindAndVPIKind() >> 16,
              static_cast<uint32_t>(vpiConstant));
    literalSources.push_back(source);
  }
  ASSERT_FALSE(literalSources.empty());
  rejects([&](auto &bytes) {
    // Relocate the semantic table and add a valid ShortReal32 node. Keep all
    // shared int32 types unchanged so only the source-constant restriction
    // rejects this otherwise width/flag-compatible frozen value.
    const uint64_t newOffset = (bytes.size() + 7) & ~uint64_t{7};
    const uint64_t count = directory.getSemanticTypeCount();
    bytes.resize(newOffset + (count + 1) * SemanticTypeLayout.size, 0);
    std::memcpy(bytes.data() + newOffset,
                image.data() + directory.getSemanticTypeOffset(),
                count * SemanticTypeLayout.size);
    auto type = SemanticTypeWriter(bytes.data() + newOffset +
                                   count * SemanticTypeLayout.size);
    type.setKindAndFlags(OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL |
                         (uint32_t{vpiShortRealTypespec}
                          << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
    type.setAliasObjectIndexAndTable(UINT32_MAX);
    type.setIdentityTargetIndexAndTable(UINT32_MAX);
    auto updated = SemanticDirectoryWriter(bytes.data() + header.getReserved());
    updated.setSemanticTypeOffset(newOffset);
    updated.setSemanticTypeCount(count + 1);
    HeaderWriter(bytes.data()).setImageSize(bytes.size());
    for (uint32_t source : literalSources) {
      bool found = false;
      for (uint64_t index = 0; index != directory.getSemanticRootBindingCount();
           ++index) {
        const uint64_t row = directory.getSemanticRootBindingOffset() +
                             index * SemanticRootBindingLayout.size;
        if (SemanticRootBindingView(bytes.data() + row)
                .getObjectIndexAndTable() != source)
          continue;
        SemanticRootBindingWriter(bytes.data() + row).setSemanticType(count);
        found = true;
        break;
      }
      EXPECT_TRUE(found);
    }
    const uint64_t row =
        directory.getFrozenValueOffset() + valueIndex * FrozenValueLayout.size;
    FrozenValueWriter(bytes.data() + row)
        .setKindAndFlags(FrozenValueView(bytes.data() + row).getKindAndFlags() &
                         ~frozenValueSigned);
  });
}
} // namespace
