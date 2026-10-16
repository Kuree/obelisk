//===- GeneratedVPIParameterAssignmentsTest.cpp ---------------------------===//
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

TEST(GeneratedDesignDatabase, SharedParameterAssignments) {
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
  auto integer = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), 0);
    return value.value.integer;
  };
  auto assignments = [&](vpiHandle scope) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(vpiParamAssign, scope);
    if (iterator)
      while (auto next = vpi_scan(iterator))
        result.push_back(keep(next));
    return result;
  };
  auto left = named("left"), right = named("right");
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  auto a = assignments(left), b = assignments(right);
  ASSERT_EQ(a.size(), 6u);
  ASSERT_EQ(b.size(), 6u);
  // Locate by exact Lhs identity, independent of the unordered iterator order.
  auto find = [&](const std::vector<vpiHandle> &list, const char *target) {
    auto expected = named(target);
    for (auto assignment : list)
      if (vpi_compare_objects(keep(vpi_handle(vpiLhs, assignment)), expected))
        return assignment;
    return vpiHandle{};
  };
  auto literal = find(a, "left.a.N"), reference = find(a, "left.b.N");
  auto otherLiteral = find(b, "right.a.N"),
       otherReference = find(b, "right.b.N");
  ASSERT_NE(literal, nullptr);
  ASSERT_NE(reference, nullptr);
  ASSERT_NE(otherLiteral, nullptr);
  ASSERT_NE(otherReference, nullptr);
  EXPECT_FALSE(vpi_compare_objects(literal, otherLiteral));
  EXPECT_TRUE(vpi_compare_objects(keep(vpi_handle(vpiScope, literal)), left));
  EXPECT_TRUE(vpi_compare_objects(keep(vpi_handle(vpiModule, literal)), left));
  EXPECT_EQ(vpi_get(vpiConnByName, literal), 0);
  EXPECT_EQ(vpi_get(vpiConnByName, reference), 1);
  EXPECT_EQ(integer(keep(vpi_handle(vpiRhs, literal))), 7);
  auto target = keep(vpi_handle(vpiLhs, literal));
  EXPECT_EQ(integer(target), 7);
  EXPECT_EQ(integer(keep(vpi_handle(vpiExpr, target))), 3);
  EXPECT_TRUE(vpi_compare_objects(keep(vpi_handle(vpiRhs, reference)),
                                  named("left.P")));
  EXPECT_TRUE(vpi_compare_objects(keep(vpi_handle(vpiRhs, otherReference)),
                                  named("right.P")));
  EXPECT_EQ(integer(keep(vpi_handle(vpiRhs, reference))), 2);
  EXPECT_EQ(integer(keep(vpi_handle(vpiRhs, otherReference))), 5);
  auto concrete = find(a, "left.a.U"), typeReference = find(a, "left.b.U");
  ASSERT_NE(concrete, nullptr);
  ASSERT_NE(typeReference, nullptr);
  auto original = keep(vpi_handle(vpiRhs, concrete));
  ASSERT_NE(original, nullptr);
  EXPECT_EQ(vpi_get(vpiType, original), vpiByteTypespec);
  EXPECT_EQ(vpi_get(vpiLineNo, original), 15);
  EXPECT_STREQ(vpi_get_str(vpiFile, original), "override.sv");
  EXPECT_TRUE(vpi_compare_objects(keep(vpi_handle(vpiRhs, typeReference)),
                                  named("left.T")));
  auto childTypeParameter = keep(vpi_handle(vpiLhs, concrete));
  EXPECT_EQ(vpi_get(vpiType, keep(vpi_handle(vpiExpr, childTypeParameter))),
            vpiIntTypespec);
  for (const char *unavailable : {"left.c.N", "left.c.U"}) {
    auto assignment = find(a, unavailable);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(vpi_handle(vpiRhs, assignment), nullptr);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  }
  using namespace reflection;
  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  EXPECT_EQ(directory.getDefinitionMemberCount(), 10u);
  EXPECT_EQ(directory.getDefinitionMemberEndpointCount(), 12u);
  EXPECT_EQ(directory.getExpressionBoundMemberCount(), 12u);
  EXPECT_EQ(directory.getExpressionRootBindingCount(), 6u);
  EXPECT_FALSE(vpi_compare_objects(keep(vpi_handle(vpiRhs, literal)),
                                   keep(vpi_handle(vpiRhs, otherLiteral))));
  // Refresh the checksum so each corruption reaches the intended metadata
  // proof.
  auto reject = [&](auto mutate) {
    auto bytes = encoded->designDatabase;
    mutate(bytes);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i != bytes.size(); ++i) {
      hash ^= i >= field::HeaderChecksum && i < field::HeaderChecksum + 8
                  ? 0
                  : bytes[i];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  std::vector<uint32_t> assignmentMembers, parameterMembers;
  for (uint32_t i = 0; i != directory.getDefinitionMemberCount(); ++i) {
    DefinitionMemberView member(encoded->designDatabase.data() +
                                directory.getDefinitionMemberOffset() +
                                i * DefinitionMemberLayout.size);
    if (member.getVPIKind() == vpiParamAssign)
      assignmentMembers.push_back(i);
    if (member.getVPIKind() == vpiParameter)
      parameterMembers.push_back(i);
  }
  ASSERT_EQ(assignmentMembers.size(), 6u);
  ASSERT_EQ(parameterMembers.size(), 2u);
  auto memberOffset = [&](uint32_t i) {
    return directory.getDefinitionMemberOffset() +
           i * DefinitionMemberLayout.size;
  };
  auto endpointOffset = [&](uint32_t i) {
    return directory.getDefinitionMemberEndpointOffset() +
           i * DefinitionMemberEndpointLayout.size;
  };
  auto boundOffset = [&](uint32_t i) {
    return directory.getExpressionBoundMemberOffset() +
           i * ExpressionBoundMemberLayout.size;
  };
  reject([&](auto &bytes) {
    DefinitionMemberWriter(bytes.data() + memberOffset(assignmentMembers[0]))
        .setName(DefinitionMemberView(bytes.data() +
                                      memberOffset(parameterMembers[0]))
                     .getName());
  });
  reject([&](auto &bytes) {
    auto offset = memberOffset(assignmentMembers[0]);
    DefinitionMemberWriter(bytes.data() + offset)
        .setFlags(DefinitionMemberView(bytes.data() + offset).getFlags() ^ 2);
  });
  reject([&](auto &bytes) {
    DefinitionMemberEndpointWriter(bytes.data() + endpointOffset(0))
        .setTargetIndexAndTable(UINT32_MAX);
  });
  // Swap endpoints across parents, retaining complete canonical pool usage.
  reject([&](auto &bytes) {
    uint32_t left =
        DefinitionMemberEndpointView(bytes.data() + endpointOffset(0))
            .getTargetIndexAndTable();
    uint32_t right =
        DefinitionMemberEndpointView(bytes.data() + endpointOffset(6))
            .getTargetIndexAndTable();
    DefinitionMemberEndpointWriter(bytes.data() + endpointOffset(0))
        .setTargetIndexAndTable(right);
    DefinitionMemberEndpointWriter(bytes.data() + endpointOffset(6))
        .setTargetIndexAndTable(left);
  });
  reject([&](auto &bytes) {
    DefinitionMemberEndpointWriter(bytes.data() + endpointOffset(1))
        .setTargetIndexAndTable(
            DefinitionMemberEndpointView(bytes.data() + endpointOffset(0))
                .getTargetIndexAndTable());
  });
  reject([&](auto &bytes) {
    auto row = DefinitionMemberEndpointView(bytes.data() + endpointOffset(0))
                   .getTargetIndexAndTable();
    ExpressionBoundMemberWriter(bytes.data() + boundOffset(row))
        .setMemberIndex(assignmentMembers[0]);
  });
  reject([&](auto &bytes) {
    auto row = DefinitionMemberEndpointView(bytes.data() + endpointOffset(0))
                   .getTargetIndexAndTable();
    uint32_t member = ExpressionBoundMemberView(bytes.data() + boundOffset(row))
                          .getMemberIndex();
    auto offset = memberOffset(member);
    DefinitionMemberWriter(bytes.data() + offset)
        .setFlags(DefinitionMemberView(bytes.data() + offset).getFlags() |
                  (1u << 8));
  });
  reject([&](auto &bytes) {
    auto offset = memberOffset(assignmentMembers[0]);
    DefinitionMemberWriter(bytes.data() + offset).setFlags(1u << 10);
  });
  reject([&](auto &bytes) {
    auto offset = memberOffset(assignmentMembers.back());
    DefinitionMemberWriter(bytes.data() + offset)
        .setFlags(DefinitionMemberView(bytes.data() + offset).getFlags() & ~1u);
  });
  reject([&](auto &bytes) {
    for (uint32_t i = 0; i != directory.getExpressionRootBindingCount(); ++i) {
      auto offset = directory.getExpressionRootBindingOffset() +
                    i * ExpressionRootBindingLayout.size;
      if (ExpressionRootBindingView(bytes.data() + offset).getSelector() ==
          vpiRhs) {
        ExpressionRootBindingWriter(bytes.data() + offset).setSelector(vpiExpr);
        break;
      }
    }
  });
  // This unavailable assignment has no recipe or root; setting completeness
  // must fail independently of orphan-pool checks.
  reject([&](auto &bytes) {
    DefinitionMemberWriter(bytes.data() + memberOffset(assignmentMembers[4]))
        .setFlags(1u | (1u << 11));
  });
  for (auto handle : handles)
    vpi_release_handle(handle);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);
}

