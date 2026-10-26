//===- GeneratedVPIBitSelectionsTest.cpp - Selected bits ---------===//
#include "../lib/RuntimeInternal.h"
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
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, BitSelections) {
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
  auto expression = [&](const char *name) {
    std::string path = std::string("top.") + name;
    auto process = named(path.c_str());
    auto statement = keep(vpi_handle(vpiStmt, process));
    auto result = keep(vpi_handle(vpiRhs, statement));
    EXPECT_NE(result, nullptr) << name;
    return result;
  };
  auto compare = [&](vpiHandle a, vpiHandle b, int expected) {
    EXPECT_EQ(vpi_compare_objects(a, b), expected);
    EXPECT_EQ(vpi_compare_objects(b, a), expected);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto write = [&](vpiHandle object, int value, unsigned unknown = 0) {
    s_vpi_vecval vector{static_cast<PLI_UINT32>(value), unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto read = [&](vpiHandle object) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    vpi_get_value(object, &data);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return data.value.integer;
  };
  auto children = [&](vpiHandle parent, int selector) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, parent);
    if (iterator) {
      auto use = keep(vpi_handle(vpiUse, iterator));
      compare(use, parent, 1);
      while (auto item = vpi_scan(iterator))
        result.push_back(keep(item));
    }
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return result;
  };
  auto statement = [&](const char *name) {
    return keep(
        vpi_handle(vpiStmt, named((std::string("top.") + name).c_str())));
  };
  auto expectUses = [&](vpiHandle selected,
                        std::initializer_list<const char *> names) {
    auto uses = children(selected, vpiUse);
    ASSERT_EQ(uses.size(), names.size());
    std::vector<bool> seen(uses.size(), false);
    for (const auto *name : names) {
      auto owner = statement(name);
      size_t match = uses.size();
      for (size_t index = 0; index != uses.size(); ++index)
        if (vpi_compare_objects(uses[index], owner)) {
          EXPECT_FALSE(seen[index]);
          seen[index] = true;
          match = index;
        }
      EXPECT_LT(match, uses.size()) << name;
    }
  };
  auto top = named("top"), a = named("top.a"), index = named("top.index");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(index, nullptr);
  write(a, 0xa5);
  write(index, 7);
  write(named("top.ascword"), 0x80);
  auto first = expression("first"), again = expression("again"),
       dynamic = expression("dynamic");
  for (auto bit : {first, again, dynamic}) {
    EXPECT_EQ(vpi_get(vpiType, bit), vpiRegBit);
    EXPECT_EQ(vpi_get(vpiSize, bit), 1);
    compare(keep(vpi_handle(vpiParent, bit)), a, 1);
    compare(keep(vpi_handle(vpiScope, bit)), top, 1);
    compare(keep(vpi_handle(vpiModule, bit)), top, 1);
    compare(keep(vpi_handle(vpiInstance, bit)), top, 1);
    EXPECT_EQ(read(bit), 1);
    EXPECT_STREQ(vpi_get_str(vpiName, bit), "a[7]");
    EXPECT_STREQ(vpi_get_str(vpiFullName, bit), "top.a[7]");
    compare(bit, keep(vpi_handle_by_index(a, 7)), 1);
    auto indices = children(bit, vpiIndex);
    ASSERT_EQ(indices.size(), 1u);
    EXPECT_EQ(read(indices.front()), 7);
    compare(indices.front(), keep(vpi_handle(vpiIndex, bit)), 1);
  }
  EXPECT_EQ(vpi_get(vpiConstantSelect, first), 1);
  EXPECT_EQ(vpi_get(vpiConstantSelect, dynamic), 0);
  compare(keep(vpi_handle(vpiIndex, first)), keep(vpi_handle(vpiIndex, again)),
          0);
  compare(keep(vpi_handle(vpiIndex, dynamic)), index, 1);
  EXPECT_EQ(read(expression("ascending")), 1);
  EXPECT_STREQ(vpi_get_str(vpiName, expression("ascending")), "ascword[0]");
  auto net = expression("net"), netword = named("top.netword");
  EXPECT_EQ(vpi_get(vpiType, net), vpiNetBit);
  compare(net, keep(vpi_handle_by_index(netword, 7)), 1);
  auto function =
      std::find_if(encoded->functions.begin(), encoded->functions.end(),
                   [](const auto &entry) { return entry.symbol == "run_net"; });
  ASSERT_NE(function, encoded->functions.end());
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, function->index, 0};
  uint64_t frameSize = 0, alignment = 0;
  ASSERT_EQ(
      obelisk_rt_validate_design_bytecode(entry, raw, &frameSize, &alignment),
      OBELISK_RT_OK);
  ASSERT_LE(alignment, alignof(uint64_t));
  std::vector<uint64_t> frame((frameSize + 7) / 8);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_execute_design_bytecode(entry, raw, frame.data(),
                                               frame.size() * 8, 0, frameSize,
                                               0, 0, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  s_vpi_value strength{};
  strength.format = vpiStrengthVal;
  vpi_get_value(net, &strength);
  ASSERT_EQ(vpi_chk_error(nullptr), 0);
  ASSERT_NE(strength.value.strength, nullptr);
  EXPECT_EQ(strength.value.strength[0].logic, vpi1);
  EXPECT_EQ(strength.value.strength[0].s0, vpiWeakDrive);
  EXPECT_EQ(strength.value.strength[0].s1, vpiWeakDrive);
  auto parameter = expression("parameter");
  EXPECT_EQ(vpi_get(vpiType, parameter), vpiBitSelect);
  EXPECT_EQ(read(parameter), 1);
  compare(keep(vpi_handle(vpiParent, parameter)), named("top.P"), 1);
  compare(keep(vpi_handle(vpiModule, parameter)), top, 1);
  compare(parameter, expression("other_parameter"), 0);
  expectUses(first, {"first", "again", "dynamic", "overlap", "whole"});
  expectUses(keep(vpi_handle_by_index(a, 7)),
             {"first", "again", "dynamic", "overlap", "whole"});
  expectUses(expression("low"), {"low", "whole"});
  write(index, 0);
  EXPECT_EQ(read(dynamic), 1);
  EXPECT_STREQ(vpi_get_str(vpiName, dynamic), "a[0]");
  compare(dynamic, keep(vpi_handle_by_index(a, 0)), 1);
  compare(dynamic, first, 0);
  expectUses(first, {"first", "again", "overlap", "whole"});
  expectUses(dynamic, {"low", "dynamic", "whole"});
  expectUses(keep(vpi_handle_by_index(a, 0)), {"low", "dynamic", "whole"});
  write(index, 6);
  EXPECT_EQ(read(dynamic), 0);
  write(a, 0x40);
  EXPECT_EQ(read(dynamic), 1);
  for (auto unavailable :
       {expression("invalid"), expression("negative"), expression("unknown")}) {
    EXPECT_EQ(vpi_get(vpiConstantSelect, unavailable), 1);
    compare(unavailable, unavailable, 0);
    EXPECT_EQ(vpi_get_str(vpiName, unavailable), nullptr);
    s_vpi_error_info error{};
    ASSERT_NE(vpi_chk_error(&error), 0);
    EXPECT_EQ(error.level, vpiNotice);
  }
  write(index, 7, 1);
  compare(dynamic, dynamic, 0);
  EXPECT_EQ(vpi_get(vpiConstantSelect, dynamic), 0);
  write(index, 7);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  using namespace obelisk::reflection;
  auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  uint32_t selectedNode = UINT32_MAX, wideSemantic = UINT32_MAX;
  for (uint32_t i = 0; i != directory.getExpressionNodeCount(); ++i) {
    ExpressionNodeView node(image.data() + directory.getExpressionNodeOffset() +
                            i * ExpressionNodeLayout.size);
    if (selectedNode == UINT32_MAX && node.getKind() == vpiRegBit)
      selectedNode = i;
    if (node.getKind() == vpiPartSelect)
      wideSemantic = node.getSemanticTypeIndex();
  }
  ASSERT_NE(selectedNode, UINT32_MAX);
  ASSERT_NE(wideSemantic, UINT32_MAX);
  for (int mutation = 0; mutation != 4; ++mutation) {
    auto bytes = image;
    ExpressionNodeWriter node(bytes.data() +
                              directory.getExpressionNodeOffset() +
                              selectedNode * ExpressionNodeLayout.size);
    if (mutation == 0)
      node.setKind(vpiNetBit);
    if (mutation == 1)
      node.setCode(1);
    if (mutation == 2)
      node.setOperandCount(3);
    if (mutation == 3)
      node.setSemanticTypeIndex(wideSemantic);
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
              OBELISK_RT_INVALID_DESIGN)
        << mutation;
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}

