#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
#include "talos/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, SharedInstanceParameters) {
  const char *input = std::getenv("TALOS_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  talos::sim::TalosSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<talos::sim::SimDesignOp>().begin();
  talos::SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
                                               encoded->executionFlags,
                                               0,
                                               encoded->bytecode.data(),
                                               encoded->bytecode.size(),
                                               encoded->designDatabase.data(),
                                               encoded->designDatabase.size(),
                                               encoded->stateBitCount,
                                               checksum,
                                               nullptr,
                                               0,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               0};
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *runtime = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &runtime),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(runtime, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime), TALOS_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](const std::string &name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name.c_str()), nullptr));
  };
  auto children = [&](int selector, vpiHandle parent) {
    std::vector<vpiHandle> result;
    if (auto iterator = vpi_iterate(selector, parent)) {
      EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiUse, iterator)), parent),
                1);
      while (auto child = vpi_scan(iterator))
        result.push_back(keep(child));
    }
    return result;
  };
  auto intValue = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.integer;
  };
  std::vector<std::vector<vpiHandle>> inventories;
  for (const char *name : {"left", "right", "different"}) {
    auto owner = named(name);
    ASSERT_NE(owner, nullptr);
    auto parameters = children(vpiParameter, owner);
    ASSERT_EQ(parameters.size(), 3u);
    EXPECT_STREQ(vpi_get_str(vpiName, parameters[0]), "N");
    EXPECT_STREQ(vpi_get_str(vpiName, parameters[1]), "L.p");
    EXPECT_STREQ(vpi_get_str(vpiName, parameters[2]), "T");
    EXPECT_EQ(vpi_get(vpiLocalParam, parameters[0]), 0);
    EXPECT_EQ(vpi_get(vpiLocalParam, parameters[1]), 1);
    EXPECT_EQ(vpi_get(vpiType, parameters[2]), vpiTypeParameter);
    EXPECT_EQ(intValue(parameters[0]),
              std::string(name) == "different" ? 5 : 4);
    EXPECT_EQ(intValue(parameters[1]), 254);
    EXPECT_EQ(vpi_get(vpiSigned, parameters[0]), 1);
    EXPECT_EQ(vpi_get(vpiSigned, parameters[1]), 0);
    EXPECT_EQ(vpi_get(vpiSize, parameters[0]), 32);
    EXPECT_EQ(vpi_get(vpiSize, parameters[1]), 8);
    s_vpi_value replacement{};
    replacement.format = vpiIntVal;
    replacement.value.integer = 77;
    EXPECT_EQ(vpi_put_value(parameters[0], &replacement, nullptr, vpiNoDelay),
              nullptr);
    s_vpi_error_info readonlyError{};
    EXPECT_NE(vpi_chk_error(&readonlyError), 0);
    EXPECT_EQ(intValue(parameters[0]),
              std::string(name) == "different" ? 5 : 4);
    for (size_t index = 0; index != parameters.size(); ++index) {
      EXPECT_EQ(vpi_compare_objects(
                    keep(vpi_handle(vpiScope, parameters[index])), owner),
                1);
      EXPECT_EQ(vpi_compare_objects(
                    keep(vpi_handle(vpiModule, parameters[index])), owner),
                1);
    }
    EXPECT_EQ(
        vpi_compare_objects(named(std::string(name) + ".L.p"), parameters[1]),
        1);
    const char *fullName = vpi_get_str(vpiFullName, parameters[1]);
    ASSERT_NE(fullName, nullptr);
    EXPECT_EQ(std::string(fullName), std::string(name) + ".L.p");
    EXPECT_EQ(vpi_handle(vpiLeftRange, parameters[0]), nullptr);
    EXPECT_EQ(intValue(keep(vpi_handle(vpiLeftRange, parameters[1]))), 7);
    EXPECT_EQ(intValue(keep(vpi_handle(vpiRightRange, parameters[1]))), 0);
    auto originalDefault = keep(vpi_handle(vpiExpr, parameters[0]));
    ASSERT_NE(originalDefault, nullptr);
    EXPECT_EQ(vpi_get(vpiType, originalDefault), vpiConstant);
    EXPECT_EQ(intValue(originalDefault), 3);
    EXPECT_EQ(intValue(parameters[0]),
              std::string(name) == "different" ? 5 : 4);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_handle(vpiExpr, parameters[1]), nullptr);
    EXPECT_EQ(vpi_chk_error(&error), 0);
    ASSERT_EQ(children(vpiIODecl, owner).size(), 1u);
    inventories.push_back(parameters);
  }
  EXPECT_EQ(vpi_compare_objects(inventories[0][0], inventories[1][0]), 0);
  auto word = named("pkg::Word");
  ASSERT_NE(word, nullptr);
  EXPECT_EQ(vpi_compare_objects(
                keep(vpi_handle(vpiTypespec, inventories[0][2])), word),
            1);
  EXPECT_EQ(
      vpi_compare_objects(keep(vpi_handle(vpiTypespec, inventories[0][2])),
                          keep(vpi_handle(vpiTypespec, inventories[1][2]))),
      1);
  EXPECT_EQ(
      vpi_compare_objects(keep(vpi_handle(vpiTypespec, inventories[0][2])),
                          keep(vpi_handle(vpiTypespec, inventories[2][2]))),
      0);
  auto bus = named("bus");
  ASSERT_NE(bus, nullptr);
  auto busParameters = children(vpiParameter, bus);
  ASSERT_EQ(busParameters.size(), 1u);
  EXPECT_EQ(intValue(busParameters[0]), 4);
  EXPECT_EQ(
      vpi_compare_objects(keep(vpi_handle(vpiScope, busParameters[0])), bus),
      1);
  EXPECT_EQ(
      vpi_compare_objects(keep(vpi_handle(vpiInstance, busParameters[0])), bus),
      1);
  EXPECT_EQ(vpi_compare_objects(named("bus.K"), busParameters[0]), 1);
  for (auto handle : handles)
    vpi_free_object(handle);
  talos_rt_v1_context_destroy(runtime);

  using namespace talos::reflection;
  const auto &original = encoded->designDatabase;
  HeaderView header(original.data());
  SemanticDirectoryView directory(original.data() + header.getReserved());
  EXPECT_EQ(directory.getDefinitionCount(), 2u);
  EXPECT_EQ(directory.getDefinitionMemberCount(), 5u);
  EXPECT_EQ(directory.getDefinitionSpecializationCount(), 3u);
  EXPECT_EQ(directory.getDefinitionSpecializationBindingCount(), 9u);
  EXPECT_EQ(directory.getDefinitionParameterValueCount(), 5u);
  // Effective 4/5/254 and the shared original literal 3.
  EXPECT_EQ(directory.getFrozenValueCount(), 4u);
  EXPECT_EQ(directory.getFrozenValueBindingCount(), 0u);
  EXPECT_EQ(directory.getDefinitionMemberEndpointCount(),
            3u); // Only IO declarations consume endpoint slots.
  auto copy = [&] {
    return std::vector<uint8_t>(original.begin(), original.end());
  };
  auto rejected = [&](std::vector<uint8_t> bytes) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != bytes.size(); ++index) {
      hash ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : bytes[index];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    execution.design_database = bytes.data();
    execution.design_database_size = bytes.size();
    EXPECT_EQ(talos_rt_v1_design_validate(&execution),
              TALOS_RT_INVALID_DESIGN);
  };
  auto memberNamed = [&](const char *name) {
    for (uint32_t index = 0; index != directory.getDefinitionMemberCount();
         ++index) {
      DefinitionMemberView member(
          original.data() + directory.getDefinitionMemberOffset() +
          uint64_t{index} * DefinitionMemberLayout.size);
      const char *stored = reinterpret_cast<const char *>(
          original.data() + header.getStringOffset() + member.getName());
      if (std::string(stored) == name)
        return index;
    }
    return UINT32_MAX;
  };
  uint32_t nMember = memberNamed("N"), ioMember = memberNamed("port"),
           typeMember = memberNamed("T");
  ASSERT_NE(nMember, UINT32_MAX);
  ASSERT_NE(ioMember, UINT32_MAX);
  ASSERT_NE(typeMember, UINT32_MAX);
  uint32_t nValueRow = UINT32_MAX, ioBinding = UINT32_MAX,
           shortValue = UINT32_MAX;
  for (uint32_t index = 0;
       index != directory.getDefinitionSpecializationBindingCount(); ++index) {
    DefinitionSpecializationBindingView binding(
        original.data() + directory.getDefinitionSpecializationBindingOffset() +
        uint64_t{index} * DefinitionSpecializationBindingLayout.size);
    if (binding.getMember() == ioMember)
      ioBinding = index;
  }
  for (uint32_t index = 0;
       index != directory.getDefinitionParameterValueCount(); ++index) {
    DefinitionParameterValueView value(
        original.data() + directory.getDefinitionParameterValueOffset() +
        uint64_t{index} * DefinitionParameterValueLayout.size);
    DefinitionSpecializationBindingView binding(
        original.data() + directory.getDefinitionSpecializationBindingOffset() +
        uint64_t{value.getSpecializationBinding()} *
            DefinitionSpecializationBindingLayout.size);
    if (binding.getMember() == nMember)
      nValueRow = index;
  }
  for (uint32_t index = 0; index != directory.getFrozenValueCount(); ++index)
    if (FrozenValueView(original.data() + directory.getFrozenValueOffset() +
                        uint64_t{index} * FrozenValueLayout.size)
            .getBitWidth() == 8)
      shortValue = index;
  ASSERT_NE(nValueRow, UINT32_MAX);
  ASSERT_NE(ioBinding, UINT32_MAX);
  ASSERT_NE(shortValue, UINT32_MAX);
  auto valueRow = directory.getDefinitionParameterValueOffset() +
                  uint64_t{nValueRow} * DefinitionParameterValueLayout.size;
  auto bytes = copy();
  DefinitionParameterValueWriter(bytes.data() + valueRow)
      .setSpecializationBinding(ioBinding);
  rejected(std::move(bytes));
  bytes = copy();
  DefinitionParameterValueWriter(bytes.data() + valueRow)
      .setFrozenValue(directory.getFrozenValueCount());
  rejected(std::move(bytes));
  bytes = copy();
  DefinitionParameterValueWriter(bytes.data() + valueRow)
      .setFrozenValue(shortValue); // Unsigned8 cannot supply signed32 N.
  rejected(std::move(bytes));
  bytes = copy();
  SemanticDirectoryWriter(bytes.data() + header.getReserved())
      .setDefinitionParameterValueCount(4);
  rejected(std::move(bytes));
  bytes = copy();
  DefinitionMemberWriter(bytes.data() + directory.getDefinitionMemberOffset() +
                         uint64_t{nMember} * DefinitionMemberLayout.size)
      .setFlags(1); // IO direction on Parameter.
  rejected(std::move(bytes));
  bytes = copy();
  DefinitionMemberWriter(bytes.data() + directory.getDefinitionMemberOffset() +
                         uint64_t{nMember} * DefinitionMemberLayout.size)
      .setFlags(1u << 9); // Scalar int cannot claim an explicit source range.
  rejected(std::move(bytes));
  bytes = copy();
  DefinitionMemberWriter(bytes.data() + directory.getDefinitionMemberOffset() +
                         uint64_t{typeMember} * DefinitionMemberLayout.size)
      .setFlags(1u << 9);
  rejected(std::move(bytes));
  uint32_t parameterGroup = UINT32_MAX;
  for (uint32_t index = 0;
       index != directory.getDefinitionMemberRelationCount(); ++index) {
    DefinitionMemberRelationView relation(
        original.data() + directory.getDefinitionMemberRelationOffset() +
        uint64_t{index} * DefinitionMemberRelationLayout.size);
    if (relation.getSelector() == vpiParameter &&
        relation.getTargetCount() == 3)
      parameterGroup = index;
  }
  ASSERT_NE(parameterGroup, UINT32_MAX);
  bytes = copy();
  DefinitionMemberRelationWriter(
      bytes.data() + directory.getDefinitionMemberRelationOffset() +
      uint64_t{parameterGroup} * DefinitionMemberRelationLayout.size)
      .setTargetCount(1);
  rejected(std::move(bytes));
}
TEST(GeneratedDesignDatabase, SharedParameterImageSize) {
  using namespace talos;
  using namespace talos::reflection;
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  sim::TalosSimulationDialect>();
  mlir::MLIRContext context(registry);
  struct Measurement {
    size_t bytes;
    uint64_t members, specializations, values, objects, endpoints;
  };
  auto measure = [&](unsigned count, bool shared) -> Measurement {
    std::string text = R"mlir(
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#four = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @repeated {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
)mlir";
    if (shared)
      text += R"mlir(
    simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N"
    simulation.vpi_definition_specialization.decl @same of @cell
    simulation.vpi_definition_member.specialize @same member @N type #int {immutable_value = #four}
)mlir";
    for (unsigned index = 0; index != count; ++index) {
      const std::string id = std::to_string(index);
      const std::string scope = std::to_string(index + 1);
      text += "    simulation.scope.decl " + scope + " parent 0 hierarchy \"i" +
              id + "\" vpi_kind 32 definition @cell";
      if (shared)
        text += " specialization @same";
      text += "\n";
      if (!shared) {
        text += "    simulation.vpi_object.anchor @owner" + id + " id " +
                std::to_string(2 * index) + " type 32 in " + scope +
                " ordinal " + id + " hierarchy \"i" + id + "\" debug \"i" + id +
                "\" {backing = #simulation.vpi_backing<kind = scope, id = " +
                scope + " : i64>}\n";
        text += "    simulation.vpi_object.anchor @N" + id + " id " +
                std::to_string(2 * index + 1) + " type 41 in " + scope +
                " parent @owner" + id + " ordinal 0 hierarchy \"i" + id +
                ".N\" debug \"N\" {vpi_type = #int, immutable_value = #four}\n";
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
    const auto &image = encoded->designDatabase;
    uint64_t checksum = 0;
    for (unsigned byte = 0; byte != 8; ++byte)
      checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
    talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
                                                 encoded->executionFlags,
                                                 0,
                                                 encoded->bytecode.data(),
                                                 encoded->bytecode.size(),
                                                 image.data(),
                                                 image.size(),
                                                 encoded->stateBitCount,
                                                 checksum,
                                                 nullptr,
                                                 0,
                                                 0,
                                                 0,
                                                 nullptr,
                                                 0,
                                                 nullptr,
                                                 0};
    EXPECT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
    HeaderView header(image.data());
    SemanticDirectoryView directory(image.data() + header.getReserved());
    return {image.size(),
            directory.getDefinitionMemberCount(),
            directory.getDefinitionSpecializationCount(),
            directory.getDefinitionParameterValueCount(),
            header.getObjectCount() + directory.getStaticObjectCount(),
            directory.getDefinitionMemberEndpointCount()};
  };
  for (unsigned instances : {1u, 32u, 128u}) {
    const auto shared = measure(instances, true);
    const auto expanded = measure(instances, false);
    EXPECT_EQ(shared.members, 1u);
    EXPECT_EQ(shared.specializations, 1u);
    EXPECT_EQ(shared.values, 1u);
    EXPECT_EQ(shared.endpoints, 0u);
    EXPECT_EQ(expanded.objects, shared.objects + instances);
    if (instances > 1)
      EXPECT_LT(shared.bytes, expanded.bytes);
    RecordProperty("shared_" + std::to_string(instances), shared.bytes);
    RecordProperty("expanded_" + std::to_string(instances), expanded.bytes);
  }
}
} // namespace
