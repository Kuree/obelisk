//===- Coverage.cpp - Context-local functional coverage state ------------===//

#include "RuntimeInternal.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

int32_t saturateI32(uint64_t value) {
  return value > static_cast<uint64_t>(INT32_MAX)
             ? INT32_MAX
             : static_cast<int32_t>(value);
}

uint64_t saturatingAdd(uint64_t left, uint64_t right) {
  return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

struct CoverageResult {
  double percentage = 0.0;
  uint64_t covered = 0;
  uint64_t total = 0;
};

obelisk_rt_status collectCoverpointBins(const uint64_t *coverpointBins,
                                        uint64_t coverpointCount,
                                        std::vector<uint32_t> &bins) {
  if (!coverpointBins || !coverpointCount ||
      coverpointCount > bins.max_size())
    return OBELISK_RT_INVALID_ARGUMENT;
  bins.reserve(static_cast<size_t>(coverpointCount));
  for (uint64_t index = 0; index < coverpointCount; ++index) {
    uint64_t count = coverpointBins[index];
    if (!count || count > UINT32_MAX)
      return OBELISK_RT_INVALID_ARGUMENT;
    bins.push_back(static_cast<uint32_t>(count));
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
findOrRegisterCoverageType(obelisk_rt_context *context, uint64_t typeID,
                           const std::vector<uint32_t> &bins,
                           CoverageTypeState *&result) {
  auto found = context->coverageTypes.find(typeID);
  if (found != context->coverageTypes.end()) {
    if (found->second.coverpointBins != bins)
      return OBELISK_RT_INVALID_DESIGN;
    result = &found->second;
    return OBELISK_RT_OK;
  }
  CoverageTypeState type;
  type.coverpointBins = bins;
  auto insertion = context->coverageTypes.emplace(typeID, std::move(type));
  if (!insertion.second)
    return OBELISK_RT_INVALID_DESIGN;
  result = &insertion.first->second;
  return OBELISK_RT_OK;
}

CoverageResult queryInstance(const CoverageInstanceState &instance) {
  CoverageResult result;
  if (instance.hits.empty())
    return result;
  double sum = 0.0;
  for (const std::vector<uint64_t> &coverpoint : instance.hits) {
    uint64_t covered = 0;
    for (uint64_t hits : coverpoint)
      covered += hits != 0;
    result.covered = saturatingAdd(result.covered, covered);
    result.total =
        saturatingAdd(result.total, static_cast<uint64_t>(coverpoint.size()));
    sum += coverpoint.empty()
               ? 0.0
               : 100.0 * static_cast<double>(covered) /
                     static_cast<double>(coverpoint.size());
  }
  result.percentage = sum / static_cast<double>(instance.hits.size());
  return result;
}

obelisk_rt_status writeResult(const CoverageResult &result,
                              double *outPercentage, int32_t *outCovered,
                              int32_t *outTotal) {
  if (!outPercentage || !outCovered || !outTotal)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outPercentage = std::max(0.0, std::min(100.0, result.percentage));
  *outCovered = saturateI32(result.covered);
  *outTotal = saturateI32(result.total);
  return OBELISK_RT_OK;
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_create(
    obelisk_rt_context *context, uint64_t typeID,
    const uint64_t *coverpointBins, uint64_t coverpointCount,
    obelisk_rt_covergroup_v1 *outHandle) {
  if (!context || !typeID || !coverpointBins || !coverpointCount ||
      !outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::vector<uint32_t> bins;
  try {
    obelisk_rt_status status =
        collectCoverpointBins(coverpointBins, coverpointCount, bins);
    if (status != OBELISK_RT_OK)
      return status;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageTypeState *type = nullptr;
    status = findOrRegisterCoverageType(context, typeID, bins, type);
    if (status != OBELISK_RT_OK)
      return status;
    uint64_t handle = context->nextCoverageInstance;
    if (!handle)
      return OBELISK_RT_OUT_OF_RESOURCES;
    CoverageInstanceState instance;
    instance.typeID = typeID;
    instance.hits.reserve(bins.size());
    for (uint32_t count : bins)
      instance.hits.emplace_back(count, uint64_t{0});
    if (type->instances.size() == type->instances.max_size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    type->instances.reserve(type->instances.size() + 1);
    auto insertion =
        context->coverageInstances.emplace(handle, std::move(instance));
    if (!insertion.second)
      return OBELISK_RT_OUT_OF_RESOURCES;
    // The reserve above makes this final publication non-throwing. From this
    // point onward the instance is reachable through both context indexes.
    type->instances.push_back(handle);
    ++context->nextCoverageInstance;
    *outHandle = handle;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_set_enabled(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    uint32_t enabled) {
  if (!context || enabled > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto found = context->coverageInstances.find(handle);
  if (!handle || found == context->coverageInstances.end())
    return OBELISK_RT_INVALID_HANDLE;
  found->second.enabled = enabled != 0;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_sample_enabled(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    uint32_t *outEnabled) {
  if (!context || !outEnabled)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto found = context->coverageInstances.find(handle);
  if (!handle || found == context->coverageInstances.end())
    return OBELISK_RT_INVALID_HANDLE;
  *outEnabled = found->second.enabled;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_bin_hit(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    uint32_t coverpoint, uint32_t bin) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto found = context->coverageInstances.find(handle);
  if (!handle || found == context->coverageInstances.end())
    return OBELISK_RT_INVALID_HANDLE;
  CoverageInstanceState &instance = found->second;
  if (coverpoint >= instance.hits.size() ||
      bin >= instance.hits[coverpoint].size())
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!instance.enabled)
    return OBELISK_RT_OK;
  uint64_t &hits = instance.hits[coverpoint][bin];
  if (hits != UINT64_MAX)
    ++hits;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_sample(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    const uint8_t *hits, uint64_t hitCount) {
  if (!context || !hits || !hitCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto found = context->coverageInstances.find(handle);
  if (!handle || found == context->coverageInstances.end())
    return OBELISK_RT_INVALID_HANDLE;
  CoverageInstanceState &instance = found->second;
  uint64_t expected = 0;
  for (const std::vector<uint64_t> &coverpoint : instance.hits)
    expected = saturatingAdd(expected, coverpoint.size());
  if (hitCount != expected)
    return OBELISK_RT_INVALID_ARGUMENT;
  for (uint64_t index = 0; index < hitCount; ++index)
    if (hits[index] > 1)
      return OBELISK_RT_INVALID_ARGUMENT;
  if (!instance.enabled)
    return OBELISK_RT_OK;
  uint64_t index = 0;
  for (std::vector<uint64_t> &coverpoint : instance.hits)
    for (uint64_t &count : coverpoint) {
      if (hits[index] && count != UINT64_MAX)
        ++count;
      ++index;
    }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_instance_query(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    double *outPercentage, int32_t *outCovered, int32_t *outTotal) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto found = context->coverageInstances.find(handle);
  if (!handle || found == context->coverageInstances.end())
    return OBELISK_RT_INVALID_HANDLE;
  return writeResult(queryInstance(found->second), outPercentage, outCovered,
                     outTotal);
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_type_query(
    obelisk_rt_context *context, uint64_t typeID,
    const uint64_t *coverpointBins, uint64_t coverpointCount,
    double *outPercentage, int32_t *outCovered, int32_t *outTotal) {
  if (!context || !typeID || !coverpointBins || !coverpointCount ||
      !outPercentage || !outCovered || !outTotal)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::vector<uint32_t> bins;
  try {
    obelisk_rt_status status =
        collectCoverpointBins(coverpointBins, coverpointCount, bins);
    if (status != OBELISK_RT_OK)
      return status;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageTypeState *type = nullptr;
    status = findOrRegisterCoverageType(context, typeID, bins, type);
    if (status != OBELISK_RT_OK)
      return status;
    CoverageResult result;
    for (uint64_t handle : type->instances) {
      auto instance = context->coverageInstances.find(handle);
      if (instance == context->coverageInstances.end())
        return OBELISK_RT_INVALID_HANDLE;
      CoverageResult current = queryInstance(instance->second);
      result.percentage += current.percentage;
      result.covered = saturatingAdd(result.covered, current.covered);
      result.total = saturatingAdd(result.total, current.total);
    }
    if (!type->instances.empty())
      result.percentage /= static_cast<double>(type->instances.size());
    return writeResult(result, outPercentage, outCovered, outTotal);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}