TEST(GeneratedDesignDatabase, SharedParameterAssignmentsImageSize) {
  using namespace obelisk;
  using namespace obelisk::reflection;
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  struct Measurement {
    size_t bytes;
    uint64_t definitions, members, roots, nodes, values, endpoints,
        boundMembers;
  };
  auto measure = [&](unsigned count, bool shared) -> Measurement {
    std::string text = R"mlir(
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
 simulation.design @repeated {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @child7 of @child
  simulation.vpi_definition_member.specialize @child7 member @N type #int {immutable_value = #seven}
)mlir";
    // The expanded baseline duplicates the source definition and its two
    // distinct assignment occurrences for each parent. Both representations
    // retain the same per-instance Lhs/Rhs facts and intern the literal value.
    for (unsigned definition = 0; definition != (shared ? 1 : count);
         ++definition) {
      std::string suffix = std::to_string(definition);
      text += "  simulation.vpi_definition.decl @parent" + suffix +
              " type 32 name \"parent\"\n";
      text += "  simulation.vpi_definition_specialization.decl "
              "@parent_specialization" +
              suffix + " of @parent" + suffix + "\n";
      for (unsigned occurrence = 0; occurrence != 2; ++occurrence) {
        std::string id = std::to_string(2 * definition + occurrence);
        text += "  simulation.vpi_definition_member.decl @assignment" + id +
                " of @parent" + suffix + " type 40 ordinal " +
                std::to_string(occurrence) + " name \"\" {has_original_rhs}\n";
        text += "  simulation.vpi_expression.decl @rhs" + id + " id " + id +
                " type 7 owned_by <kind = symbol, symbol = @assignment" + id +
                "> selector 82 handle ordinal 0 : i32 {definition = @parent" +
                suffix + ", immutable_value = #seven, vpi_type = #int}\n";
        text +=
            "  simulation.vpi_relation.decl <kind = symbol, symbol = "
            "@assignment" +
            id +
            "> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs" +
            id + ">\n";
      }
    }
    for (unsigned index = 0; index != count; ++index) {
      std::string scope = std::to_string(1 + index * 3);
      std::string suffix = std::to_string(shared ? 0 : index);
      std::string path = "i" + std::to_string(index);
      text += "  simulation.scope.decl " + scope + " parent 0 hierarchy \"" +
              path + "\" vpi_kind 32 definition @parent" + suffix +
              " specialization @parent_specialization" + suffix + "\n";
      for (unsigned occurrence = 0; occurrence != 2; ++occurrence) {
        std::string child = std::to_string(2 + index * 3 + occurrence);
        std::string id = std::to_string(2 * (shared ? 0 : index) + occurrence);
        text += "  simulation.scope.decl " + child + " parent " + scope +
                " hierarchy \"" + path + "." +
                std::string(1, char('a' + occurrence)) +
                "\" vpi_kind 32 definition @child specialization @child7\n";
        text += "  simulation.vpi_definition_member.bind scope " + scope +
                " member @assignment" + id +
                " expr <kind = definition_member, id = " + child +
                ", symbol = @N>\n";
      }
    }
    text += R"mlir(
  simulation.code_unit.decl 1 in 1 initial hierarchy "i0.initial"
  simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} { simulation.return }
 }
})mlir";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    EXPECT_TRUE(module);
    if (!module)
      return {};
    SimulationBytecodeOptions options;
    options.vpi = "read";
    auto encoded = encodeSimulationDesign(
        *module->getOps<sim::SimDesignOp>().begin(), options);
    EXPECT_TRUE(mlir::succeeded(encoded));
    if (mlir::failed(encoded))
      return {};
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
    EXPECT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
    obelisk_rt_context *raw = nullptr;
    EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &raw),
              OBELISK_RT_OK);
    if (!raw)
      return {};
    std::unique_ptr<obelisk_rt_context,
                    decltype(&obelisk_rt_v1_context_destroy)>
        runtime(raw, obelisk_rt_v1_context_destroy);
    EXPECT_EQ(obelisk_rt_v1_vpi_startup(raw, nullptr, 0), OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_vpi_end_compile(raw), OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_vpi_start_simulation(raw), OBELISK_RT_OK);
    auto parent = vpi_handle_by_name(const_cast<char *>("i0"), nullptr);
    auto iterator = vpi_iterate(vpiParamAssign, parent);
    EXPECT_NE(iterator, nullptr);
    auto first = iterator ? vpi_scan(iterator) : nullptr;
    auto second = iterator ? vpi_scan(iterator) : nullptr;
    EXPECT_NE(first, nullptr);
    EXPECT_NE(second, nullptr);
    if (first && second) {
      EXPECT_EQ(vpi_scan(iterator), nullptr);
      auto firstRhs = vpi_handle(vpiRhs, first),
           secondRhs = vpi_handle(vpiRhs, second);
      auto firstLhs = vpi_handle(vpiLhs, first),
           secondLhs = vpi_handle(vpiLhs, second);
      EXPECT_FALSE(vpi_compare_objects(first, second));
      EXPECT_FALSE(vpi_compare_objects(firstRhs, secondRhs));
      EXPECT_FALSE(vpi_compare_objects(firstLhs, secondLhs));
      for (auto valueHandle : {firstRhs, secondRhs, firstLhs, secondLhs}) {
        s_vpi_value value{};
        value.format = vpiIntVal;
        vpi_get_value(valueHandle, &value);
        EXPECT_EQ(value.value.integer, 7);
        s_vpi_error_info error{};
        EXPECT_EQ(vpi_chk_error(&error), 0);
        vpi_release_handle(valueHandle);
      }
      vpi_release_handle(first);
      vpi_release_handle(second);
    }
    vpi_release_handle(parent);
    HeaderView header(encoded->designDatabase.data());
    SemanticDirectoryView directory(encoded->designDatabase.data() +
                                    header.getReserved());
    return {encoded->designDatabase.size(),
            directory.getDefinitionCount(),
            directory.getDefinitionMemberCount(),
            directory.getExpressionRootBindingCount(),
            directory.getExpressionNodeCount(),
            directory.getFrozenValueCount(),
            directory.getDefinitionMemberEndpointCount(),
            directory.getExpressionBoundMemberCount()};
  };
  for (unsigned count : {1u, 32u, 128u}) {
    auto shared = measure(count, true), expanded = measure(count, false);
    EXPECT_EQ(shared.definitions, 2u);
    EXPECT_EQ(shared.members, 3u);
    EXPECT_EQ(shared.roots, 2u);
    EXPECT_EQ(shared.nodes, 2u);
    EXPECT_EQ(shared.values, 1u);
    EXPECT_EQ(expanded.definitions, count + 1);
    EXPECT_EQ(expanded.members, 2 * count + 1);
    EXPECT_EQ(expanded.roots, 2 * count);
    EXPECT_EQ(expanded.nodes, 2 * count);
    EXPECT_EQ(expanded.values, 1u);
    EXPECT_EQ(shared.endpoints, 2 * count);
    EXPECT_EQ(shared.boundMembers, 2 * count);
    EXPECT_EQ(expanded.endpoints, shared.endpoints);
    EXPECT_EQ(expanded.boundMembers, shared.boundMembers);
    if (count == 1)
      EXPECT_EQ(shared.bytes, expanded.bytes);
    else
      EXPECT_LT(shared.bytes, expanded.bytes);
    RecordProperty("shared_" + std::to_string(count), shared.bytes);
    RecordProperty("expanded_" + std::to_string(count), expanded.bytes);
  }
}

