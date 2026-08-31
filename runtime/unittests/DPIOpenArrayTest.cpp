//===- DPIOpenArrayTest.cpp - DPI open-array C layer tests ----------------===//

#include "obelisk/Runtime/Runtime.h"
#include "svdpi.h"

#include "../lib/RuntimeInternal.h"

#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {

TEST(DPIOpenArray, RecursivePreparationHasNoDimensionDepthCap) {
  constexpr uint32_t dimensions = 1025;
  std::vector<int64_t> shapePlan;
  shapePlan.reserve(uint64_t{dimensions} * 8);
  for (uint32_t dimension = 0; dimension != dimensions; ++dimension)
    shapePlan.insert(shapePlan.end(), {0, 0, 0, 0, 32, 32, 0, 1});
  const std::array<int64_t, 8> elementPlan{0, 0, 0, 4, 32, 0, 1, 0};
  uint32_t value = 0x12345678;
  obelisk_rt_dpi_open_array_storage_v1 storage{};
  ASSERT_EQ(obelisk_rt_v1_dpi_open_array_prepare_recursive(
                &value, nullptr, sizeof(value), 32, 0, 1, 13, 32, 0, 31, 0,
                sizeof(value), alignof(uint32_t), 0, elementPlan.data(),
                elementPlan.size(), shapePlan.data(), dimensions, &storage),
            OBELISK_RT_OK);
  EXPECT_EQ(storage.descriptor.dimensions, dimensions);
  EXPECT_EQ(storage.descriptor.data_size, sizeof(value));
  EXPECT_EQ(*static_cast<const uint32_t *>(storage.descriptor.data), value);
  obelisk_rt_v1_dpi_open_array_release_recursive(&storage);
}

TEST(DPIOpenArray, RecursiveWritableDescriptorAcceptsCanonicalWrites) {
  const std::array<int64_t, 16> shapePlan{0, 0, 0, 0, 8, 8, 0, 1,
                                          0, 0, 0, 0, 8, 8, 0, 1};
  const std::array<int64_t, 8> elementPlan{0, 0, 0, 6, 8, 0, 0, 0};
  uint8_t value = 0x12;
  obelisk_rt_dpi_open_array_storage_v1 storage{};
  ASSERT_EQ(obelisk_rt_v1_dpi_open_array_prepare_recursive(
                &value, nullptr, sizeof(value), 8, 0, 1, 6, 8, 0, 7, 0,
                sizeof(svBitVecVal), alignof(svBitVecVal), 0,
                elementPlan.data(), elementPlan.size(), shapePlan.data(), 2,
                &storage),
            OBELISK_RT_OK);
  ASSERT_NE(storage.descriptor.data, nullptr);
  EXPECT_NE(storage.descriptor.flags & OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE, 0u);
  svBitVecVal replacement = 0xa5;
  svPutBitArrElem2VecVal(&storage.descriptor, &replacement, 0, 0);
  EXPECT_EQ(*static_cast<const svBitVecVal *>(storage.descriptor.data) & 0xff,
            replacement);
  obelisk_rt_v1_dpi_open_array_release_recursive(&storage);
}

class DPIOpenArrayManagedTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
              OBELISK_RT_OK);
  }

  void TearDown() override {
    obelisk_rt_managed_execution_leave(lane, entered);
    obelisk_rt_v1_context_destroy(context);
  }

  obelisk_rt_context *context = nullptr;
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
};