TEST(GeneratedDesignDatabase, SourceBitSelections) {
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

  auto top = named("top"), a = named("top.\\a.with.dot "),
       index = named("top.index"), logic = named("top.l");
  ASSERT_NE(top, nullptr);
  ASSERT_NE(a, nullptr);
  // Exact overrides are sparse: escaped identifiers need one existing Name
  // property each; ordinary declaration names keep their zero-row fallback.
  for (const auto &name :
       {std::string("top.\\a.with.dot "), std::string("top.\\n.with.dot "),
        std::string("top.index"), std::string("top.l")}) {
    obelisk_rt_design_cursor_v1 cursor{};
    ASSERT_EQ(obelisk_rt_v1_design_lookup(
                  &execution, reinterpret_cast<const uint8_t *>(name.data()),
                  name.size(), &cursor),
              OBELISK_RT_OK);
    VPIFixedPropertyValue exact{};
    auto status =
        obelisk_rt_cached_vpi_fixed_property(raw, cursor, vpiName, &exact);
    if (name.find("with.dot") != std::string::npos) {
      ASSERT_EQ(status, OBELISK_RT_OK);
      EXPECT_EQ(std::string(reinterpret_cast<const char *>(exact.stringData),
                            exact.stringSize),
                name[5] == 'a' ? "a.with.dot" : "n.with.dot");
    } else
      EXPECT_EQ(status, OBELISK_RT_EOF);
  }
  auto write = [&](vpiHandle object, int value, unsigned unknown = 0) {
    s_vpi_vecval vector{static_cast<PLI_UINT32>(value), unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto read = [&](vpiHandle object) {
    s_vpi_value data{};
    data.format = vpiBinStrVal;
    vpi_get_value(object, &data);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return std::string(data.value.str ? data.value.str : "");
  };
  write(a, 0xa5);
  write(logic, 0xa5);
  write(index, 7);
  std::map<std::string, vpiHandle> selections;
  auto processes = vpi_iterate(vpiProcess, top);
  ASSERT_NE(processes, nullptr);
  while (auto process = vpi_scan(processes)) {
    keep(process);
    auto statement = keep(vpi_handle(vpiStmt, process));
    ASSERT_NE(statement, nullptr);
    auto lhs = keep(vpi_handle(vpiLhs, statement));
    ASSERT_NE(lhs, nullptr);
    auto name = vpi_get_str(vpiName, lhs);
    ASSERT_NE(name, nullptr);
    std::string local(name);
    auto rhs = keep(vpi_handle(vpiRhs, statement));
    ASSERT_NE(rhs, nullptr) << local;
    selections.emplace(local, rhs);
  }
  ASSERT_EQ(selections.size(), 6u);
  for (const auto &entry : selections) {
    auto rhs = entry.second;
    uint32_t expected = entry.first == "net_bit"         ? vpiNetBit
                        : entry.first == "parameter_bit" ? vpiBitSelect
                                                         : vpiRegBit;
    EXPECT_EQ(vpi_get(vpiType, rhs), expected) << entry.first;
    EXPECT_EQ(vpi_get(vpiSize, rhs), 1);
    auto typespec = keep(vpi_handle(vpiTypespec, rhs));
    ASSERT_NE(typespec, nullptr);
    EXPECT_EQ(vpi_get(vpiType, typespec),
              entry.first == "logic_bit" || entry.first == "net_bit"
                  ? vpiLogicTypespec
                  : vpiBitTypespec);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiModule, rhs)), top), 1);
    auto indexExpression = keep(vpi_handle(vpiIndex, rhs));
    ASSERT_NE(indexExpression, nullptr);
    if (entry.first == "constant_bit" || entry.first == "parameter_bit" ||
        entry.first == "computed_bit") {
      EXPECT_EQ(vpi_get(vpiConstantSelect, rhs), 1);
      EXPECT_EQ(vpi_get(vpiType, indexExpression),
                entry.first == "computed_bit" ? vpiOperation : vpiConstant);
    } else {
      EXPECT_EQ(vpi_get(vpiConstantSelect, rhs), 0);
      EXPECT_EQ(vpi_compare_objects(indexExpression, index), 1);
    }
    if (entry.first != "net_bit")
      EXPECT_EQ(read(rhs), "1");
  }
  EXPECT_STREQ(vpi_get_str(vpiDecompile, selections.at("dynamic_bit")),
               "\\a.with.dot [ \\index ]");
  EXPECT_STREQ(vpi_get_str(vpiDecompile, selections.at("constant_bit")),
               "\\a.with.dot [ 32'sb00000000000000000000000000000111 ]");
  EXPECT_EQ(vpi_compare_objects(
                keep(vpi_handle(vpiParent, selections.at("parameter_bit"))),
                named("top.P")),
            1);
  write(index, 6);
  EXPECT_EQ(read(selections.at("dynamic_bit")), "0");
  EXPECT_EQ(read(selections.at("logic_bit")), "0");
  auto actual = keep(vpi_handle_by_index(a, 6));
  EXPECT_EQ(vpi_compare_objects(selections.at("dynamic_bit"), actual), 1);
  std::string actualName(vpi_get_str(vpiName, actual));
  EXPECT_EQ(actualName, "a.with.dot[6]");
  EXPECT_EQ(std::string(vpi_get_str(vpiName, selections.at("dynamic_bit"))),
            actualName);
  EXPECT_STREQ(vpi_get_str(vpiFullName, a), "top.\\a.with.dot ");
  std::string actualFullName(vpi_get_str(vpiFullName, actual));
  EXPECT_EQ(actualFullName, "top.\\a.with.dot [6]");
  EXPECT_EQ(std::string(vpi_get_str(vpiFullName, selections.at("dynamic_bit"))),
            actualFullName);
  auto netParent = named("top.\\n.with.dot ");
  auto netActual = keep(vpi_handle_by_index(netParent, 6));
  EXPECT_STREQ(vpi_get_str(vpiName, netActual), "n.with.dot[6]");
  EXPECT_STREQ(vpi_get_str(vpiName, selections.at("net_bit")), "n.with.dot[6]");
  EXPECT_STREQ(vpi_get_str(vpiFullName, selections.at("net_bit")),
               "top.\\n.with.dot [6]");
  write(index, -1);
  EXPECT_EQ(read(selections.at("dynamic_bit")), "0");
  EXPECT_EQ(read(selections.at("logic_bit")), "x");
  write(index, 0, 1);
  EXPECT_EQ(read(selections.at("logic_bit")), "x");
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
} // namespace