TEST(GeneratedDesignDatabase, ParameterAssignmentsInInstanceScopes) {
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
  auto integer = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), 0);
    return value.value.integer;
  };
  auto assignments = [&](vpiHandle scope) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(vpiParamAssign, scope);
    if (iterator)
      while (auto next = vpi_scan(iterator))
        result.push_back(keep(next));
    return result;
  };
  auto top = named("top");
  ASSERT_NE(top, nullptr);
  std::vector<vpiHandle> literals;
  for (auto [path, kind, value] : {std::tuple{"top.i", vpiInterface, 11},
                                   std::tuple{"top.j", vpiInterface, 13},
                                   std::tuple{"loose", vpiInterface, 19}}) {
    SCOPED_TRACE(path);
    auto parent = named(path);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(vpi_get(vpiType, parent), kind);
    auto list = assignments(parent);
    ASSERT_EQ(list.size(), 3u);
    auto lhsA = named((std::string(path) + ".a.N").c_str());
    auto lhsB = named((std::string(path) + ".b.N").c_str());
    auto lhsType = named((std::string(path) + ".a.U").c_str());
    auto p = named((std::string(path) + ".P").c_str());
    auto t = named((std::string(path) + ".T").c_str());
    ASSERT_NE(lhsA, nullptr);
    ASSERT_NE(lhsB, nullptr);
    ASSERT_NE(lhsType, nullptr);
    auto parentModule = keep(vpi_handle(vpiModule, p));
    if (std::string_view(path) == "loose")
      EXPECT_EQ(parentModule, nullptr);
    else
      EXPECT_TRUE(vpi_compare_objects(parentModule, top));
    auto typeModule = keep(vpi_handle(vpiModule, t));
    if (std::string_view(path) == "loose")
      EXPECT_EQ(typeModule, nullptr);
    else
      EXPECT_TRUE(vpi_compare_objects(typeModule, top));
    EXPECT_TRUE(vpi_compare_objects(keep(vpi_handle(vpiInstance, p)), parent));
    bool seenA = false, seenB = false, seenType = false;
    for (auto assignment : list) {
      EXPECT_EQ(vpi_get(vpiType, assignment), vpiParamAssign);
      EXPECT_TRUE(
          vpi_compare_objects(keep(vpi_handle(vpiScope, assignment)), parent));
      auto moduleHandle = keep(vpi_handle(vpiModule, assignment));
      if (std::string_view(path) == "loose")
        EXPECT_EQ(moduleHandle, nullptr);
      else
        EXPECT_TRUE(vpi_compare_objects(moduleHandle, top));
      auto lhs = keep(vpi_handle(vpiLhs, assignment));
      auto rhs = keep(vpi_handle(vpiRhs, assignment));
      ASSERT_NE(rhs, nullptr);
      if (vpi_compare_objects(lhs, lhsA)) {
        EXPECT_FALSE(seenA);
        seenA = true;
        EXPECT_TRUE(vpi_compare_objects(rhs, p));
        EXPECT_EQ(integer(rhs), value);
        EXPECT_EQ(integer(lhs), value);
        EXPECT_EQ(vpi_get(vpiConnByName, assignment), 1);
      } else if (vpi_compare_objects(lhs, lhsB)) {
        EXPECT_FALSE(seenB);
        seenB = true;
        EXPECT_EQ(vpi_get(vpiType, rhs), vpiConstant);
        EXPECT_EQ(integer(rhs), 7);
        EXPECT_EQ(vpi_get(vpiConnByName, assignment), 0);
        auto rhsModule = keep(vpi_handle(vpiModule, rhs));
        if (std::string_view(path) == "loose")
          EXPECT_EQ(rhsModule, nullptr);
        else
          EXPECT_TRUE(vpi_compare_objects(rhsModule, top));
        EXPECT_TRUE(
            vpi_compare_objects(keep(vpi_handle(vpiInstance, rhs)), parent));
        literals.push_back(rhs);
      } else {
        EXPECT_TRUE(vpi_compare_objects(lhs, lhsType));
        EXPECT_FALSE(seenType);
        seenType = true;
        EXPECT_EQ(vpi_get(vpiType, rhs), vpiTypeParameter);
        EXPECT_TRUE(vpi_compare_objects(rhs, t));
      }
    }
    EXPECT_TRUE(seenA);
    EXPECT_TRUE(seenB);
    EXPECT_TRUE(seenType);
  }
  ASSERT_EQ(literals.size(), 3u);
  for (unsigned i = 0; i != literals.size(); ++i)
    for (unsigned j = i + 1; j != literals.size(); ++j)
      EXPECT_FALSE(vpi_compare_objects(literals[i], literals[j]));
  EXPECT_EQ(vpi_iterate(vpiParamAssign, top), nullptr);
  using namespace reflection;
  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  unsigned sharedAssignments = 0;
  for (uint64_t i = 0; i != directory.getDefinitionMemberCount(); ++i) {
    DefinitionMemberView member(encoded->designDatabase.data() +
                                directory.getDefinitionMemberOffset() +
                                i * DefinitionMemberLayout.size);
    sharedAssignments += member.getVPIKind() == vpiParamAssign;
  }
  // Three repeated interfaces share one declaration/RHS inventory.
  // Only child endpoints are contextual.
  EXPECT_EQ(sharedAssignments, 3u);
  EXPECT_EQ(directory.getDefinitionMemberEndpointCount(), 9u);
  // An ancestor relation may skip an Interface, but may not retarget a
  // different module. Locate the exact physical source and target by IDs.
  uint32_t otherIndex = UINT32_MAX;
  for (uint32_t i = 0; i != header.getScopeCount(); ++i) {
    ScopeView scope(encoded->designDatabase.data() + header.getScopeOffset() +
                    i * ScopeLayout.size);
    const char *name = reinterpret_cast<const char *>(
        encoded->designDatabase.data() + scope.getName());
    if (std::string_view(name) == "other")
      otherIndex = i;
  }
  ASSERT_NE(otherIndex, UINT32_MAX);
  bool mutated = false;
  auto bytes = encoded->designDatabase;
  for (uint32_t i = 0; i != header.getRelationCount(); ++i) {
    uint64_t offset = header.getRelationOffset() + i * RelationLayout.size;
    RelationView relation(bytes.data() + offset);
    if (unpackRelationSourceTable(relation.getSourceKindAndTable()) !=
            TableKind::Scope ||
        unpackRelationSourceKind(relation.getSourceKindAndTable()) !=
            vpiInterface ||
        relation.getSelector() != vpiModule)
      continue;
    ScopeView sourceScope(bytes.data() + header.getScopeOffset() +
                          relation.getSourceIndex() * ScopeLayout.size);
    if (sourceScope.getParent() == 0)
      continue;
    ScopeView parentScope(bytes.data() + sourceScope.getParent());
    if (unpackRecordKindPayload(parentScope.getKindAndVPIKind()) !=
        vpiInterface)
      continue;
    uint32_t target = 0;
    ASSERT_TRUE(tryPackTableIndex(TableKind::Scope, otherIndex, target));
    RelationWriter(bytes.data() + offset).setTargetIndexAndTable(target);
    mutated = true;
    break;
  }
  ASSERT_TRUE(mutated);
  {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i != bytes.size(); ++i) {
      uint8_t byte = i >= field::HeaderChecksum && i < field::HeaderChecksum + 8
                         ? 0
                         : bytes[i];
      hash = (hash ^ byte) * UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  }

  for (auto handle : handles)
    vpi_free_object(handle);
}
