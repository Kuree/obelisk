//===- GeneratedVPIBoxTypesTest.cpp - Exact boxed value types ----------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace obelisk::reflection;

TEST(GeneratedDesignDatabase, BoxTypeLayouts) {
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
  obelisk_rt_execution_descriptor_v1 execution{OBELISK_RT_VERSION,
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

  // Execute the encoded binding instruction, then inspect the published box.
  // Metadata-only encoding cannot detect a missing bytecode dispatch case.
  auto initial =
      std::find_if(encoded->functions.begin(), encoded->functions.end(),
                   [](const auto &f) { return f.symbol == "initial"; });
  ASSERT_NE(initial, encoded->functions.end());
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, initial->index, 0};
  uint64_t scratchSize = 0, scratchAlignment = 0;
  ASSERT_EQ(obelisk_rt_validate_design_bytecode(entry, runtime, &scratchSize,
                                                &scratchAlignment),
            OBELISK_RT_OK);
  ASSERT_LE(scratchAlignment, alignof(uint64_t));
  std::vector<uint64_t> frame((scratchSize + 7) / 8);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_execute_design_bytecode(entry, runtime, frame.data(),
                                               frame.size() * 8, 0, scratchSize,
                                               0, 0, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  obelisk_rt_design_cursor_v1 output{};
  constexpr char outputName[] = "top.box";
  ASSERT_EQ(obelisk_rt_cached_design_lookup(
                runtime, reinterpret_cast<const uint8_t *>(outputName),
                sizeof(outputName) - 1, &output),
            OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(runtime, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  uint64_t boxWord = 0, boxID = 0;
  ASSERT_EQ(obelisk_rt_v1_design_read(runtime, output, &boxWord, nullptr, 64),
            OBELISK_RT_OK);
  const obelisk_rt_element_type_v1 *element = nullptr;
  std::vector<uint8_t> planes;
  ASSERT_EQ(
      obelisk_rt_box_snapshot(obelisk_rt_object_from_managed_word(boxWord),
                              &boxID, &element, planes),
      OBELISK_RT_OK);
  EXPECT_EQ(boxID, 7u);
  ASSERT_NE(element, nullptr);
  EXPECT_EQ(element->bit_width, 32u);
  ASSERT_EQ(planes.size(), sizeof(int32_t));
  int32_t value = 0;
  std::memcpy(&value, planes.data(), sizeof(value));
  EXPECT_EQ(value, -42);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);

  VPIBoxTypeBinding signedType{}, unsignedType{}, enumType{};
  ASSERT_EQ(obelisk_rt_cached_vpi_box_type(runtime, 7, &signedType),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_cached_vpi_box_type(runtime, 8, &unsignedType),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_cached_vpi_box_type(runtime, 9, &enumType),
            OBELISK_RT_OK);
  EXPECT_EQ(signedType.physical.offset, unsignedType.physical.offset);
  EXPECT_EQ(signedType.physical.offset, enumType.physical.offset);
  EXPECT_NE(signedType.semantic.offset, unsignedType.semantic.offset);
  obelisk_rt_design_semantic_type_info_v1 enumInfo{};
  ASSERT_EQ(obelisk_rt_cached_design_semantic_type_info(
                runtime, enumType.semantic, &enumInfo),
            OBELISK_RT_OK);
  EXPECT_EQ(enumInfo.kind, OBELISK_RT_DESIGN_SEMANTIC_ENUM);
  obelisk_rt_design_cursor_v1 physical{};
  ASSERT_EQ(obelisk_rt_cached_vpi_value_element_type(
                runtime, enumType.layout, enumType.semantic, &physical),
            OBELISK_RT_OK);
  EXPECT_EQ(physical.offset, enumType.physical.offset);
  obelisk_rt_design_semantic_type_edge_v1 enumBase{};
  ASSERT_EQ(obelisk_rt_cached_design_semantic_type_edge(
                runtime, enumType.semantic, 0, &enumBase),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_cached_vpi_value_element_type(runtime, enumType.layout,
                                                     enumBase.child, &physical),
            OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_cached_vpi_box_type(runtime, 10, &enumType),
            OBELISK_RT_EOF);
  obelisk_rt_v1_context_destroy(runtime);

  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  const uint64_t boxes = directory.getBoxTypeBindingOffset();
  const uint64_t rows = directory.getValueElementBindingOffset();
  const uint64_t count = directory.getValueElementBindingCount();
  ASSERT_EQ(directory.getBoxTypeBindingCount(), 3u);
  ASSERT_EQ(count, 6u); // Scalar and array storage, plus three exact box roots.
  auto original = [&] {
    return std::vector<uint8_t>(encoded->designDatabase.begin(),
                                encoded->designDatabase.end());
  };
  auto rejected = [&](std::vector<uint8_t> image) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != image.size(); ++index) {
      hash ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : image[index];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(image.data()).setChecksum(hash);
    execution.design_database = image.data();
    execution.design_database_size = image.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&execution),
              OBELISK_RT_INVALID_DESIGN);
  };
  ASSERT_EQ(ValueElementBindingView(encoded->designDatabase.data() + rows)
                .getOwnerKind(),
            static_cast<uint32_t>(ValueLayoutOwnerKind::ReflectedObject));
  auto image = original();
  BoxTypeBindingWriter(image.data() + boxes).setID(0);
  rejected(std::move(image));
  image = original();
  BoxTypeBindingWriter(image.data() + boxes + BoxTypeBindingLayout.size)
      .setID(7);
  rejected(std::move(image));
  image = original();
  BoxTypeBindingWriter(image.data() + boxes)
      .setSemanticTypeIndex(directory.getSemanticTypeCount());
  rejected(std::move(image));
  image = original();
  BoxTypeBindingWriter(image.data() + boxes)
      .setPhysicalTypeIndex(header.getTypeCount());
  rejected(std::move(image));
  image = original();
  SemanticDirectoryWriter(image.data() + header.getReserved())
      .setValueElementBindingCount(count - 1);
  rejected(
      std::move(image)); // The enum box still requires its exact root pair.
  image = original();
  uint64_t firstBoxRow = 0;
  while (firstBoxRow < count &&
         ValueElementBindingView(image.data() + rows +
                                 firstBoxRow * ValueElementBindingLayout.size)
                 .getOwnerKind() !=
             static_cast<uint32_t>(ValueLayoutOwnerKind::BoxDescriptor))
    ++firstBoxRow;
  ASSERT_LT(firstBoxRow, count);
  ValueElementBindingWriter(image.data() + rows +
                            firstBoxRow * ValueElementBindingLayout.size)
      .setOwnerIndex(3);
  rejected(std::move(image)); // No fourth descriptor.
  image = original();
  ValueElementBindingWriter(image.data() + rows).setOwnerKind(UINT32_MAX);
  rejected(std::move(image));
  image = original();
  ValueElementBindingWriter(image.data() + rows).setOwnerIndex(UINT32_MAX);
  rejected(std::move(image));
  image = original();
  // The physical i32 matches, but a storage layout must start at that storage's
  // exact semantic root, not another source declaration's same-width type.
  ValueElementBindingWriter(image.data() + rows)
      .setSemanticTypeIndex(unsignedType.semantic.offset - 1);
  rejected(std::move(image));
  image = original();
  uint32_t package = 0;
  ASSERT_TRUE(tryPackTableIndex(TableKind::StaticObject, 0, package));
  ValueElementBindingWriter(image.data() + rows).setOwnerIndex(package);
  rejected(std::move(image)); // A lexical package is not value storage.
}
} // namespace