TEST_F(DPIOpenArrayManagedTest,
       RecursiveFinishImportsCanonicalWritesIntoDynamicArray) {
  const obelisk_rt_element_type_v1 elementType{
      OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_BITS, 91, 0,      0,
      sizeof(uint8_t),    alignof(uint8_t),        8,  nullptr};
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &elementType, 2, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &array), OBELISK_RT_OK);
  uint8_t first = 0x12;
  uint8_t second = 0x34;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 0, &first, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 1, &second, nullptr),
            OBELISK_RT_OK);

  const std::array<int64_t, 8> shapePlan{1, 0, -1, 0, 0, 8, 0, 0};
  const std::array<int64_t, 8> elementPlan{0, 0, 0, 6, 8, 0, 0, 0};
  obelisk_rt_managed_word_v1 transport =
      static_cast<obelisk_rt_managed_word_v1>(
          reinterpret_cast<uintptr_t>(array));
  obelisk_rt_dpi_open_array_storage_v1 storage{};
  ASSERT_EQ(obelisk_rt_v1_dpi_open_array_prepare_recursive(
                &transport, nullptr, sizeof(transport), 64, 0, 1, 6, 8, 0, 7, 0,
                sizeof(svBitVecVal), alignof(svBitVecVal), 0,
                elementPlan.data(), elementPlan.size(), shapePlan.data(), 1,
                &storage),
            OBELISK_RT_OK);
  ASSERT_EQ(storage.descriptor.data_size, 2 * sizeof(svBitVecVal));
  auto *canonical = static_cast<svBitVecVal *>(storage.descriptor.data);
  canonical[0] = 0xa5;
  canonical[1] = 0x5a;

  EXPECT_EQ(obelisk_rt_v1_dpi_open_array_finish_recursive(
                OBELISK_RT_OK, context, &storage, &transport, nullptr,
                sizeof(transport), 64, 0, 8, 0, sizeof(svBitVecVal),
                elementPlan.data(), elementPlan.size(), shapePlan.data(), 1),
            OBELISK_RT_OK);
  uint8_t observed = 0;
  EXPECT_EQ(obelisk_rt_v1_container_read(array, 0, &observed, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(observed, 0xa5);
  EXPECT_EQ(obelisk_rt_v1_container_read(array, 1, &observed, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(observed, 0x5a);

  obelisk_rt_v1_dpi_open_array_release_recursive(&storage);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

obelisk_rt_dpi_open_array_v1
makeArray(void *data, uint64_t size, const obelisk_rt_dpi_dimension_v1 *ranges,
          uint32_t dimensions, uint64_t elementSize, uint32_t elementBits,
          uint32_t flags, int32_t packedLeft = 0, int32_t packedRight = 0) {
  return {OBELISK_RT_DPI_OPEN_ARRAY_MAGIC,
          flags,
          dimensions,
          elementBits,
          packedLeft,
          packedRight,
          elementSize,
          data,
          size,
          ranges,
          0};
}

TEST(DPIOpenArray, QueriesAndCLayoutPointersPreserveDeclaredRanges) {
  std::array<int32_t, 6> values{10, 11, 12, 20, 21, 22};
  std::array<obelisk_rt_dpi_dimension_v1, 2> ranges{{
      {2, 1, sizeof(int32_t) * 3},
      {-1, 1, sizeof(int32_t)},
  }};
  auto array = makeArray(
      values.data(), sizeof(values), ranges.data(), 2, sizeof(int32_t), 32,
      OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT | OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE);
  svOpenArrayHandle handle = &array;

  EXPECT_EQ(svDimensions(handle), 2);
  EXPECT_EQ(svLeft(handle, 1), 2);
  EXPECT_EQ(svRight(handle, 1), 1);
  EXPECT_EQ(svLow(handle, 1), 1);
  EXPECT_EQ(svHigh(handle, 1), 2);
  EXPECT_EQ(svIncrement(handle, 1), 1);
  EXPECT_EQ(svSize(handle, 1), 2);
  EXPECT_EQ(svLeft(handle, 2), -1);
  EXPECT_EQ(svRight(handle, 2), 1);
  EXPECT_EQ(svIncrement(handle, 2), -1);
  EXPECT_EQ(svSize(handle, 2), 3);
  EXPECT_EQ(svSize(handle, 0), 0);
  EXPECT_EQ(svGetArrayPtr(handle), values.data());
  EXPECT_EQ(svSizeOfArray(handle), static_cast<int>(sizeof(values)));
  EXPECT_EQ(*static_cast<int32_t *>(svGetArrElemPtr2(handle, 2, 0)), 21);
  EXPECT_EQ(*static_cast<int32_t *>(svGetArrElemPtr(handle, 1, 1)), 12);
  EXPECT_EQ(svGetArrElemPtr2(handle, 3, 0), nullptr);
  EXPECT_EQ(svGetArrElemPtr1(handle, 2), nullptr);
}

TEST(DPIOpenArray, PackedBitVectorCopiesAndMasksTail) {
  std::array<svBitVecVal, 4> values{0xffffffffU, 0xffffffffU, 0, 0};
  obelisk_rt_dpi_dimension_v1 range{4, 5, sizeof(svBitVecVal) * 2};
  auto array = makeArray(
      values.data(), sizeof(values), &range, 1, sizeof(svBitVecVal) * 2, 33,
      OBELISK_RT_DPI_OPEN_ARRAY_PACKED | OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE, 32,
      0);
  svOpenArrayHandle handle = &array;

  EXPECT_EQ(svLeft(handle, 0), 32);
  EXPECT_EQ(svRight(handle, 0), 0);
  EXPECT_EQ(svSize(handle, 0), 33);
  std::array<svBitVecVal, 2> read{};
  svGetBitArrElem1VecVal(read.data(), handle, 4);
  EXPECT_EQ(read[0], 0xffffffffU);
  EXPECT_EQ(read[1], 1U);

  std::array<svBitVecVal, 2> replacement{0x12345678U, 0xffffffffU};
  svPutBitArrElemVecVal(handle, replacement.data(), 5);
  EXPECT_EQ(values[2], 0x12345678U);
  EXPECT_EQ(values[3], 1U);
}

TEST(DPIOpenArray, FourStateVectorsAndScalarUtilitiesRoundTrip) {
  std::array<svLogicVecVal, 2> vectors{{{0xffffffffU, 0xffffffffU}, {0, 0}}};
  obelisk_rt_dpi_dimension_v1 range{0, 1, sizeof(svLogicVecVal)};
  auto logicArray = makeArray(
      vectors.data(), sizeof(vectors), &range, 1, sizeof(svLogicVecVal), 5,
      OBELISK_RT_DPI_OPEN_ARRAY_PACKED | OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE |
          OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE,
      4, 0);
  svLogicVecVal read{};
  svGetLogicArrElem1VecVal(&read, &logicArray, 0);
  EXPECT_EQ(read.aval, 0x1fU);
  EXPECT_EQ(read.bval, 0x1fU);
  svLogicVecVal replacement{0x16U, 0x0cU};
  svPutLogicArrElem1VecVal(&logicArray, &replacement, 1);
  EXPECT_EQ(vectors[1].aval, 0x16U);
  EXPECT_EQ(vectors[1].bval, 0x0cU);

  std::array<svLogic, 2> scalars{sv_x, sv_0};
  obelisk_rt_dpi_dimension_v1 scalarRange{0, 1, sizeof(svLogic)};
  auto scalarArray = makeArray(scalars.data(), sizeof(scalars), &scalarRange, 1,
                               sizeof(svLogic), 1,
                               OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE |
                                   OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE);
  EXPECT_EQ(svGetLogicArrElem1(&scalarArray, 0), sv_x);
  svPutLogicArrElem(&scalarArray, sv_z, 1);
  EXPECT_EQ(scalars[1], sv_z);
}

TEST(DPIOpenArray, PartSelectGetPreservesUnselectedDestinationBits) {
  svBitVecVal bits[] = {0x12345678U, 0x9abcdef0U};
  svBitVecVal bitResult = 0xa5a50000U;
  svGetPartselBit(&bitResult, bits, 4, 8);
  EXPECT_EQ(bitResult, 0xa5a50067U);

  svLogicVecVal logic[] = {{0x12345678U, 0x0f0f0f0fU},
                           {0x9abcdef0U, 0xf0f0f0f0U}};
  svLogicVecVal logicResult{0xa5a50000U, 0x5a5a0000U};
  svGetPartselLogic(&logicResult, logic, 4, 8);
  EXPECT_EQ(logicResult.aval, 0xa5a50067U);
  EXPECT_EQ(logicResult.bval, 0x5a5a00f0U);
}

TEST(DPIOpenArray, ReadOnlyAndNonCLayoutAreEnforced) {
  svBit value = sv_1;
  obelisk_rt_dpi_dimension_v1 range{0, 0, sizeof(value)};
  auto array = makeArray(&value, sizeof(value), &range, 1, sizeof(value), 1, 0);
  EXPECT_EQ(svGetArrayPtr(&array), nullptr);
  EXPECT_EQ(svSizeOfArray(&array), 0);
  EXPECT_EQ(svGetArrElemPtr1(&array, 0), nullptr);
  EXPECT_EQ(svGetBitArrElem1(&array, 0), sv_1);
  svPutBitArrElem1(&array, sv_0, 0);
  EXPECT_EQ(value, sv_1);
}

TEST(DPIOpenArray, CompactPlanesCanonicalizeWithoutAllocation) {
  // Three five-bit four-state elements. Internal Simulation IR stores value
  // xor unknown in one compact plane and unknown in the other; Annex H stores
  // one {aval,bval} word pair per element.
  uint32_t internal = 0;
  uint32_t unknown = 0;
  const std::array<uint32_t, 3> aval{0x15, 0x02, 0x1f};
  const std::array<uint32_t, 3> bval{0x0c, 0x01, 0x10};
  for (uint32_t element = 0; element != 3; ++element) {
    internal |= ((aval[element] ^ bval[element]) & 0x1f) << (element * 5);
    unknown |= (bval[element] & 0x1f) << (element * 5);
  }
  std::array<svLogicVecVal, 3> canonical{};
  EXPECT_EQ(obelisk_rt_v1_dpi_open_array_pack(
                &internal, &unknown, sizeof(internal), 15, 1, 7, 5, 3, 0,
                canonical.data(), sizeof(canonical)),
            OBELISK_RT_OK);
  for (uint32_t element = 0; element != 3; ++element) {
    EXPECT_EQ(canonical[element].aval, aval[element]);
    EXPECT_EQ(canonical[element].bval, bval[element]);
  }
  uint32_t unpacked = 0xffffffffU;
  uint32_t unpackedUnknown = 0xffffffffU;
  EXPECT_EQ(obelisk_rt_v1_dpi_open_array_unpack(
                canonical.data(), sizeof(canonical), 7, 5, 3, &unpacked,
                &unpackedUnknown, sizeof(unpacked), 15, 1, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(unpacked & 0x7fffU, internal);
  EXPECT_EQ(unpackedUnknown & 0x7fffU, unknown);
  EXPECT_EQ(unpacked >> 15, 0U);
  EXPECT_EQ(unpackedUnknown >> 15, 0U);
}

} // namespace
