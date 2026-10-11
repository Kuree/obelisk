// Portable coverage scaling probe. See README.md for build instructions.
#include "obelisk/Coverage/CoverageDatabase.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace obelisk::coverage;

int main(int argc, char **argv) {
  const uint64_t count = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 65536;
  const uint64_t runCount = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
  if (!count || count > UINT32_MAX / 4 || !runCount || runCount > 65536)
    return 2;
  const bool sameLine = argc > 4 && std::string(argv[4]) == "same-line";
  Database db;
  db.producer = "coverage-format-scaling";
  db.sourceFiles.push_back({UINT64_MAX, "probe.sv", {}});
  for (uint64_t index = 1; index <= runCount; ++index) {
    Run run;
    for (unsigned byte = 0; byte != 8; ++byte)
      run.uuid[15 - byte] = index >> (byte * 8);
    run.name = "run" + std::to_string(index);
    run.flags = RunContainsLine | RunContainsToggle;
    run.tags.emplace_back("probe", "scaling");
    db.runs.push_back(std::move(run));
  }
  for (uint64_t index = 1; index <= count; ++index) {
    const uint64_t scope = index * 9, line = index * 17;
    db.scopes.push_back({scope, index == 1 ? 0 : scope - 9,
                         "scope" + std::to_string(index), 0, "dut"});
    db.linePoints.push_back({line, UINT64_MAX, UINT64_MAX, scope, "",
                             static_cast<uint32_t>(sameLine ? 1 : index), 1,
                             static_cast<uint32_t>(sameLine ? 1 : index), 2, 0,
                             0});
    db.counters.push_back(
        {db.runs.front().uuid, MetricKind::Line, line, 0, 0, 0, 1});
  }
  db.toggleObjects.push_back(
      {1, 9, UINT64_MAX, "data", 0, count, 1, 1, 1, 2, 0});
  db.toggleDimensions.push_back({1, UINT32_MAX, ToggleDimensionKind::Root, 1,
                                 static_cast<uint32_t>(count), 0, 0, 0, count,
                                 "", 0});
  for (uint64_t index = 0; index != count; ++index) {
    db.toggleDimensions.push_back({1, 0, ToggleDimensionKind::Field, UINT32_MAX,
                                   0, 0, 0, index, 1,
                                   "field" + std::to_string(index), 0});
    db.counters.push_back({db.runs.front().uuid, MetricKind::Toggle, 1, 0,
                           static_cast<uint32_t>(index * 4), 0, 1});
  }
  const auto begin = std::chrono::steady_clock::now();
  Diagnostic diagnostic;
  std::vector<uint8_t> bytes;
  Status status = serialize(db, bytes, &diagnostic);
  const auto encoded = std::chrono::steady_clock::now();
  Database parsed;
  if (status == Status::Ok)
    status = parse(bytes.data(), bytes.size(), parsed, {}, &diagnostic);
  const auto end = std::chrono::steady_clock::now();
  std::printf(
      "records=%llu runs=%llu bytes=%zu serialize=%.6f parse=%.6f status=%s\n",
      static_cast<unsigned long long>(count),
      static_cast<unsigned long long>(runCount), bytes.size(),
      std::chrono::duration<double>(encoded - begin).count(),
      std::chrono::duration<double>(end - encoded).count(), statusName(status));
  if (status != Status::Ok) {
    std::fprintf(stderr, "field=%s\n",
                 diagnostic.field ? diagnostic.field : "");
    return 1;
  }
  if (parsed.scopes.size() != count || parsed.counters.size() != 2 * count ||
      parsed.runs.size() != runCount)
    return 1;
  if (argc > 3) {
    FILE *output = std::fopen(argv[3], "wb");
    if (!output)
      return 1;
    const bool complete =
        std::fwrite(bytes.data(), 1, bytes.size(), output) == bytes.size();
    const bool closed = std::fclose(output) == 0;
    if (!complete || !closed)
      return 1;
  }
  return 0;
}
